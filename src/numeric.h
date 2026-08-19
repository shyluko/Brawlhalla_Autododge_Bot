#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

inline bool IsFiniteNumber(double value) {
    return std::isfinite(value) != 0;
}

inline double SanitizeDouble(double value, double fallback, double minValue, double maxValue) {
    if (!IsFiniteNumber(value)) return fallback;
    return std::clamp(value, minValue, maxValue);
}

inline float SanitizeFloat(float value, float fallback, float minValue, float maxValue) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minValue, maxValue);
}

inline int SanitizeInt(int value, int fallback, int minValue, int maxValue) {
    if (value < minValue || value > maxValue) {
        if (value == 0 && fallback != 0) return fallback;
    }
    return std::clamp(value, minValue, maxValue);
}

inline std::string SanitizeLogLevel(std::string level) {
    for (char& ch : level) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (level == "DEBUG" || level == "INFO" || level == "WARN" || level == "ERROR") return level;
    return "INFO";
}
