#!/usr/bin/env python3
"""
BLE client for the STM32 Accelerator GATT server.

Talks to the firmware in this repo (Core/Src/gatt_db.c + app_ble.c), which
advertises as "STM32_ACC" and exposes one custom service with three chars:

  Service                 1BC5D5A5-0200-36AC-E111-010000000000
    characteristic_a      1BC5D5A5-0200-36AC-E111-0100AA000000  notify  (6 B)
                          int16 x_mg, int16 y_mg, int16 z_mg   (little-endian)
    characteristic_b      1BC5D5A5-0200-36AC-E111-0100BB000000  write   (2 B)
                          uint16 freq_hz                        (little-endian)
    characteristic_c      1BC5D5A5-0200-36AC-E111-0100CC000000  notify  (1 B)
                          uint8 motion_count   - LSM6DSL significant motion
                          event counter (++ each time the sensor's embedded
                          SMD function fires on INT1 / PD11)

Usage on a Raspberry Pi (any Linux with BlueZ >= 5.50):

    sudo apt install -y python3-pip bluez
    python3 -m pip install --user bleak
    python3 acc_client.py                       # scan + connect + stream
    python3 acc_client.py --freq 50             # set 50 Hz, then stream
    python3 acc_client.py --address AA:BB:CC:DD:EE:FF --freq 104 --duration 10
    python3 acc_client.py --csv samples.csv     # log to CSV
    python3 acc_client.py --no-interactive      # just stream, no stdin prompt

While the client is streaming, type commands at the prompt:

    > 50           set sampling frequency to 50 Hz (1..104)
    > s            read back the current freq from char_b
    > h            show this help
    > q            quit (same as Ctrl-C)

No root needed if the user is in the `bluetooth` group.
"""

from __future__ import annotations

import argparse
import asyncio
import csv
import signal
import struct
import sys
import time
from contextlib import suppress
from typing import Optional

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

# --- Must match firmware ---------------------------------------------------
DEVICE_NAME        = "STM32_ACC"
SERVICE_UUID       = "1bc5d5a5-0200-36ac-e111-010000000000"
ACCEL_CHAR_UUID    = "1bc5d5a5-0200-36ac-e111-0100aa000000"  # notify  (6 B)
FREQ_CHAR_UUID     = "1bc5d5a5-0200-36ac-e111-0100bb000000"  # write   (2 B)
MOTION_CHAR_UUID   = "1bc5d5a5-0200-36ac-e111-0100cc000000"  # notify  (1 B)

FREQ_MIN_HZ        = 1
FREQ_MAX_HZ        = 104


# --- Helpers ---------------------------------------------------------------
def parse_accel(data: bytes) -> tuple[int, int, int]:
    """Decode characteristic_a payload: int16 x,y,z in mg (little-endian)."""
    if len(data) < 6:
        raise ValueError(f"expected 6 bytes, got {len(data)}: {data.hex()}")
    x, y, z = struct.unpack_from("<hhh", data, 0)
    return x, y, z


def encode_freq(freq_hz: int) -> bytes:
    """Encode characteristic_b payload: uint16 (little-endian)."""
    if not (FREQ_MIN_HZ <= freq_hz <= FREQ_MAX_HZ):
        raise ValueError(
            f"freq must be {FREQ_MIN_HZ}..{FREQ_MAX_HZ} Hz, got {freq_hz}"
        )
    return struct.pack("<H", freq_hz)


def print_prompt_help() -> None:
    print(
        "[CTRL] type a frequency in Hz (1..104), or one of:\n"
        "       s   - read back current freq\n"
        "       h   - this help\n"
        "       q   - quit"
    )


async def stdin_control(client: BleakClient, stop_evt: asyncio.Event) -> None:
    """
    Interactive stdin reader. Runs concurrently with the notification
    stream so that the user can change the sampling frequency at any
    time by typing a new value.

    sys.stdin.readline() is blocking; we schedule it on the default
    executor so the asyncio event loop can still service BLE callbacks.
    """
    loop = asyncio.get_running_loop()
    print_prompt_help()

    while not stop_evt.is_set():
        try:
            line = await loop.run_in_executor(None, sys.stdin.readline)
        except Exception as e:
            print(f"[CTRL] stdin error: {e}")
            return

        if not line:  # EOF (e.g. input piped and closed)
            stop_evt.set()
            return

        cmd = line.strip()
        if not cmd:
            continue

        low = cmd.lower()
        if low in ("q", "quit", "exit"):
            print("[CTRL] quit requested")
            stop_evt.set()
            return

        if low in ("h", "help", "?"):
            print_prompt_help()
            continue

        if low in ("s", "status", "r", "read"):
            try:
                raw = await client.read_gatt_char(FREQ_CHAR_UUID)
                (cur_hz,) = struct.unpack("<H", raw[:2])
                print(f"[FREQ] current = {cur_hz} Hz")
            except Exception as e:
                print(f"[FREQ] read failed: {e}")
            continue

        # Otherwise: try to parse as an integer frequency.
        try:
            freq = int(cmd)
        except ValueError:
            print(f"[CTRL] '{cmd}' is not a frequency or a known command "
                  f"(try 'h')")
            continue

        try:
            payload = encode_freq(freq)
        except ValueError as e:
            print(f"[CTRL] {e}")
            continue

        try:
            await client.write_gatt_char(FREQ_CHAR_UUID, payload, response=True)
        except Exception as e:
            print(f"[FREQ] write failed: {e}")
            continue

        # Read back so the user sees the server's clamp (1..104).
        try:
            raw = await client.read_gatt_char(FREQ_CHAR_UUID)
            (back_hz,) = struct.unpack("<H", raw[:2])
            if back_hz != freq:
                print(f"[FREQ] wrote {freq} Hz -> server clamped to {back_hz} Hz")
            else:
                print(f"[FREQ] wrote {freq} Hz (confirmed)")
        except Exception:
            print(f"[FREQ] wrote {freq} Hz")


async def find_device(address: Optional[str], name: str, timeout: float) -> BLEDevice:
    """Resolve the target device either by address or by advertised name."""
    if address:
        print(f"[SCAN] looking for {address} ...")
        dev = await BleakScanner.find_device_by_address(address, timeout=timeout)
        if dev is None:
            raise RuntimeError(f"Device {address} not found within {timeout:.0f} s")
        return dev

    print(f"[SCAN] looking for name=\"{name}\" ...")
    dev = await BleakScanner.find_device_by_name(name, timeout=timeout)
    if dev is None:
        raise RuntimeError(
            f"Device \"{name}\" not found. Make sure the STM32 is powered, "
            f"advertising, and not already connected elsewhere."
        )
    return dev


# --- Main client -----------------------------------------------------------
async def run(args: argparse.Namespace) -> int:
    dev = await find_device(args.address, args.name, args.scan_timeout)
    print(f"[SCAN] found {dev.name or '?'} @ {dev.address}")

    # Set up graceful Ctrl-C.
    stop_evt = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        with suppress(NotImplementedError):
            loop.add_signal_handler(sig, stop_evt.set)

    # Optional CSV logger.
    csv_writer = None
    csv_file = None
    if args.csv:
        csv_file = open(args.csv, "w", newline="")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["host_time_s", "x_mg", "y_mg", "z_mg"])
        print(f"[CSV ] logging to {args.csv}")

    # Stats for measuring the actual notification rate.
    stats = {"count": 0, "t_last_report": time.monotonic(), "total": 0,
             "motion_events": 0, "motion_last": None}

    # Printing policy:
    #   --print-every N > 0  : print one line every N samples
    #   --print-every 0      : print every sample (beware: 104/s is a lot)
    #   default              : print one summary line per second
    print_every = args.print_every

    def on_motion(_char, data: bytearray) -> None:
        """
        characteristic_c: 1-byte rolling counter. The LSM6DSL's embedded
        "significant motion" block fires INT1; the STM32 bumps this counter
        and notifies. We detect new events by counter change (handles the
        uint8 wrap-around at 255 cleanly).
        """
        if not data:
            return
        cnt = int(data[0])
        prev = stats["motion_last"]
        if prev is None:
            # First sample after (re)subscribe - don't count as "new".
            stats["motion_last"] = cnt
            print(f"[MOT ] initial motion counter = {cnt}")
            return
        delta = (cnt - prev) & 0xFF
        if delta == 0:
            return
        stats["motion_events"] += delta
        stats["motion_last"] = cnt
        if delta == 1:
            print(f"[MOT ] *** SIGNIFICANT MOTION *** (counter={cnt}, "
                  f"total_events={stats['motion_events']})")
        else:
            print(f"[MOT ] *** {delta} MOTION events *** (counter={cnt}, "
                  f"total_events={stats['motion_events']})")

    def on_accel(_char, data: bytearray) -> None:
        try:
            x, y, z = parse_accel(bytes(data))
        except ValueError as e:
            print(f"[ACC ] bad notification: {e}")
            return
        now = time.monotonic()
        stats["count"] += 1
        stats["total"] += 1
        if csv_writer is not None:
            csv_writer.writerow([f"{now:.6f}", x, y, z])

        if print_every is not None:
            # Fixed cadence: every N samples (0 = every sample).
            if print_every <= 0 or (stats["total"] % print_every) == 0:
                print(
                    f"[ACC ] x={x:+5d} y={y:+5d} z={z:+5d} mg  "
                    f"(#{stats['total']})"
                )
        else:
            # Default: one summary line per wall-clock second.
            if (now - stats["t_last_report"]) >= 1.0:
                elapsed = now - stats["t_last_report"]
                rate = stats["count"] / elapsed
                print(
                    f"[ACC ] x={x:+5d} y={y:+5d} z={z:+5d} mg  "
                    f"({rate:5.1f} Hz, total={stats['total']})"
                )
                stats["count"] = 0
                stats["t_last_report"] = now

    async with BleakClient(dev, timeout=args.connect_timeout) as client:
        print(f"[CONN] connected={client.is_connected} mtu={client.mtu_size}")

        # Discover and sanity-check the service.
        svcs = client.services
        if SERVICE_UUID.lower() not in {s.uuid.lower() for s in svcs}:
            print("[WARN] service UUID not advertised in GATT table - "
                  "continuing anyway (BlueNRG-MS sometimes omits it from SDP)")
        found_motion = False
        for s in svcs:
            if s.uuid.lower() == SERVICE_UUID.lower():
                for c in s.characteristics:
                    props = ",".join(c.properties)
                    print(f"       char {c.uuid}  [{props}]")
                    if c.uuid.lower() == MOTION_CHAR_UUID:
                        found_motion = True
                        if "notify" not in c.properties:
                            print("[WARN] char_c present but does NOT "
                                  "advertise 'notify' - firmware GATT "
                                  "DB is wrong")
                        # Descriptors include the CCCD (0x2902). If it's
                        # missing, start_notify() will silently do nothing.
                        for d in c.descriptors:
                            print(f"              descr {d.uuid} "
                                  f"(handle={d.handle})")
        if not found_motion:
            print("[WARN] characteristic_c (motion) NOT found in GATT "
                  "table - firmware did not register it. Expected UUID: "
                  f"{MOTION_CHAR_UUID}")

        # Read the current frequency from char_b (firmware seeds it to 10 Hz).
        try:
            cur = await client.read_gatt_char(FREQ_CHAR_UUID)
            (cur_hz,) = struct.unpack("<H", cur[:2])
            print(f"[FREQ] current sampling freq = {cur_hz} Hz")
        except Exception as e:
            print(f"[FREQ] read failed: {e}")

        # If asked, write a new sampling frequency.
        if args.freq is not None:
            payload = encode_freq(args.freq)
            # Use write-with-response so we know the server processed it;
            # firmware also supports write-without-response.
            await client.write_gatt_char(FREQ_CHAR_UUID, payload, response=True)
            print(f"[FREQ] wrote {args.freq} Hz -> {payload.hex()}")
            # Read it back (firmware clamps 1..104).
            back = await client.read_gatt_char(FREQ_CHAR_UUID)
            (back_hz,) = struct.unpack("<H", back[:2])
            print(f"[FREQ] readback = {back_hz} Hz")

        # Subscribe to characteristic_a (acceleration stream) and
        # characteristic_c (significant-motion events).
        await client.start_notify(ACCEL_CHAR_UUID, on_accel)
        print("[NOTF] subscribed to characteristic_a - streaming ...")
        motion_subscribed = False
        if found_motion:
            try:
                await client.start_notify(MOTION_CHAR_UUID, on_motion)
                motion_subscribed = True
                print("[NOTF] subscribed to characteristic_c - "
                      "waiting for significant-motion events")
                # Prime the counter so the first real event registers as +1
                # rather than being swallowed by the "first sample" path in
                # on_motion(). We poll once; firmware initial value is 0.
                try:
                    cur = await client.read_gatt_char(MOTION_CHAR_UUID)
                    if cur:
                        stats["motion_last"] = int(cur[0])
                        print(f"[MOT ] baseline motion counter = "
                              f"{stats['motion_last']}")
                except Exception as e:
                    print(f"[MOT ] baseline read failed (non-fatal): {e}")
            except Exception as e:
                print(f"[NOTF] motion subscribe FAILED: {e}")
        else:
            print("[NOTF] skipping motion subscribe (char_c not in GATT)")

        # Start the interactive stdin task (unless disabled).
        control_task: Optional[asyncio.Task] = None
        if args.interactive:
            control_task = asyncio.create_task(stdin_control(client, stop_evt))
        else:
            print("       (Ctrl-C to stop; --no-interactive, stdin ignored)")

        # Run until timeout, Ctrl-C, or 'q' from the control loop.
        try:
            if args.duration > 0:
                with suppress(asyncio.TimeoutError):
                    await asyncio.wait_for(stop_evt.wait(), timeout=args.duration)
            else:
                await stop_evt.wait()
        finally:
            if control_task is not None:
                control_task.cancel()
                # The executor thread may still be blocked in readline(),
                # but cancelling the Task unparents it from the event loop.
                with suppress(Exception, asyncio.CancelledError):
                    await control_task
            with suppress(Exception):
                await client.stop_notify(ACCEL_CHAR_UUID)
            if motion_subscribed:
                with suppress(Exception):
                    await client.stop_notify(MOTION_CHAR_UUID)
            print(f"[DONE] received {stats['total']} samples, "
                  f"{stats['motion_events']} significant-motion events")
            if csv_file is not None:
                csv_file.close()

    return 0


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="BLE client for the STM32 accelerometer GATT server.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--address", "-a", default=None,
                   help="BDADDR of the STM32 (e.g. C0:11:22:33:44:55). "
                        "If omitted, scan for --name instead.")
    p.add_argument("--name", default=DEVICE_NAME,
                   help="Advertised local name of the STM32.")
    p.add_argument("--freq", "-f", type=int, default=None,
                   help=f"If set, write this sampling frequency in Hz "
                        f"({FREQ_MIN_HZ}..{FREQ_MAX_HZ}) to characteristic_b "
                        f"before streaming.")
    p.add_argument("--duration", "-d", type=float, default=0.0,
                   help="Stop after N seconds. 0 = run until Ctrl-C.")
    p.add_argument("--csv", default=None,
                   help="Optional CSV file to log (host_time_s,x_mg,y_mg,z_mg).")
    p.add_argument("--scan-timeout", type=float, default=10.0,
                   help="How long to scan for the device.")
    p.add_argument("--connect-timeout", type=float, default=15.0,
                   help="GATT connection timeout.")
    p.add_argument("--no-interactive", dest="interactive",
                   action="store_false", default=True,
                   help="Disable the interactive stdin prompt (useful when "
                        "piping output to a file or running non-attached).")
    p.add_argument("--print-every", type=int, default=None,
                   help="Print every Nth notification instead of one "
                        "summary line per second (0 = print every sample). "
                        "Default: 1-second summary.")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 130
    except RuntimeError as e:
        print(f"[ERR ] {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
