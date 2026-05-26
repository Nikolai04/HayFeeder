#include "feeder_servo.h"

#define SERVO_POWER_SETTLE_MS 300U

extern TIM_HandleTypeDef htim2;

static void FeederServo_SetPower(GPIO_PinState state)
{
  HAL_GPIO_WritePin(SERVO_POWER_EN_GPIO_Port, SERVO_POWER_EN_Pin, state);
}

static void FeederServo_SetPulse(uint16_t pulse_us)
{
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse_us);
}

void FeederServo_MoveTo(uint16_t pulse_us, uint32_t settle_ms)
{
  FeederServo_SetPower(GPIO_PIN_SET);
  HAL_Delay(SERVO_POWER_SETTLE_MS);

  FeederServo_SetPulse(pulse_us);
  if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_Delay(settle_ms);
  HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
  FeederServo_SetPower(GPIO_PIN_RESET);
}
