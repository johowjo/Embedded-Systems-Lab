/**
  ******************************************************************************
  * @file    lsm6dsm.c
  * @brief   LSM6DSM/LSM6DSL accelerometer driver (I2C2, internal MEMS bus).
  ******************************************************************************
  */

#include "lsm6dsm.h"

#define LSM6_ADDR_PRIMARY     (0x6AU << 1)   /* SA0=0 (B-L4S5I-IOT01A) */
#define LSM6_ADDR_SECONDARY   (0x6BU << 1)

#define LSM6_REG_WHO_AM_I     0x0FU
#define LSM6_REG_CTRL1_XL     0x10U
#define LSM6_REG_CTRL3_C      0x12U
#define LSM6_REG_OUTX_L_XL    0x28U

#define LSM6_WHO_AM_I_DSM     0x6AU
#define LSM6_WHO_AM_I_DSL     0x6AU   /* LSM6DSL also replies 0x6A */

static I2C_HandleTypeDef *s_hi2c  = NULL;
static uint16_t           s_addr  = LSM6_ADDR_PRIMARY;

static HAL_StatusTypeDef lsm6_read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
  return HAL_I2C_Mem_Read(s_hi2c, s_addr, reg,
                          I2C_MEMADD_SIZE_8BIT, data, len, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef lsm6_write_reg(uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(s_hi2c, s_addr, reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1U, HAL_MAX_DELAY);
}

bool LSM6DSM_Init(I2C_HandleTypeDef *hi2c)
{
  uint8_t who = 0U;

  if (hi2c == NULL)
  {
    return false;
  }
  s_hi2c = hi2c;

  /* Probe primary / secondary slave address. */
  s_addr = LSM6_ADDR_PRIMARY;
  if (lsm6_read_reg(LSM6_REG_WHO_AM_I, &who, 1U) != HAL_OK || who != LSM6_WHO_AM_I_DSM)
  {
    s_addr = LSM6_ADDR_SECONDARY;
    if (lsm6_read_reg(LSM6_REG_WHO_AM_I, &who, 1U) != HAL_OK || who != LSM6_WHO_AM_I_DSM)
    {
      return false;
    }
  }

  /* CTRL3_C = 0x44 -> BDU=1, IF_INC=1 */
  if (lsm6_write_reg(LSM6_REG_CTRL3_C, 0x44U) != HAL_OK)
  {
    return false;
  }

  /* CTRL1_XL = 0x60 -> ODR=416 Hz, FS=+/-2g.
     We let the sensor free-run and let the task decide when to sample. */
  if (lsm6_write_reg(LSM6_REG_CTRL1_XL, 0x60U) != HAL_OK)
  {
    return false;
  }

  return true;
}

bool LSM6DSM_ReadAccel(LSM6DSM_Axes_t *out)
{
  uint8_t raw[6];
  int16_t x_raw;
  int16_t y_raw;
  int16_t z_raw;

  if (out == NULL || s_hi2c == NULL)
  {
    return false;
  }

  if (lsm6_read_reg(LSM6_REG_OUTX_L_XL, raw, sizeof(raw)) != HAL_OK)
  {
    return false;
  }

  x_raw = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
  y_raw = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);
  z_raw = (int16_t)(((uint16_t)raw[5] << 8) | raw[4]);

  /* Sensitivity at +/-2g is 0.061 mg/LSB. Convert to mg. */
  out->x_mg = (int16_t)((x_raw * 61) / 1000);
  out->y_mg = (int16_t)((y_raw * 61) / 1000);
  out->z_mg = (int16_t)((z_raw * 61) / 1000);

  return true;
}
