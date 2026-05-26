#ifndef FEEDER_SERVO_H
#define FEEDER_SERVO_H

#include "main.h"

#define FEEDER_SERVO_CLOSED_US 550U
#define FEEDER_SERVO_OPEN_US   1450U
#define FEEDER_SERVO_HALF_US   ((FEEDER_SERVO_CLOSED_US + FEEDER_SERVO_OPEN_US) / 2U)

void FeederServo_MoveTo(uint16_t pulse_us, uint32_t settle_ms);

#endif /* FEEDER_SERVO_H */
