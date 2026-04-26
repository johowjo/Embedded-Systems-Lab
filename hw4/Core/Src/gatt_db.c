/**
  ******************************************************************************
  * @file    gatt_db.c
  * @brief   GATT database for the custom Accelerator Service.
  ******************************************************************************
  */

#include <string.h>
#include "bluenrg_def.h"
#include "bluenrg_conf.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_gatt_server.h"

#include "gatt_db.h"

/* Acc sampling frequency default / bounds ------------------------------------*/
#define ACC_FREQ_DEFAULT_HZ     10U
#define ACC_FREQ_MIN_HZ         1U
#define ACC_FREQ_MAX_HZ         104U

/* 128-bit UUIDs (little-endian, LSB first) -----------------------------------*/
#define COPY_UUID_128(u, b15,b14,b13,b12,b11,b10,b9,b8,b7,b6,b5,b4,b3,b2,b1,b0) \
do {                                                                            \
  (u)[0]=b0;  (u)[1]=b1;  (u)[2]=b2;  (u)[3]=b3;                                \
  (u)[4]=b4;  (u)[5]=b5;  (u)[6]=b6;  (u)[7]=b7;                                \
  (u)[8]=b8;  (u)[9]=b9;  (u)[10]=b10;(u)[11]=b11;                              \
  (u)[12]=b12;(u)[13]=b13;(u)[14]=b14;(u)[15]=b15;                              \
} while (0)

/* Accelerator service UUID */
#define COPY_ACC_SERVICE_UUID(u) \
  COPY_UUID_128((u),0x1B,0xC5,0xD5,0xA5,0x02,0x00,0x36,0xAC, \
                    0xE1,0x11,0x01,0x00,0x00,0x00,0x00,0x00)

/* Characteristic_a : XYZ acceleration (notify) */
#define COPY_ACC_VALUE_CHAR_UUID(u) \
  COPY_UUID_128((u),0x1B,0xC5,0xD5,0xA5,0x02,0x00,0x36,0xAC, \
                    0xE1,0x11,0x01,0x00,0xAA,0x00,0x00,0x00)

/* Characteristic_b : sampling frequency (write) */
#define COPY_ACC_FREQ_CHAR_UUID(u) \
  COPY_UUID_128((u),0x1B,0xC5,0xD5,0xA5,0x02,0x00,0x36,0xAC, \
                    0xE1,0x11,0x01,0x00,0xBB,0x00,0x00,0x00)

/* Characteristic_c : significant-motion event counter (notify) */
#define COPY_ACC_MOTION_CHAR_UUID(u) \
  COPY_UUID_128((u),0x1B,0xC5,0xD5,0xA5,0x02,0x00,0x36,0xAC, \
                    0xE1,0x11,0x01,0x00,0xCC,0x00,0x00,0x00)

/* Handles (filled by aci_gatt_add_serv/add_char) ------------------------------*/
static uint16_t AccServiceHandle    = 0;
static uint16_t AccValueCharHandle  = 0;
static uint16_t AccFreqCharHandle   = 0;
static uint16_t AccMotionCharHandle = 0;

/* Current accelerometer sampling frequency (Hz) ------------------------------*/
static volatile uint16_t acc_freq_hz = ACC_FREQ_DEFAULT_HZ;

/* Significant-motion event counter (wraps at 255). Exposed on char_c. --------*/
static volatile uint8_t motion_count = 0;

tBleStatus Add_Acc_Service(void)
{
  tBleStatus ret;
  uint8_t uuid[16];

  /* 1) Primary service: enough records for 3 characteristics (1 svc + 3*N) */
  COPY_ACC_SERVICE_UUID(uuid);
  ret = aci_gatt_add_serv(UUID_TYPE_128, uuid, PRIMARY_SERVICE,
                          1 + 3 * 3, &AccServiceHandle);
  if (ret != BLE_STATUS_SUCCESS)
  {
    return ret;
  }

  /* 2) characteristic_a : 6-byte XYZ, NOTIFY */
  COPY_ACC_VALUE_CHAR_UUID(uuid);
  ret = aci_gatt_add_char(AccServiceHandle, UUID_TYPE_128, uuid,
                          6,
                          CHAR_PROP_NOTIFY | CHAR_PROP_READ,
                          ATTR_PERMISSION_NONE,
                          GATT_DONT_NOTIFY_EVENTS,
                          16, 0,
                          &AccValueCharHandle);
  if (ret != BLE_STATUS_SUCCESS)
  {
    return ret;
  }

  /* 3) characteristic_b : 2-byte freq, WRITE (with response) + WRITE_NO_RESP */
  COPY_ACC_FREQ_CHAR_UUID(uuid);
  ret = aci_gatt_add_char(AccServiceHandle, UUID_TYPE_128, uuid,
                          2,
                          CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP | CHAR_PROP_READ,
                          ATTR_PERMISSION_NONE,
                          GATT_NOTIFY_ATTRIBUTE_WRITE,
                          16, 0,
                          &AccFreqCharHandle);
  if (ret != BLE_STATUS_SUCCESS)
  {
    return ret;
  }

  /* Seed the initial frequency value so a client that reads sees the default */
  uint8_t init_freq[2];
  init_freq[0] = (uint8_t)(acc_freq_hz & 0xFF);
  init_freq[1] = (uint8_t)((acc_freq_hz >> 8) & 0xFF);
  (void)aci_gatt_update_char_value(AccServiceHandle, AccFreqCharHandle,
                                   0, 2, init_freq);

  /* 4) characteristic_c : 1-byte motion counter, NOTIFY (+READ for convenience) */
  COPY_ACC_MOTION_CHAR_UUID(uuid);
  ret = aci_gatt_add_char(AccServiceHandle, UUID_TYPE_128, uuid,
                          1,
                          CHAR_PROP_NOTIFY | CHAR_PROP_READ,
                          ATTR_PERMISSION_NONE,
                          GATT_DONT_NOTIFY_EVENTS,
                          16, 0,
                          &AccMotionCharHandle);
  if (ret != BLE_STATUS_SUCCESS)
  {
    return ret;
  }

  uint8_t init_mot = motion_count;
  (void)aci_gatt_update_char_value(AccServiceHandle, AccMotionCharHandle,
                                   0, 1, &init_mot);

  return BLE_STATUS_SUCCESS;
}

tBleStatus Acc_Notify(const AccelAxes_t *axes)
{
  uint8_t buff[6];

  if (axes == NULL)
  {
    return BLE_STATUS_ERROR;
  }

  buff[0] = (uint8_t)(axes->x_mg & 0xFF);
  buff[1] = (uint8_t)((axes->x_mg >> 8) & 0xFF);
  buff[2] = (uint8_t)(axes->y_mg & 0xFF);
  buff[3] = (uint8_t)((axes->y_mg >> 8) & 0xFF);
  buff[4] = (uint8_t)(axes->z_mg & 0xFF);
  buff[5] = (uint8_t)((axes->z_mg >> 8) & 0xFF);

  return aci_gatt_update_char_value(AccServiceHandle, AccValueCharHandle,
                                    0, 6, buff);
}

void Acc_GattWrite_CB(uint16_t attr_handle, uint8_t data_len, const uint8_t *data)
{
  /* The attribute VALUE for the freq characteristic lives at handle+1 */
  if (attr_handle == (uint16_t)(AccFreqCharHandle + 1U))
  {
    if (data_len >= 2U && data != NULL)
    {
      uint16_t new_freq = (uint16_t)(((uint16_t)data[1] << 8) | data[0]);

      if (new_freq < ACC_FREQ_MIN_HZ)
      {
        new_freq = ACC_FREQ_MIN_HZ;
      }
      else if (new_freq > ACC_FREQ_MAX_HZ)
      {
        new_freq = ACC_FREQ_MAX_HZ;
      }

      acc_freq_hz = new_freq;
    }
  }
}

uint16_t Acc_GetSamplingFreqHz(void)
{
  return acc_freq_hz;
}

tBleStatus Motion_Notify(void)
{
  motion_count = (uint8_t)(motion_count + 1U);
  uint8_t v = motion_count;
  return aci_gatt_update_char_value(AccServiceHandle, AccMotionCharHandle,
                                    0, 1, &v);
}

uint8_t Motion_GetCount(void)
{
  return motion_count;
}
