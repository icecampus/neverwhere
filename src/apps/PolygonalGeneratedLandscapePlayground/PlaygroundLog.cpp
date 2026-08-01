#include "PlaygroundLog.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

std::string formatTimestamp(const char* dateFmt) {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &timeT);
#else
    localtime_r(&timeT, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, dateFmt);
    return oss.str();
}

std::filesystem::path resolveLogDir(const char* argv0) {
    std::filesystem::path exePath = std::filesystem::absolute(std::filesystem::path(argv0));
    std::filesystem::path dir = exePath.parent_path();
    while (!dir.empty() && dir.has_parent_path()) {
        if (std::filesystem::exists(dir / "CMakeLists.txt") &&
            std::filesystem::exists(dir / "vcpkg.json")) {
            return dir / "logs";
        }
        if (dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return std::filesystem::current_path() / "logs";
}

} // namespace

void setupLogger(const char* argv0) {
    try {
        std::filesystem::path logDir = resolveLogDir(argv0);
        std::filesystem::create_directories(logDir);

        std::string timestamp = formatTimestamp("%Y%m%d_%H%M%S");
        std::string logFileName = "PolygonalGeneratedLandscapePlayground_" + timestamp + ".json.log";
        std::filesystem::path logPath = logDir / logFileName;

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), false);

        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

        auto logger = std::make_shared<spdlog::logger>("meshgen", sinks.begin(), sinks.end());

        logger->set_pattern(
            "{\"ts\":\"%Y-%m-%dT%H:%M:%S.%e\",\"level\":\"%l\",\"msg\":\"%v\"}"
        );

        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::trace);

        spdlog::info("logger initialized: file={}", logPath.string());
    } catch (const std::exception& e) {
        spdlog::set_level(spdlog::level::trace);
        spdlog::error("setupLogger failed: {}", e.what());
    }
}

} // namespace meshgen_playground
