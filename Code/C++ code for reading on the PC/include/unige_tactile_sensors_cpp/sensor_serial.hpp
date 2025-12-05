#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>
#define NUM_TACTILE_SENSORS 8

class SensorSerial {

public:

    using DataCallback = std::function<
        void(uint32_t timestamp, uint16_t sequence, const std::vector<double>&)
    >;

    explicit SensorSerial(const std::string& _port_name = "",
                          const uint _baudrate = 1000000,
                          const size_t _num_sensors = NUM_TACTILE_SENSORS);

    ~SensorSerial();

    bool isSensorAlive() const;

    void getLatestValues(std::vector<double>& out_values_);

    void setDataCallback(DataCallback cb);

    inline double getReadFrequency() const {
        std::lock_guard<std::mutex> lock(this->freq_mutex);
        return this->read_frequency;
    }

private:

    const uint8_t header[3] = {0x3c, 0x3e, 0x00};

    int serial_fd;
    fd_set readfds;
    uint baudrate;
    size_t num_sensors;

    std::mutex data_mutex;

    std::vector<double> sensor_values;
    uint32_t last_timestamp = 0;   // NEW
    uint16_t last_sequence  = 0;   // NEW

    std::mutex callback_mutex;
    DataCallback data_callback;

    std::thread read_thread;
    std::atomic<bool> stop_thread;

    bool openPort(const std::string& _port);
    bool isSerialPortOpened() const;
    bool configurePort();
    bool sendCommand(const std::string& _cmd);
    int waitForData(const int _timeout_ms);

    bool initSensor(const std::string& _port_name = "");
    bool configureSensor();
    void closeSensor();
    std::vector<std::string> getAvailableSerialPortsList() const;
    bool findAndConnectSerialPortAutomatic();

    bool readUntilHeader(const uint _timeout_milliseconds = 3000);
    void readLoop();

    double read_frequency;
    mutable std::mutex freq_mutex;
};

