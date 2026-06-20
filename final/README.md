# Final Project Report

## Motivation

- Commercial wearable devices can't detect swimming post, hence failing to provide more detailed monitoring service.
- STM32 provides a variety of sensors as well as nice computation capabilities, making data collection and local inference possible.

## Methods

### System Architecture

The project has two runtime components:

- `stm32/`: STM32 firmware for the B-L475E-IOT01 board.
- `server/server.py`: Python HTTP receiver with a Textual terminal dashboard.

The board connects to a configured Wi-Fi network, opens a TCP connection to the
host server, and sends HTTP GET requests:

- `/accel?x=...&y=...&z=...` once per second for raw acceleration.
- `/prediction?class=...&p1_milli=...` after each 10-sample inference window.


### Firmware

The firmware logic can be briefly summarized as follows:

1. Initializes HAL, system clock, UART logging, LED, Wi-Fi, and the LSM6DSL
   accelerometer.
2. Configures LSM6DSL significant-motion detection with interrupt latching.
3. Connects to the configured access point in `stm32/Src/main.c`.
4. Opens a TCP client connection to `RemoteIP:8002`.
5. Samples accelerometer data every second.
6. Packs 10 consecutive `(x, y, z)` samples into a 30-feature vector.
7. Runs the embedded classifier and sends the predicted class and class-1
   probability to the server.

The classifier interface is defined in `stm32/Src/classifier/classifier.h`.
The generated C implementation in `stm32/Src/classifier/classifier.c` contains:

- 30 input features.
- 2 output classes.
- A 100-tree random forest.
- Embedded standard-scaler mean and scale values.

The class labels used by the dashboard are:

- `0`: Freestyle
- `1`: Frog pose

### Host Server

The Python server uses `ThreadingHTTPServer` to receive board requests and keep
the connection alive. It records recent requests, tracks the latest
accelerometer sample, and tracks the latest pose prediction.

When run in an interactive terminal, it starts a Textual dashboard showing:

- Server status and request count.
- Latest pose prediction.
- Latest accelerometer values.
- Recent HTTP requests from the board.

If the terminal UI is disabled, the server prints received requests as JSON.

### Model selection

The Random Forest model is selected for the following reasons:

- Performance advantage: Comapred to larger models like NN, RF is more light-weight, and hence can run smoothly on STM32.
- Easy implementation: Simple training/inference logic, can be easily implemented with C++.
- High explaianability: Can gain insight by inspecting model parameters and hence enhance model

## Build and Run

### Server

From the project root:

```sh
cd server
python3 -m pip install -r requirements.txt
python3 server.py
```

### Firmware

Update the network settings in `stm32/Src/main.c` before flashing:

- `SSID`
- `PASSWORD`
- `RemoteIP`
- `RemotePORT`

Build the firmware:

```sh
cd stm32
make clean
make
```

Flash with `st-flash`:

```sh
make flash
```

The Makefile first looks for the STM32CubeIDE ARM toolchain and otherwise falls
back to `arm-none-eabi-` on `PATH`.

## Results

The device differentiates between freestyle and frog post swimming with very high accuracy(no incorrect prediction observed) and very little delay.

[Demo video](https://youtu.be/jbbeCX487q8)

## References
- ChatGPT
- [st-flash GitHub repo](https://github.com/stlink-org/stlink)
- EE3021 course slides
