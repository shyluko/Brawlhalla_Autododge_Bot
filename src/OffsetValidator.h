#pragma once

#include "GameDataManager.h"
#include "logger.h"
#include "offsets.h"
#include "paths.h"

#include "json.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

struct ValidationCheck {
    std::string identifier;
    OffsetConfidence confidence = OffsetConfidence::Confirmed;
    bool passed = false;
    std::string expected;
    std::string observed;
    std::string detail;
};

struct ValidationReport {
    std::string gameVersion = "unknown";
    std::string timestamp;
    bool criticalFailure = false;
    std::vector<ValidationCheck> checks;
    std::string summary;

    std::string toText() const {
        std::ostringstream out;
        out << "Validation report\n";
        out << "Timestamp: " << timestamp << "\n";
        out << "Game version: " << gameVersion << "\n";
        out << "Critical: " << (criticalFailure ? "yes" : "no") << "\n";
        out << "Summary: " << summary << "\n";
        for (const auto& check : checks) {
            out << "- [" << OffsetConfidenceName(check.confidence) << "] "
                << check.identifier << " => " << (check.passed ? "PASS" : "FAIL")
                << " expected=" << check.expected
                << " observed=" << check.observed
                << " " << check.detail << "\n";
        }
        return out.str();
    }
};

class OffsetValidator {
public:
    ValidationReport last;

    ValidationReport run(const std::string& gameVersion,
                         bool hookInstalled,
                         uintptr_t hookTarget,
                         int entityCount,
                         int validEntityCount,
                         bool localFound,
                         bool enemyFound,
                         int moveCount,
                         bool processPresent) {
        ValidationReport report;
        report.gameVersion = gameVersion.empty() ? "unknown" : gameVersion;
        report.timestamp = NowStamp();

        Add(report, "process", OffsetConfidence::Confirmed, processPresent,
            "Brawlhalla running", processPresent ? "present" : "missing",
            "Game process must exist");
        Add(report, "capture_pattern", OffsetConfidence::Confirmed, hookInstalled && hookTarget != 0,
            "F2 0F 10 8B 40 01 00 00", hookInstalled ? "installed" : "not found",
            "Capture hook signature");
        Add(report, "physobject.pos", OffsetConfidence::Confirmed, validEntityCount > 0 || !hookInstalled,
            "finite fighter positions", std::to_string(validEntityCount) + " valid actors",
            "Actor identification");
        Add(report, "entity.local_marker", OffsetConfidence::Conditional, localFound || validEntityCount == 0,
            "marker 9 or movement calibration", localFound ? "local found" : "unresolved",
            "Local player identification");
        Add(report, "actor.pair", OffsetConfidence::Confirmed, !hookInstalled || (localFound && enemyFound) || entityCount == 0,
            "local + enemy", (localFound && enemyFound) ? "both" : "incomplete",
            "Expected fight structure");
        Add(report, "game_data", OffsetConfidence::Confirmed, moveCount > 0,
            "loaded attack JSON / tables", std::to_string(moveCount) + " moves",
            "Game-data compatibility");

        for (const auto& check : report.checks) {
            if (!check.passed && (check.confidence == OffsetConfidence::Confirmed ||
                                  check.identifier == "process" ||
                                  check.identifier == "capture_pattern")) {
                report.criticalFailure = true;
            }
        }

        report.summary = report.criticalFailure ? "Critical validation failed" : "Validation passed or non-critical";
        last = report;
        Logger::validation(report.toText());
        if (report.criticalFailure) WriteFile(report);
        return report;
    }

    std::filesystem::path lastReportPath() const { return lastPath_; }

private:
    std::filesystem::path lastPath_;

    static std::string NowStamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    static void Add(ValidationReport& report, const std::string& id, OffsetConfidence conf,
                    bool passed, const std::string& expected, const std::string& observed,
                    const std::string& detail) {
        report.checks.push_back({id, conf, passed, expected, observed, detail});
        if (!passed) {
            Logger::warn("Validation failed: " + id + " expected=" + expected + " observed=" + observed);
        }
    }

    void WriteFile(const ValidationReport& report) {
        const auto dir = LogsDirectory() / "validation";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        lastPath_ = dir / ("validation_" + std::to_string(std::time(nullptr)) + ".txt");
        std::ofstream out(lastPath_);
        if (!out) return;
        out << report.toText();
        nlohmann::json json;
        json["game_version"] = report.gameVersion;
        json["timestamp"] = report.timestamp;
        json["critical"] = report.criticalFailure;
        json["summary"] = report.summary;
        json["checks"] = nlohmann::json::array();
        for (const auto& check : report.checks) {
            json["checks"].push_back({
                {"id", check.identifier},
                {"confidence", OffsetConfidenceName(check.confidence)},
                {"passed", check.passed},
                {"expected", check.expected},
                {"observed", check.observed},
                {"detail", check.detail}
            });
        }
        out << "\n" << json.dump(2) << "\n";
    }
};
