/**
  ******************************************************************************
  * @file    hci_tl_interface.h
  * @brief   HCI transport layer interface for SPBTLE-RF (BlueNRG-MS) on
  *          B-L4S5I-IOT01A. Uses SPI3, PD13=CS, PE6=IRQ, PA8=RST.
  ******************************************************************************
  */

#ifndef HCI_TL_INTERFACE_H
#define HCI_TL_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* BlueNRG SPI IRQ line (rising edge = data available) */
#define HCI_TL_SPI_EXTI_PORT       SPBTLE_RF_IRQ_EXTI6_GPIO_Port
#define HCI_TL_SPI_EXTI_PIN        SPBTLE_RF_IRQ_EXTI6_Pin
#define HCI_TL_SPI_EXTI_IRQn       EXTI9_5_IRQn

/* Chip-select */
#define HCI_TL_SPI_CS_PORT         SPBTLE_RF_SPI3_CSN_GPIO_Port
#define HCI_TL_SPI_CS_PIN          SPBTLE_RF_SPI3_CSN_Pin

/* Reset */
#define HCI_TL_RST_PORT            SPBTLE_RF_RST_GPIO_Port
#define HCI_TL_RST_PIN             SPBTLE_RF_RST_Pin

extern EXTI_HandleTypeDef          hexti_bluenrg;
#define H_EXTI_0                   hexti_bluenrg

int32_t HCI_TL_SPI_Init   (void *pConf);
int32_t HCI_TL_SPI_DeInit (void);
int32_t HCI_TL_SPI_Receive(uint8_t *buffer, uint16_t size);
int32_t HCI_TL_SPI_Send   (uint8_t *buffer, uint16_t size);
int32_t HCI_TL_SPI_Reset  (void);

void    hci_tl_lowlevel_init(void);
void    hci_tl_lowlevel_isr (void);

#ifdef __cplusplus
}
#endif
#endif /* HCI_TL_INTERFACE_H */
