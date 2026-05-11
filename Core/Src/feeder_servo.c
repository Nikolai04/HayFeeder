#include "feeder_servo.h"

extern TIM_HandleTypeDef htim2;

static void FeederServo_SetPulse(uint16_t pulse_us)
{
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse_us);
}

void FeederServo_MoveTo(uint16_t pulse_us, uint32_t settle_ms)
{
  FeederServo_SetPulse(pulse_us);
  if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_Delay(settle_ms);
  HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
}
