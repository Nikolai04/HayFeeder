# HayFeeder Firmware

Low-power STM32WB55 hay-feeder controller with RTC scheduling, manual reload control, BLE time/schedule setup, and switched servo power.

## Features

- Opens a servo-driven hatch at three daily feed times.
- Uses the internal RTC with an external LSE crystal.
- Sleeps in STOP2 between feed alarms or reload-button events.
- Enables BLE setup mode only on demand.
- Syncs clock and feed schedule from the Android/iOS companion apps.
- Switches servo power through a relay/module or load switch to reduce sleep current.

## Hardware

- MCU: STM32WB55CGU6, UFQFPN48
- Servo PWM: `PA0` / `TIM2_CH1`
- Reload switch input: `PA1` / `EXTI1`, switch to GND, internal pull-up enabled
- Servo power enable: `PA2`, active high
- Serial console: `USART1`, `PB6` TX / `PB7` RX, 115200 8N1
- RTC clock: 32.768 kHz LSE crystal on `PC14` / `PC15`
- Debug/programming: SWD on `PA13` / `PA14`

## Wiring

### Reload Switch

```text
PA1 / RELOAD_BUTTON ---- switch ---- GND
```

### Servo With KY-019 Relay Module

```text
KY-019 VCC -> 5V
KY-019 GND -> common GND
KY-019 S   -> PA2 / SERVO_POWER_EN

Relay COM  -> 5V servo supply
Relay NO   -> servo red wire
Servo GND  -> common GND
Servo PWM  -> PA0 / TIM2_CH1
```

Use `COM` and `NO` so the servo is normally unpowered. Keep the STM32 ground, relay-module ground, servo ground, and supply ground connected together. Do not route the servo's main ground current through thin MCU-board traces if avoidable.

## Pin Numbers

For STM32WB55CGU6 UFQFPN48:

```text
PA0  / servo PWM     = pin 9
PA1  / reload switch = pin 10
PA2  / servo power   = pin 11
PB6  / USART1 TX     = pin 46
PB7  / USART1 RX     = pin 47
PC14 / LSE in        = pin 2
PC15 / LSE out       = pin 3
PA13 / SWDIO         = pin 39
PA14 / SWCLK         = pin 41
NRST                 = pin 7
GND                  = exposed pad / pin 49
```

## Behavior

Default feed times:

```text
14:00
19:00
23:00
```

At each feed time:

1. Enables servo power on `PA2`.
2. Waits briefly for relay/servo power.
3. Starts TIM2 PWM.
4. Opens the hatch.
5. Holds the hatch open.
6. Closes the hatch.
7. Keeps PWM/power briefly after the move finishes.
8. Stops PWM and turns servo power off.
9. Schedules the next RTC alarm and enters STOP2.

The reload switch opens the hatch while held and closes it when released. A quick open/close sequence twice within 10 seconds enables BLE setup mode for 10 minutes.

## Servo Settings

Servo pulse widths are in `Core/Inc/feeder_servo.h`:

```c
#define FEEDER_SERVO_CLOSED_US 1000U
#define FEEDER_SERVO_OPEN_US   2000U
```

Servo power timing is in `Core/Src/feeder_servo.c`:

```c
#define SERVO_POWER_SETTLE_MS    50U
#define SERVO_POWER_OFF_DELAY_MS 300U
```

Feed/reload timing is in `Core/Src/feeder_app.c`:

```c
#define HATCH_OPEN_TIME_MS        5000U
#define HATCH_CLOSE_SETTLE_MS     200U
#define RELOAD_OPEN_HOLD_MS       100U
```

## BLE Protocol

The feeder advertises as:

```text
HayFeeder
```

BLE setup mode must be enabled first by opening/closing the reload switch twice within 10 seconds.

Writable characteristic:

- Service UUID: `0000fe40-cc7a-482a-984a-7f2ed5b3e58f`
- Characteristic UUID: `0000fe41-8e22-4541-9d4c-21edae82ed19`

Accepted UTF-8 commands:

```text
T:14:32:05
T:2026-05-10 14:32:05
F:14:00,19:00,23:00
S
```

- `T:` sets the RTC time.
- `F:` sets the three daily feed times and stores them in RTC backup registers.
- `S` exits BLE setup mode and returns to low-power operation.

The Android and iPhone apps use this protocol automatically.

## STM32WB BLE Notes

BLE requires the STM32WB CPU2 wireless stack to be installed with STM32CubeProgrammer/FUS. The board also needs the correct RF/HSE clock hardware for reliable BLE advertising.

If serial output stops after `BLE APPE init requested`, or prints `BLE CPU2 no ready event after 5s`, CPU1 is running but CPU2 did not start the wireless firmware.

## Build

Open this folder in STM32CubeIDE as an existing STM32CubeIDE project.

Typical build options:

- Use STM32CubeIDE's **Build** button.
- Or build the generated Debug makefile from the `Debug` directory if your STM32CubeIDE toolchain is on `PATH`.

## Flash

Flash with STM32CubeIDE or STM32CubeProgrammer over SWD:

```text
SWDIO -> PA13
SWCLK -> PA14
NRST  -> NRST
GND   -> GND
3V3   -> target voltage sense
```

The firmware has been tested with an ST-LINK/V3 using SWD under reset.

## Project Layout

- `HayFeeder.ioc`: STM32CubeMX configuration
- `Core/Src/main.c`: CubeMX startup, clock setup, peripheral init, HAL callback forwarding
- `Core/Src/feeder_app.c`: feeder state machine, reload switch, STOP2 sleep, BLE setup mode
- `Core/Src/feeder_schedule.c`: RTC clock, feed schedule storage, next alarm, BLE schedule/time commands
- `Core/Src/feeder_servo.c`: servo PWM and servo power switching
- `STM32_WPAN/App`: BLE advertising and custom characteristic handling
- `Middlewares_BLE/Src`: copied BLE middleware sources used by this project
