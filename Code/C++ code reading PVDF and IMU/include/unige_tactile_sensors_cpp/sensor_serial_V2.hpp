#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>
#include <cstdint>
#include <array>
#include <sys/select.h>

class SensorSerial
{

public:
    using ImuValues = std::array<int16_t, 6>;

    using DataCallback = std::function<
        void(uint32_t timestamp, const std::vector<double> &)>;
    using ImuDataCallback = std::function<
        void(uint32_t timestamp, const ImuValues &)>;

    explicit SensorSerial(const std::string &_port_name = "",
                          uint32_t _baudrate = 3000000,
                          const std::string &_start_cmd = "start\n");

    ~SensorSerial();

    bool isSensorAlive() const;

    void getLatestValues(std::vector<double> &out_values_);
    void getLatestImuValues(ImuValues &out_values_);

    void setDataCallback(DataCallback cb);
    void setImuDataCallback(ImuDataCallback cb);

    inline double getReadFrequency() const
    {
        std::lock_guard<std::mutex> lock(this->freq_mutex);
        return this->read_frequency;
    }

    size_t getNumSensors() const { return num_sensors; }

    const std::vector<size_t> &getActiveSensorIndices() const
    {
        return active_indices;
    }

private:
    enum class PacketType
    {
        Pvdf,
        Imu
    };

    const uint8_t pvdf_header[3] = {0x3c, 0x3e, 0x00};
    const uint8_t imu_header[3] = {0xaa, 0xbb, 0x00};

    int serial_fd;
    fd_set readfds;
    uint32_t baudrate;
    size_t num_sensors;

    std::mutex data_mutex;
    std::vector<double> sensor_values;
    uint32_t last_timestamp = 0;
    ImuValues imu_values = {0, 0, 0, 0, 0, 0};
    uint32_t last_imu_timestamp = 0;

    std::mutex callback_mutex;
    DataCallback data_callback;
    std::mutex imu_callback_mutex;
    ImuDataCallback imu_data_callback;

    std::thread read_thread;
    std::atomic<bool> stop_thread;

    std::string start_command;
    std::vector<size_t> active_indices;

    bool openPort(const std::string &_port);
    bool isSerialPortOpened() const;
    bool configurePort();
    bool sendCommand(const std::string &_cmd);
    int waitForData(const int _timeout_ms);

    bool initSensor(const std::string &_port_name = "");
    bool configureSensor();
    void closeSensor();
    std::vector<std::string> getAvailableSerialPortsList() const;
    bool findAndConnectSerialPortAutomatic();

    bool readUntilHeader(const uint _timeout_milliseconds = 3000);
    bool readUntilPacketHeader(PacketType &packet_type,
                               const PacketType *expected_type = nullptr,
                               const uint _timeout_milliseconds = 3000);
    bool readExact(uint8_t *dst,
                   size_t size,
                   const uint _timeout_milliseconds = 1000);
    void readLoop();
    bool parseStartCommand();

    bool has_pending_packet_header = false;
    PacketType pending_packet_type = PacketType::Pvdf;

    double read_frequency = 0.0;
    mutable std::mutex freq_mutex;
};
