# RPi BLE client for the STM32 accelerometer service

Pairs with the firmware in this repo (advertised name `STM32_ACC`).

## Install (Raspberry Pi OS / any Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y bluez python3-pip
# Make sure BlueZ is running and the adapter is up:
sudo systemctl enable --now bluetooth
hciconfig hci0 up           # or: bluetoothctl power on

# Python deps (user install, no venv needed; use a venv if you prefer):
python3 -m pip install --user -r requirements.txt
```

If you get `org.bluez.Error.NotPermitted` on `connect`, add your user to the
`bluetooth` group and log out/in:

```bash
sudo usermod -aG bluetooth "$USER"
```

## Run

```bash
# Just scan, connect, and stream at whatever freq the STM32 currently has:
python3 acc_client.py

# Set 50 Hz, stream for 10 s, log to CSV:
python3 acc_client.py --freq 50 --duration 10 --csv samples.csv

# Connect directly by address (avoids scan):
python3 acc_client.py --address C0:11:22:33:44:55 --freq 104

# Non-interactive (no stdin prompt):
python3 acc_client.py --no-interactive
```

### Change the sampling frequency at runtime

While the client is running and streaming, type commands at the terminal
(followed by <Enter>):

| Input            | Effect                                         |
|------------------|------------------------------------------------|
| `50` (a number)  | Write 50 Hz to characteristic_b and read back  |
| `s`              | Read the current frequency from the server     |
| `h`              | Print help                                     |
| `q`              | Quit cleanly                                   |

Invalid values are clamped by the STM32 to 1..104 Hz; the readback shows
the clamped value.

Press `Ctrl-C` (or type `q`) to stop.

## Protocol (for reference)

| Role              | UUID                                     | Props        | Payload |
|-------------------|------------------------------------------|--------------|---------|
| Service           | `1bc5d5a5-0200-36ac-e111-010000000000`   | -            | - |
| characteristic_a  | `1bc5d5a5-0200-36ac-e111-0100aa000000`   | notify, read | 6 B: `int16 x, int16 y, int16 z` (mg, LE) |
| characteristic_b  | `1bc5d5a5-0200-36ac-e111-0100bb000000`   | write, read  | 2 B: `uint16 freq_hz` (LE, clamped 1..104) |

## Troubleshooting

* Device not found: on the STM32 serial terminal you should see
  `[BLE] stack ready, advertising as STM32_ACC`. If you don't, the BlueNRG-MS
  isn't initializing — check SPI3 / the IRQ (PE6) line.
* Found but can't connect: another phone/app may be already connected. The
  STM32 only accepts one central. Disconnect it and retry.
* `bleak.exc.BleakDBusError: org.bluez.Error.InProgress`: another scan is
  running. `bluetoothctl` → `scan off`, then retry.
* Notifications received but values look garbage: make sure the firmware was
  built with the gatt_db.c from this repo (6-byte LE `int16 x,y,z` in mg).
