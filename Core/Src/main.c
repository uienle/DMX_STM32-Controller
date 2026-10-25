/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "usbd_cdc_if.h"
#include "effects.h"
#include "flash.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
#define RING_BUF_SIZE 3072
extern uint8_t UsbRingBuffer[RING_BUF_SIZE];
extern uint16_t RingWriteIdx;
extern uint16_t RingReadIdx;
extern volatile uint8_t UsbDataReady;

#define SCENARIO_START_BYTE  0xAA
#define MAX_SCENARIO_EVENTS  100
#define MAX_CHANGES_PER_EVENT 10

// Cấu trúc 1 sự kiện mốc thời gian kịch bản
// (DeviceChange_t đã được chuyển sang effects.h nên ta gỡ ở main.c để trống trải hơn)
typedef struct {
    uint32_t TimeMs;
    uint8_t ActiveNum;
    DeviceChange_t Changes[MAX_CHANGES_PER_EVENT];
} ScenarioEvent_t;

ScenarioEvent_t ScenarioTrack[MAX_SCENARIO_EVENTS];
uint16_t TotalScenarioEvents = 0;

uint8_t IsScenarioPlaying = 0;
uint32_t PlayStartTime = 0;
uint16_t CurrentPlayEventIdx = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
void Save_Scenario_To_Flash(void);
void Load_Scenario_From_Flash(void);
uint16_t GetRingBufferAvailable(void);
uint8_t PeekRingBuffer(uint16_t offset);
uint8_t PopRingBuffer(void);
uint8_t Parse_Scenario_Frame(void);
void MX_USART1_UART_DeInit(void);
void Send_DMX_Buffer(void);
void delay_us(uint32_t us);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint16_t GetRingBufferAvailable(void)
{
    if (RingWriteIdx >= RingReadIdx) {
        return RingWriteIdx - RingReadIdx;
    } else {
        return (RING_BUF_SIZE - RingReadIdx) + RingWriteIdx;
    }
}

uint8_t PeekRingBuffer(uint16_t offset)
{
    uint16_t idx = (RingReadIdx + offset) % RING_BUF_SIZE;
    return UsbRingBuffer[idx];
}

uint8_t PopRingBuffer(void)
{
    uint8_t val = UsbRingBuffer[RingReadIdx];
    RingReadIdx = (RingReadIdx + 1) % RING_BUF_SIZE;
    return val;
}

uint8_t Parse_Scenario_Frame(void)
{
    uint16_t avail = GetRingBufferAvailable();
    if (avail < 4) return 0;

    // Đồng bộ byte khởi đầu gói dữ liệu
    while (GetRingBufferAvailable() > 0)
    {
        if (PeekRingBuffer(0) == SCENARIO_START_BYTE) {
            break;
        }
        PopRingBuffer();
    }

    if (GetRingBufferAvailable() < 4) return 0;

    uint8_t high_evt = PeekRingBuffer(1);
    uint8_t low_evt  = PeekRingBuffer(2);
    uint16_t temp_events = ((uint16_t)high_evt << 8) | low_evt;

    if (temp_events > MAX_SCENARIO_EVENTS) {
        PopRingBuffer();
        return 0;
    }

    uint32_t expected_length = 3;
    uint16_t offset = 3;

    for (uint16_t i = 0; i < temp_events; i++) {
        if (avail < offset + 5) return 0;
        uint8_t active_num = PeekRingBuffer(offset + 4);
        expected_length += 5 + (active_num * 7);
        offset += 5 + (active_num * 7);
    }
    expected_length += 1;

    if (avail < expected_length) {
        return 0;
    }

    // Checksum tính toán
    uint8_t calculated_checksum = 0;
    for (uint32_t i = 0; i < expected_length - 1; i++) {
        calculated_checksum ^= PeekRingBuffer(i);
    }
    uint8_t frame_checksum = PeekRingBuffer(expected_length - 1);

    if (calculated_checksum != frame_checksum) {
        for (uint32_t i = 0; i < expected_length; i++) PopRingBuffer();
        return 2;
    }

    PopRingBuffer();
    PopRingBuffer();
    PopRingBuffer();
    TotalScenarioEvents = temp_events;

    for (uint16_t i = 0; i < TotalScenarioEvents; i++)
    {
        uint32_t t = (uint32_t)PopRingBuffer() << 24;
        t |= (uint32_t)PopRingBuffer() << 16;
        t |= (uint32_t)PopRingBuffer() << 8;
        t |= (uint32_t)PopRingBuffer();
        ScenarioTrack[i].TimeMs = t;

        uint8_t active = PopRingBuffer();
        ScenarioTrack[i].ActiveNum = (active > MAX_CHANGES_PER_EVENT) ? MAX_CHANGES_PER_EVENT : active;

        for (uint8_t j = 0; j < active; j++)
        {
            DeviceChange_t change;
            change.Address = (uint16_t)PopRingBuffer() << 8;
            change.Address |= (uint16_t)PopRingBuffer();

            change.EffectID = PopRingBuffer();
            change.Speed = PopRingBuffer();
            change.Data1 = PopRingBuffer();
            change.Data2 = PopRingBuffer();
            change.Data3 = PopRingBuffer();

            if (j < MAX_CHANGES_PER_EVENT) {
                ScenarioTrack[i].Changes[j] = change;
            }
        }
    }
    PopRingBuffer();

    Save_Scenario_To_Flash();

    IsScenarioPlaying = 1;
    PlayStartTime = HAL_GetTick();
    CurrentPlayEventIdx = 0;

    return 1;
}

#define ADDR_FLASH_SCENARIO_START    ((uint32_t)0x0800F000)

void Save_Scenario_To_Flash(void)
{
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PageError = 0;
    EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.PageAddress = ADDR_FLASH_SCENARIO_START;
    EraseInitStruct.NbPages     = 4;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) == HAL_OK)
    {
        uint32_t current_addr = ADDR_FLASH_SCENARIO_START;
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, current_addr, TotalScenarioEvents);
        current_addr += 2;

        for (uint16_t i = 0; i < TotalScenarioEvents; i++)
        {
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, current_addr, ScenarioTrack[i].TimeMs);
            current_addr += 4;

            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, current_addr, ScenarioTrack[i].ActiveNum);
            current_addr += 4;

            for (uint8_t j = 0; j < ScenarioTrack[i].ActiveNum; j++)
            {
                DeviceChange_t ch = ScenarioTrack[i].Changes[j];
                uint32_t w1 = ((uint32_t)ch.Address) | ((uint32_t)ch.EffectID << 16) | ((uint32_t)ch.Speed << 24);
                uint32_t w2 = ((uint32_t)ch.Data1) | ((uint32_t)ch.Data2 << 8) | ((uint32_t)ch.Data3 << 16);

                HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, current_addr, w1);
                current_addr += 4;
                HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, current_addr, w2);
                current_addr += 4;
            }
        }
    }
    HAL_FLASH_Lock();
}

void Load_Scenario_From_Flash(void)
{
    uint32_t current_addr = ADDR_FLASH_SCENARIO_START;
    uint16_t stored_events = *(__IO uint16_t*)current_addr;
    current_addr += 2;

    if (stored_events == 0xFFFF || stored_events > MAX_SCENARIO_EVENTS) {
        TotalScenarioEvents = 0;
        return;
    }

    TotalScenarioEvents = stored_events;
    for (uint16_t i = 0; i < TotalScenarioEvents; i++)
    {
        ScenarioTrack[i].TimeMs = *(__IO uint32_t*)current_addr;
        current_addr += 4;

        uint32_t raw_active_num = *(__IO uint32_t*)current_addr;
        current_addr += 4;

        uint32_t load_active_num = raw_active_num;
        if (load_active_num > MAX_CHANGES_PER_EVENT) {
            load_active_num = MAX_CHANGES_PER_EVENT;
        }
        ScenarioTrack[i].ActiveNum = (uint8_t)load_active_num;

        for (uint8_t j = 0; j < raw_active_num; j++)
        {
            uint32_t w1 = *(__IO uint32_t*)current_addr;
            current_addr += 4;
            uint32_t w2 = *(__IO uint32_t*)current_addr;
            current_addr += 4;

            if (j < MAX_CHANGES_PER_EVENT) {
                ScenarioTrack[i].Changes[j].Address = (uint16_t)(w1 & 0xFFFF);
                ScenarioTrack[i].Changes[j].EffectID = (uint8_t)((w1 >> 16) & 0xFF);
                ScenarioTrack[i].Changes[j].Speed = (uint8_t)((w1 >> 24) & 0xFF);

                ScenarioTrack[i].Changes[j].Data1 = (uint8_t)(w2 & 0xFF);
                ScenarioTrack[i].Changes[j].Data2 = (uint8_t)((w2 >> 8) & 0xFF);
                ScenarioTrack[i].Changes[j].Data3 = (uint8_t)((w2 >> 16) & 0xFF);
            }
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
    // Tải kịch bản đã lưu từ bộ nhớ flash khi bo mạch khởi động lại (reset)
    Load_Scenario_From_Flash();

    // NẾU CÓ DỮ LIỆU ĐÃ LƯU, TỰ ĐỘNG CHẠY LUÔN KHỎI CHỜ
    if(TotalScenarioEvents > 0) {
        IsScenarioPlaying = 1;
        PlayStartTime = HAL_GetTick();
    }
    /* USER CODE END 2 */

    /* Infinite loop */
      /* USER CODE BEGIN WHILE */
      while (1)
      {
          // 1. Nhận thông tin mới từ USB tải xuống (Nếu App gửi)
          if (UsbDataReady)
          {
              Parse_Scenario_Frame();
              UsbDataReady = 0;
          }

          // 2. Chạy quét trục thời gian của kịch bản
          if (IsScenarioPlaying && TotalScenarioEvents > 0)
          {
              uint32_t elapsed = HAL_GetTick() - PlayStartTime;

              while (CurrentPlayEventIdx < TotalScenarioEvents && elapsed >= ScenarioTrack[CurrentPlayEventIdx].TimeMs)
              {
                  ScenarioEvent_t current_event = ScenarioTrack[CurrentPlayEventIdx];
                  for (uint8_t i = 0; i < current_event.ActiveNum; i++)
                  {
                      Update_Device_Effect(current_event.Changes[i]);
                  }
                  CurrentPlayEventIdx++;
              }

              if (CurrentPlayEventIdx >= TotalScenarioEvents)
              {
                  IsScenarioPlaying = 0;
                  CurrentPlayEventIdx = 0;
                  Clear_All_Effects();
                  // Gửi thêm 1 khung trắng (tất cả 0) để tắt sạch thiết bị trước khi ngưng truyền hoàn toàn
                  Send_DMX_Buffer();
              }
          }

        // 3. Tính toán hình thái đồ thị LED/Motor (Lưu nội bộ vào mảng DMX_Buffer)
          Update_Active_Effects_Loop();

          // 4. Bắn Start Code chuẩn và xuất 512 Kênh DMX ra UART phần cứng đồng bộ khi đang phát kịch bản
          if (IsScenarioPlaying && TotalScenarioEvents > 0)
          {
              Send_DMX_Buffer();
          }

          HAL_Delay(25); // Làm mượt, tạo frame rate khoảng 40 khung hình / giây
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
      }
      /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7199;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 9999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 250000;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_2;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// ------ KHỐI HÀM PHỤC VỤ TRUYỀN DMX512 ------ //

// Hàm tạo trễ khoảng vi giây bằng vòng lặp (Chặn Compiler tối ưu hóa bằng từ khóa volatile)
void delay_us(uint32_t us)
{
    // Giả sử xung nhịp hệ thống STM32F103 là 72MHz.
    uint32_t count = us * (72 / 4);
    for(volatile uint32_t i = 0; i < count; i++);
}

void Config_PA9_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void MX_USART1_UART_DeInit(void)
{
    HAL_UART_DeInit(&huart1);
    __HAL_RCC_USART1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
}

void Send_DMX_Buffer(void)
{
    // CƠ CHẾ SINH START CODE VÀ ĐỒNG BỘ DMX512

    // 1. Phá vỡ UART để giành quyền điều khiển chân PA9 bằng tay
    HAL_UART_DeInit(&huart1);
    Config_PA9_Output();

    // 2. Gửi tín hiệu Pulse Break (Kéo Low khoảng 100 µs - Chuẩn yêu cầu > 88µs)
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);
    delay_us(150); // Tăng Break lên 150us để RX dễ dàng nhận diện hơn

    // 3. Gửi tín hiệu MAB - Mark After Break (Kéo High)
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);
    // Quan trọng: Vì hàm ngắt FE bên RX sử dụng HAL_UART_DMAStop/Receive chạy rất chậm (mất hàng chục us),
    // ta CẦN thả MAB dài ra để RX kịp thời setup lại bộ nhận xong DMA trước khi bit 0x00 đầu tiên chạy vào!
    delay_us(200);

    // 4. Áp lại bộ UART1 (PA9 tự động quay dính về chip UART phần cứng)
    MX_USART1_UART_Init();
    delay_us(20); // Chờ thêm 1 tí xíu cho thiết lập UART ổn định hoàn toàn

    // 5. Gửi lập tức 513 bytes. DMX_Buffer[0] mặc định = 0x00 chính là Start Code.
    DMX_Buffer[0] = 0;
    HAL_UART_Transmit(&huart1, DMX_Buffer, 513, 100);
}

// ---------------------------------------------- //

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
