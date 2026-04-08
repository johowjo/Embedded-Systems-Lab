#!/usr/bin/env python3

import struct
import sys
import time

from bluepy.btle import Scanner, DefaultDelegate, Peripheral, BTLEDisconnectError

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
        print(f"[INDICATION] handle=0x{cHandle:04X} data={data.hex()} text={text}")


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

        print(f"Connected. Found service {svc.uuid}")
        for ch in svc.getCharacteristics():
            print(
                f"UUID={ch.uuid} "
                f"handle=0x{ch.getHandle():04X} "
                f"props={ch.propertiesToString()}"
            )

        indicate_char = svc.getCharacteristics(INDICATE_CHAR_UUID)[0]
        print(f"Indicate characteristic handle:   0x{indicate_char.getHandle():04X}")

        cccd_handle = find_cccd_handle(indicate_char)
        if cccd_handle is None:
            print("Could not locate CCCD (0x2902) for indicate characteristic.")
            sys.exit(1)

        print(f"CCCD handle found: 0x{cccd_handle:04X}")

        # 0x0002 in little-endian => b"\x02\x00"
        cccd_value = struct.pack("<H", 0x0002)
        print(f"Writing CCCD value {cccd_value.hex()} (enable indications)")
        p.writeCharacteristic(cccd_handle, cccd_value, withResponse=True)
        print("CCCD write complete.")
        print("This should set the phone-side CCCD to 0x0002.")

        print("\nWaiting up to 30 seconds for indications...\n")
        end_time = time.time() + 30
        while time.time() < end_time:
            if p.waitForNotifications(1.0):
                continue
            print("...waiting...")

        print("\nDisabling indications (writing CCCD = 0x0000)")
        p.writeCharacteristic(cccd_handle, struct.pack("<H", 0x0000), withResponse=True)
        print("Done.")

    finally:
        p.disconnect()
        print("Disconnected.")


if __name__ == "__main__":
    main()
