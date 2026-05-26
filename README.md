# HayFeeder

Low-power hay feeder controller for STM32CubeIDE.

## Hardware

- MCU: STM32WB55CGU6, UFQFPN48
- Servo PWM: PA0 / TIM2_CH1
- Servo power relay/module enable: PA2, active high
- Reload holding switch: PA1 / EXTI1, switch to GND, internal pull-up enabled
- Serial console: USART1 on PB6 TX / PB7 RX, 115200 8N1
- RTC clock: LSE 32.768 kHz crystal on PC14 / PC15
- System clock: internal HSI for the feeder logic
- BLE radio: STM32WB wireless stack on CPU2, with suitable RF/HSE clock hardware
- Debug: SWD on PA13 / PA14

## Behavior

The controller opens the hatch at:

- 14:00
- 19:00
- 23:00

The reload holding switch controls the hatch manually:

- Hold/close the switch to GND to open.
- Release/open the switch to close.
- Both switch edges wake the MCU from STOP2.
- Quick open/close twice within 10 seconds enables BLE setup mode for 10 minutes.
- The hatch only opens when the switch is held closed for about 1 second, so quick BLE setup clicks do not move the servo.

At startup it drives the servo to the closed position, then stops TIM2 PWM.

At each feeding time it:

1. Starts TIM2 PWM.
2. Drives the servo to the open position.
3. Holds the hatch open for 5 seconds.
4. Drives the servo back closed.
5. Stops TIM2 PWM.
6. Sets the next RTC alarm and enters STOP2 mode.

## Low Power

The firmware is set up for low power:

- CPU sleeps in STOP2 between RTC alarms.
- SysTick is suspended while sleeping.
- Clocks are restored after wakeup.
- PA2 powers the servo relay/module only while moving the servo.
- TIM2 only runs while moving the servo.
- PA1 is kept as the wake-capable reload switch.
- USART1 is enabled for serial time prints.
- Unused PB8 and PB9 are configured as analog inputs.

Important: the MCU can sleep, but a powered servo can still draw current. For a battery feeder, power the servo through a MOSFET or load switch and only enable servo power during the feed cycle.

## Servo Settings

Servo pulse widths are defined in `Core/Inc/feeder_servo.h`, and the movement timings are defined in `Core/Src/feeder_app.c`:

```c
#define FEEDER_SERVO_CLOSED_US      1000U
#define FEEDER_SERVO_OPEN_US        2000U
#define SERVO_POWER_SETTLE_MS       50U
#define SERVO_POWER_OFF_DELAY_MS    300U
#define HATCH_OPEN_TIME_MS          5000U
#define HATCH_CLOSE_SETTLE_MS       200U
#define RELOAD_BUTTON_DEBOUNCE_MS   30U
```

Change `FEEDER_SERVO_CLOSED_US` and `FEEDER_SERVO_OPEN_US` if the hatch moves the wrong direction or needs different travel.

For a KY-019 relay module:

```text
KY-019 VCC -> 5V
KY-019 GND -> common GND
KY-019 S   -> PA2 / SERVO_POWER_EN
Relay COM  -> 5V servo supply
Relay NO   -> servo red wire
Servo GND  -> common GND
Servo PWM  -> PA0 / TIM2_CH1
```

## Serial Console

USART1 prints the current RTC time at startup and when the MCU wakes for a feed alarm or reload switch change.
It also prints simple status messages for sleep, BLE setup mode, and the next scheduled feed time.

- TX: PB6
- RX: PB7
- Settings: 115200 baud, 8 data bits, no parity, 1 stop bit

Example:

```text
Startup RTC 12:00:00
Reload switch: quick open/close twice within 10s enables BLE setup
Reload switch: hold closed for 1s to open hatch, release to close
BLE is off until setup mode is enabled
Next feed set to 14:00
Sleep: waiting for feed alarm or reload switch
Wake RTC 14:00:00
Feed RTC 14:00:00
```

## RTC Time

The firmware starts the RTC at 12:00:00 on every reset, then BLE can update it to the real time.

## BLE Time Sync

The feeder advertises as:

```text
HayFeeder
```

Use an app such as nRF Connect or ST BLE Toolbox:

1. Scan for and connect to `HayFeeder`.
2. Open the custom service.
3. Write the current time to the writable custom characteristic as UTF-8 text.
   Use either the full date/time:

```text
2026-05-10 14:32:05
```

This also accepts `2026-05-10T14:32:05`.

For quick testing you can also write only:

```text
14:32:05
```

The characteristic accepts both normal write and write without response.

The custom Android app in:

```text
C:\Users\Nikolai\Documents\New project\HayFeederApp
```

uses the same BLE characteristic and can sync phone time automatically.

First enable BLE setup mode on the feeder by opening/closing the reload switch twice within 10 seconds. The serial console prints:

```text
BLE setup edge 1/4
BLE setup edge 2/4
BLE setup edge 3/4
BLE setup edge 4/4
BLE setup mode enabled for 10 min: connect with HayFeeder app now
```

It writes:

```text
T:2026-05-10 14:32:05
```

for clock sync, and:

The Android app sends the shorter clock form:

```text
T:14:32:05
```

because only the time of day matters for the daily feed schedule.

```text
F:14:00,19:00,23:00
```

to change the three daily feeding times. The feeding schedule is stored in RTC backup registers and is reloaded on reset while the backup domain is kept.

The Android app sends:

```text
S
```

when you press disconnect, so the feeder resets out of BLE setup mode and returns to normal low-power sleep.

When the write succeeds, the serial console prints:

```text
BLE TIME RTC 14:32:05
```

The firmware then reschedules the next feed alarm for 14:00, 19:00, or 23:00.

The writable characteristic is the BLE_Custom LED characteristic reused as a time-set characteristic:

- Service UUID: `0000fe40-cc7a-482a-984a-7f2ed5b3e58f`
- Characteristic UUID: `0000fe41-8e22-4541-9d4c-21edae82ed19`

Important STM32WB notes:

- BLE requires the STM32WB wireless stack to be installed on CPU2 with STM32CubeProgrammer/FUS.
- BLE also needs the board's RF clock setup to be correct. The RTC-only feeder could run without HSE, but BLE generally needs the proper RF/HSE clock hardware to advertise reliably.
- If serial stops after `BLE APPE init requested`, or prints `BLE CPU2 no ready event after 5s`, CPU1 is running but CPU2 did not start the wireless firmware. Check the STM32WB FUS/wireless stack installation in STM32CubeProgrammer before debugging the phone scan.
- A successful BLE start should print `CPU2 ready`, `CPU2 wireless firmware running`, and `BLE advertising OK: HayFeeder`.

## Project Files

- `HayFeeder.ioc`: STM32CubeMX configuration
- `Core/Src/main.c`: CubeMX startup, clock setup, peripheral init, HAL callback forwarding
- `Core/Src/feeder_app.c`: feeder runtime state machine, reload switch, STOP2 sleep, BLE setup mode
- `Core/Src/feeder_schedule.c`: RTC clock setup, feed schedule storage, next alarm setup, BLE time/schedule commands
- `Core/Src/feeder_servo.c`: TIM2 servo PWM movement helper
- `STM32_WPAN/App`: BLE advertising and custom characteristic handling
- `Middlewares_BLE/Src`: minimal copied BLE middleware sources needed by this project
- `Core/Src/stm32wbxx_hal_msp.c`: peripheral MSP setup
- `Core/Src/stm32wbxx_it.c`: interrupt handlers

## Build

Open the `HayFeeder` folder in STM32CubeIDE or import it as an existing STM32CubeIDE project.

The Debug build has been verified locally with the generated makefile:

```powershell
cd 'C:\Users\Nikolai\Documents\New project\HayFeeder\Debug'
make -j8 all
```

Expected result:

```text
Build Finished. 0 errors, 0 warnings.
```

## Backup

A backup made before the BLE changes is here:

```text
C:\Users\Nikolai\Documents\New project\HayFeeder_backup_20260510_115018
```
