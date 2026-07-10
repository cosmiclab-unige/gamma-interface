#include "unige_imu_cpp/sensor_serial_V2.hpp"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <mutex>
#include <memory>
#include <ctime>
#include <cstdlib>

// --------------------------------------------------
// Globals for CSV handling
// --------------------------------------------------

std::stringstream imu_stream;
std::mutex csv_mutex;

// --------------------------------------------------
// Non-blocking key input
// --------------------------------------------------

char getKeyNonBlocking()
{
    struct termios oldt, newt;
    char ch = 0;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    return ch;
}

// --------------------------------------------------
// CSV helpers
// --------------------------------------------------

void writeImuHeader(std::stringstream &stream)
{
    stream << "IMUTimestamp_ms(uint32)"
           << ",AccelX_raw,AccelY_raw,AccelZ_raw"
           << ",GyroX_raw,GyroY_raw,GyroZ_raw\n";
}

std::string makeTimestampedFilename()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time;
    localtime_r(&now_time, &local_time);

    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &local_time);

    return std::string("imu_data_") + timestamp + ".csv";
}

// --------------------------------------------------
// Callback
// --------------------------------------------------

void imuCallbackFunction(uint32_t board_timestamp_ms,
                         const SensorSerial::ImuValues &data)
{
    std::lock_guard<std::mutex> lock(csv_mutex);

    imu_stream << board_timestamp_ms;
    for (int16_t v : data)
        imu_stream << "," << v;
    imu_stream << "\n";
}

// --------------------------------------------------
// Main
// --------------------------------------------------

int main(int argc, char **argv)
{

    std::string port_name;
    uint32_t baudrate = 115200;

    if (argc > 1)
        port_name = argv[1];

    if (argc > 2)
        baudrate = static_cast<uint32_t>(std::stoul(argv[2]));

    {
        std::lock_guard<std::mutex> lock(csv_mutex);
        writeImuHeader(imu_stream);
    }

    auto sensor = std::make_unique<SensorSerial>(
        port_name, // empty string enables auto-detect
        baudrate);

    if (!sensor->isSensorAlive())
    {
        std::cerr << "[ERROR] IMU serial reader not alive\n";
        return -1;
    }

    sensor->setImuDataCallback(imuCallbackFunction);

    std::cout << "Press [space] to read latest IMU values, "
              << "[esc] to exit and save CSV.\n";

    bool running = true;
    while (running)
    {

        char key = getKeyNonBlocking();

        if (key == ' ')
        {
            SensorSerial::ImuValues imu_snapshot;
            sensor->getLatestImuValues(imu_snapshot);

            std::cout << "[IMU raw ax ay az gx gy gz]: ";
            for (int16_t v : imu_snapshot)
                std::cout << v << " ";
            std::cout << "| freq: "
                      << std::fixed << std::setprecision(1)
                      << sensor->getReadFrequency() << " Hz\n";
        }

        if (key == 27)
        {
            std::cout << "Exiting...\n";
            running = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // --------------------------------------------------
    // Save CSV file
    // --------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(csv_mutex);
        std::string filename = makeTimestampedFilename();

        std::ofstream outfile(filename);
        if (!outfile.is_open())
        {
            std::cerr << "Cannot open " << filename << "\n";
            return -1;
        }

        outfile << imu_stream.str();
        std::cout << "[INFO] Saved: " << filename << "\n";
    }

    return 0;
}
