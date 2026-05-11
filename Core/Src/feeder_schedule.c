#include "feeder_schedule.h"

#include <stdio.h>

#define FEED_SCHEDULE_COUNT         3U
#define FEED_SCHEDULE_BACKUP_MARKER 0x4846U
#define RTC_CLOCK_BACKUP_MARKER     0x434C4B31U
#define RTC_CLOCK_BACKUP_DR         RTC_BKP_DR3

extern RTC_HandleTypeDef hrtc;

static uint16_t feed_schedule_minutes[FEED_SCHEDULE_COUNT] = {
  14U * 60U,
  19U * 60U,
  23U * 60U
};

static void FeederSchedule_SetDefaultClock(void);
static void FeederSchedule_Load(void);
static void FeederSchedule_Save(void);
static uint8_t FeederSchedule_ParseFeedSchedule(const uint8_t *payload, uint8_t length);
static uint8_t FeederSchedule_ParseTimeOfDay(const uint8_t *payload, uint16_t *minutes);
static uint8_t FeederSchedule_ParseAsciiTime(const uint8_t *payload, uint8_t length, RTC_TimeTypeDef *time, RTC_DateTypeDef *date);
static uint8_t FeederSchedule_ParseBinaryTime(const uint8_t *payload, uint8_t length, RTC_TimeTypeDef *time, RTC_DateTypeDef *date);

void FeederSchedule_Init(void)
{
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_CLOCK_BACKUP_DR) != RTC_CLOCK_BACKUP_MARKER)
  {
    FeederSchedule_SetDefaultClock();
  }
  FeederSchedule_Load();
}

void FeederSchedule_SetNextAlarm(void)
{
  RTC_TimeTypeDef now_time = {0};
  RTC_DateTypeDef now_date = {0};
  RTC_AlarmTypeDef alarm = {0};
  uint16_t now_minutes;
  uint16_t next_minutes = feed_schedule_minutes[0];

  if (HAL_RTC_GetTime(&hrtc, &now_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_RTC_GetDate(&hrtc, &now_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }

  now_minutes = ((uint16_t)now_time.Hours * 60U) + now_time.Minutes;
  for (uint32_t i = 0; i < FEED_SCHEDULE_COUNT; i++)
  {
    if (feed_schedule_minutes[i] > now_minutes)
    {
      next_minutes = feed_schedule_minutes[i];
      break;
    }
  }

  HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);

  alarm.Alarm = RTC_ALARM_A;
  alarm.AlarmTime.Hours = (uint8_t)(next_minutes / 60U);
  alarm.AlarmTime.Minutes = (uint8_t)(next_minutes % 60U);
  alarm.AlarmTime.Seconds = 0U;
  alarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;
  alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;

  if (HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }

  printf("Next feed set to %02u:%02u\r\n",
         (unsigned int)alarm.AlarmTime.Hours,
         (unsigned int)alarm.AlarmTime.Minutes);
}

void FeederSchedule_PrintCurrentTime(const char *label)
{
  RTC_TimeTypeDef time = {0};
  RTC_DateTypeDef date = {0};

  if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }

  printf("%s RTC %02u:%02u:%02u\r\n",
         label,
         (unsigned int)time.Hours,
         (unsigned int)time.Minutes,
         (unsigned int)time.Seconds);
}

void HayFeeder_BleSetTime(const uint8_t *payload, uint8_t length)
{
  RTC_TimeTypeDef time = {0};
  RTC_DateTypeDef date = {0};

  if ((length >= 2U) && (payload[0] == 'F') && (payload[1] == ':'))
  {
    if (FeederSchedule_ParseFeedSchedule(&payload[2], (uint8_t)(length - 2U)) == 0U)
    {
      printf("BLE feed schedule rejected\r\n");
      return;
    }

    FeederSchedule_Save();
    FeederSchedule_SetNextAlarm();
    printf("BLE FEED %02u:%02u %02u:%02u %02u:%02u\r\n",
           (unsigned int)(feed_schedule_minutes[0] / 60U),
           (unsigned int)(feed_schedule_minutes[0] % 60U),
           (unsigned int)(feed_schedule_minutes[1] / 60U),
           (unsigned int)(feed_schedule_minutes[1] % 60U),
           (unsigned int)(feed_schedule_minutes[2] / 60U),
           (unsigned int)(feed_schedule_minutes[2] % 60U));
    return;
  }

  if ((length >= 2U) && (payload[0] == 'T') && (payload[1] == ':'))
  {
    payload = &payload[2];
    length = (uint8_t)(length - 2U);
  }

  if ((FeederSchedule_ParseBinaryTime(payload, length, &time, &date) == 0U) &&
      (FeederSchedule_ParseAsciiTime(payload, length, &time, &date) == 0U))
  {
    printf("BLE time rejected\r\n");
    return;
  }

  if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_CLOCK_BACKUP_DR, RTC_CLOCK_BACKUP_MARKER);

  FeederSchedule_SetNextAlarm();
  FeederSchedule_PrintCurrentTime("BLE TIME");
}

static uint8_t FeederSchedule_ParseFeedSchedule(const uint8_t *payload, uint8_t length)
{
  uint16_t parsed[FEED_SCHEDULE_COUNT];

  if (length != 17U)
  {
    return 0U;
  }

  if ((payload[5] != ',') || (payload[11] != ','))
  {
    return 0U;
  }

  if ((FeederSchedule_ParseTimeOfDay(&payload[0], &parsed[0]) == 0U) ||
      (FeederSchedule_ParseTimeOfDay(&payload[6], &parsed[1]) == 0U) ||
      (FeederSchedule_ParseTimeOfDay(&payload[12], &parsed[2]) == 0U))
  {
    return 0U;
  }

  if ((parsed[0] >= parsed[1]) || (parsed[1] >= parsed[2]))
  {
    return 0U;
  }

  for (uint32_t i = 0U; i < FEED_SCHEDULE_COUNT; i++)
  {
    feed_schedule_minutes[i] = parsed[i];
  }

  return 1U;
}

static uint8_t FeederSchedule_ParseTimeOfDay(const uint8_t *payload, uint16_t *minutes)
{
  uint8_t hour;
  uint8_t minute;

  if ((payload[0] < '0') || (payload[0] > '9') ||
      (payload[1] < '0') || (payload[1] > '9') ||
      (payload[2] != ':') ||
      (payload[3] < '0') || (payload[3] > '9') ||
      (payload[4] < '0') || (payload[4] > '9'))
  {
    return 0U;
  }

  hour = (uint8_t)((payload[0] - '0') * 10U + (payload[1] - '0'));
  minute = (uint8_t)((payload[3] - '0') * 10U + (payload[4] - '0'));
  if ((hour > 23U) || (minute > 59U))
  {
    return 0U;
  }

  *minutes = ((uint16_t)hour * 60U) + minute;
  return 1U;
}

static uint8_t FeederSchedule_ParseBinaryTime(const uint8_t *payload, uint8_t length, RTC_TimeTypeDef *time, RTC_DateTypeDef *date)
{
  uint16_t year;

  if (length == 3U)
  {
    if ((payload[0] > 23U) || (payload[1] > 59U) || (payload[2] > 59U))
    {
      return 0U;
    }

    time->Hours = payload[0];
    time->Minutes = payload[1];
    time->Seconds = payload[2];
    time->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time->StoreOperation = RTC_STOREOPERATION_RESET;

    date->WeekDay = RTC_WEEKDAY_MONDAY;
    date->Month = RTC_MONTH_JANUARY;
    date->Date = 1U;
    date->Year = 26U;

    return 1U;
  }

  if (length != 7U)
  {
    return 0U;
  }

  year = ((uint16_t)payload[0] << 8) | payload[1];
  if ((year < 2000U) || (year > 2099U) ||
      (payload[2] < 1U) || (payload[2] > 12U) ||
      (payload[3] < 1U) || (payload[3] > 31U) ||
      (payload[4] > 23U) || (payload[5] > 59U) || (payload[6] > 59U))
  {
    return 0U;
  }

  time->Hours = payload[4];
  time->Minutes = payload[5];
  time->Seconds = payload[6];
  time->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time->StoreOperation = RTC_STOREOPERATION_RESET;

  date->WeekDay = RTC_WEEKDAY_MONDAY;
  date->Month = payload[2];
  date->Date = payload[3];
  date->Year = (uint8_t)(year - 2000U);

  return 1U;
}

static uint8_t FeederSchedule_ParseAsciiTime(const uint8_t *payload, uint8_t length, RTC_TimeTypeDef *time, RTC_DateTypeDef *date)
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;

  if (length == 8U)
  {
    if ((payload[2] != ':') || (payload[5] != ':'))
    {
      return 0U;
    }

    for (uint8_t i = 0U; i < 8U; i++)
    {
      if ((i == 2U) || (i == 5U))
      {
        continue;
      }
      if ((payload[i] < '0') || (payload[i] > '9'))
      {
        return 0U;
      }
    }

    hour = (uint8_t)((payload[0] - '0') * 10U + (payload[1] - '0'));
    minute = (uint8_t)((payload[3] - '0') * 10U + (payload[4] - '0'));
    second = (uint8_t)((payload[6] - '0') * 10U + (payload[7] - '0'));
    if ((hour > 23U) || (minute > 59U) || (second > 59U))
    {
      return 0U;
    }

    time->Hours = hour;
    time->Minutes = minute;
    time->Seconds = second;
    time->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time->StoreOperation = RTC_STOREOPERATION_RESET;

    date->WeekDay = RTC_WEEKDAY_MONDAY;
    date->Month = RTC_MONTH_JANUARY;
    date->Date = 1U;
    date->Year = 26U;

    return 1U;
  }

  if (length < 19U)
  {
    return 0U;
  }

  if ((payload[4] != '-') || (payload[7] != '-') ||
      ((payload[10] != ' ') && (payload[10] != 'T')) ||
      (payload[13] != ':') || (payload[16] != ':'))
  {
    return 0U;
  }

  for (uint8_t i = 0U; i < 19U; i++)
  {
    if ((i == 4U) || (i == 7U) || (i == 10U) || (i == 13U) || (i == 16U))
    {
      continue;
    }
    if ((payload[i] < '0') || (payload[i] > '9'))
    {
      return 0U;
    }
  }

  year = (uint16_t)((payload[0] - '0') * 1000U + (payload[1] - '0') * 100U +
                    (payload[2] - '0') * 10U + (payload[3] - '0'));
  month = (uint8_t)((payload[5] - '0') * 10U + (payload[6] - '0'));
  day = (uint8_t)((payload[8] - '0') * 10U + (payload[9] - '0'));
  hour = (uint8_t)((payload[11] - '0') * 10U + (payload[12] - '0'));
  minute = (uint8_t)((payload[14] - '0') * 10U + (payload[15] - '0'));
  second = (uint8_t)((payload[17] - '0') * 10U + (payload[18] - '0'));

  if ((year < 2000U) || (year > 2099U) ||
      (month < 1U) || (month > 12U) ||
      (day < 1U) || (day > 31U) ||
      (hour > 23U) || (minute > 59U) || (second > 59U))
  {
    return 0U;
  }

  time->Hours = hour;
  time->Minutes = minute;
  time->Seconds = second;
  time->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time->StoreOperation = RTC_STOREOPERATION_RESET;

  date->WeekDay = RTC_WEEKDAY_MONDAY;
  date->Month = month;
  date->Date = day;
  date->Year = (uint8_t)(year - 2000U);

  return 1U;
}

static void FeederSchedule_Load(void)
{
  uint32_t marker;
  uint32_t packed_a;
  uint32_t packed_b;
  uint16_t saved[FEED_SCHEDULE_COUNT];

  marker = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
  if (marker != FEED_SCHEDULE_BACKUP_MARKER)
  {
    return;
  }

  packed_a = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
  packed_b = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
  saved[0] = (uint16_t)(packed_a & 0xFFFFU);
  saved[1] = (uint16_t)((packed_a >> 16U) & 0xFFFFU);
  saved[2] = (uint16_t)(packed_b & 0xFFFFU);

  if ((saved[0] < saved[1]) && (saved[1] < saved[2]) && (saved[2] < (24U * 60U)))
  {
    for (uint32_t i = 0U; i < FEED_SCHEDULE_COUNT; i++)
    {
      feed_schedule_minutes[i] = saved[i];
    }
  }
}

static void FeederSchedule_Save(void)
{
  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, FEED_SCHEDULE_BACKUP_MARKER);
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1,
                      ((uint32_t)feed_schedule_minutes[1] << 16U) | feed_schedule_minutes[0]);
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, feed_schedule_minutes[2]);
}

static void FeederSchedule_SetDefaultClock(void)
{
  RTC_TimeTypeDef time = {0};
  RTC_DateTypeDef date = {0};

  time.Hours = 12U;
  time.Minutes = 00U;
  time.Seconds = 00U;
  time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time.StoreOperation = RTC_STOREOPERATION_RESET;

  date.WeekDay = RTC_WEEKDAY_MONDAY;
  date.Month = RTC_MONTH_JANUARY;
  date.Date = 1;
  date.Year = 26;

  if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_CLOCK_BACKUP_DR, RTC_CLOCK_BACKUP_MARKER);
}
