#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/select.h>

class SensorSerial
{

public:
    using ImuValues = std::array<int16_t, 6>;
    using ImuDataCallback = std::function<
        void(uint32_t timestamp_ms, const ImuValues &)>;

    explicit SensorSerial(const std::string &_port_name = "",
                          uint32_t _baudrate = 115200);

    ~SensorSerial();

    bool isSensorAlive() const;

    void getLatestImuValues(ImuValues &out_values_);
    void setImuDataCallback(ImuDataCallback cb);

    inline double getReadFrequency() const
    {
        std::lock_guard<std::mutex> lock(this->freq_mutex);
        return this->read_frequency;
    }

private:
    const uint8_t imu_header[3] = {0xaa, 0xbb, 0x00};

    int serial_fd;
    fd_set readfds;
    uint32_t baudrate;

    std::mutex data_mutex;
    ImuValues imu_values = {0, 0, 0, 0, 0, 0};
    uint32_t last_imu_timestamp = 0;

    std::mutex imu_callback_mutex;
    ImuDataCallback imu_data_callback;

    std::thread read_thread;
    std::atomic<bool> stop_thread;

    bool openPort(const std::string &_port);
    bool isSerialPortOpened() const;
    bool configurePort();
    int waitForData(const int _timeout_ms);

    bool initSensor(const std::string &_port_name = "");
    bool configureSensor();
    void closeSensor();
    std::vector<std::string> getAvailableSerialPortsList() const;
    bool findAndConnectSerialPortAutomatic();

    bool readUntilImuHeader(const uint _timeout_milliseconds = 3000);
    bool readExact(uint8_t *dst,
                   size_t size,
                   const uint _timeout_milliseconds = 1000);
    void readLoop();

    bool has_pending_imu_header = false;

    double read_frequency = 0.0;
    mutable std::mutex freq_mutex;
};
