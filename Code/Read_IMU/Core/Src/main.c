/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Read MPU6050 over I2C1 and stream IMU samples over USART1.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define MPU6050_I2C_ADDR             (0x68U << 1)
#define MPU6050_REG_SMPLRT_DIV       0x19U
#define MPU6050_REG_GYRO_CONFIG      0x1BU
#define MPU6050_REG_ACCEL_CONFIG     0x1CU
#define MPU6050_REG_PWR_MGMT_1       0x6BU
#define MPU6050_REG_WHO_AM_I         0x75U
#define MPU6050_REG_ACCEL_XOUT_H     0x3BU
#define MPU6050_RAW_FRAME_SIZE       14U
#define IMU_SAMPLE_PERIOD_MS         10U
#define UART_BAUDRATE                115200U
#define IMU_STATUS_PERIOD_MS         500U
#define IMU_CSV_BUFFER_SIZE          96U

typedef struct __attribute__((packed))
{
  uint8_t  header[3];
  uint32_t timestamp_ms;
  int16_t  ch[6];
} ImuPacket;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static ImuPacket imu_packet;
static char uart_line[IMU_CSV_BUFFER_SIZE];
static uint8_t uart_ready;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static HAL_StatusTypeDef MPU6050_Init(void);
static HAL_StatusTypeDef MPU6050_ReadPacket(ImuPacket *packet);
static void UART_SendText(const char *text);
static void UART_SendImuCsv(const ImuPacket *packet);
static int16_t i16_from_be(uint8_t msb, uint8_t lsb);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  UART_SendText("BOOT\r\n");

  MX_I2C1_Init();

  if (MPU6050_Init() != HAL_OK)
  {
    UART_SendText("IMU INIT FAIL\r\n");
    while (1)
    {
      UART_SendText("IMU INIT FAIL\r\n");
      HAL_Delay(IMU_STATUS_PERIOD_MS);
    }
  }

  UART_SendText("IMU OK\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (MPU6050_ReadPacket(&imu_packet) == HAL_OK)
    {
      UART_SendImuCsv(&imu_packet);
    }
    else
    {
      UART_SendText("IMU READ FAIL\r\n");
    }

    HAL_Delay(IMU_SAMPLE_PERIOD_MS);
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 120;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x307075B1;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

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
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
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
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static HAL_StatusTypeDef MPU6050_Init(void)
{
  uint8_t check = 0U;
  uint8_t data = 0U;

  if (HAL_I2C_IsDeviceReady(&hi2c1, MPU6050_I2C_ADDR, 3, 1000) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_I2C_Mem_Read(&hi2c1,
                       MPU6050_I2C_ADDR,
                       MPU6050_REG_WHO_AM_I,
                       I2C_MEMADD_SIZE_8BIT,
                       &check,
                       1,
                       1000) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (check != 0x68U)
  {
    return HAL_ERROR;
  }

  data = 0x00U;
  if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_I2C_ADDR, MPU6050_REG_PWR_MGMT_1,
                        I2C_MEMADD_SIZE_8BIT, &data, 1, 1000) != HAL_OK)
  {
    return HAL_ERROR;
  }

  data = 0x07U;
  if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_I2C_ADDR, MPU6050_REG_SMPLRT_DIV,
                        I2C_MEMADD_SIZE_8BIT, &data, 1, 1000) != HAL_OK)
  {
    return HAL_ERROR;
  }

  data = 0x00U;
  if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_I2C_ADDR, MPU6050_REG_ACCEL_CONFIG,
                        I2C_MEMADD_SIZE_8BIT, &data, 1, 1000) != HAL_OK)
  {
    return HAL_ERROR;
  }

  data = 0x00U;
  if (HAL_I2C_Mem_Write(&hi2c1, MPU6050_I2C_ADDR, MPU6050_REG_GYRO_CONFIG,
                        I2C_MEMADD_SIZE_8BIT, &data, 1, 1000) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_Delay(100);
  return HAL_OK;
}

static HAL_StatusTypeDef MPU6050_ReadPacket(ImuPacket *packet)
{
  uint8_t raw[MPU6050_RAW_FRAME_SIZE];

  if (HAL_I2C_Mem_Read(&hi2c1,
                       MPU6050_I2C_ADDR,
                       MPU6050_REG_ACCEL_XOUT_H,
                       I2C_MEMADD_SIZE_8BIT,
                       raw,
                       MPU6050_RAW_FRAME_SIZE,
                       1000) != HAL_OK)
  {
    return HAL_ERROR;
  }

  packet->header[0] = 0xAA;
  packet->header[1] = 0xBB;
  packet->header[2] = 0x00;
  packet->timestamp_ms = HAL_GetTick();
  packet->ch[0] = i16_from_be(raw[0], raw[1]);
  packet->ch[1] = i16_from_be(raw[2], raw[3]);
  packet->ch[2] = i16_from_be(raw[4], raw[5]);
  packet->ch[3] = i16_from_be(raw[8], raw[9]);
  packet->ch[4] = i16_from_be(raw[10], raw[11]);
  packet->ch[5] = i16_from_be(raw[12], raw[13]);

  return HAL_OK;
}

static void UART_SendText(const char *text)
{
  (void)HAL_UART_Transmit(&huart1,
                          (uint8_t *)text,
                          (uint16_t)strlen(text),
                          HAL_MAX_DELAY);
}

static void UART_SendImuCsv(const ImuPacket *packet)
{
  int len = snprintf(uart_line,
                     sizeof(uart_line),
                     "%lu,%d,%d,%d,%d,%d,%d\r\n",
                     (unsigned long)packet->timestamp_ms,
                     packet->ch[0],
                     packet->ch[1],
                     packet->ch[2],
                     packet->ch[3],
                     packet->ch[4],
                     packet->ch[5]);

  if (len > 0)
  {
    if ((uint32_t)len >= sizeof(uart_line))
    {
      len = sizeof(uart_line) - 1;
    }

    (void)HAL_UART_Transmit(&huart1,
                            (uint8_t *)uart_line,
                            (uint16_t)len,
                            HAL_MAX_DELAY);
  }
}

static int16_t i16_from_be(uint8_t msb, uint8_t lsb)
{
  return (int16_t)(((uint16_t)msb << 8) | lsb);
}
/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  if (uart_ready != 0U)
  {
    while (1)
    {
      UART_SendText("ERROR\r\n");
      HAL_Delay(IMU_STATUS_PERIOD_MS);
    }
  }

  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
