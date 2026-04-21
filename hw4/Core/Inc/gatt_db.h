/**
  ******************************************************************************
  * @file    gatt_db.h
  * @brief   GATT database definition for the custom Accelerator Service.
  *
  *   Service:
  *     UUID : 1B-C5-D5-A5-02-00-36-AC-E1-11-01-00-00-00-00-00 (128-bit)
  *
  *   characteristic_a: 3-axis acceleration values     -> NOTIFY
  *     UUID : ...AA...
  *     Value: int16_t X, int16_t Y, int16_t Z  (little-endian, 6 bytes, mg)
  *
  *   characteristic_b: sampling frequency in Hz       -> WRITE
  *     UUID : ...BB...
  *     Value: uint16_t freq_hz (little-endian, 2 bytes)
  ******************************************************************************
  */

#ifndef GATT_DB_H
#define GATT_DB_H

#include <stdint.h>
#include "bluenrg_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  int16_t x_mg;
  int16_t y_mg;
  int16_t z_mg;
} AccelAxes_t;

tBleStatus Add_Acc_Service(void);

tBleStatus Acc_Notify(const AccelAxes_t *axes);

void Acc_GattWrite_CB(uint16_t attr_handle, uint8_t data_len, const uint8_t *data);

uint16_t Acc_GetSamplingFreqHz(void);

#ifdef __cplusplus
}
#endif
#endif /* GATT_DB_H */
