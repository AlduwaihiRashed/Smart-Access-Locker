/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "keypad.h"     // Lab 3 keypad library
#include "lcd_i2c.h"    // Lab 3 LCD library
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ---------- Settings we can change ---------- */
#define CORRECT_PIN    "0000"          // 4-digit PIN that opens the door
#define ALLOWED_CARD   "2600D3A790"    // ID of our RFID card (the other card, 290093F110, is the "wrong card")

#define SERVO_CLOSED   1000            // servo pulse (microseconds) when the door is locked
#define SERVO_OPEN     2000            // servo pulse (microseconds) when the door is open
#define DOOR_OPEN_MS   5000            // the door stays open for 5 seconds
#define TILT_WAIT_MS   3000            // ignore the tilt switch for 3 seconds after an alert

/* ---------- Events sent through the queues ---------- */
#define EVT_PIN_OK     1               // correct PIN typed
#define EVT_PIN_FAIL   2               // wrong PIN typed
#define EVT_CARD_OK    3               // allowed card tapped
#define EVT_CARD_FAIL  4               // unknown card tapped
#define EVT_TAMPER     5               // door moved while it was locked
#define EVT_LOCKED     6               // door locked again (Wi-Fi message only)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* PIN typed on the keypad (Lab 3) */
char attempt[5];                       // 4 digits + '\0'
uint8_t pin_index = 0;                 // number of digits typed so far

/* RFID reader: it sends 0x0A, then 10 ID characters, then 0x0D */
uint8_t rfid_byte;                     // last byte received from the reader
char rfid_id[11];                      // card ID (10 characters + '\0')
uint8_t rfid_index = 0;                // number of ID characters received so far

/* Tilt (ball) switch */
volatile uint8_t door_open = 0;        // 1 while the door is open (tilt is not a tamper then)
volatile uint32_t last_tilt = 0;       // time of the last tamper alert
/* USER CODE END Variables */
/* Definitions for keypadTask */
osThreadId_t keypadTaskHandle;
const osThreadAttr_t keypadTask_attributes = {
  .name = "keypadTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for rfidTask */
osThreadId_t rfidTaskHandle;
const osThreadAttr_t rfidTask_attributes = {
  .name = "rfidTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for doorTask */
osThreadId_t doorTaskHandle;
const osThreadAttr_t doorTask_attributes = {
  .name = "doorTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for wifiTask */
osThreadId_t wifiTaskHandle;
const osThreadAttr_t wifiTask_attributes = {
  .name = "wifiTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for doorQueue */
osMessageQueueId_t doorQueueHandle;
const osMessageQueueAttr_t doorQueue_attributes = {
  .name = "doorQueue"
};
/* Definitions for wifiQueue */
osMessageQueueId_t wifiQueueHandle;
const osMessageQueueAttr_t wifiQueue_attributes = {
  .name = "wifiQueue"
};
/* Definitions for lcdMutex */
osMutexId_t lcdMutexHandle;
const osMutexAttr_t lcdMutex_attributes = {
  .name = "lcdMutex"
};
/* Definitions for rfidSemaphore */
osSemaphoreId_t rfidSemaphoreHandle;
const osSemaphoreAttr_t rfidSemaphore_attributes = {
  .name = "rfidSemaphore"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void play_tone(uint32_t freq, uint32_t duration_ms);
void SetColor(uint8_t red, uint8_t green);
void Servo_WritePulseUS(uint16_t pulse_us);
void LCD_ShowMessage(char *line1, char *line2);
/* USER CODE END FunctionPrototypes */

void StartKeypadTask(void *argument);
void StartRfidTask(void *argument);
void StartDoorTask(void *argument);
void StartWifiTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* This runs once, before the tasks start */

  /* Start the PWM outputs (Lab 4 and Lab 7) */
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);   // RGB LED red   (PA8)
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);   // RGB LED green (PA9)
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);   // passive buzzer (PB10), silent for now
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);   // servo (PA6)

  SetColor(0, 0);                             // LED off
  Servo_WritePulseUS(SERVO_CLOSED);           // door starts locked

  /* LCD and keypad (Lab 3) */
  LCD_Init();
  LCD_ShowMessage("ENTER PIN / CARD", "");
  Keypad_Init();
  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of lcdMutex */
  lcdMutexHandle = osMutexNew(&lcdMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of rfidSemaphore */
  rfidSemaphoreHandle = osSemaphoreNew(1, 0, &rfidSemaphore_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of doorQueue */
  doorQueueHandle = osMessageQueueNew (10, sizeof(uint8_t), &doorQueue_attributes);

  /* creation of wifiQueue */
  wifiQueueHandle = osMessageQueueNew (10, sizeof(uint8_t), &wifiQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of keypadTask */
  keypadTaskHandle = osThreadNew(StartKeypadTask, NULL, &keypadTask_attributes);

  /* creation of rfidTask */
  rfidTaskHandle = osThreadNew(StartRfidTask, NULL, &rfidTask_attributes);

  /* creation of doorTask */
  doorTaskHandle = osThreadNew(StartDoorTask, NULL, &doorTask_attributes);

  /* creation of wifiTask */
  wifiTaskHandle = osThreadNew(StartWifiTask, NULL, &wifiTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartKeypadTask */
/**
  * @brief  Function implementing the keypadTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartKeypadTask */
void StartKeypadTask(void *argument)
{
  /* USER CODE BEGIN StartKeypadTask */
  /* keypadTask: reads the keypad and checks the PIN */
  char key;
  uint8_t event;

  for(;;)
  {
    key = Keypad_Scan();                      // Lab 3: returns the key, or 0 if no key

    if (key)
    {
      /* TEST: print every key to Tera Term (Lab 3 Part B echo test).
         Delete these 3 lines when all keys work. */
      char out[16];
      sprintf(out, "Key: %c\r\n", key);
      HAL_UART_Transmit(&huart2, (uint8_t *)out, strlen(out), 100);

      /* wait here if the door task is using the LCD (Lab 7 mutex) */
      osMutexAcquire(lcdMutexHandle, osWaitForever);

      if (key >= '0' && key <= '9')           // a digit was pressed
      {
        attempt[pin_index] = key;             // store the digit
        LCD_SetCursor(1, pin_index);
        LCD_PrintChar('*');                   // show * instead of the digit
        pin_index++;

        if (pin_index == 4)                   // 4 digits typed: check the PIN
        {
          attempt[4] = '\0';

          if (strcmp(attempt, CORRECT_PIN) == 0)
          {
            event = EVT_PIN_OK;
          }
          else
          {
            event = EVT_PIN_FAIL;
          }

          pin_index = 0;
          osMessageQueuePut(doorQueueHandle, &event, 0, 0);   // tell the door task
        }
      }
      else if (key == '#')                    // # clears the typed digits
      {
        pin_index = 0;
        LCD_SetCursor(1, 0);
        LCD_Print("    ");
      }

      osMutexRelease(lcdMutexHandle);
    }

    osDelay(20);                              // check the keypad every 20 ms
  }
  /* USER CODE END StartKeypadTask */
}

/* USER CODE BEGIN Header_StartRfidTask */
/**
* @brief Function implementing the rfidTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartRfidTask */
void StartRfidTask(void *argument)
{
  /* USER CODE BEGIN StartRfidTask */
  /* rfidTask: checks the card after the UART interrupt received a full ID */
  uint8_t event;

  HAL_GPIO_WritePin(RFID_EN_GPIO_Port, RFID_EN_Pin, GPIO_PIN_RESET);   // EN low = reader on
  HAL_UART_Receive_IT(&huart4, &rfid_byte, 1);                         // start receiving bytes

  for(;;)
  {
    /* sleep here until the interrupt releases the semaphore (Lab 7) */
    if (osSemaphoreAcquire(rfidSemaphoreHandle, osWaitForever) == osOK)
    {
      /* reader off, so the same card is not read again and again */
      HAL_GPIO_WritePin(RFID_EN_GPIO_Port, RFID_EN_Pin, GPIO_PIN_SET);

      if (strcmp(rfid_id, ALLOWED_CARD) == 0)
      {
        event = EVT_CARD_OK;
      }
      else
      {
        event = EVT_CARD_FAIL;
      }

      osMessageQueuePut(doorQueueHandle, &event, 0, 0);   // tell the door task

      osDelay(3000);                                      // wait before reading the next card
      osSemaphoreAcquire(rfidSemaphoreHandle, 0);         // forget a repeated read of the same card
      HAL_GPIO_WritePin(RFID_EN_GPIO_Port, RFID_EN_Pin, GPIO_PIN_RESET);   // reader on again
    }
  }
  /* USER CODE END StartRfidTask */
}

/* USER CODE BEGIN Header_StartDoorTask */
/**
* @brief Function implementing the doorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDoorTask */
void StartDoorTask(void *argument)
{
  /* USER CODE BEGIN StartDoorTask */
  /* doorTask: controls the LCD, LED, buzzer and servo */
  uint8_t event;
  uint8_t locked = EVT_LOCKED;

  for(;;)
  {
    /* wait here until the keypad task, the RFID task or the tilt interrupt sends an event */
    if (osMessageQueueGet(doorQueueHandle, &event, NULL, osWaitForever) == osOK)
    {
      /* keep the LCD until this event is finished (Lab 7 mutex) */
      osMutexAcquire(lcdMutexHandle, osWaitForever);

      osMessageQueuePut(wifiQueueHandle, &event, 0, 0);   // report the event over Wi-Fi

      if (event == EVT_PIN_OK || event == EVT_CARD_OK)
      {
        /* ----- correct PIN or card: open the door for 5 seconds ----- */
        door_open = 1;
        LCD_ShowMessage("ACCESS GRANTED", "DOOR OPEN");
        SetColor(0, 255);                     // green
        Servo_WritePulseUS(SERVO_OPEN);       // unlock

        play_tone(523, 150);                  // success sound (notes going up)
        play_tone(659, 150);
        play_tone(784, 300);

        osDelay(DOOR_OPEN_MS);                // hold the door open

        Servo_WritePulseUS(SERVO_CLOSED);     // lock again
        SetColor(0, 0);                       // LED off
        LCD_ShowMessage("DOOR LOCKED", "");
        osMessageQueuePut(wifiQueueHandle, &locked, 0, 0);
        osDelay(1000);

        last_tilt = HAL_GetTick();            // give the door time to stop moving
        door_open = 0;
      }
      else if (event == EVT_PIN_FAIL || event == EVT_CARD_FAIL)
      {
        /* ----- wrong PIN or card: the servo does not move ----- */
        LCD_ShowMessage("ACCESS DENIED", "TRY AGAIN");
        SetColor(255, 0);                     // red

        play_tone(300, 250);                  // fail sound (notes going down)
        play_tone(200, 500);

        osDelay(1000);
        SetColor(0, 0);
      }
      else if (event == EVT_TAMPER)
      {
        /* ----- door moved while locked: alarm, the servo does not move ----- */
        LCD_ShowMessage("TAMPER ALERT!", "DOOR IS LOCKED");
        SetColor(255, 0);                     // red

        for (int i = 0; i < 5; i++)           // alarm sound
        {
          play_tone(1000, 200);
          play_tone(700, 200);
        }

        SetColor(0, 0);
      }

      /* back to the start screen */
      pin_index = 0;
      LCD_ShowMessage("ENTER PIN / CARD", "");

      osMutexRelease(lcdMutexHandle);
    }
  }
  /* USER CODE END StartDoorTask */
}

/* USER CODE BEGIN Header_StartWifiTask */
/**
* @brief Function implementing the wifiTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartWifiTask */
void StartWifiTask(void *argument)
{
  /* USER CODE BEGIN StartWifiTask */
  /* wifiTask: sends one text line per event to the NodeMCU (Lab 8) */
  uint8_t event;
  char msg[50];

  strcpy(msg, "LOCKER READY\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 200);
  HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 200);

  for(;;)
  {
    /* wait here until the door task sends an event */
    if (osMessageQueueGet(wifiQueueHandle, &event, NULL, osWaitForever) == osOK)
    {
      if (event == EVT_PIN_OK)
      {
        strcpy(msg, "ACCESS GRANTED (PIN)\r\n");
      }
      else if (event == EVT_PIN_FAIL)
      {
        strcpy(msg, "ACCESS DENIED (WRONG PIN)\r\n");
      }
      else if (event == EVT_CARD_OK)
      {
        snprintf(msg, sizeof(msg), "ACCESS GRANTED (CARD %s)\r\n", rfid_id);
      }
      else if (event == EVT_CARD_FAIL)
      {
        snprintf(msg, sizeof(msg), "ACCESS DENIED (CARD %s)\r\n", rfid_id);
      }
      else if (event == EVT_TAMPER)
      {
        strcpy(msg, "TAMPER ALERT: DOOR MOVED\r\n");
      }
      else
      {
        strcpy(msg, "DOOR LOCKED\r\n");
      }

      HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 200);   // USART1 -> NodeMCU -> Wi-Fi
      HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 200);   // USART2 -> Tera Term
    }
  }
  /* USER CODE END StartWifiTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* Lab 4: play one tone on the passive buzzer (TIM2 channel 3, PB10) */
void play_tone(uint32_t freq, uint32_t duration_ms)
{
  uint32_t timer_clk = 1000000;               // 1 MHz based on 79 prescaler
  uint32_t period = timer_clk / freq;

  __HAL_TIM_SET_AUTORELOAD(&htim2, period - 1);
  __HAL_TIM_SET_COUNTER(&htim2, 0);           // restart the count so it is never above the new period
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, period / 4);   // 25% duty cycle
  osDelay(duration_ms);

  // Silence between notes
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0);
  osDelay(30);
}

/* Lab 4: set the RGB LED color, 0-255 for each (TIM1 channels 1 and 2) */
void SetColor(uint8_t red, uint8_t green)
{
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (red   * arr) / 255);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (green * arr) / 255);
}

/* Lab 7: move the servo (TIM3 channel 1, PA6) */
void Servo_WritePulseUS(uint16_t pulse_us)
{
  // Limit pulse width to 1000-2000 microseconds
  if (pulse_us < 1000) pulse_us = 1000;
  if (pulse_us > 2000) pulse_us = 2000;

  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse_us);
}

/* Show two lines of text on the LCD */
void LCD_ShowMessage(char *line1, char *line2)
{
  LCD_Clear();
  LCD_SetCursor(0, 0);
  LCD_Print(line1);
  LCD_SetCursor(1, 0);
  LCD_Print(line2);
}

/* Lab 5: interrupt from the tilt (ball) switch on PA4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == TILT_Pin)
  {
    uint32_t now = HAL_GetTick();

    /* ignore the switch while the door is open, and for 3 seconds after
       an alert because the ball bounces many times */
    if (door_open == 0 && (now - last_tilt) > TILT_WAIT_MS)
    {
      uint8_t event = EVT_TAMPER;

      last_tilt = now;
      osMessageQueuePut(doorQueueHandle, &event, 0, 0);   // tell the door task
    }
  }
}

/* Interrupt: one byte arrived from the RFID reader (UART4, PC11) */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    if (rfid_byte == 0x0A)                    // start byte: a new ID begins
    {
      rfid_index = 0;
    }
    else if (rfid_byte == 0x0D)               // stop byte: the ID is finished
    {
      if (rfid_index == 10)
      {
        rfid_id[10] = '\0';
        osSemaphoreRelease(rfidSemaphoreHandle);   // wake up rfidTask
      }
      rfid_index = 0;
    }
    else if (rfid_index < 10)                 // one of the 10 ID characters
    {
      rfid_id[rfid_index] = rfid_byte;
      rfid_index++;
    }

    HAL_UART_Receive_IT(&huart4, &rfid_byte, 1);   // wait for the next byte
  }
}

/* If the RFID line had noise, start receiving again */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    HAL_UART_Receive_IT(&huart4, &rfid_byte, 1);
  }
}
/* USER CODE END Application */

