#include "unige_imu_cpp/sensor_serial_V2.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <iostream>
#include <dirent.h>
#include <algorithm>
#include <cerrno>

#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif

// --------------------------------------------------
// Constants
// --------------------------------------------------

static constexpr int HEADER_SIZE = 3;
static constexpr int TIMESTAMP_SIZE = 4;
static constexpr int IMU_NUM_CHANNELS = 6;

#ifdef __linux__

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

#endif

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

static int16_t readI16Le(const uint8_t *data)
{
    return int16_t(uint16_t(data[0]) | (uint16_t(data[1]) << 8));
}

// --------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------

SensorSerial::SensorSerial(const std::string &_port_name,
                           uint32_t _baudrate)
    : serial_fd(-1),
      baudrate(_baudrate),
      stop_thread(false),
      read_frequency(0.0)
{
    if (!_port_name.empty())
    {
        if (!initSensor(_port_name))
        {
            std::cerr << "[ERROR] Failed to initialize IMU serial reader\n";
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

    std::cout << "[INFO] IMU communication thread started\n";
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

void SensorSerial::getLatestImuValues(ImuValues &out_values_)
{
    std::lock_guard<std::mutex> lock(data_mutex);
    out_values_ = imu_values;
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

    stop_thread = false;

    if (!openPort(port))
        return false;

    if (!configurePort())
    {
        closeSensor();
        return false;
    }

    if (!configureSensor())
    {
        closeSensor();
        return false;
    }

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
// Sensor configuration
// --------------------------------------------------

bool SensorSerial::configureSensor()
{

    if (!readUntilImuHeader(10000))
    {
        std::cerr << "[ERROR] No IMU header received\n";
        return false;
    }

    has_pending_imu_header = true;
    return true;
}

// --------------------------------------------------
// Serial port configuration
// --------------------------------------------------

bool SensorSerial::configurePort()
{

#ifdef __linux__
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
#else
    struct termios tio;
    if (tcgetattr(serial_fd, &tio) < 0)
    {
        std::cerr << "[ERROR] tcgetattr failed: " << strerror(errno) << "\n";
        return false;
    }

    cfmakeraw(&tio);
    tio.c_cflag &= ~(PARENB | CSTOPB);
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;
#endif
    tio.c_cflag |= CS8 | CLOCAL | CREAD;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

#ifdef __APPLE__
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);

    if (tcsetattr(serial_fd, TCSANOW, &tio) < 0)
    {
        std::cerr << "[ERROR] tcsetattr failed: " << strerror(errno) << "\n";
        return false;
    }

    speed_t custom_speed = baudrate;
    if (ioctl(serial_fd, IOSSIOSPEED, &custom_speed) < 0)
    {
        const char *tty_name = ttyname(serial_fd);
        std::string device_name = tty_name ? tty_name : "";
        if (device_name.rfind("/dev/ttys", 0) == 0)
        {
            std::cerr << "[WARN] IOSSIOSPEED not supported by PTY test device; "
                      << "continuing with termios speed\n";
        }
        else
        {
            std::cerr << "[ERROR] IOSSIOSPEED failed: " << strerror(errno) << "\n";
            return false;
        }
    }
#else
    if (cfsetspeed(&tio, baudrate) < 0)
    {
        std::cerr << "[ERROR] cfsetspeed failed for baudrate "
                  << baudrate << ": " << strerror(errno) << "\n";
        return false;
    }

    if (tcsetattr(serial_fd, TCSANOW, &tio) < 0)
    {
        std::cerr << "[ERROR] tcsetattr failed: " << strerror(errno) << "\n";
        return false;
    }
#endif

    std::cout << "[INFO] Serial configured @ " << baudrate << " baud\n";
    return true;
#endif
}

// --------------------------------------------------
// Serial helpers
// --------------------------------------------------

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

bool SensorSerial::readUntilImuHeader(uint timeout_ms)
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

            if (filled >= HEADER_SIZE && headersMatch(window, imu_header))
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

    auto last_imu_time = std::chrono::steady_clock::now();

    while (!stop_thread)
    {
        if (has_pending_imu_header)
        {
            has_pending_imu_header = false;
        }
        else if (!readUntilImuHeader())
        {
            continue;
        }

        std::vector<uint8_t> buffer(TIMESTAMP_SIZE + 2 * IMU_NUM_CHANNELS);
        if (!readExact(buffer.data(), buffer.size()))
            continue;

        uint32_t timestamp_ms = readU32Le(buffer.data());
        ImuValues values;
        for (size_t i = 0; i < values.size(); ++i)
        {
            const uint8_t *raw_ptr = buffer.data() + TIMESTAMP_SIZE + 2 * i;
            values[i] = readI16Le(raw_ptr);
        }

        {
            std::lock_guard<std::mutex> lock(data_mutex);
            imu_values = values;
            last_imu_timestamp = timestamp_ms;
        }

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_imu_time).count();
        last_imu_time = now;

        {
            std::lock_guard<std::mutex> lock(freq_mutex);
            read_frequency = (dt > 0) ? 1.0 / dt : 0.0;
        }

        {
            std::lock_guard<std::mutex> lock(imu_callback_mutex);
            if (imu_data_callback)
                imu_data_callback(timestamp_ms, values);
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
            name.find("ttyACM") != std::string::npos ||
            name.find("tty.usb") != std::string::npos ||
            name.find("cu.usb") != std::string::npos)
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
