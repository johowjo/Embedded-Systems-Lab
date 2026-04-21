/**
  ******************************************************************************
  * @file    hci_tl_interface.c
  * @brief   HCI TL interface for the SPBTLE-RF (BlueNRG-MS) module on the
  *          B-L4S5I-IOT01A board (SPI3). Adapted from X-CUBE-BLE1 sample.
  ******************************************************************************
  */

#include "hci_tl_interface.h"
#include "hci_tl.h"

#define HEADER_SIZE       5U
#define MAX_BUFFER_SIZE   255U
#define TIMEOUT_DURATION  15U

extern SPI_HandleTypeDef hspi3;

EXTI_HandleTypeDef hexti_bluenrg;

static int32_t BSP_SPI_SendRecv(uint8_t *pTx, uint8_t *pRx, uint16_t len)
{
  if (HAL_SPI_TransmitReceive(&hspi3, pTx, pRx, len, 1000) != HAL_OK)
  {
    return -1;
  }
  return 0;
}

int32_t HCI_TL_SPI_Init(void *pConf)
{
  (void)pConf;

  /* CS, RST, IRQ are already configured by CubeMX MX_GPIO_Init().
   * Make sure CS is de-asserted and RST is released. */
  HAL_GPIO_WritePin(HCI_TL_SPI_CS_PORT, HCI_TL_SPI_CS_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(HCI_TL_RST_PORT,   HCI_TL_RST_PIN,    GPIO_PIN_SET);
  return 0;
}

int32_t HCI_TL_SPI_DeInit(void)
{
  return 0;
}

int32_t HCI_TL_SPI_Reset(void)
{
  HAL_GPIO_WritePin(HCI_TL_SPI_CS_PORT, HCI_TL_SPI_CS_PIN, GPIO_PIN_SET);

  HAL_GPIO_WritePin(HCI_TL_RST_PORT, HCI_TL_RST_PIN, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(HCI_TL_RST_PORT, HCI_TL_RST_PIN, GPIO_PIN_SET);
  HAL_Delay(5);
  return 0;
}

int32_t HCI_TL_SPI_Receive(uint8_t *buffer, uint16_t size)
{
  uint16_t byte_count;
  uint8_t  len = 0;
  uint8_t  char_ff = 0xFF;
  volatile uint8_t read_char;

  uint8_t header_master[HEADER_SIZE] = { 0x0B, 0x00, 0x00, 0x00, 0x00 };
  uint8_t header_slave [HEADER_SIZE];

  HAL_GPIO_WritePin(HCI_TL_SPI_CS_PORT, HCI_TL_SPI_CS_PIN, GPIO_PIN_RESET);

  BSP_SPI_SendRecv(header_master, header_slave, HEADER_SIZE);

  if (header_slave[0] == 0x02)
  {
    byte_count = ((uint16_t)header_slave[4] << 8) | header_slave[3];

    if (byte_count > 0U)
    {
      if (byte_count > size)
      {
        byte_count = size;
      }

      for (len = 0; len < byte_count; len++)
      {
        BSP_SPI_SendRecv(&char_ff, (uint8_t *)&read_char, 1);
        buffer[len] = read_char;
      }
    }
  }

  HAL_GPIO_WritePin(HCI_TL_SPI_CS_PORT, HCI_TL_SPI_CS_PIN, GPIO_PIN_SET);
  return (int32_t)len;
}

int32_t HCI_TL_SPI_Send(uint8_t *buffer, uint16_t size)
{
  int32_t  result;
  uint8_t  header_master[HEADER_SIZE] = { 0x0A, 0x00, 0x00, 0x00, 0x00 };
  uint8_t  header_slave [HEADER_SIZE];
  static   uint8_t read_char_buf[MAX_BUFFER_SIZE];
  uint32_t tickstart = HAL_GetTick();

  do
  {
    result = 0;

    HAL_GPIO_WritePin(HCI_TL_SPI_CS_PORT, HCI_TL_SPI_CS_PIN, GPIO_PIN_RESET);

    BSP_SPI_SendRecv(header_master, header_slave, HEADER_SIZE);

    if (header_slave[0] == 0x02)
    {
      if (header_slave[1] >= size)
      {
        BSP_SPI_SendRecv(buffer, read_char_buf, size);
      }
      else
      {
        result = -2;
      }
    }
    else
    {
      result = -1;
    }

    HAL_GPIO_WritePin(HCI_TL_SPI_CS_PORT, HCI_TL_SPI_CS_PIN, GPIO_PIN_SET);

    if ((HAL_GetTick() - tickstart) > TIMEOUT_DURATION)
    {
      result = -3;
      break;
    }
  } while (result < 0);

  return result;
}

static int32_t IsDataAvailable(void)
{
  return (HAL_GPIO_ReadPin(HCI_TL_SPI_EXTI_PORT, HCI_TL_SPI_EXTI_PIN) == GPIO_PIN_SET);
}

static int32_t BSP_GetTick(void)
{
  return (int32_t)HAL_GetTick();
}

void hci_tl_lowlevel_init(void)
{
  tHciIO fops;

  fops.Init    = HCI_TL_SPI_Init;
  fops.DeInit  = HCI_TL_SPI_DeInit;
  fops.Send    = HCI_TL_SPI_Send;
  fops.Receive = HCI_TL_SPI_Receive;
  fops.Reset   = HCI_TL_SPI_Reset;
  fops.GetTick = BSP_GetTick;

  hci_register_io_bus(&fops);

  /* EXTI6 for the SPBTLE_RF IRQ line is already enabled by CubeMX.
   * The actual dispatch to hci_tl_lowlevel_isr() happens in the
   * HAL_GPIO_EXTI_Callback() override inside stm32l4xx_it.c. */
  (void)hexti_bluenrg;
}

void hci_tl_lowlevel_isr(void)
{
  while (IsDataAvailable())
  {
    if (hci_notify_asynch_evt(NULL))
    {
      return;
    }
  }
}
