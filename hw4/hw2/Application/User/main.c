/**
  ******************************************************************************
  * @file    Wifi/WiFi_Client_Server/src/main.c
  * @author  MCD Application Team
  * @brief   This file provides main program functions
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Private defines -----------------------------------------------------------*/

#define TERMINAL_USE

/* Update SSID and PASSWORD with own Access point settings */
#define SSID     "iPhone"
#define PASSWORD "88881234"

uint8_t RemoteIP[] = {172,20,10,7};
#define RemotePORT	8002

#define WIFI_WRITE_TIMEOUT 10000
#define WIFI_READ_TIMEOUT  10000

#define CONNECTION_TRIAL_MAX          10

#if defined (TERMINAL_USE)
#define TERMOUT(...)  printf(__VA_ARGS__)
#else
#define TERMOUT(...)
#endif

/* Private variables ---------------------------------------------------------*/
#if defined (TERMINAL_USE)
extern UART_HandleTypeDef hDiscoUart;
#endif /* TERMINAL_USE */
static uint8_t RxData [500];
static volatile uint8_t MotionExtiPending = 0;


/* Private function prototypes -----------------------------------------------*/
#if defined (TERMINAL_USE)
#ifdef __GNUC__
/* With GCC, small TERMOUT (option LD Linker->Libraries->Small TERMOUT
   set to 'Yes') calls __io_putchar() */
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */
#endif /* TERMINAL_USE */

static void SystemClock_Config(void);
static void LSM6DSL_EnableSignificantMotion(void);
static uint8_t LSM6DSL_ReadFuncSrc1(void);



extern  SPI_HandleTypeDef hspi;

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Main program
  * @param  None
  * @retval None
  */
int main(void)
{
  uint8_t  MAC_Addr[6] = {0};
  uint8_t  IP_Addr[4] = {0};
  char TxData[192];
  int32_t Socket = -1;
  uint16_t Datalen;
  int32_t ret;
  int16_t Trials = CONNECTION_TRIAL_MAX;
  int16_t accel[3];
  int tx_len;
  uint8_t func_src1;
  uint32_t LastFuncSrcPollMs = 0;
  uint32_t MotionWarmupEndMs = 0;
  uint32_t LastAccelSendMs = 0;

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();
  /* Configure LED2 */
  BSP_LED_Init(LED2);
  BSP_ACCELERO_Init();
  LSM6DSL_EnableSignificantMotion();
  /* Clear any stale latched flags at startup to avoid false first event. */
  (void)LSM6DSL_ReadFuncSrc1();
  HAL_Delay(20);
  (void)LSM6DSL_ReadFuncSrc1();
  MotionWarmupEndMs = HAL_GetTick() + 3000U;

#if defined (TERMINAL_USE)
  /* Initialize all configured peripherals */
  hDiscoUart.Instance = DISCOVERY_COM1;
  hDiscoUart.Init.BaudRate = 115200;
  hDiscoUart.Init.WordLength = UART_WORDLENGTH_8B;
  hDiscoUart.Init.StopBits = UART_STOPBITS_1;
  hDiscoUart.Init.Parity = UART_PARITY_NONE;
  hDiscoUart.Init.Mode = UART_MODE_TX_RX;
  hDiscoUart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hDiscoUart.Init.OverSampling = UART_OVERSAMPLING_16;
  hDiscoUart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hDiscoUart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  BSP_COM_Init(COM1, &hDiscoUart);
#endif /* TERMINAL_USE */

  TERMOUT("****** WIFI Module in TCP Client mode demonstration ****** \n\n");
  TERMOUT("TCP Client Instructions :\n");
  TERMOUT("1- Run the Python server on your computer on port 8002.\n");
  TERMOUT("2- Set RemoteIP to your computer LAN IP address.\n");
  TERMOUT("3- Make sure board and computer are on same Wi-Fi network.\n\n");



  /*Initialize  WIFI module */
  if(WIFI_Init() ==  WIFI_STATUS_OK)
  {
    TERMOUT("> WIFI Module Initialized.\n");
    if(WIFI_GetMAC_Address(MAC_Addr, sizeof(MAC_Addr)) == WIFI_STATUS_OK)
    {
      TERMOUT("> es-wifi module MAC Address : %X:%X:%X:%X:%X:%X\n",
               MAC_Addr[0],
               MAC_Addr[1],
               MAC_Addr[2],
               MAC_Addr[3],
               MAC_Addr[4],
               MAC_Addr[5]);
    }
    else
    {
      TERMOUT("> ERROR : CANNOT get MAC address\n");
      BSP_LED_On(LED2);
    }

    if( WIFI_Connect(SSID, PASSWORD, WIFI_ECN_WPA2_PSK) == WIFI_STATUS_OK)
    {
      TERMOUT("> es-wifi module connected \n");
      if(WIFI_GetIP_Address(IP_Addr, sizeof(IP_Addr)) == WIFI_STATUS_OK)
      {
        TERMOUT("> es-wifi module got IP Address : %d.%d.%d.%d\n",
               IP_Addr[0],
               IP_Addr[1],
               IP_Addr[2],
               IP_Addr[3]);

        TERMOUT("> Trying to connect to Server: %d.%d.%d.%d:%d ...\n",
               RemoteIP[0],
               RemoteIP[1],
               RemoteIP[2],
               RemoteIP[3],
							 RemotePORT);


        while (Trials--)
        {
          if( WIFI_OpenClientConnection(0, WIFI_TCP_PROTOCOL, "TCP_CLIENT", RemoteIP, RemotePORT, 0) == WIFI_STATUS_OK)
          {
            TERMOUT("> TCP Connection opened successfully.\n");
            Socket = 0;
            break;
          }
        }
        if(Socket == -1)
        {
          TERMOUT("> ERROR : Cannot open Connection\n");
          BSP_LED_On(LED2);
        }

      }
      else
      {
        TERMOUT("> ERROR : es-wifi module CANNOT get IP address\n");
        BSP_LED_On(LED2);
      }

    }
    else
    {
      TERMOUT("> ERROR : es-wifi module NOT connected\n");
      BSP_LED_On(LED2);
    }
  }
  else
  {
    TERMOUT("> ERROR : WIFI Module cannot be initialized.\n");
    BSP_LED_On(LED2);
  }

  while(1)
  {
    if(Socket != -1)
    {
      if (MotionExtiPending)
      {
        MotionExtiPending = 0;
        func_src1 = LSM6DSL_ReadFuncSrc1();
        if ((func_src1 & 0x40U) != 0U)
        {
          if (HAL_GetTick() < MotionWarmupEndMs)
          {
            TERMOUT("> Ignored startup motion flag (FUNC_SRC1=0x%02X)\n", func_src1);
            HAL_Delay(50);
            continue;
          }

          BSP_ACCELERO_AccGetXYZ(accel);
          TERMOUT("> Significant motion detected! FUNC_SRC1=0x%02X\n", func_src1);

          tx_len = snprintf(
            TxData,
            sizeof(TxData),
            "GET /motion?event=significant&x=%d&y=%d&z=%d&src=0x%02X HTTP/1.1\r\n"
            "Host: %d.%d.%d.%d:%d\r\n"
            "Connection: keep-alive\r\n"
            "\r\n",
            accel[0],
            accel[1],
            accel[2],
            func_src1,
            RemoteIP[0],
            RemoteIP[1],
            RemoteIP[2],
            RemoteIP[3],
            RemotePORT);

          if ((tx_len <= 0) || (tx_len >= (int)sizeof(TxData)))
          {
            TERMOUT("> ERROR : Failed to format motion HTTP request\n");
            break;
          }

          ret = WIFI_SendData(Socket, (uint8_t *)TxData, (uint16_t)tx_len, &Datalen, WIFI_WRITE_TIMEOUT);
          if (ret != WIFI_STATUS_OK)
          {
            TERMOUT("> ERROR : Failed to Send motion event, connection closed\n");
            break;
          }

          ret = WIFI_ReceiveData(Socket, RxData, sizeof(RxData)-1, &Datalen, WIFI_READ_TIMEOUT);
          if(ret == WIFI_STATUS_OK)
          {
            if(Datalen > 0)
            {
              RxData[Datalen]=0;
              TERMOUT("Received:\n%s\n",RxData);
            }
            else
            {
              TERMOUT("> Server closed connection.\n");
              break;
            }
          }
          else
          {
            TERMOUT("> ERROR : Failed to Receive Data, connection closed\n");
            break;
          }
        }
      }
      else if ((HAL_GetTick() - LastFuncSrcPollMs) >= 100U)
      {
        /* Fallback polling in case INT1 is not wired to this EXTI line. */
        LastFuncSrcPollMs = HAL_GetTick();
        func_src1 = LSM6DSL_ReadFuncSrc1();
        if ((func_src1 & 0x40U) != 0U)
        {
          if (HAL_GetTick() < MotionWarmupEndMs)
          {
            TERMOUT("> Ignored startup motion flag (polled FUNC_SRC1=0x%02X)\n", func_src1);
            HAL_Delay(50);
            continue;
          }

          BSP_ACCELERO_AccGetXYZ(accel);
          TERMOUT("> Significant motion detected (polled)! FUNC_SRC1=0x%02X\n", func_src1);

          tx_len = snprintf(
            TxData,
            sizeof(TxData),
            "GET /motion?event=significant&x=%d&y=%d&z=%d&src=0x%02X HTTP/1.1\r\n"
            "Host: %d.%d.%d.%d:%d\r\n"
            "Connection: keep-alive\r\n"
            "\r\n",
            accel[0],
            accel[1],
            accel[2],
            func_src1,
            RemoteIP[0],
            RemoteIP[1],
            RemoteIP[2],
            RemoteIP[3],
            RemotePORT);

          if ((tx_len <= 0) || (tx_len >= (int)sizeof(TxData)))
          {
            TERMOUT("> ERROR : Failed to format motion HTTP request\n");
            break;
          }

          ret = WIFI_SendData(Socket, (uint8_t *)TxData, (uint16_t)tx_len, &Datalen, WIFI_WRITE_TIMEOUT);
          if (ret != WIFI_STATUS_OK)
          {
            TERMOUT("> ERROR : Failed to Send motion event, connection closed\n");
            break;
          }

          ret = WIFI_ReceiveData(Socket, RxData, sizeof(RxData)-1, &Datalen, WIFI_READ_TIMEOUT);
          if(ret == WIFI_STATUS_OK)
          {
            if(Datalen > 0)
            {
              RxData[Datalen]=0;
              TERMOUT("Received:\n%s\n",RxData);
            }
            else
            {
              TERMOUT("> Server closed connection.\n");
              break;
            }
          }
          else
          {
            TERMOUT("> ERROR : Failed to Receive Data, connection closed\n");
            break;
          }
        }
      }

      /* Periodic accelerometer streaming to the host server. */
      if ((HAL_GetTick() - LastAccelSendMs) >= 1000U)
      {
        LastAccelSendMs = HAL_GetTick();

        BSP_ACCELERO_AccGetXYZ(accel);
        TERMOUT("> Accel: x=%d y=%d z=%d\n", accel[0], accel[1], accel[2]);

        tx_len = snprintf(
          TxData,
          sizeof(TxData),
          "GET /accel?x=%d&y=%d&z=%d HTTP/1.1\r\n"
          "Host: %d.%d.%d.%d:%d\r\n"
          "Connection: keep-alive\r\n"
          "\r\n",
          accel[0],
          accel[1],
          accel[2],
          RemoteIP[0],
          RemoteIP[1],
          RemoteIP[2],
          RemoteIP[3],
          RemotePORT);

        if ((tx_len <= 0) || (tx_len >= (int)sizeof(TxData)))
        {
          TERMOUT("> ERROR : Failed to format accel HTTP request\n");
          break;
        }

        ret = WIFI_SendData(Socket, (uint8_t *)TxData, (uint16_t)tx_len, &Datalen, WIFI_WRITE_TIMEOUT);
        if (ret != WIFI_STATUS_OK)
        {
          TERMOUT("> ERROR : Failed to Send accel request, connection closed\n");
          break;
        }

        ret = WIFI_ReceiveData(Socket, RxData, sizeof(RxData)-1, &Datalen, WIFI_READ_TIMEOUT);
        if (ret == WIFI_STATUS_OK)
        {
          if (Datalen > 0)
          {
            RxData[Datalen] = 0;
            /* Body not printed to keep UART logs readable. */
          }
          else
          {
            TERMOUT("> Server closed connection.\n");
            break;
          }
        }
        else
        {
          TERMOUT("> ERROR : Failed to Receive accel data, connection closed\n");
          break;
        }
      }

      HAL_Delay(50);
    }
  }

  if (Socket != -1)
  {
    WIFI_CloseClientConnection(Socket);
  }
}

/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follow :
  *            System Clock source            = PLL (MSI)
  *            SYSCLK(Hz)                     = 80000000
  *            HCLK(Hz)                       = 80000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 1
  *            APB2 Prescaler                 = 1
  *            MSI Frequency(Hz)              = 4000000
  *            PLL_M                          = 1
  *            PLL_N                          = 40
  *            PLL_R                          = 2
  *            PLL_P                          = 7
  *            PLL_Q                          = 4
  *            Flash Latency(WS)              = 4
  * @param  None
  * @retval None
  */
static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_OscInitTypeDef RCC_OscInitStruct;

  /* MSI is enabled after System reset, activate PLL with MSI as source */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLP = 7;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    /* Initialization Error */
    while(1);
  }

  /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2
     clocks dividers */
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    /* Initialization Error */
    while(1);
  }
}

#if defined (TERMINAL_USE)
/**
  * @brief  Retargets the C library TERMOUT function to the USART.
  * @param  None
  * @retval None
  */
PUTCHAR_PROTOTYPE
{
  /* Place your implementation of fputc here */
  /* e.g. write a character to the USART1 and Loop until the end of transmission */
  HAL_UART_Transmit(&hDiscoUart, (uint8_t *)&ch, 1, 0xFFFF);

  return ch;
}
#endif /* TERMINAL_USE */

#ifdef USE_FULL_ASSERT

/**
   * @brief Reports the name of the source file and the source line number
   * where the assert_param error has occurred.
   * @param file: pointer to the source file name
   * @param line: assert_param error line source number
   * @retval None
   */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
    ex: TERMOUT("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */

}

#endif

/**
  * @brief  EXTI line detection callback.
  * @param  GPIO_Pin: Specifies the port pin connected to corresponding EXTI line.
  * @retval None
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  switch (GPIO_Pin)
  {
    case (GPIO_PIN_1):
    {
      SPI_WIFI_ISR();
      MotionExtiPending = 1;
      break;
    }
    default:
    {
      break;
    }
  }
}

static void LSM6DSL_EnableSignificantMotion(void)
{
  /* AN5040 sequence:
   * 1) FUNC_CFG_ACCESS=0x80 (Bank A), 2) SM_THS=0x08, 3) FUNC_CFG_ACCESS=0x00
   * 4) CTRL10_C |= FUNC_EN(0x04) and SIGN_MOTION_EN(0x01), 5) INT1_CTRL |= INT1_SIGN_MOT(0x40)
   */
  uint8_t reg;

  SENSOR_IO_Write(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_FUNC_CFG_ACCESS, 0x80);
  SENSOR_IO_Write(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_SM_STEP_THS, 0x08);
  SENSOR_IO_Write(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_FUNC_CFG_ACCESS, 0x00);

  reg = SENSOR_IO_Read(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_CTRL10_C);
  reg |= 0x05;
  SENSOR_IO_Write(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_CTRL10_C, reg);

  /* Latch interrupts so SIGN_MOTION_IA remains set until FUNC_SRC1 is read. */
  reg = SENSOR_IO_Read(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_TAP_CFG1);
  reg |= 0x01;
  SENSOR_IO_Write(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_TAP_CFG1, reg);

  reg = SENSOR_IO_Read(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_INT1_CTRL);
  reg |= 0x40;
  SENSOR_IO_Write(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_INT1_CTRL, reg);

  TERMOUT("> Significant motion configured (SM_THS=0x08, INT1 enabled).\n");
}

static uint8_t LSM6DSL_ReadFuncSrc1(void)
{
  return SENSOR_IO_Read(LSM6DSL_ACC_GYRO_I2C_ADDRESS_LOW, LSM6DSL_ACC_GYRO_FUNC_SRC);
}

void SPI3_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&hspi);
}
