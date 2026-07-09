#include "unige_tactile_sensors_cpp/sensor_serial_V2.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <iostream>
#include <dirent.h>
#include <algorithm>
#include <sstream>
#include <cerrno>

#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>

// --------------------------------------------------
// Constants
// --------------------------------------------------

static constexpr int HEADER_SIZE = 3;
static constexpr int TIMESTAMP_SIZE = 4;
static constexpr int PVDF_MAX_CHANNELS = 64;
static constexpr int IMU_NUM_CHANNELS = 6;

#ifndef BOTHER
#define BOTHER 0010000
#endif

#ifndef TCGETS2
#define TCGETS2 _IOR('T', 0x2A, struct termios2)
#endif

#ifndef TCSETS2
#define TCSETS2 _IOW('T', 0x2B, struct termios2)
#endif

// --------------------------------------------------
// termios2 (for arbitrary baudrate)
// --------------------------------------------------

struct termios2
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[19];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

static bool headersMatch(const uint8_t *a, const uint8_t *b)
{
    return std::equal(a, a + HEADER_SIZE, b);
}

static uint32_t readU32Le(const uint8_t *data)
{
    return uint32_t(data[0]) |
           (uint32_t(data[1]) << 8) |
           (uint32_t(data[2]) << 16) |
           (uint32_t(data[3]) << 24);
}

static uint16_t readU16Be(const uint8_t *data)
{
    return (uint16_t(data[0]) << 8) | uint16_t(data[1]);
}

static int16_t readI16Le(const uint8_t *data)
{
    return int16_t(uint16_t(data[0]) | (uint16_t(data[1]) << 8));
}

// --------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------

SensorSerial::SensorSerial(const std::string &_port_name,
                           uint32_t _baudrate,
                           const std::string &_start_cmd)
    : serial_fd(-1),
      baudrate(_baudrate),
      num_sensors(0),
      stop_thread(false),
      start_command(_start_cmd),
      read_frequency(0.0)
{
    if (!start_command.empty() && start_command.back() != '\n')
        start_command += '\n';

    if (!_port_name.empty())
    {
        if (!initSensor(_port_name))
        {
            std::cerr << "[ERROR] Failed to initialize sensor\n";
            return;
        }
    }
    else
    {
        std::cout << "[INFO] No serial port specified, auto-detecting...\n";
        if (!findAndConnectSerialPortAutomatic())
            return;
    }

    stop_thread = false;
    read_thread = std::thread(&SensorSerial::readLoop, this);

    std::cout << "[INFO] Sensor communication thread started\n";
}

SensorSerial::~SensorSerial()
{
    closeSensor();
}

// --------------------------------------------------
// Public API
// --------------------------------------------------

bool SensorSerial::isSensorAlive() const
{
    return !stop_thread && isSerialPortOpened();
}

void SensorSerial::getLatestValues(std::vector<double> &out_values_)
{
    std::lock_guard<std::mutex> lock(data_mutex);
    out_values_ = sensor_values;
}

void SensorSerial::getLatestImuValues(ImuValues &out_values_)
{
    std::lock_guard<std::mutex> lock(data_mutex);
    out_values_ = imu_values;
}

void SensorSerial::setDataCallback(DataCallback cb)
{
    std::lock_guard<std::mutex> lock(callback_mutex);
    data_callback = cb;
}

void SensorSerial::setImuDataCallback(ImuDataCallback cb)
{
    std::lock_guard<std::mutex> lock(imu_callback_mutex);
    imu_data_callback = cb;
}

// --------------------------------------------------
// Initialization
// --------------------------------------------------

bool SensorSerial::initSensor(const std::string &port)
{

    if (!openPort(port))
        return false;

    if (!configurePort())
        return false;

    if (!configureSensor())
        return false;

    return true;
}

bool SensorSerial::openPort(const std::string &port)
{

    serial_fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (serial_fd < 0)
    {
        std::cerr << "[ERROR] open(): " << strerror(errno) << "\n";
        return false;
    }

    int flags = fcntl(serial_fd, F_GETFL, 0);
    fcntl(serial_fd, F_SETFL, flags | O_NONBLOCK);

    std::cout << "[INFO] Opened serial port: " << port << "\n";
    return true;
}

bool SensorSerial::isSerialPortOpened() const
{
    return serial_fd >= 0;
}

// --------------------------------------------------
// Start command parsing
// --------------------------------------------------

bool SensorSerial::parseStartCommand()
{

    active_indices.clear();

    if (start_command == "start\n" || start_command == "start")
    {
        for (size_t i = 1; i <= PVDF_MAX_CHANNELS; ++i)
            active_indices.push_back(i);
    }
    else if (start_command.rfind("start,", 0) == 0)
    {

        std::string args = start_command.substr(6);
        args.erase(std::remove(args.begin(), args.end(), '\n'), args.end());

        std::stringstream ss(args);
        std::string token;

        while (std::getline(ss, token, ','))
        {

            if (token.find('-') != std::string::npos)
            {
                size_t a, b;
                if (sscanf(token.c_str(), "%zu-%zu", &a, &b) == 2)
                {
                    if (a < 1)
                        a = 1;
                    if (b > PVDF_MAX_CHANNELS)
                        b = PVDF_MAX_CHANNELS;
                    if (a > b)
                        continue;

                    for (size_t i = a; i <= b; ++i)
                        active_indices.push_back(i);
                }
            }
            else
            {
                size_t index = std::stoul(token);
                if (index >= 1 && index <= PVDF_MAX_CHANNELS)
                    active_indices.push_back(index);
            }
        }
    }

    std::sort(active_indices.begin(), active_indices.end());
    active_indices.erase(
        std::unique(active_indices.begin(), active_indices.end()),
        active_indices.end());

    num_sensors = active_indices.size();
    sensor_values.assign(num_sensors, 0.0);

    std::cout << "[INFO] Active sensors (" << num_sensors << "): ";
    for (auto i : active_indices)
        std::cout << i << " ";
    std::cout << "\n";

    return num_sensors > 0;
}

// --------------------------------------------------
// Sensor configuration
// --------------------------------------------------

bool SensorSerial::configureSensor()
{

    if (!parseStartCommand())
        return false;

    if (!sendCommand(start_command))
        return false;

    if (!readUntilHeader(10000))
    {
        std::cerr << "[ERROR] No header after start command\n";
        return false;
    }

    pending_packet_type = PacketType::Pvdf;
    has_pending_packet_header = true;

    return true;
}

// --------------------------------------------------
// Serial port configuration
// --------------------------------------------------

bool SensorSerial::configurePort()
{

    struct termios2 tio;
    if (ioctl(serial_fd, TCGETS2, &tio) < 0)
    {
        std::cerr << "[ERROR] TCGETS2 failed\n";
        return false;
    }

    tio.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    #ifdef CBAUD
    tio.c_cflag &= ~CBAUD;
    #endif
    tio.c_cflag |= CS8 | BOTHER;

    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                     INLCR | IGNCR | ICRNL | IXON);

    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    tio.c_ispeed = baudrate;
    tio.c_ospeed = baudrate;

    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (ioctl(serial_fd, TCSETS2, &tio) < 0)
    {
        std::cerr << "[ERROR] TCSETS2 failed\n";
        return false;
    }

    std::cout << "[INFO] Serial configured @ " << baudrate << " baud\n";
    return true;
}

// --------------------------------------------------
// Serial helpers
// --------------------------------------------------

bool SensorSerial::sendCommand(const std::string &cmd)
{

    ssize_t res = write(serial_fd, cmd.c_str(), cmd.size());
    if (res < 0)
    {
        std::cerr << "[ERROR] write(): " << strerror(errno) << "\n";
        return false;
    }

    tcdrain(serial_fd);
    return true;
}

int SensorSerial::waitForData(int timeout_ms)
{

    FD_ZERO(&readfds);
    FD_SET(serial_fd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    return select(serial_fd + 1, &readfds, nullptr, nullptr, &tv);
}

// --------------------------------------------------
// Header sync
// --------------------------------------------------

bool SensorSerial::readUntilHeader(uint timeout_ms)
{
    PacketType packet_type;
    const PacketType expected_type = PacketType::Pvdf;
    return readUntilPacketHeader(packet_type, &expected_type, timeout_ms);
}

bool SensorSerial::readUntilPacketHeader(PacketType &packet_type,
                                         const PacketType *expected_type,
                                         uint timeout_ms)
{
    uint8_t byte;
    uint8_t window[HEADER_SIZE] = {0, 0, 0};
    size_t filled = 0;

    auto start = std::chrono::steady_clock::now();

    while (!stop_thread)
    {

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count() > timeout_ms)
            return false;

        int ready = waitForData(10);
        if (ready <= 0)
            continue;

        ssize_t n = ::read(serial_fd, &byte, 1);
        if (n == 1)
        {
            if (filled < HEADER_SIZE)
            {
                window[filled++] = byte;
            }
            else
            {
                window[0] = window[1];
                window[1] = window[2];
                window[2] = byte;
            }

            if (filled < HEADER_SIZE)
                continue;

            bool matched = false;
            PacketType found_type = PacketType::Pvdf;
            if (headersMatch(window, pvdf_header))
            {
                matched = true;
                found_type = PacketType::Pvdf;
            }
            else if (headersMatch(window, imu_header))
            {
                matched = true;
                found_type = PacketType::Imu;
            }

            if (!matched)
                continue;

            if (expected_type != nullptr && found_type != *expected_type)
                continue;

            packet_type = found_type;
            return true;
        }
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            return false;
        }
    }

    return false;
}

bool SensorSerial::readExact(uint8_t *dst, size_t size, uint timeout_ms)
{
    size_t read_pos = 0;
    auto start = std::chrono::steady_clock::now();

    while (read_pos < size && !stop_thread)
    {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count() > timeout_ms)
            return false;

        int ready = waitForData(10);
        if (ready <= 0)
            continue;

        ssize_t n = ::read(serial_fd, dst + read_pos, size - read_pos);
        if (n > 0)
        {
            read_pos += n;
        }
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            return false;
        }
    }

    return read_pos == size;
}

// --------------------------------------------------
// Background read loop
// --------------------------------------------------

void SensorSerial::readLoop()
{

    auto last_pvdf_time = std::chrono::steady_clock::now();

    while (!stop_thread)
    {
        PacketType packet_type = PacketType::Pvdf;

        if (has_pending_packet_header)
        {
            packet_type = pending_packet_type;
            has_pending_packet_header = false;
        }
        else if (!readUntilPacketHeader(packet_type))
        {
            continue;
        }

        if (packet_type == PacketType::Pvdf)
        {
            uint8_t packet_channels = 0;
            if (!readExact(&packet_channels, sizeof(packet_channels)))
                continue;

            if (packet_channels == 0 || packet_channels > PVDF_MAX_CHANNELS)
            {
                std::cerr << "[WARN] Invalid PVDF channel count: "
                          << int(packet_channels) << "\n";
                continue;
            }

            std::vector<uint8_t> buffer(TIMESTAMP_SIZE + 2 * packet_channels);
            if (!readExact(buffer.data(), buffer.size()))
                continue;

            if (packet_channels != num_sensors)
            {
                std::cerr << "[WARN] PVDF channel mismatch. Expected "
                          << num_sensors << ", received "
                          << int(packet_channels) << "\n";
                continue;
            }

            uint32_t timestamp = readU32Le(buffer.data());

            std::vector<double> values(packet_channels);
            for (size_t i = 0; i < packet_channels; ++i)
            {
                const uint8_t *raw_ptr = buffer.data() + TIMESTAMP_SIZE + 2 * i;
                uint16_t raw = readU16Be(raw_ptr);
                values[i] = (raw * 4.09) / 65535.0;
            }

            {
                std::lock_guard<std::mutex> lock(data_mutex);
                sensor_values = values;
                last_timestamp = timestamp;
            }

            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - last_pvdf_time).count();
            last_pvdf_time = now;

            {
                std::lock_guard<std::mutex> lock(freq_mutex);
                read_frequency = (dt > 0) ? 1.0 / dt : 0.0;
            }

            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                if (data_callback)
                    data_callback(timestamp, values);
            }
        }
        else
        {
            std::vector<uint8_t> buffer(TIMESTAMP_SIZE + 2 * IMU_NUM_CHANNELS);
            if (!readExact(buffer.data(), buffer.size()))
                continue;

            uint32_t timestamp = readU32Le(buffer.data());
            ImuValues values;
            for (size_t i = 0; i < values.size(); ++i)
            {
                const uint8_t *raw_ptr = buffer.data() + TIMESTAMP_SIZE + 2 * i;
                values[i] = readI16Le(raw_ptr);
            }

            {
                std::lock_guard<std::mutex> lock(data_mutex);
                imu_values = values;
                last_imu_timestamp = timestamp;
            }

            {
                std::lock_guard<std::mutex> lock(imu_callback_mutex);
                if (imu_data_callback)
                    imu_data_callback(timestamp, values);
            }
        }
    }
}

// --------------------------------------------------
// Cleanup
// --------------------------------------------------

void SensorSerial::closeSensor()
{

    stop_thread = true;

    if (read_thread.joinable())
        read_thread.join();

    if (serial_fd >= 0)
    {
        ::close(serial_fd);
        serial_fd = -1;
    }
}

// --------------------------------------------------
// Serial port auto-detection
// --------------------------------------------------

std::vector<std::string> SensorSerial::getAvailableSerialPortsList() const
{

    std::vector<std::string> ports;
    DIR *dir = opendir("/dev");
    if (!dir)
        return ports;

    dirent *ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        std::string name = ent->d_name;
        if (name.find("ttyUSB") != std::string::npos ||
            name.find("ttyACM") != std::string::npos)
            ports.push_back("/dev/" + name);
    }

    closedir(dir);
    return ports;
}

bool SensorSerial::findAndConnectSerialPortAutomatic()
{

    auto ports = getAvailableSerialPortsList();
    for (const auto &p : ports)
    {
        if (initSensor(p))
            return true;
        closeSensor();
    }
    return false;
}
