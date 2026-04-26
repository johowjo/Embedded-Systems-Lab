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

/*
 * Significant Motion Detection (SMD)
 * ----------------------------------
 * Enables the LSM6DSL embedded "significant motion" function and routes
 * its event onto the INT1 pin (wired to PD11 / EXTI11 on B-L4S5I-IOT01A).
 *
 * LSM6DSM_EnableSigMotion():
 *   Programs CTRL10_C / TAP_CFG / INT1_CTRL and returns true on success.
 *   Must be called after LSM6DSM_Init() and after CTRL1_XL has been set
 *   to an ODR >= 26 Hz (our Init sets 416 Hz, so that's satisfied).
 *
 * LSM6DSM_ClearSigMotionFlag():
 *   Reads FUNC_SRC1 (0x53) which both returns the latched SIGN_MOTION_IA
 *   status and deasserts the INT1 line. Call this from the task handling
 *   the EXTI event, NOT from the ISR (reads I2C which may block).
 */
bool LSM6DSM_EnableSigMotion(void);
bool LSM6DSM_ClearSigMotionFlag(void);

#ifdef __cplusplus
}
#endif
#endif /* LSM6DSM_H */
