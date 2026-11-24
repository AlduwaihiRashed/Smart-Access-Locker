# Smart Access Locker

COE411 – Embedded and Cyber Physical Systems, Fall 2026 (American University of Sharjah)

A small locker that opens with a 4-digit PIN or an RFID card. A tilt (ball) switch on the door detects tampering while the door is locked. Every event is shown on the LCD and sent over Wi-Fi as a phone notification.

## How it works

| Event | LCD | RGB LED | Buzzer | Servo | Phone |
|---|---|---|---|---|---|
| Correct PIN or allowed card | ACCESS GRANTED | Green | Success sound | Opens, holds 5 s, closes | "Locker opened" |
| Wrong PIN or unknown card | ACCESS DENIED | Red | Fail sound | Stays closed | "Access denied" |
| Door moved while locked | TAMPER ALERT! | Red | Alarm sound | Stays closed | "TAMPER ALERT" |

- Type 4 digits on the keypad. Each digit shows as `*`. `#` clears the digits.
- The STM32 sends one text line per event to the NodeMCU over UART. The NodeMCU publishes it to MQTT and sends a notification to the ntfy app.
- The same lines are also printed to the PC (Tera Term, 115200 baud) through the ST-Link.

## Hardware

- STM32 NUCLEO-L476RG
- NodeMCU ESP8266
- 4x4 membrane keypad
- 16x2 I2C LCD (address 0x27)
- 125 kHz serial RFID reader (EN / SOUT pins)
- Positional servo (separate 5 V supply, shared GND)
- KY-020 tilt (ball) switch
- RGB LED module
- Passive buzzer

## Pin map

| Part | STM32 pin | Setup in the .ioc |
|---|---|---|
| Keypad rows | PC0–PC3 | GPIO output |
| Keypad columns | PC4–PC7 | GPIO input, pull-down |
| LCD SCL / SDA | PB8 / PB9 | I2C1, 100 kHz |
| RFID SOUT (through a voltage divider) | PC11 | UART4 RX, 2400 baud, interrupt |
| RFID EN | PB5 | GPIO output (low = reader on) |
| Tilt switch S | PA4 | EXTI4, both edges, pull-up |
| Servo signal | PA6 | TIM3 CH1 PWM, 50 Hz (PSC 79, ARR 19999) |
| RGB red / green | PA8 / PA9 | TIM1 CH1 / CH2 PWM, 1 kHz |
| Buzzer S | PB10 | TIM2 CH3 PWM (tone set in code) |
| NodeMCU RX / TX | PB6 / PB7 | USART1, 9600 baud |
| Tera Term | PA2 / PA3 | USART2, 115200 baud |

The keypad on our board is wired in the opposite order, so `keypad.c` lists the row and column pins backwards.

## Software

FreeRTOS (CMSIS-RTOS v2) with 4 tasks, all set up in `Project.ioc`:

| Task | Job |
|---|---|
| keypadTask | Scans the keypad every 20 ms, shows `*`, checks the PIN |
| rfidTask | Waits for a card ID from the UART interrupt, checks it |
| doorTask | Runs the LCD, LED, buzzer and servo for each event |
| wifiTask | Sends one text line per event to the NodeMCU and Tera Term |

- `doorQueue`: keypad task, RFID task and tilt interrupt → door task
- `wifiQueue`: door task → Wi-Fi task
- `lcdMutex`: the keypad task and the door task both write to the LCD
- `rfidSemaphore`: the UART interrupt wakes the RFID task when a full card ID arrives

All our code is in `STM32_Project/Core/Src/freertos.c` (inside the USER CODE sections). `keypad.c` and `lcd_i2c.c` are the course libraries from Lab 3. The PIN and the allowed card are set at the top of `freertos.c` (`CORRECT_PIN`, `ALLOWED_CARD`).

## How to run

### STM32

1. In STM32CubeIDE: File > Import > General > Existing Projects into Workspace, and select the `STM32_Project` folder.
2. Build and run on the NUCLEO-L476RG.
3. Open Tera Term on the ST-Link COM port at 115200 baud to see the events.

### NodeMCU

1. In Arduino IDE, install the board package "esp8266 by ESP8266 Community" (board URL `https://arduino.esp8266.com/stable/package_esp8266com_index.json`) and the library "PubSubClient" by Nick O'Leary.
2. Open `NodeMCU/SmartLockerWifi/SmartLockerWifi.ino` and put your Wi-Fi name and password in `WIFI_SSID` and `WIFI_PASS` (2.4 GHz network).
3. Select the board "NodeMCU 1.0 (ESP-12E Module)". Unplug the wire on the RX pin while uploading.
4. Connect STM32 PB6 → NodeMCU RX and GND → GND.

### Notifications

- Install the ntfy app and subscribe to the topic `coe411-project`.
- MQTT messages are on `broker.emqx.io`, topic `coe411/f26/locker-7k3q9/events` (we used MQTTX to check them).
