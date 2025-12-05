#include "unige_tactile_sensors_cpp/sensor_serial.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <iostream>
#include <dirent.h>
#include <sys/select.h>
#include <algorithm>



SensorSerial::SensorSerial(const std::string& _port_name, const uint _baudrate, const size_t _num_sensors)
    : baudrate(_baudrate), num_sensors(_num_sensors), serial_fd(-1), stop_thread(false), sensor_values(_num_sensors, 0.0) {

    if (!_port_name.empty()) {
        if (!SensorSerial::initSensor(_port_name)) {
            std::cerr << "[ERROR] Failed to initialize the sensor." << std::endl;
            return;
        }
    } else {
        // No port specified: Start automatic detection and connection of the sensor serial port
        std::cout << "[INFO] No serial port specified; starting automatic detection and connection..." << std::endl;
        if (!SensorSerial::findAndConnectSerialPortAutomatic()) {
            return;
        }
    }

    // Start the read thread to receive sensor data asynchronously
    this->stop_thread = false;
    this->read_thread = std::thread(&SensorSerial::readLoop, this);
    std::cout << "[INFO] Sensor communicaction thread initialized." << std::endl;

}


SensorSerial::~SensorSerial() {
    SensorSerial::closeSensor();
}

//  ////////////////////////////////////////////////
//  PUBLIC METHODS
//  ////////////////////////////////////////////////

// Used to know if the sensor is alive, that is: Initialized and the communication opened:
bool SensorSerial::isSensorAlive() const {

    return !this->stop_thread && SensorSerial::isSerialPortOpened();

}


// Retrieves the latest sensor values in a thread-safe manner
void SensorSerial::getLatestValues(std::vector<double>& out_values_) {
    std::lock_guard<std::mutex> lock(this->data_mutex);
    out_values_ = this->sensor_values;
}


// Sets the callback function to be called when new data is received
void SensorSerial::setDataCallback(DataCallback cb) {
    std::lock_guard<std::mutex> lock(this->callback_mutex);
    this->data_callback = cb;
}


//  ////////////////////////////////////////////////
//  PRIVATE METHODS
//  ////////////////////////////////////////////////

bool SensorSerial::initSensor(const std::string& _port_name) {

    // Attempt to open the user-provided serial port:
    if (!SensorSerial::openPort(_port_name)) {
        std::cerr << "[WARN] Failed to open port: " << _port_name << std::endl;
        return false;
    }

    // Configure port settings (baudrate, parity, etc.)
    if (!SensorSerial::configurePort()) {
        std::cerr << "[WARN] Failed to configure port: " << _port_name << std::endl;
        SensorSerial::closeSensor();
        return false;
    }

    // Configure sensor settings
    if (!SensorSerial::configureSensor()) {
        std::cerr << "[WARN] Failed to configure sensor." << std::endl;
        SensorSerial::closeSensor();
        return false;
    }
    
    return true;    // Initialization succeeded
}


// Closes the serial port and stops the background thread
void SensorSerial::closeSensor() {
    // Signal the thread to stop
    this->stop_thread = true;

    // Wait for the thread to finish
    if (this->read_thread.joinable()) {
        this->read_thread.join();
    }

    // Close the serial port if open
    if (SensorSerial::isSerialPortOpened()) {
        ::close(this->serial_fd);
        this->serial_fd = -1;
        std::cout << "[INFO] Serial port closed." << std::endl;
    }
}


// Checks if the serial port is currently open
bool SensorSerial::isSerialPortOpened() const {
    return this->serial_fd != -1;
}


// Opens a serial port with specified name and sets it up
bool SensorSerial::openPort(const std::string& _port) {
    this->serial_fd = ::open(_port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (this->serial_fd < 0) {
        std::cerr << "[ERROR] Opening port failed: " << strerror(errno) << std::endl;
        return false;
    }
    int flags = fcntl(this->serial_fd, F_GETFL, 0);
    fcntl(this->serial_fd, F_SETFL, flags | O_NONBLOCK);

    std::cout << "[INFO] Opened serial port: " << _port << std::endl;
    return true;
}


// Configures the serial port parameters (baud rate, data bits, parity, stop bits)
bool SensorSerial::configurePort() {
    if (!SensorSerial::isSerialPortOpened()) {
        return false;
    }

    struct termios tty{};
    if (tcgetattr(this->serial_fd, &tty) != 0) {
        std::cerr << "[ERROR] tcgetattr failed: " << strerror(errno) << std::endl;
        return false;
    }

    speed_t speed;
    switch (this->baudrate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 921600: speed = B921600; break;
        case 115200: speed = B115200; break;
        case 1000000: speed = B1000000; break;
        default:
            std::cerr << "[WARN] Unsupported baudrate, defaulting to 1.000.000" << std::endl;
            speed = B1000000;
            break;
    }

    cfsetospeed(&tty, speed);    // Set output speed
    cfsetispeed(&tty, speed);    // Set input speed

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8 bits of data
    tty.c_cflag &= ~PARENB;                     // No parity
    tty.c_cflag &= ~CSTOPB;                     // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                    // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;              // Enable receiver, ignore modem control lines

    // Raw mode:
    cfmakeraw(&tty);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);     // Disable software flow control
    // tty.c_lflag = 0;                            // Raw input mode
    // tty.c_oflag = 0;                            // Raw output mode

    // tty.c_cc[VMIN] = 0;                       // Non-blocking mode
    // tty.c_cc[VMIN] = 3 + 2 * this->num_sensors;     // Blocking mode, block until all data received
    tty.c_cc[VMIN] = 2 * this->num_sensors;     // Blocking mode, block until all data received
    tty.c_cc[VTIME] = 0;                            // Read without timeout, so returns when we recieve all package
    // tty.c_cc[VTIME] = 1;                        // Read timeout in deciseconds (1 = 0.1 second)

    // Apply configuration:
    if (tcsetattr(this->serial_fd, TCSANOW, &tty) != 0) {
        std::cerr << "[ERROR] Failed to set attributes: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}


bool SensorSerial::sendCommand(const std::string& _cmd) {
    if (!SensorSerial::isSerialPortOpened()) {
        return false;
    }
    ssize_t res = write(this->serial_fd, _cmd.c_str(), _cmd.size());
    if (res < 0) {
        std::cerr << "[ERROR] Write failed: " << strerror(errno) << std::endl;
        return false;
    }
    tcdrain(this->serial_fd);   // Wait until all data is transmited.
    return true;
}


bool SensorSerial::configureSensor() {
    if (!SensorSerial::sendCommand("numx,all\n")) {
        return false;
    }
    if (!SensorSerial::readUntilHeader(10000)) {
        std::cerr << "[ERROR] No header after sensor configuration command." << std::endl;
        return false;
    }
    std::cout << "[INFO] Sensor configured: command 'numx,all' sent and header received. Ready to acquire data." << std::endl;
    return true;
}


// Efficient method to wait the serial port without active checking:
int SensorSerial::waitForData(const int _timeout_ms) {
    // fd_set readfds;
    FD_ZERO(&this->readfds);
    FD_SET(this->serial_fd, &this->readfds);
    
    struct timeval timeout;
    timeout.tv_sec = _timeout_ms / 1000;
    timeout.tv_usec = (_timeout_ms % 1000) * 1000;
    return select(this->serial_fd + 1, &this->readfds, nullptr, nullptr, &timeout);
}


// // Wait for the fixed sensor header byte sequence to validate data stream
bool SensorSerial::readUntilHeader(const uint _timeout_milliseconds) {
    uint8_t byte = 0;
    int header_idx = 0;
    int ready = 0;
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point now = start_time;

    while (!this->stop_thread) {

        now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > _timeout_milliseconds) {
            std::cout << "[WARN] Timeout exceeded without detecting header!" << std::endl;
            return false;
        }

        ready = SensorSerial::waitForData(10);
        if (ready > 0 && FD_ISSET(this->serial_fd, &this->readfds)) {
            ssize_t n = ::read(this->serial_fd, &byte, 1);
            if (n == 1) {
                // std::cout << "[DEBUG] Read byte for header: 0x" << std::hex << (int)byte << std::dec << std::endl;
                if (byte == this->header[header_idx]) {
                    header_idx++;
                    if (header_idx == 3) {
                        // std::cout << "[INFO] Header detected." << std::endl;
                        return true;
                    }
                } else {
                    if(header_idx > 0) //std::cout << "[DEBUG] Header mismatch, resetting index." << std::endl;
                    header_idx = 0; // Reset if byte does not match
                }
            } else if (n == 0) {
                // No data, loop again
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::cout << "[DEBUG] No data while reading header, waiting..." << std::endl;
            } else { // n < 0, error real
                std::cerr << "[ERROR] Reading header failed: " << strerror(errno) << std::endl;
                return false;
            }
        } else if (ready == 0) {
            //std::cout << "[WARN] Timeout, no data to read when checking for header..." << std::endl;
        } else {
            std::cerr << "[ERROR] select() error: " << strerror(errno) << std::endl;
            return false;
        }
    }
    return false;
}


// Returns a list of available USB serial ports (ttyUSB* and ttyACM* devices)
std::vector<std::string> SensorSerial::getAvailableSerialPortsList() const {
    std::vector<std::string> ports;
    const std::string dev_path = "/dev/";
    DIR* dir = opendir(dev_path.c_str());
    if (!dir) {
        return ports;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // Detect typical USB serial devices
        if (name.find("ttyUSB") != std::string::npos || name.find("ttyACM") != std::string::npos) {
            ports.push_back(dev_path + name);
        }
    }
    closedir(dir);
    return ports;
}


// Automatically find and connect the serial port of the sensor (detect via header)
bool SensorSerial::findAndConnectSerialPortAutomatic() {
    std::vector<std::string> ports = SensorSerial::getAvailableSerialPortsList();
    if (ports.empty()) {
        std::cout << "[WARN] No USB serial devices found." << std::endl;
        return false;
    }

    for (const std::string& port : ports) {
        std::cout << "[INFO] Testing port: " << port << std::endl;

        if (SensorSerial::initSensor(port)) {
            // Sensor detected and connected automatically.
            return true;
        }

        std::cerr << "[INFO] No sensor header on port: " << port << ", closing." << std::endl;
        SensorSerial::closeSensor();
    }

    return false;  // No suitable port found
}


// Main read loop running in dedicated thread to continuously read sensor data
void SensorSerial::readLoop() {

    const int header_size   = 3;
    const int timestamp_size = 4;
    const int sequence_size  = 2;
    const int sensor_bytes   = 2 * this->num_sensors;

    const int frame_size = header_size + timestamp_size + sequence_size + sensor_bytes;

    std::vector<uint8_t> buffer(frame_size);
    int ready = 0;
    size_t read_pos = 0;

    while (!this->stop_thread) {

        if (!SensorSerial::isSerialPortOpened()) {
            SensorSerial::closeSensor();
            break;
        }

        //------------------------------------------------------
        // 1. Wait for header synchronisation
        //------------------------------------------------------
        if (!this->readUntilHeader()) {
            std::cerr << "[WARN] Lost header sync, retrying..." << std::endl;
            continue;
        }

        //------------------------------------------------------
        // 2. Read the remainder of the frame
        //------------------------------------------------------
        read_pos = 0;
        while (read_pos < frame_size - header_size && !this->stop_thread) {

            ready = this->waitForData(10);

            if (ready > 0) {

                ssize_t n = ::read(
                    this->serial_fd,
                    buffer.data() + read_pos,
                    (frame_size - header_size) - read_pos
                );

                if (n > 0) {
                    read_pos += n;
                }
                else if (n < 0) {
                    std::cerr << "[ERROR] Serial read failed: "
                              << strerror(errno) << std::endl;
                    this->stop_thread = true;
                    break;
                }

            } else if (ready == 0) {
                // timeout
                continue;
            } else {
                std::cerr << "[ERROR] select() error: "
                          << strerror(errno) << std::endl;
                this->stop_thread = true;
                break;
            }
        }

        if (this->stop_thread) break;

        //------------------------------------------------------
        // 3. We now have full frame payload after header
        //------------------------------------------------------
        if (read_pos != (frame_size - header_size)) {
            std::cerr << "[WARN] Incomplete packet received: "
                      << read_pos << " bytes" << std::endl;
            continue;
        }

        //------------------------------------------------------
        // Parse fields
        //------------------------------------------------------

        // Offsets:
        // header   : already consumed
        // buffer[0..3]   = timestamp
        // buffer[4..5]   = sequence
        // buffer[6...]   = sensor values

        uint32_t timestamp =
              (uint32_t(buffer[0])      )
            | (uint32_t(buffer[1]) << 8 )
            | (uint32_t(buffer[2]) << 16)
            | (uint32_t(buffer[3]) << 24);

        uint16_t sequence =
              (uint16_t(buffer[4])      )
            | (uint16_t(buffer[5]) << 8 );

        // Parse sensors (2 bytes each)
        std::vector<double> values(this->num_sensors);

        for (size_t i = 0; i < this->num_sensors; ++i) {
            uint16_t raw = (uint16_t(buffer[6 + 2*i]) << 8) |
                           (uint16_t(buffer[7 + 2*i]));

            values[i] = (raw * 4.09f) / 65535.0f;
        }

        //------------------------------------------------------
        // Store last data thread-safe
        //------------------------------------------------------
        {
            std::lock_guard<std::mutex> lock(this->data_mutex);
            this->sensor_values = values;
            this->last_timestamp = timestamp;
            this->last_sequence = sequence;
        }

        //------------------------------------------------------
        // Call callback
        //------------------------------------------------------
        {
            std::lock_guard<std::mutex> lock(this->callback_mutex);
            if (this->data_callback) {
                this->data_callback(timestamp, sequence, values);
            }
        }
    }
}



