# UniGe Tactile Sensors C++

A lightweight C++ library for communicating with UniGe tactile sensors via serial interface.

It provides automatic device detection, real-time data acquisition, and calibration tools for sensor thresholding and cumulative analysis.

---

## Overview

The project includes three main modules:

1. **SensorSerial** — Real-time serial communication with the tactile sensor array.  
2. **ThresholdsCalibration** — Computes the individual activation thresholds for each sensor channel.  
3. **CumulativeCalibration** — Performs cumulative analysis on recorded sensor data to detect and quantify signal events.

Each module has its own test program under the `./test` directory.


---


## 1. SensorSerial

`SensorSerial` is a C++ class for asynchronous communication with multi-channel tactile sensors over a serial connection. It automatically detects available ports, configures the communication interface, and handles live sensor data streams using a background thread.


### How It Works (Class Overview)


1. **Initialization**  
   When constructed, the class tries to open the given serial port. If no port is specified, it automatically searches for USB serial devices and attempts a connection by scanning for a valid sensor header.

2. **Configuration**  
   Once connected, it configures the serial interface with appropriate baud rate, parity, and flow control settings. The command `"numx,all\r\n"` is sent to the device to configure the sensor channels.

3. **Data Acquisition**  
   A dedicated background thread continuously reads sensor frames from the serial stream, converts each raw 2-byte reading into voltage values, and updates a shared buffer.

4. **Thread Safety**  
   Access to sensor data and callbacks is protected with mutexes to avoid race conditions between threads.

### Public API

- **isSensorAlive()**  
  Checks if the serial port and background thread are running.

- **getLatestValues()**  
  Retrieves the most recent sensor values in a thread-safe way.

- **setDataCallback()**  
  Registers a user-defined callback that is invoked each time a new frame of sensor data is received.


### Features

- Automatic detection and connection to available serial devices (`/dev/ttyUSB*`, `/dev/ttyACM*`)
- Non-blocking serial communication using `select()`
- Thread-safe access to the latest sensor readings
- Asynchronous callback mechanism for continuous data acquisition
- Configurable serial port parameters (baud rate, number of sensors)
- Safe multithreaded architecture using C++11 features


### Example Usage

A complete usage example can be found in  
`./test/run_sensor.cpp`.

This program demonstrates:
- How to create a `SensorSerial` instance  
- How to register a callback function to handle incoming data  
- How to poll sensor values on demand and store them to a CSV file

Run it to test live communication with the tactile sensor and verify streaming data acquisition.

---

## 2. Thresholds Calibration

`ThresholdsCalibration` computes per-sensor activation thresholds through data collection and statistical analysis. It determines baseline noise levels and captures the expected range of “normal” signal activity.

### Algorithm

The class computes three key statistics per sensor:
- Median of all sampled values
- 20th and 80th percentiles
- Thresholds defined as:
    ```
    threshold_min = median - factor * (q80 - q20)
    threshold_max = median + factor * (q80 - q20)
    ```
A higher multiplication factor increases the allowed signal range.


### Workflow

1. Connect to the sensor through `SensorSerial`.  
2. Record sensor output for a fixed duration via the `SensorSerial` callback.  
3. Compute median, minimum, and maximum thresholds.  
4. Save all results to a timestamped CSV file for later use.

See the example for an implementation:  
`./test/calibrate_sensor_thresholds.cpp`

---

## 3. CumulativeCalibration

`CumulativeCalibration` performs an additional analysis step on previously recorded sensor data using the thresholds computed in the **ThresholdsCalibration** phase.  
Its purpose is to identify meaningful tactile events—moments when a sensor’s signal significantly exceeds its normal range—and to compute a **cumulative magnitude** value that measures the total intensity or strength of the sensor’s response during each event.

### Concept

Each time a sensor’s signal crosses its calibrated thresholds (either above or below), it is considered a potential tactile event.  
Around each such event, the algorithm selects a short temporal window of data and calculates how much the signal *varies* in total within that period, rather than just how high it peaks.  
This cumulative variation expresses how “active” or “energetic” the sensor’s response was during that event.

In simple terms:
- Thresholds define what constitutes a significant variation.
- The cumulative value measures how much the signal changed overall during that event.
- The result gives the sensor’s most intense cumulative response across the analyzed sequence.

### Workflow

1. Load threshold data from a CSV file (previous output from `ThresholdsCalibration`).  
2. Load a tab-separated `.lvm` file containing recorded sensor data.  
3. For each sensor channel:
   - Detect any moment where its readings cross the predefined thresholds.  
   - Around each detected point, extract a small time window (of length `event_length`).  
   - Subtract the sensor’s median value to normalize the signal.  
   - Compute the **sum of absolute differences** of consecutive samples within that segment.  
   - Store the highest cumulative value found for that sensor.  
4. Save one cumulative maximum per sensor into a timestamped CSV file.

Each value corresponds to the sensor's strongest cumulative activity observed across the dataset, representing how much the signal fluctuated during its biggest tactile event.

See the example for an implementation:  
`./test/compute_cumulative_calibration.cpp`


---

## 4. Online Event Detector

The **Online Event Detector** module continuously processes real-time tactile sensor data to identify meaningful touch events dynamically. It operates by comparing incoming sensor readings against pre-calibrated thresholds, accumulating signal variations to detect when a sensor has been activated or released.

### Functionality Overview

- **Real-time Processing**  
  Receives and buffers recent samples per sensor to capture temporal context.

- **Threshold-Based Event Initiation**  
  Triggers event capture when sensor readings cross minimum or maximum thresholds, including pre-event data for context.

- **Accumulative Signal Analysis**  
  Calculates the cumulative sum of signed differences relative to the median to determine the event’s intensity and polarity.

- **Event Polarity and State Tracking**  
  Tracks event states per sensor, allowing distinction between activation (positive) and release (negative) events, and manages transitions robustly.

- **Global Event Management**  
  Maintains counts of active events, manages state resets, and supports continuous detection across multiple sensors.

- **Output Information**  
  Provides event status and normalized heatmaps for downstream consumption.


### Key Design Points

- Pre-event buffering enhances detection accuracy by including immediate signal history.  
- Signed cumulative measure preserves event polarity information.  
- Thread-safe and modular design enables integration with asynchronous sensor communication.


---

## Test Programs Summary

| Test Program |  Purpose |
|---------------|----------|
| `./test/run_sensor.cpp` | Read and display live sensor data |
| `./test/calibrate_sensor_thresholds.cpp` | Compute statistical sensor thresholds |
| `./test/compute_cumulative_calibration.cpp` | Analyze data files for cumulative sensor activity |
| `./test/main.cpp` | Enables the sensor and starts the online event detection using the calibration files. |

---

### Dependencies

This library targets Linux systems and depends only on:
- Standard C++11 STL (`thread`, `mutex`, `vector`, `string`, etc.)
- POSIX headers (`fcntl.h`, `termios.h`, `unistd.h`, `sys/select.h`, `dirent.h`)

No external dependencies or frameworks are required.

---

## License

This project is released under the MIT License.  
You are free to use, modify, and distribute the code with proper attribution.
