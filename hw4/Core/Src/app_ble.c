/**
  ******************************************************************************
  * @file    app_ble.c
  * @brief   Application-level glue for the X-CUBE-BLE1 (BlueNRG-MS) stack.
  *
  *          - Initializes the BlueNRG-MS controller (HCI/GAP/GATT).
  *          - Registers the custom GATT "Accelerator Service" (see gatt_db.c).
  *          - Implements TASK_BLE_Run() and TASK_ACC_Run().
  *
  *          Reference:
  *          * UM2071 (en.DM00169392.pdf), "Getting started with the
  *            X-CUBE-BLE1 Bluetooth Low Energy software expansion for
  *            STM32Cube" - sections on HCI, GAP, GATT APIs.
  *          * X-CUBE-BLE1 SensorDemo_BLESensor-App reference project.
  ******************************************************************************
  */

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "main.h"

#include "bluenrg_def.h"
#include "bluenrg_conf.h"
#include "bluenrg_types.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_hal_aci.h"
#include "bluenrg_aci_const.h"
#include "hci.h"
#include "hci_le.h"
#include "hci_const.h"
#include "hci_tl.h"
#include "hci_tl_interface.h"
#include "sm.h"

#include "gatt_db.h"
#include "lsm6dsm.h"
#include "app_ble.h"

/* I2C handle for the internal MEMS bus (declared in main.c). */
extern I2C_HandleTypeDef hi2c2;

/* ---- Connection / advertising state ---------------------------------------*/
#define BDADDR_SIZE                 6
#define IDB04A1                     0
#define IDB05A1                     1
#define ADV_INTERVAL_MIN_MS         1000
#define ADV_INTERVAL_MAX_MS         1200
#define LOCAL_NAME                  'S','T','M','3','2','_','A','C','C'

static uint8_t        bnrg_expansion_board = IDB04A1;
static uint8_t        bdaddr[BDADDR_SIZE];

static volatile uint8_t  set_connectable     = 1;
static volatile uint16_t connection_handle   = 0;
static volatile uint8_t  connected           = FALSE;

/* ---- Forward declarations -------------------------------------------------*/
static void App_Set_DeviceConnectable(void);
static void GAP_DisconnectionComplete_CB(void);
static void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle);

/* Called by the BlueNRG stack whenever an HCI event is received --------------*/
void user_notify(void *pData)
{
  hci_uart_pckt  *hci_pckt   = (hci_uart_pckt *)pData;
  hci_event_pckt *event_pckt = (hci_event_pckt *)hci_pckt->data;

  if (hci_pckt->type != HCI_EVENT_PKT)
  {
    return;
  }

  switch (event_pckt->evt)
  {
    case EVT_DISCONN_COMPLETE:
      GAP_DisconnectionComplete_CB();
      break;

    case EVT_LE_META_EVENT:
    {
      evt_le_meta_event *evt = (evt_le_meta_event *)event_pckt->data;
      if (evt->subevent == EVT_LE_CONN_COMPLETE)
      {
        evt_le_connection_complete *cc = (evt_le_connection_complete *)evt->data;
        GAP_ConnectionComplete_CB(cc->peer_bdaddr, cc->handle);
      }
      break;
    }

    case EVT_VENDOR:
    {
      evt_blue_aci *blue_evt = (evt_blue_aci *)event_pckt->data;
      switch (blue_evt->ecode)
      {
        /* client -> server: characteristic_b (sampling frequency) write */
        case EVT_BLUE_GATT_ATTRIBUTE_MODIFIED:
        {
          evt_gatt_attr_modified_IDB05A1 *pr =
              (evt_gatt_attr_modified_IDB05A1 *)blue_evt->data;
          printf("[BLE] GATT write: attr_handle=0x%04X len=%u\r\n",
                 pr->attr_handle, pr->data_length);
          Acc_GattWrite_CB(pr->attr_handle, pr->data_length, pr->att_data);
          printf("[ACC] new sampling freq = %u Hz\r\n",
                 Acc_GetSamplingFreqHz());
          break;
        }

        default:
          break;
      }
      break;
    }

    default:
      break;
  }
}

/* ---- BLE controller init --------------------------------------------------*/
void APP_BLE_Init(void)
{
  uint16_t service_handle;
  uint16_t dev_name_char_handle;
  uint16_t appearance_char_handle;
  uint8_t  hwVersion;
  uint16_t fwVersion;
  uint8_t  bdaddr_len_out;
  int      ret;
  const char *device_name = "STM32_ACC";

  printf("\r\n==== STM32 BLE Accelerometer (GATT server) boot ====\r\n");

  /* Wire our SPI/IRQ/CS/RST functions into the HCI transport layer. */
  hci_tl_lowlevel_init();

  /* Bring up the stack and hand it our event callback. */
  hci_init(user_notify, NULL);

  /* Reset the controller so that aci_hal_write_config_data works. */
  getBlueNRGVersion(&hwVersion, &fwVersion);
  hci_reset();
  osDelay(100);   /* FreeRTOS-aware sleep, yields to other tasks */

  printf("[BLE] BlueNRG HW=0x%02X FW=0x%04X\r\n", hwVersion, fwVersion);

  if (hwVersion > 0x30)
  {
    bnrg_expansion_board = IDB05A1;
  }

  /* Grab the factory static random address. */
  ret = aci_hal_read_config_data(CONFIG_DATA_RANDOM_ADDRESS, BDADDR_SIZE,
                                 &bdaddr_len_out, bdaddr);
  (void)ret;
  if ((bdaddr[5] & 0xC0) != 0xC0)
  {
    /* Force a valid static random address if the one stored is not valid. */
    bdaddr[5] |= 0xC0;
  }

  /* GATT + GAP stack init. */
  (void)aci_gatt_init();

  if (bnrg_expansion_board == IDB05A1)
  {
    (void)aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, 0x09,
                               &service_handle, &dev_name_char_handle,
                               &appearance_char_handle);
  }
  else
  {
    (void)aci_gap_init_IDB04A1(GAP_PERIPHERAL_ROLE_IDB04A1,
                               &service_handle, &dev_name_char_handle,
                               &appearance_char_handle);
  }

  (void)aci_gatt_update_char_value(service_handle, dev_name_char_handle, 0,
                                   (uint8_t)strlen(device_name),
                                   (uint8_t *)device_name);

  (void)aci_gap_set_auth_requirement(MITM_PROTECTION_NOT_REQUIRED,
                                     OOB_AUTH_DATA_ABSENT, NULL,
                                     7, 16,
                                     USE_FIXED_PIN_FOR_PAIRING, 123456,
                                     BONDING);

  /* Register our custom accelerator service (characteristic_a + _b). */
  (void)Add_Acc_Service();

  (void)aci_hal_set_tx_power_level(1, 4);

  printf("[BLE] BDADDR %02X:%02X:%02X:%02X:%02X:%02X  name=\"%s\"\r\n",
         bdaddr[5], bdaddr[4], bdaddr[3], bdaddr[2], bdaddr[1], bdaddr[0],
         device_name);

  /* Initialize the LSM6DSM/LSM6DSL over I2C2. */
  if (LSM6DSM_Init(&hi2c2))
  {
    printf("[ACC] LSM6DSM init OK\r\n");
  }
  else
  {
    printf("[ACC] LSM6DSM init FAILED (check I2C2)\r\n");
  }

  printf("[BLE] stack ready, advertising as STM32_ACC\r\n");
}

/* ---- GAP helpers ----------------------------------------------------------*/
static void App_Set_DeviceConnectable(void)
{
  const char local_name[] = { AD_TYPE_COMPLETE_LOCAL_NAME, LOCAL_NAME };

  (void)hci_le_set_scan_resp_data(0, NULL);
  (void)aci_gap_set_discoverable(ADV_DATA_TYPE,
                                 (ADV_INTERVAL_MIN_MS * 1000) / 625,
                                 (ADV_INTERVAL_MAX_MS * 1000) / 625,
                                 STATIC_RANDOM_ADDR, NO_WHITE_LIST_USE,
                                 sizeof(local_name), local_name,
                                 0, NULL, 0, 0);
}

static void GAP_DisconnectionComplete_CB(void)
{
  connected         = FALSE;
  connection_handle = 0;
  set_connectable   = TRUE;
  printf("[GAP] disconnected, restart advertising\r\n");
}

static void GAP_ConnectionComplete_CB(uint8_t addr[6], uint16_t handle)
{
  connected         = TRUE;
  connection_handle = handle;
  printf("[GAP] connected to %02X:%02X:%02X:%02X:%02X:%02X handle=0x%04X\r\n",
         addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], handle);
}

/* ---- BLE mutex helper -----------------------------------------------------
 * Both tasks must not hit the BlueNRG-MS concurrently. The mutex is created
 * in main.c (bleMutexHandle). The helpers below are safe no-ops before the
 * mutex exists (i.e. during APP_BLE_Init(), which runs from inside TASK_BLE
 * before the first TASK_BLE_Run() iteration - no contention possible). */
static inline void App_Ble_Lock(void)
{
  if (bleMutexHandle != NULL)
  {
    (void)osMutexAcquire(bleMutexHandle, osWaitForever);
  }
}

static inline void App_Ble_Unlock(void)
{
  if (bleMutexHandle != NULL)
  {
    (void)osMutexRelease(bleMutexHandle);
  }
}

/* ---- TASK_BLE --------------------------------------------------------------*/
/*  Runs inside StartTask02 (FreeRTOS thread "TASK_BLE"):
 *    - Re-arms advertising when not connected.
 *    - Drains BlueNRG events (hci_user_evt_proc()) so that user_notify()
 *      is called for GAP/GATT/Vendor events including writes to
 *      characteristic_b (sampling frequency).
 *  Serializes every BlueNRG access with TASK_ACC via bleMutexHandle.
 *----------------------------------------------------------------------------*/
void TASK_BLE_Run(void)
{
  App_Ble_Lock();
  if (set_connectable)
  {
    App_Set_DeviceConnectable();
    set_connectable = FALSE;
  }
  hci_user_evt_proc();
  App_Ble_Unlock();
}

/* ---- TASK_ACC --------------------------------------------------------------*/
/*  Runs inside StartTask03 (FreeRTOS thread "TASK_ACC"):
 *    - Reads one sample from LSM6DSM over I2C2.
 *    - Pushes it over characteristic_a via aci_gatt_update_char_value()
 *      (notify), under bleMutexHandle.
 *  The FreeRTOS wrapper sleeps for 1000/freq_hz ms between calls so that
 *  the effective notification rate matches whatever the client wrote to
 *  characteristic_b.
 *----------------------------------------------------------------------------*/
void TASK_ACC_Run(void)
{
  static uint32_t last_log_ms    = 0;
  static uint32_t samples_in_sec = 0;
  LSM6DSM_Axes_t      raw;
  AccelAxes_t         axes;
  uint32_t            now;

  if (!connected)
  {
    return;
  }

  /* I2C2 to the LSM6DSM is independent of the BlueNRG SPI bus, so no
   * BLE mutex is needed here. */
  if (!LSM6DSM_ReadAccel(&raw))
  {
    return;
  }

  axes.x_mg = raw.x_mg;
  axes.y_mg = raw.y_mg;
  axes.z_mg = raw.z_mg;

  App_Ble_Lock();
  (void)Acc_Notify(&axes);
  App_Ble_Unlock();

  /* Throttled status line so the terminal stays readable even at 104 Hz. */
  samples_in_sec++;
  now = HAL_GetTick();
  if ((now - last_log_ms) >= 1000U)
  {
    printf("[ACC] x=%+5d y=%+5d z=%+5d mg (%lu samples/s, target %u Hz)\r\n",
           axes.x_mg, axes.y_mg, axes.z_mg,
           (unsigned long)samples_in_sec,
           Acc_GetSamplingFreqHz());
    samples_in_sec = 0;
    last_log_ms    = now;
  }
}
