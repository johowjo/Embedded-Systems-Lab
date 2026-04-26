/**
  ******************************************************************************
  * @file    app_ble.h
  * @brief   TASK_BLE / TASK_ACC application layer for the X-CUBE-BLE1 stack.
  ******************************************************************************
  */

#ifndef APP_BLE_H
#define APP_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os.h"

/* ---- RTOS synchronization objects (defined in main.c) ---------------------
 * bleMutexHandle   : mutex that serializes every SPI3/HCI access to the
 *                    BlueNRG-MS. Must be held by TASK_BLE around
 *                    hci_user_evt_proc() and by TASK_ACC around any
 *                    aci_gatt_update_char_value() / Acc_Notify() call.
 * appEventsHandle  : event flags used for task-to-task signalling.
 *                    APP_EVT_BLE_READY is set by TASK_BLE after the stack
 *                    finishes APP_BLE_Init(); TASK_ACC waits on it before
 *                    it starts sampling.
 */
extern osMutexId_t       bleMutexHandle;
extern osEventFlagsId_t  appEventsHandle;

#define APP_EVT_BLE_READY   (1U << 0)
#define APP_EVT_MOTION      (1U << 1)   /* LSM6DSL significant motion seen */

/* Called once from TASK_BLE before it enters its event loop. Brings up
 * the BlueNRG-MS, creates the Accelerator service, starts advertising
 * and initializes the LSM6DSM sensor. */
void APP_BLE_Init(void);

/* The two RTOS task bodies.
 *   TASK_BLE_Run : drains BlueNRG HCI events, re-arms advertising.
 *   TASK_ACC_Run : reads one LSM6DSM sample and pushes it via notify.
 * Both functions take bleMutexHandle internally for BlueNRG access, so
 * they are safe to call from independent FreeRTOS threads. */
void TASK_BLE_Run(void);
void TASK_ACC_Run(void);

/* Called from the EXTI15_10 ISR when PD11 (LSM6DSL INT1) pulses. Sets
 * APP_EVT_MOTION so TASK_ACC can do the I2C ack + BLE notify safely in
 * task context. MUST only use ISR-safe CMSIS-RTOS2 APIs. */
void Motion_OnIsr(void);

#ifdef __cplusplus
}
#endif
#endif /* APP_BLE_H */
