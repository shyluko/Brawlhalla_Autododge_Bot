#pragma once

#include <filesystem>
#include <string>

class Logger {
public:
    static void initialize(const std::filesystem::path& baseDirectory = {});
    static void shutdown();

    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
    static void debug(const std::string& message);

    static void decision(const std::string& message);
    static void diagnostic(const std::string& message);
    static void calibration(const std::string& message);
    static void validation(const std::string& message);
    static void versionChange(const std::string& message);

    static std::filesystem::path logDirectory();

private:
    static void write(const std::string& level, const std::string& message, const char* channelFile = nullptr);
};
