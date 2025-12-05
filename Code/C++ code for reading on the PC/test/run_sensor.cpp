#include "unige_tactile_sensors_cpp/sensor_serial.hpp"

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

// Vector de fluxos de dades, un per cada CSV
std::vector<std::stringstream> data_streams;
std::mutex data_mutex;
size_t current_stream_index = 0;

// Funció auxiliar per obtenir una puntuació temporal amb microsegons
double getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
               now.time_since_epoch())
               .count() /
           1e6;
}

// Lectura no bloquejant de teclat
char getKeyNonBlocking() {
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

// Escriu capçalera
void writeHeader(std::stringstream &stream) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_local;
    localtime_r(&now_time, &tm_local);
    
    stream << "# YYYY/MM/DD | HH:MM:SS -> "
           << std::put_time(&tm_local, "%Y/%m/%d | %H:%M:%S") << "\n";

    stream << "HostTimestamp(seconds),BoardTimestamp(uint32),Sequence(uint16)";
    
    // Adjust number of sensors here if needed
    for (size_t i = 0; i < NUM_TACTILE_SENSORS; ++i)
        stream << ",Sensor" << i + 1;

    stream << "\n";
}

// Callback actualitzat: rep timestamp i seq
void callbackFunction(uint32_t board_timestamp,
                      uint16_t sequence,
                      const std::vector<double> &_data)
{
    static double start_host_ts = 0.0;
    double host_timestamp = getCurrentTimestamp();

    if (start_host_ts == 0.0)
        start_host_ts = host_timestamp;

    // ===== PRINT BOARD TIMESTAMP AND SEQUENCE =====
    std::cout << "[PACKET] BoardTS = " << board_timestamp
              << " | Seq = " << sequence << std::endl;

    // ===== OPTIONAL: KEEP WRITING TO CSV EVERY 0.5s =====
    if (host_timestamp - start_host_ts >= 0.5) {
        std::lock_guard<std::mutex> lock(data_mutex);
        if (data_streams.empty()) return;

        std::stringstream &current = data_streams.back();

        // Host timestamp
        current << std::fixed << std::setprecision(6)
                << host_timestamp << ",";

        // Board timestamp + sequence
        current << board_timestamp << "," << sequence << ",";

        // Sensor data
        for (size_t i = 0; i < _data.size(); ++i) {
            current << std::setprecision(5) << _data[i];
            if (i < _data.size() - 1)
                current << ",";
        }
        current << "\n";
    }
}


int main() {
    std::unique_ptr<SensorSerial> sensor = std::make_unique<SensorSerial>();

    // NEW: Use extended callback
    sensor->setDataCallback(callbackFunction);

    {
        std::lock_guard<std::mutex> lock(data_mutex);
        data_streams.emplace_back();
        writeHeader(data_streams.back());
    }

    std::cout << "Press [space] to read values, [s] to split CSV, [esc] to exit.\n";

    bool running = true;
    while (running) {
        char key = getKeyNonBlocking();

        if (key == 32) {  // Space bar
            std::vector<double> snapshot;
            sensor->getLatestValues(snapshot);

            std::cout << "[Polled values]: ";
            for (double v : snapshot) std::cout << v << " ";
            std::cout << std::endl;
        }

        if (key == 's' || key == 'S') {
            std::lock_guard<std::mutex> lock(data_mutex);
            data_streams.emplace_back();
            writeHeader(data_streams.back());
            current_stream_index++;
            std::cout << "[INFO] Split created: dataset #" 
                      << current_stream_index << std::endl;
        }

        if (key == 27) {  // ESC
            std::cout << "Exiting the main program..." << std::endl;
            running = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Escriure tots els CSVs
    {
        std::lock_guard<std::mutex> lock(data_mutex);

        for (size_t i = 0; i < data_streams.size(); ++i) {
            std::string filename = "sensor_data_" + std::to_string(i + 1) + ".csv";
            std::ofstream outfile(filename);

            if (!outfile.is_open()) {
                std::cerr << "Error opening file " << filename << " for writing.\n";
                continue;
            }

            outfile << data_streams[i].str();
            std::cout << "[INFO] Saved: " << filename << std::endl;
        }
    }

    return 0;
}

