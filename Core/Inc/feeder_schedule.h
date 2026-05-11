#ifndef FEEDER_SCHEDULE_H
#define FEEDER_SCHEDULE_H

#include "main.h"

void FeederSchedule_Init(void);
void FeederSchedule_SetNextAlarm(void);
void FeederSchedule_PrintCurrentTime(const char *label);
void HayFeeder_BleSetTime(const uint8_t *payload, uint8_t length);

#endif /* FEEDER_SCHEDULE_H */
