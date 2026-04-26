/**
  ******************************************************************************
  * @file    lsm6dsm.c
  * @brief   LSM6DSM/LSM6DSL accelerometer driver (I2C2, internal MEMS bus).
  ******************************************************************************
  */

#include <stdio.h>

#include "lsm6dsm.h"

#define LSM6_ADDR_PRIMARY     (0x6AU << 1)   /* SA0=0 (B-L4S5I-IOT01A) */
#define LSM6_ADDR_SECONDARY   (0x6BU << 1)

#define LSM6_REG_FUNC_CFG_ACCESS  0x01U
#define LSM6_REG_WHO_AM_I         0x0FU
#define LSM6_REG_INT1_CTRL        0x0DU
#define LSM6_REG_CTRL1_XL         0x10U
#define LSM6_REG_CTRL3_C          0x12U
#define LSM6_REG_CTRL10_C         0x19U
#define LSM6_REG_OUTX_L_XL        0x28U
#define LSM6_REG_FUNC_SRC1        0x53U   /* SMD / step / tilt status */
#define LSM6_REG_TAP_CFG          0x58U   /* aka TAP_CFG1 in BSP headers */

/* Embedded-functions bank A (accessible only while FUNC_CFG_ACCESS=0x80). */
#define LSM6_EMB_REG_SM_THS       0x13U

/* Bit masks */
#define LSM6_CTRL10_FUNC_EN       0x04U
#define LSM6_CTRL10_SIG_MOT_EN    0x01U
#define LSM6_CTRL10_SMD_MASK      (LSM6_CTRL10_FUNC_EN | LSM6_CTRL10_SIG_MOT_EN)
#define LSM6_INT1_SIGN_MOT        0x40U
#define LSM6_TAP_CFG_LIR          0x01U   /* latch embedded-fn interrupts   */
#define LSM6_FUNC_SRC1_SIG_MOT_IA 0x40U   /* SMD event flag in FUNC_SRC1    */

/* SM_THS = number of motion "steps" before the SMD block fires.
 *  Reset default: 6,  AN5040 §6.2 example: 8,  hw2 demo: 8.
 *  Lower = more sensitive = easier to trigger. 4 is a reasonable demo
 *  value without running into the "case c)" caveat in AN5040 (requires
 *  also lowering DEB_STEP when going below the pedometer debounce floor). */
#define LSM6_SM_THS_VALUE         4U

/* Ignore SMD events fired during the first N ms after enable. The LSM6DSL
 * very often latches a spurious SIGN_MOTION_IA as part of its own start-up,
 * and we don't want to hand that to the RPi as a "real" motion event.
 * This matches the MotionWarmupEndMs approach used in hw2/. */
#define LSM6_SMD_WARMUP_MS        1500U

#define LSM6_WHO_AM_I_DSM     0x6AU
#define LSM6_WHO_AM_I_DSL     0x6AU   /* LSM6DSL also replies 0x6A */

static I2C_HandleTypeDef *s_hi2c  = NULL;
static uint16_t           s_addr  = LSM6_ADDR_PRIMARY;

/* Wall-clock tick (HAL_GetTick) at which SMD events become "real". Any
 * SIGN_MOTION_IA before this is treated as a startup glitch and dropped. */
static uint32_t           s_smd_warmup_end = 0U;

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

/* ---------------------------------------------------------------------------
 * Significant Motion Detection (LSM6DSL embedded function)
 *
 * Follows ST AN5040 rev3 §6.2 "Significant motion" and mirrors the working
 * routine from hw2/Application/User/main.c (LSM6DSL_EnableSignificantMotion):
 *
 *    1. Write 80h to FUNC_CFG_ACCESS   // open embedded reg bank A
 *    2. Write <thr> to SM_THS          // motion threshold
 *    3. Write 00h to FUNC_CFG_ACCESS   // close bank A
 *    4. CTRL1_XL already at >=26 Hz    // we set 416 Hz in LSM6DSM_Init()
 *    5. CTRL10_C  |= 0x05              // FUNC_EN | SIGN_MOTION_EN (RMW)
 *    6. TAP_CFG   |= 0x01              // LIR = latch embedded-fn IRQs (RMW)
 *    7. INT1_CTRL |= 0x40              // INT1_SIGN_MOT -> PD11 (RMW)
 *
 * Differences vs a strict AN5040 translation:
 *   - We use read-modify-write for CTRL10_C / TAP_CFG / INT1_CTRL so we
 *     don't clobber unrelated bits (matches hw2's approach).
 *   - We enable latched mode (TAP_CFG.LIR=1). With LIR=0 the INT1 line
 *     only pulses ~150 us per event (AN5040 §6.2); latched mode holds it
 *     high until FUNC_SRC1 is read, which is more forgiving for software
 *     that might be briefly preempted. EXTI is edge-triggered either way.
 *   - We arm a warmup window (see s_smd_warmup_end) so the spurious
 *     SIGN_MOTION_IA that the LSM6DSL latches during its own start-up
 *     doesn't get forwarded to the RPi as a real event.
 * -------------------------------------------------------------------------*/
bool LSM6DSM_EnableSigMotion(void)
{
  uint8_t who       = 0U;
  uint8_t ctrl1_xl  = 0U;
  uint8_t sm_ths_rb = 0U;
  uint8_t reg       = 0U;
  uint8_t ctrl10_c  = 0U;
  uint8_t tap_cfg   = 0U;
  uint8_t int1_ctrl = 0U;

  if (s_hi2c == NULL)
  {
    return false;
  }

  /* Sanity: I2C2 actually talks to the LSM6DSL. */
  if (lsm6_read_reg(LSM6_REG_WHO_AM_I, &who, 1U) != HAL_OK || who != 0x6AU)
  {
    printf("[ACC] SMD: WHO_AM_I readback failed (got 0x%02X)\r\n", who);
    return false;
  }

  /* AN5040 step 4: accel ODR must be >= 26 Hz. Init() already set 416 Hz. */
  (void)lsm6_read_reg(LSM6_REG_CTRL1_XL, &ctrl1_xl, 1U);
  if ((ctrl1_xl & 0xF0U) == 0U)
  {
    printf("[ACC] SMD: accel in power-down (CTRL1_XL=0x%02X), aborting\r\n",
           ctrl1_xl);
    return false;
  }

  /* AN5040 steps 1-3: program SM_THS via embedded-function bank A. Always
   * restore FUNC_CFG_ACCESS=0x00 before returning so the main bank is
   * reachable again even on I2C failure. */
  if (lsm6_write_reg(LSM6_REG_FUNC_CFG_ACCESS, 0x80U) != HAL_OK) return false;
  if (lsm6_write_reg(LSM6_EMB_REG_SM_THS, LSM6_SM_THS_VALUE) != HAL_OK)
  {
    (void)lsm6_write_reg(LSM6_REG_FUNC_CFG_ACCESS, 0x00U);
    return false;
  }
  (void)lsm6_read_reg(LSM6_EMB_REG_SM_THS, &sm_ths_rb, 1U);
  if (lsm6_write_reg(LSM6_REG_FUNC_CFG_ACCESS, 0x00U) != HAL_OK) return false;

  /* Step 5: CTRL10_C |= FUNC_EN | SIGN_MOTION_EN (read-modify-write). */
  if (lsm6_read_reg(LSM6_REG_CTRL10_C, &reg, 1U) != HAL_OK) return false;
  reg |= LSM6_CTRL10_SMD_MASK;
  if (lsm6_write_reg(LSM6_REG_CTRL10_C, reg) != HAL_OK) return false;

  /* Step 6: TAP_CFG.LIR = 1 so SIGN_MOTION_IA stays latched until we ack
   * it via FUNC_SRC1 (matches hw2 demo). */
  if (lsm6_read_reg(LSM6_REG_TAP_CFG, &reg, 1U) != HAL_OK) return false;
  reg |= LSM6_TAP_CFG_LIR;
  if (lsm6_write_reg(LSM6_REG_TAP_CFG, reg) != HAL_OK) return false;

  /* Step 7: INT1_CTRL |= INT1_SIGN_MOT (route SMD to PD11). */
  if (lsm6_read_reg(LSM6_REG_INT1_CTRL, &reg, 1U) != HAL_OK) return false;
  reg |= LSM6_INT1_SIGN_MOT;
  if (lsm6_write_reg(LSM6_REG_INT1_CTRL, reg) != HAL_OK) return false;

  /* Read everything back so the UART log proves the sensor accepted the
   * config (catches wrong slave address, silently-NAK'd writes, etc.). */
  (void)lsm6_read_reg(LSM6_REG_CTRL10_C,  &ctrl10_c,  1U);
  (void)lsm6_read_reg(LSM6_REG_TAP_CFG,   &tap_cfg,   1U);
  (void)lsm6_read_reg(LSM6_REG_INT1_CTRL, &int1_ctrl, 1U);

  printf("[ACC] SMD regs readback: CTRL1_XL=0x%02X SM_THS=%u "
         "CTRL10_C=0x%02X TAP_CFG=0x%02X INT1_CTRL=0x%02X\r\n",
         ctrl1_xl, sm_ths_rb, ctrl10_c, tap_cfg, int1_ctrl);

  if (sm_ths_rb != LSM6_SM_THS_VALUE                            ||
      (ctrl10_c  & LSM6_CTRL10_SMD_MASK) != LSM6_CTRL10_SMD_MASK ||
      (tap_cfg   & LSM6_TAP_CFG_LIR)     == 0U                   ||
      (int1_ctrl & LSM6_INT1_SIGN_MOT)   == 0U)
  {
    printf("[ACC] SMD: register readback mismatch, SMD is NOT enabled\r\n");
    return false;
  }

  /* Arm the warm-up window and eat any pre-existing latched status so we
   * start from a known state. */
  s_smd_warmup_end = HAL_GetTick() + LSM6_SMD_WARMUP_MS;
  (void)LSM6DSM_ClearSigMotionFlag();
  return true;
}

/* Reads FUNC_SRC1, which both tells us whether a real SMD event is latched
 * and (by the act of reading) deasserts INT1. Returns true only for
 * post-warmup events where SIGN_MOTION_IA is set - that lets the caller
 * distinguish "genuine motion, notify the RPi" from "spurious / unrelated
 * EXTI edge" and from "startup glitch during the warmup window".
 * Matches the FUNC_SRC1 + MotionWarmupEndMs logic in hw2/main.c. */
bool LSM6DSM_ClearSigMotionFlag(void)
{
  uint8_t src = 0U;

  if (s_hi2c == NULL)
  {
    return false;
  }

  if (lsm6_read_reg(LSM6_REG_FUNC_SRC1, &src, 1U) != HAL_OK)
  {
    return false;
  }

  if ((src & LSM6_FUNC_SRC1_SIG_MOT_IA) == 0U)
  {
    return false;   /* EXTI fired but SMD bit not set -> not a real SMD */
  }

  if (HAL_GetTick() < s_smd_warmup_end)
  {
    printf("[ACC] SMD: ignoring startup glitch (FUNC_SRC1=0x%02X)\r\n", src);
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
