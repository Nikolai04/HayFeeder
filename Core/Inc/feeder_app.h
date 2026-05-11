#ifndef FEEDER_APP_H
#define FEEDER_APP_H

#include "main.h"

void FeederApp_Init(uint8_t hse_ready);
void FeederApp_Process(void);
void FeederApp_OnFeedAlarm(RTC_HandleTypeDef *rtc);
void FeederApp_OnReloadSwitchInterrupt(uint16_t gpio_pin);
void FeederApp_RequestBleSleep(void);

#endif /* FEEDER_APP_H */
