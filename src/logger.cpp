#include "logger.h"
#include "paths.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

#include <windows.h>

namespace {
std::mutex& get_logger_mutex() {
    static std::mutex log_mutex;
    return log_mutex;
}

std::ofstream& get_log_stream() {
    static std::ofstream stream;
    return stream;
}

std::filesystem::path& get_log_dir() {
    static std::filesystem::path dir;
    return dir;
}

std::string get_timestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto time = clock::to_time_t(now);
    std::tm local_time = {};
    localtime_s(&local_time, &time);
    std::ostringstream ss;
    ss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void append_channel(const std::filesystem::path& file, const std::string& line) {
    std::ofstream channel(file, std::ios::app);
    if (channel) {
        channel << line << '\n';
    }
}
} // namespace

void Logger::initialize(const std::filesystem::path& baseDirectory) {
    get_log_dir() = baseDirectory.empty() ? LogsDirectory() : (baseDirectory / "logs");
    std::filesystem::create_directories(get_log_dir());
    const std::filesystem::path logFilePath = get_log_dir() / "bot.log";

    std::lock_guard<std::mutex> lock(get_logger_mutex());
    get_log_stream().open(logFilePath, std::ios::app);
    if (get_log_stream().is_open()) {
        get_log_stream() << "\n[INFO] Logger initialized\n";
        get_log_stream().flush();
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(get_logger_mutex());
    if (get_log_stream().is_open()) {
        get_log_stream().close();
    }
}

std::filesystem::path Logger::logDirectory() {
    return get_log_dir().empty() ? LogsDirectory() : get_log_dir();
}

void Logger::write(const std::string& level, const std::string& message, const char* channelFile) {
    const std::string formatted = "[" + level + "] " + message;
    const std::string line = "[" + get_timestamp() + "] " + formatted;

    {
        std::lock_guard<std::mutex> lock(get_logger_mutex());
        if (get_log_stream().is_open()) {
            get_log_stream() << line << '\n';
            get_log_stream().flush();
        }
        if (channelFile) {
            append_channel(Logger::logDirectory() / channelFile, line);
        }
    }

    std::cout << line << '\n';
    std::cout.flush();
}

void Logger::info(const std::string& message) { write("INFO", message); }
void Logger::warn(const std::string& message) { write("WARN", message); }
void Logger::error(const std::string& message) { write("ERROR", message); }
void Logger::debug(const std::string& message) { write("DEBUG", message); }
void Logger::decision(const std::string& message) { write("DECISION", message, "decisions.log"); }
void Logger::diagnostic(const std::string& message) { write("DIAG", message, "diagnostics.log"); }
void Logger::calibration(const std::string& message) { write("CALIB", message, "calibration_events.log"); }
void Logger::validation(const std::string& message) { write("VALID", message, "validation.log"); }
void Logger::versionChange(const std::string& message) { write("VERSION", message, "version_changes.log"); }
