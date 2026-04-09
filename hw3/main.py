#!/usr/bin/env python3

import struct
import sys
import time

from bluepy.btle import Scanner, DefaultDelegate, Peripheral, BTLEDisconnectError, BTLEGattError

# ====== Match these to your iPhone nRF Connect setup ======
TARGET_NAME = "pad"
TARGET_ADDR = None  # or set directly if you want, e.g. "65:e5:2f:38:6c:b3"

SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
INDICATE_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef2"
CCCD_UUID_STR = "00002902-0000-1000-8000-00805f9b34fb"
# ==========================================================


class NotificationDelegate(DefaultDelegate):
    def __init__(self):
        super().__init__()

    def handleNotification(self, cHandle, data):
        try:
            text = data.decode("utf-8", errors="replace")
        except Exception:
            text = repr(data)
        # bluepy delivers both notifications and indications through this callback.
        print(f"[NOTIFY/INDICATE] handle=0x{cHandle:04X} data={data.hex()} text={text}")


def scan_for_device(name, timeout=8):
    print(f"Scanning for BLE peripheral named '{name}' ...")
    scanner = Scanner()
    devices = scanner.scan(timeout)

    for dev in devices:
        dev_name = None
        for _, desc, value in dev.getScanData():
            if desc in ("Complete Local Name", "Short Local Name"):
                dev_name = value
                break

        if dev_name == name:
            print(f"Found {dev_name} at {dev.addr}")
            return dev.addr

    return None


def find_cccd_handle(char_obj):
    """
    Search a few handles after the characteristic value handle
    for the CCCD descriptor (0x2902).
    """
    start = char_obj.getHandle() + 1
    end = char_obj.getHandle() + 8

    descriptors = char_obj.peripheral.getDescriptors(startHnd=start, endHnd=end)
    for d in descriptors:
        if str(d.uuid).lower() == CCCD_UUID_STR:
            return d.handle
    return None


def main():
    addr = TARGET_ADDR

    if addr is None:
        addr = scan_for_device(TARGET_NAME)
        if addr is None:
            print("Could not find target BLE peripheral.")
            sys.exit(1)

    print(f"Connecting to {addr} ...")
    try:
        # iPhone peripherals usually need addrType="random"
        p = Peripheral(addr, addrType="random")
    except BTLEDisconnectError as e:
        print(f"Connect failed: {e}")
        sys.exit(1)

    p.setDelegate(NotificationDelegate())

    try:
        svc = p.getServiceByUUID(SERVICE_UUID)
        print(f"Connected. Found service {svc.uuid}")
        for ch in svc.getCharacteristics():
            print(
                f"UUID={ch.uuid} "
                f"handle=0x{ch.getHandle():04X} "
                f"props={ch.propertiesToString()}"
            )

        indicate_char = svc.getCharacteristics(INDICATE_CHAR_UUID)[0]
        print(f"Target characteristic handle:     0x{indicate_char.getHandle():04X}")

        cccd_handle = find_cccd_handle(indicate_char)
        if cccd_handle is None:
            print("Could not locate CCCD (0x2902) for indicate characteristic.")
            sys.exit(1)

        print(f"CCCD handle found: 0x{cccd_handle:04X}")

        # Try dual-subscription first, then gracefully fall back.
        # 0x0003: notify+indicate, 0x0002: indicate only, 0x0001: notify only
        cccd_candidates = [
            (0x0003, "notify + indicate"),
            (0x0002, "indicate only"),
            (0x0001, "notify only"),
        ]
        enabled_mode = None
        for cccd_raw, label in cccd_candidates:
            cccd_value = struct.pack("<H", cccd_raw)
            try:
                print(f"Writing CCCD value {cccd_value.hex()} ({label})")
                p.writeCharacteristic(cccd_handle, cccd_value, withResponse=True)
                print(f"CCCD write complete. Enabled mode: {label}")
                enabled_mode = cccd_raw
                break
            except BTLEGattError as e:
                print(f"CCCD write failed for {label}: {e}")
            except BTLEDisconnectError as e:
                print(f"Disconnected during CCCD write for {label}: {e}")
                break

        if enabled_mode is None:
            print("Could not enable notify/indicate on CCCD with 0x0003/0x0002/0x0001.")
            sys.exit(1)

        print("\nWaiting up to 30 seconds for notify/indicate packets...\n")
        print("Polling read is also enabled to detect value changes without notifications.")
        end_time = time.time() + 30
        last_read_value = None
        while time.time() < end_time:
            try:
                got_event = p.waitForNotifications(1.0)
            except BTLEDisconnectError as e:
                print(f"Disconnected while waiting for notifications: {e}")
                break

            try:
                current_value = indicate_char.read()
            except BTLEDisconnectError as e:
                print(f"Disconnected while reading characteristic: {e}")
                break
            except BTLEGattError as e:
                print(f"Read failed: {e}")
                current_value = None

            if current_value is not None and current_value != last_read_value:
                try:
                    text = current_value.decode("utf-8", errors="replace")
                except Exception:
                    text = repr(current_value)
                print(
                    f"[READ CHANGE] handle=0x{indicate_char.getHandle():04X} "
                    f"data={current_value.hex()} text={text}"
                )
                last_read_value = current_value

            if not got_event:
                print("...waiting...")

        print("\nDisabling notify/indicate (writing CCCD = 0x0000)")
        p.writeCharacteristic(cccd_handle, struct.pack("<H", 0x0000), withResponse=True)
        print("Done.")

    finally:
        p.disconnect()
        print("Disconnected.")


if __name__ == "__main__":
    main()
