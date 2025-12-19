#include "unige_tactile_sensors_cpp/sensor_serial_V2.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <mutex>
#include <memory>

// --------------------------------------------------
// Globals for CSV handling
// --------------------------------------------------

std::vector<std::stringstream> data_streams;
std::mutex data_mutex;
size_t current_stream_index = 0;

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
// CSV header
// --------------------------------------------------

void writeHeader(std::stringstream &stream,
                 const std::vector<size_t> &sensor_ids)
{
    stream << "BoardTimestamp(uint32)";
    for (size_t id : sensor_ids)
        stream << ",Sensor" << id;
    stream << "\n";
}

// --------------------------------------------------
// Callback
// --------------------------------------------------

void callbackFunction(uint32_t board_timestamp,
                      const std::vector<double> &data)
{
    std::lock_guard<std::mutex> lock(data_mutex);
    if (data_streams.empty())
        return;

    std::stringstream &current = data_streams.back();

    current << board_timestamp;
    for (double v : data)
        current << "," << std::setprecision(6) << v;
    current << "\n";
}

// --------------------------------------------------
// Main
// --------------------------------------------------

int main(int argc, char **argv)
{

    std::string start_cmd = "start\n";
    if (argc > 1)
    {
        start_cmd = argv[1];
        if (start_cmd.back() != '\n')
            start_cmd += '\n';
    }

    auto sensor = std::make_unique<SensorSerial>(
        "", // auto-detect port
        3000000,
        start_cmd);

    if (!sensor->isSensorAlive())
    {
        std::cerr << "[ERROR] Sensor not alive\n";
        return -1;
    }

    sensor->setDataCallback(callbackFunction);

    {
        std::lock_guard<std::mutex> lock(data_mutex);
        data_streams.emplace_back();
        writeHeader(
            data_streams.back(),
            sensor->getActiveSensorIndices());
    }

    std::cout << "Press [space] to read values, "
              << "[s] to split CSV, [esc] to exit.\n";

    bool running = true;
    while (running)
    {

        char key = getKeyNonBlocking();

        if (key == ' ')
        {
            std::vector<double> snapshot;
            sensor->getLatestValues(snapshot);

            std::cout << "[Polled values]: ";
            for (double v : snapshot)
                std::cout << v << " ";
            std::cout << "\n";
        }

        if (key == 's' || key == 'S')
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            data_streams.emplace_back();
            writeHeader(
                data_streams.back(),
                sensor->getActiveSensorIndices());
            current_stream_index++;
            std::cout << "[INFO] Split created: dataset #"
                      << current_stream_index << "\n";
        }

        if (key == 27)
        {
            std::cout << "Exiting...\n";
            running = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // --------------------------------------------------
    // Save CSV files
    // --------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(data_mutex);
        for (size_t i = 0; i < data_streams.size(); ++i)
        {
            std::string filename =
                "sensor_data_" + std::to_string(i + 1) + ".csv";

            std::ofstream outfile(filename);
            if (!outfile.is_open())
            {
                std::cerr << "Cannot open " << filename << "\n";
                continue;
            }

            outfile << data_streams[i].str();
            std::cout << "[INFO] Saved: " << filename << "\n";
        }
    }

    return 0;
}
