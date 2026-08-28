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
#include <math.h>
#include <stdio.h>

#include "lvgl.h"
#include <string.h>
#include <sys/types.h>

#include "i2s.h"
#include "touch_800x480.h"
#include "porting/lv_port_disp.h"
#include "porting/lv_port_indev.h"
#include "LED.h"
#include "../../lvgl_main/ui.h"
#include "../../lvgl_main/ui_buttons.h"
#include "../../lvgl_main/ui_center_labels.h"
#include "tim.h"
#include "benchmark/lv_demo_benchmark.h"
#include "arm_math.h"
#include "usbd_core.h"
#include "main.h"
#include "semphr.h"
#include "usbd_audio_if.h"
#include "usb_device.h"
#include "Test.h"
#include "gui_guider.h"
#include "events_init.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define QSPI_FLASH_TEST_ADDR   0x000000U
#define QSPI_FLASH_TEST_SIZE   256U
#define KEY_COUNT              4U
#define KEY_SCAN_PERIOD_MS     1U
/* temp 存在独立扇区，避免和自检数据撞车 */
#define TEMP_FLASH_ADDR        0x001000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 由 TIM3 输入捕获中断更新，任务里通过 TIM3_CaptureGet() 读取。 */
static volatile TIM3_CaptureResult_t s_tim3_capture;

/* USER CODE END Variables */
/* Definitions for DisplayTask */
osThreadId_t DisplayTaskHandle;
uint32_t DisplayTaskBuffer[ 2048 ];
osStaticThreadDef_t DisplayTaskControlBlock;
const osThreadAttr_t DisplayTask_attributes = {
  .name = "DisplayTask",
  .cb_mem = &DisplayTaskControlBlock,
  .cb_size = sizeof(DisplayTaskControlBlock),
  .stack_mem = &DisplayTaskBuffer[0],
  .stack_size = sizeof(DisplayTaskBuffer),
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for DatacollecTask */
osThreadId_t DatacollecTaskHandle;
uint32_t DatacollecTaskBuffer[ 1024 ];
osStaticThreadDef_t DatacollecTaskControlBlock;
const osThreadAttr_t DatacollecTask_attributes = {
  .name = "DatacollecTask",
  .cb_mem = &DatacollecTaskControlBlock,
  .cb_size = sizeof(DatacollecTaskControlBlock),
  .stack_mem = &DatacollecTaskBuffer[0],
  .stack_size = sizeof(DatacollecTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void DisplayTaskEntry(void *argument);
void DatacollecTaskEntry(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationIdleHook(void);
void vApplicationTickHook(void);

/* USER CODE BEGIN 2 */
void vApplicationIdleHook( void )
{
   /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
   task. It is essential that code added to this hook function never attempts
   to block in any way (for example, call xQueueReceive() with a block time
   specified, or call vTaskDelay()). If the application makes use of the
   vTaskDelete() API function (as this demo application does) then it is also
   important that vApplicationIdleHook() is permitted to return to its calling
   function, because it is the responsibility of the idle task to clean up
   memory allocated by the kernel to any task that has since been deleted. */
    //__WFI();
}
/* USER CODE END 2 */

/* USER CODE BEGIN 3 */
#define LOG_BUFFER_SIZE 256
#define Audio_Buffer_Size 64

volatile static uint32_t tick;
__attribute__((section(".ram"))) float x = 0;
__attribute__((section(".ram"))) float z = 0;
__attribute__((section(".ram"))) int32_t y = 0;
__attribute__((section(".ram"))) uint8_t USBaudio_buffer[USB_AUDIO_BUFFER_SIZE];
__attribute__((section(".ram"))) int32_t txaudio_buffer[Audio_Buffer_Size];
__attribute__((section(".ram"))) int32_t rxaudio_buffer[Audio_Buffer_Size];
__attribute__((section(".ram"))) uint8_t log_buffer[LOG_BUFFER_SIZE];
__attribute__((section(".ram"))) float audio_process_buffer[Audio_Buffer_Size/2];

volatile SemaphoreHandle_t xRxI2SSemaphore;
volatile uint8_t I2S_RX_State;

lv_ui guider_ui;
void vApplicationTickHook(void)
{
    /* This function will be called by each tick interrupt if
    configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
    added here, but the tick hook is called from an interrupt context, so
    code must not attempt to block, and only the interrupt safe FreeRTOS API
    functions can be used (those that end in FromISR()). */

    lv_tick_inc(1);
}

/* USER CODE END 3 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
    /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
    /* add semaphores, ... */

  xRxI2SSemaphore = xSemaphoreCreateBinary();
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
    /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
    /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of DisplayTask */
  DisplayTaskHandle = osThreadNew(DisplayTaskEntry, NULL, &DisplayTask_attributes);

  /* creation of DatacollecTask */
  DatacollecTaskHandle = osThreadNew(DatacollecTaskEntry, NULL, &DatacollecTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
    /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
    /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_DisplayTaskEntry */
/**
  * @brief  Function implementing the DisplayTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_DisplayTaskEntry */
void DisplayTaskEntry(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN DisplayTaskEntry */
    Touch_Init();
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
  setup_ui(&guider_ui);           // 初始化 UI
  events_init(&guider_ui);
    //ui_init();
    //ui_center_labels_create(ui_Screen1);
    //lv_demo_music();

    //ui_buttons_create(ui_Screen1);
    // lv_obj_t * Canvas = ui_Canvas_Creat(ui_Container8);
    /* Infinite loop */
    for (;;)
    {
        lv_task_handler();
        Touch_Scan();
        // ui_center_labels_set_text_fmt(0,"rerror:%ld",Get_read_error_count());
        // ui_center_labels_set_text_fmt(1,"werror:%ld",Get_write_error_count());
        // ui_center_labels_set_text_fmt(2,"tick:%ld",rxaudio_buffer[0]);
        {
          extern volatile uint32_t audio_fb_hz_dbg;
          //ui_center_labels_set_text_fmt(3,"hz:%lu",(unsigned long)audio_fb_hz_dbg);
        }
        TickType_t xLastWakeTime = xTaskGetTickCount();
        vTaskDelayUntil(&xLastWakeTime, 13);
    }
  /* USER CODE END DisplayTaskEntry */
}

/* USER CODE BEGIN Header_DatacollecTaskEntry */
/**
* @brief Function implementing the DatacollecTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_DatacollecTaskEntry */
void DatacollecTaskEntry(void *argument)
{
  /* USER CODE BEGIN DatacollecTaskEntry */
    uint8_t debug;
    MX_USB_DEVICE_Init();
    TIM3_CaptureResult_t tim3_result;

    osDelay(1000);
    HAL_I2S_Transmit_DMA(&hi2s2,txaudio_buffer,Audio_Buffer_Size);
    //HAL_I2S_Receive_DMA(&hi2s3,rxaudio_buffer,Audio_Buffer_Size);
    for (;;)
    {
      //AUDIO_Buffer_Read(USBaudio_buffer,USB_AUDIO_BUFFER_SIZE);

      //HAL_I2S_Transmit(&hi2s2,&y,1,1);
      // TickType_t xLastWakeTime = xTaskGetTickCount();
      // vTaskDelayUntil(&xLastWakeTime, 1);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      if (I2S_RX_State == 1)
      {
        memcpy(txaudio_buffer,rxaudio_buffer,sizeof(int32_t)*Audio_Buffer_Size/2);
      }
      else if (I2S_RX_State == 2)
      {
        memcpy(txaudio_buffer + Audio_Buffer_Size / 2,rxaudio_buffer + Audio_Buffer_Size / 2,sizeof(int32_t)*Audio_Buffer_Size/2);

      }
    }
  /* USER CODE END DatacollecTaskEntry */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
effect_t Test_Effect_T;

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  Test_Effect_T = Get_Test_t();
  if (AUDIO_Buffer_Read(USBaudio_buffer,Audio_Buffer_Size) != 0)
  {
    for (uint8_t i = 0; i < Audio_Buffer_Size / 2; i++)
    {
      txaudio_buffer[i] =0;
      //read_error_count++;
    }
  }
  else
  {
    uint16_t R, L;
    for (uint8_t i = 0; i < Audio_Buffer_Size / 4; i++)
    {
      L = USBaudio_buffer[4 * i] | (USBaudio_buffer[4 * i + 1] << 8);
      R = USBaudio_buffer[4 * i + 2] | (USBaudio_buffer[4 * i + 3] << 8);

      audio_process_buffer[i] = (float)((int32_t)(int16_t)L << 8) * (1.0f / 8388607.0f);
      // txaudio_buffer[2 * i] = (int32_t)(int16_t)L << 8;
      // txaudio_buffer[2 * i + 1] = (int32_t)(int16_t)R << 8;
    }
    float audio_out_buffer[Audio_Buffer_Size / 4];
    Test_Effect_T.Init();
    Test_Effect_T.Process(audio_process_buffer,audio_out_buffer,Audio_Buffer_Size/4);
    for (uint8_t i = 0; i < Audio_Buffer_Size / 4; i++)
    {
      txaudio_buffer[2 * i] = (int32_t)(audio_out_buffer[i] / (1.0f / 8388607.0f));
      txaudio_buffer[2 * i + 1] = (int32_t)(audio_out_buffer[i] / (1.0f / 8388607.0f));
    }
  }
  SCB_CleanDCache_by_Addr(txaudio_buffer,sizeof(txaudio_buffer)/2);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  Test_Effect_T = Get_Test_t();
  if (AUDIO_Buffer_Read(USBaudio_buffer, Audio_Buffer_Size) != 0)
  {
    for (uint16_t i = Audio_Buffer_Size / 2; i < Audio_Buffer_Size; i++)
    {
      txaudio_buffer[i] = 0;
    }
    Plus_read_error_count();
  }
  else
  {
    uint16_t R, L;
    for (uint16_t i = 0; i < Audio_Buffer_Size / 4; i++)
    {
      L = USBaudio_buffer[4 * i]     | (USBaudio_buffer[4 * i + 1] << 8);
      R = USBaudio_buffer[4 * i + 2] | (USBaudio_buffer[4 * i + 3] << 8);

      audio_process_buffer[i] = (float)((int32_t)(int16_t)L << 8) * (1.0f / 8388607.0f);
    }

    float audio_out_buffer[Audio_Buffer_Size / 4];
    Test_Effect_T.Init();
    Test_Effect_T.Process(audio_process_buffer, audio_out_buffer, Audio_Buffer_Size / 4);

    for (uint16_t i = 0; i < Audio_Buffer_Size / 4; i++)
    {
      txaudio_buffer[Audio_Buffer_Size / 2 + 2 * i]     = (int32_t)(audio_out_buffer[i] / (1.0f / 8388607.0f));
      txaudio_buffer[Audio_Buffer_Size / 2 + 2 * i + 1] = (int32_t)(audio_out_buffer[i] / (1.0f / 8388607.0f));
    }
  }
  SCB_CleanDCache_by_Addr((uint32_t *)&txaudio_buffer[Audio_Buffer_Size / 2], sizeof(txaudio_buffer) / 2);
}


void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  I2S_RX_State = 1;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(DatacollecTaskHandle, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

  SCB_InvalidateDCache_by_Addr(rxaudio_buffer,sizeof(txaudio_buffer) / 2);
}
void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  I2S_RX_State = 2;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(DatacollecTaskHandle, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

  SCB_InvalidateDCache_by_Addr(rxaudio_buffer + Audio_Buffer_Size / 2,sizeof(txaudio_buffer) / 2);
}
// static void QSPI_Flash_SelfTest(void)
// {
//     uint8_t wbuf[QSPI_FLASH_TEST_SIZE];
//     uint8_t rbuf[QSPI_FLASH_TEST_SIZE];
//     uint32_t i;
//     uint32_t mismatch;
//     int8_t ret;
//
//     /* 1. 初始化 QSPI Flash（W25Q64） */
//     ret = QSPI_W25Qxx_Init();
//     if(ret != QSPI_W25Qxx_OK) {
//         ui_center_labels_set_text(0, "QSPI init fail");
//         ui_center_labels_set_text_fmt(1, "err: %d", (int)ret);
//         return;
//     }
//     ui_center_labels_set_text(0, "QSPI init OK");
//
//     /* 2. 擦除测试扇区（4KB） */
//     ret = QSPI_W25Qxx_SectorErase(QSPI_FLASH_TEST_ADDR);
//     if(ret != QSPI_W25Qxx_OK) {
//         ui_center_labels_set_text(1, "Erase fail");
//         ui_center_labels_set_text_fmt(2, "err: %d", (int)ret);
//         return;
//     }
//     ui_center_labels_set_text(1, "Erase OK");
//
//     /* 3. 准备并写入测试数据 */
//     for(i = 0; i < QSPI_FLASH_TEST_SIZE; i++) {
//         wbuf[i] = (uint8_t)(i ^ 0x5AU);
//     }
//     ret = QSPI_W25Qxx_WriteBuffer(wbuf, QSPI_FLASH_TEST_ADDR, QSPI_FLASH_TEST_SIZE);
//     if(ret != QSPI_W25Qxx_OK) {
//         ui_center_labels_set_text(2, "Write fail");
//         ui_center_labels_set_text_fmt(3, "err: %d", (int)ret);
//         return;
//     }
//     ui_center_labels_set_text_fmt(2, "Write %u B OK", (unsigned)QSPI_FLASH_TEST_SIZE);
//
//     /* 4. 读回并校验 */
//     memset(rbuf, 0, sizeof(rbuf));
//     ret = QSPI_W25Qxx_ReadBuffer(rbuf, QSPI_FLASH_TEST_ADDR, QSPI_FLASH_TEST_SIZE);
//     if(ret != QSPI_W25Qxx_OK) {
//         ui_center_labels_set_text(3, "Read fail");
//         return;
//     }
//
//     mismatch = 0;
//     for(i = 0; i < QSPI_FLASH_TEST_SIZE; i++) {
//         if(rbuf[i] != wbuf[i]) {
//             mismatch++;
//         }
//     }
//
//     if(mismatch == 0) {
//         ui_center_labels_set_text(3, "Verify OK");
//     } else {
//         ui_center_labels_set_text_fmt(3, "Verify fail %lu", (unsigned long)mismatch);
//     }
// }


/* USER CODE END Application */

