#pragma once
#include <fstream>
#include <mutex>
#include <string>

class Logger {
public:
    Logger(const std::string& filename);
    ~Logger();

    void write(const std::string& message);
    std::string readLine();

private:
    std::ofstream logFile;
    std::ifstream logFileReader;
    std::mutex mtx;
};
