#include "feeder_app.h"

#include "app_entry.h"
#include "feeder_schedule.h"
#include "feeder_servo.h"
#include <stdio.h>

#define HATCH_OPEN_TIME_MS          100U
#define HATCH_CLOSE_SETTLE_MS       200U
#define RELOAD_BUTTON_DEBOUNCE_MS   30U
#define BLE_SETUP_SEQUENCE_MS       10000U
#define BLE_SETUP_ACTIVE_MS         600000U
#define BLE_SETUP_EDGE_COUNT        4U
#define BLE_SLEEP_RESET_MARKER      0x42534C50U
#define BLE_SLEEP_RESET_BACKUP_DR   RTC_BKP_DR4

extern RTC_HandleTypeDef hrtc;

void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
void FeederSerial_PrepareStop(void);
void FeederSerial_RestoreAfterStop(void);

static volatile uint8_t feed_due;
static volatile uint8_t reload_button_due;
static volatile uint8_t ble_sleep_requested;
static uint8_t reload_hatch_open;
static uint8_t reload_close_step;
static GPIO_PinState reload_switch_state;
static uint8_t ble_setup_edge_count;
static uint8_t ble_started;
static uint8_t ble_wait_reported;
static uint32_t ble_setup_sequence_start_ms;
static uint32_t ble_setup_active_until_ms;
static uint32_t ble_wait_start_ms;

static void Feeder_EnterStopUntilAlarm(void);
static void Feeder_PrintBleCpu2Diag(void);
static uint8_t Feeder_ServiceReloadSwitch(void);
static uint8_t Feeder_BleSetupModeActive(void);
static void Feeder_RunCycle(void);
static void Feeder_ReloadCloseStep(void);
static void Feeder_RecordReloadSwitchEdge(void);
static void Feeder_StartBleSetup(void);
static uint8_t Feeder_ConsumeBleSleepResetMarker(void);
static void Feeder_SetBleSleepResetMarker(void);

void FeederApp_Init(uint8_t hse_ready)
{
  FeederSchedule_Init();

  if (Feeder_ConsumeBleSleepResetMarker() == 0U)
  {
    FeederServo_MoveTo(FEEDER_SERVO_CLOSED_US, HATCH_CLOSE_SETTLE_MS);
  }
  reload_hatch_open = 0U;
  reload_close_step = 0U;
  reload_switch_state = HAL_GPIO_ReadPin(RELOAD_BUTTON_GPIO_Port, RELOAD_BUTTON_Pin);

  FeederSchedule_SetNextAlarm();
  FeederSchedule_PrintCurrentTime("Startup");
  printf("Clock HSE %s\r\n", hse_ready ? "OK" : "MISSING");
  printf("BLE is off until setup mode is enabled\r\n");
}

void FeederApp_Process(void)
{
  if (ble_started != 0U)
  {
    MX_APPE_Process();

    if ((HayFeeder_BleCpu2Status() == 0U) &&
        (ble_wait_reported == 0U) &&
        ((HAL_GetTick() - ble_wait_start_ms) > 5000U))
    {
      ble_wait_reported = 1U;
      printf("BLE CPU2 no ready event after 5s\r\n");
      printf("Check STM32WB CPU2 wireless stack/FUS and option bytes\r\n");
      Feeder_PrintBleCpu2Diag();
    }

    if (ble_sleep_requested != 0U)
    {
      printf("BLE disconnect requested: returning to low-power sleep\r\n");
      Feeder_SetBleSleepResetMarker();
      HAL_Delay(100U);
      NVIC_SystemReset();
    }
  }

  if (feed_due != 0U)
  {
    feed_due = 0U;
    FeederSchedule_PrintCurrentTime("MAT TID");
    Feeder_RunCycle();
    reload_button_due = 0U;
    __HAL_GPIO_EXTI_CLEAR_IT(RELOAD_BUTTON_Pin);
    FeederSchedule_SetNextAlarm();
  }

  if (reload_button_due != 0U)
  {
    reload_button_due = 0U;
    if (Feeder_ServiceReloadSwitch() != 0U)
    {
      FeederSchedule_PrintCurrentTime("Reload");
    }
  }

  if (Feeder_ServiceReloadSwitch() != 0U)
  {
    FeederSchedule_PrintCurrentTime("Reload");
  }

  if (reload_button_due != 0U)
  {
    return;
  }

  if (Feeder_BleSetupModeActive() == 0U)
  {
    Feeder_EnterStopUntilAlarm();
  }
}

void FeederApp_OnFeedAlarm(RTC_HandleTypeDef *rtc)
{
  if (rtc->Instance == RTC)
  {
    feed_due = 1U;
  }
}

void FeederApp_OnReloadSwitchInterrupt(uint16_t gpio_pin)
{
  if (gpio_pin == RELOAD_BUTTON_Pin)
  {
    reload_button_due = 1U;
  }
}

void FeederApp_RequestBleSleep(void)
{
  ble_sleep_requested = 1U;
}

static void Feeder_EnterStopUntilAlarm(void)
{
  printf("Sleep: waiting for feed alarm or reload switch\r\n");
  FeederSerial_PrepareStop();
  HAL_SuspendTick();
  HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
  HAL_ResumeTick();

  SystemClock_Config();
  PeriphCommonClock_Config();
  FeederSerial_RestoreAfterStop();
  FeederSchedule_PrintCurrentTime("VAKNE");
}

static uint8_t Feeder_BleSetupModeActive(void)
{
  uint32_t now = HAL_GetTick();

  if ((ble_setup_active_until_ms != 0U) &&
      ((int32_t)(ble_setup_active_until_ms - now) > 0))
  {
    return 1U;
  }

  if (ble_setup_active_until_ms != 0U)
  {
    ble_setup_active_until_ms = 0U;
    printf("BLE setup mode ended\r\n");
    if (ble_started != 0U)
    {
      printf("Restarting once to turn BLE fully off and return to low power\r\n");
      HAL_Delay(100U);
      NVIC_SystemReset();
    }
  }

  if ((ble_setup_edge_count != 0U) &&
      ((now - ble_setup_sequence_start_ms) <= BLE_SETUP_SEQUENCE_MS))
  {
    return 1U;
  }

  ble_setup_edge_count = 0U;
  return 0U;
}

static void Feeder_PrintBleCpu2Diag(void)
{
  printf("BLE diag C2BOOT=%lu C2DS=%lu C2SB=%lu\r\n",
         LL_PWR_IsEnabledBootC2(),
         LL_PWR_IsActiveFlag_C2DS(),
         LL_PWR_IsActiveFlag_C2SB());
  printf("BLE diag IPCC C1MR=%08lX C1TOC2=%08lX C2TOC1=%08lX\r\n",
         IPCC->C1MR,
         IPCC->C1TOC2SR,
         IPCC->C2TOC1SR);
}

static uint8_t Feeder_ServiceReloadSwitch(void)
{
  GPIO_PinState switch_state;

  HAL_Delay(RELOAD_BUTTON_DEBOUNCE_MS);
  switch_state = HAL_GPIO_ReadPin(RELOAD_BUTTON_GPIO_Port, RELOAD_BUTTON_Pin);

  if (switch_state != reload_switch_state)
  {
    reload_switch_state = switch_state;
    Feeder_RecordReloadSwitchEdge();

    if (reload_hatch_open == 0U)
    {
      printf("Reload switch toggled: opening hatch\r\n");
      FeederServo_MoveTo(FEEDER_SERVO_OPEN_US, HATCH_CLOSE_SETTLE_MS);
      reload_hatch_open = 1U;
      reload_close_step = 0U;
      return 1U;
    }

    Feeder_ReloadCloseStep();
    return 1U;
  }

  return 0U;
}

static void Feeder_ReloadCloseStep(void)
{
  if (reload_close_step == 0U)
  {
    printf("Reload close step 1/3: halfway closed\r\n");
    FeederServo_MoveTo(FEEDER_SERVO_HALF_US, HATCH_CLOSE_SETTLE_MS);
    reload_close_step = 1U;
    return;
  }

  if (reload_close_step == 1U)
  {
    printf("Reload close step 2/3: closed\r\n");
    FeederServo_MoveTo(FEEDER_SERVO_CLOSED_US, HATCH_CLOSE_SETTLE_MS);
    reload_close_step = 2U;
    return;
  }

  printf("Reload close step 3/3: finished\r\n");
  reload_close_step = 0U;
  reload_hatch_open = 0U;
}

static void Feeder_RecordReloadSwitchEdge(void)
{
  uint32_t now = HAL_GetTick();

  if ((ble_setup_edge_count == 0U) ||
      ((now - ble_setup_sequence_start_ms) > BLE_SETUP_SEQUENCE_MS))
  {
    ble_setup_sequence_start_ms = now;
    ble_setup_edge_count = 0U;
  }

  ble_setup_edge_count++;
  printf("BLE setup edge %u/%u\r\n",
         (unsigned int)ble_setup_edge_count,
         (unsigned int)BLE_SETUP_EDGE_COUNT);

  if (ble_setup_edge_count >= BLE_SETUP_EDGE_COUNT)
  {
    ble_setup_edge_count = 0U;
    ble_setup_active_until_ms = now + BLE_SETUP_ACTIVE_MS;
    printf("BLE setup mode enabled for 10 min: connect with HayFeeder app now\r\n");
    Feeder_StartBleSetup();
  }
}

static void Feeder_StartBleSetup(void)
{
  if (ble_started != 0U)
  {
    return;
  }

  ble_started = 1U;
  ble_wait_reported = 0U;
  ble_wait_start_ms = HAL_GetTick();
  printf("Starting BLE now\r\n");
  MX_APPE_Init();
  printf("BLE APPE init requested\r\n");
}

static uint8_t Feeder_ConsumeBleSleepResetMarker(void)
{
  if (HAL_RTCEx_BKUPRead(&hrtc, BLE_SLEEP_RESET_BACKUP_DR) != BLE_SLEEP_RESET_MARKER)
  {
    return 0U;
  }

  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, BLE_SLEEP_RESET_BACKUP_DR, 0U);
  printf("BLE sleep reset: startup servo close skipped\r\n");
  return 1U;
}

static void Feeder_SetBleSleepResetMarker(void)
{
  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, BLE_SLEEP_RESET_BACKUP_DR, BLE_SLEEP_RESET_MARKER);
}

static void Feeder_RunCycle(void)
{
  FeederServo_MoveTo(FEEDER_SERVO_OPEN_US, HATCH_OPEN_TIME_MS);
  FeederServo_MoveTo(FEEDER_SERVO_CLOSED_US, HATCH_CLOSE_SETTLE_MS);
  reload_hatch_open = 0U;
  reload_close_step = 0U;
}
