/**
  ******************************************************************************
  * @file    bluenrg_conf.h
  * @brief   BlueNRG-MS stack configuration for B-L4S5I-IOT01A
  ******************************************************************************
  */

#ifndef BLUENRG_CONF_H
#define BLUENRG_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"
#include <string.h>

#define BLE1_DEBUG                 1
#define PRINT_CSV_FORMAT           0

#define HCI_READ_PACKET_SIZE       128
#define HCI_MAX_PAYLOAD_SIZE       128

#define SCAN_P                     16384
#define SCAN_L                     16384
#define SUPERV_TIMEOUT             60
#define CONN_P1                    40
#define CONN_P2                    40
#define CONN_L1                    2000
#define CONN_L2                    2000

#define ADV_DATA_TYPE              ADV_IND
#define ADV_INTERV_MIN             2048
#define ADV_INTERV_MAX             4096

#define L2CAP_INTERV_MIN           9
#define L2CAP_INTERV_MAX           20
#define L2CAP_TIMEOUT_MULTIPLIER   600

#define HCI_DEFAULT_TIMEOUT_MS     1000

#define BLUENRG_memcpy             memcpy
#define BLUENRG_memset             memset

#if (BLE1_DEBUG == 1)
#include <stdio.h>
#define PRINTF(...)                printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#if PRINT_CSV_FORMAT
#include <stdio.h>
#define PRINT_CSV(...)             printf(__VA_ARGS__)
void print_csv_time(void);
#else
#define PRINT_CSV(...)
#endif

#ifdef __cplusplus
}
#endif
#endif /* BLUENRG_CONF_H */
