#ifndef FEEDER_SERVO_H
#define FEEDER_SERVO_H

#include "main.h"

#define FEEDER_SERVO_CLOSED_US 1000U
#define FEEDER_SERVO_OPEN_US   2000U

void FeederServo_MoveTo(uint16_t pulse_us, uint32_t settle_ms);

#endif /* FEEDER_SERVO_H */
