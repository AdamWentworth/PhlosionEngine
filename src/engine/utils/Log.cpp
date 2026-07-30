// src/engine/utils/Log.cpp
#include "engine/utils/Log.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace engine::log {

namespace {
std::atomic<Level> g_minLevel{Level::Info};
std::mutex g_mutex;
std::unique_ptr<std::ofstream> g_file;

const char* levelName(Level lvl) {
    switch (lvl) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "INFO";
}

std::string timestampUtc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
#if defined(_MSC_VER)
    std::tm tm_utc;
    gmtime_s(&tm_utc, &t);
    const std::tm* ptm = &tm_utc;
#else
    std::tm tm_utc;
    gmtime_r(&t, &tm_utc);
    const std::tm* ptm = &tm_utc;
#endif
    std::ostringstream oss;
    oss << std::put_time(ptm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool shouldPrint(Level lvl) {
    return static_cast<int>(lvl) >= static_cast<int>(g_minLevel.load(std::memory_order_relaxed));
}

} // namespace

void setMinLevel(Level lvl) { g_minLevel.store(lvl, std::memory_order_relaxed); }
Level getMinLevel() { return g_minLevel.load(std::memory_order_relaxed); }

bool setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (path.empty()) {
        g_file.reset();
        return true;
    }
    auto f = std::make_unique<std::ofstream>(path, std::ios::app);
    if (!f->is_open()) return false;
    g_file = std::move(f);
    return true;
}

void write(Level lvl, std::string_view tag, std::string_view msg) {
    if (!shouldPrint(lvl)) return;

    if (tag.empty()) tag = "LOG";

    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostringstream line;
    line << "[" << timestampUtc() << "][" << levelName(lvl) << "][" << tag << "] " << msg << "\n";

    if (lvl == Level::Warn || lvl == Level::Error) {
        std::cerr << line.str();
    } else {
        std::cout << line.str();
    }

    if (g_file) {
        (*g_file) << line.str();
        g_file->flush();
    }
}

} // namespace engine::log
