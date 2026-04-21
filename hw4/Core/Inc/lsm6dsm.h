/**
  ******************************************************************************
  * @file    lsm6dsm.h
  * @brief   Minimal I2C driver for LSM6DSM / LSM6DSL 6-axis IMU
  *          (the B-L4S5I-IOT01A board actually has LSM6DSL, which is
  *          register-compatible with LSM6DSM for basic accel usage).
  ******************************************************************************
  */

#ifndef LSM6DSM_H
#define LSM6DSM_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  int16_t x_mg;
  int16_t y_mg;
  int16_t z_mg;
} LSM6DSM_Axes_t;

bool LSM6DSM_Init(I2C_HandleTypeDef *hi2c);
bool LSM6DSM_ReadAccel(LSM6DSM_Axes_t *out);

#ifdef __cplusplus
}
#endif
#endif /* LSM6DSM_H */
