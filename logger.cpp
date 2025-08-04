#include "logger.h"
#include <iostream>

Logger::Logger(const std::string& filename) {
    logFile.open(filename, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << "\n";
    }

    logFileReader.open(filename, std::ios::in);
    if (!logFileReader.is_open()) {
        std::cerr << "Failed to open log file for reading: " << filename << "\n";
    }
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
    if (logFileReader.is_open()) {
        logFileReader.close();
    }
}

void Logger::write(const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    if (logFile.is_open()) {
        logFile << message << std::endl;
        logFile.flush();  
    }
}

std::string Logger::readLine() {
    std::lock_guard<std::mutex> lock(mtx);
    std::string line;
    if (logFileReader.is_open() && std::getline(logFileReader, line)) {
        return line;
    }
    return "";
}
