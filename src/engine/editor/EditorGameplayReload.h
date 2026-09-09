#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine::editor {

// Host-only configuration: this does not change the project/package DLL ABI.
struct GameplayReloadConfig {
    bool enabled = false;
    std::filesystem::path buildDirectory;
    std::string target;
    std::vector<std::filesystem::path> watchPaths;
    int debounceMs = 1200;
};

bool loadGameplayReloadConfig(const std::filesystem::path& descriptor,
    GameplayReloadConfig& out, std::string& error);

using SourceSnapshot = std::map<std::filesystem::path,
    std::pair<std::filesystem::file_time_type, std::uintmax_t>>;
bool scanGameplaySources(const std::filesystem::path& root,
    const GameplayReloadConfig& config, SourceSnapshot& out, std::string& error);

// Separated from I/O so save bursts, failures and saves during compilation have
// deterministic tests. A build only represents the revision at its start.
class GameplayReloadSchedule {
public:
    using Clock = std::chrono::steady_clock;
    void changed(Clock::time_point now);
    bool shouldBuild(Clock::time_point now, int debounceMs, bool automatic) const;
    void requestBuild();
    void started();
    void finished(bool success);
    void consumed();
    bool building() const { return building_; }
    bool ready() const { return ready_; }
    bool pending() const { return dirty_; }
private:
    Clock::time_point lastChange_{};
    std::uint64_t revision_ = 0;
    std::uint64_t buildRevision_ = 0;
    bool dirty_ = false;
    bool manual_ = false;
    bool building_ = false;
    bool ready_ = false;
};

// Owns the child process and its descendants. Destruction cancels a build;
// process polling and log reads never block the editor on compilation.
class GameplayBuildProcess {
public:
    GameplayBuildProcess();
    ~GameplayBuildProcess();
    GameplayBuildProcess(const GameplayBuildProcess&) = delete;
    GameplayBuildProcess& operator=(const GameplayBuildProcess&) = delete;
    bool start(const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& workingDirectory,
        const std::filesystem::path& logPath, std::string& error);
    std::optional<int> poll();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Keep this alive until AFTER the library has unloaded. Copies sit beside the
// original DLL so its sibling dependency lookup remains unchanged on Windows.
class GameplayPluginCopy {
public:
    ~GameplayPluginCopy();
    const std::filesystem::path& path() const { return path_; }
    static std::shared_ptr<GameplayPluginCopy> create(
        const std::filesystem::path& source, std::string& error);
private:
    std::filesystem::path path_;
};

class EditorGameplayReload {
public:
    void configure(const std::filesystem::path& descriptor,
        const std::string& configuration, bool allowAutomatic);
    void tick();
    void requestBuild() { schedule_.requestBuild(); }
    void setAutomatic(bool value);
    bool automatic() const { return automatic_; }
    bool available() const { return !config_.target.empty(); }
    bool building() const { return schedule_.building(); }
    bool ready() const { return schedule_.ready(); }
    void reloaded(bool success, const std::string& detail);
    const std::string& status() const { return status_; }
    const std::string& log() const { return log_; }
    const std::filesystem::path& logPath() const { return logPath_; }
private:
    GameplayReloadConfig config_;
    GameplayReloadSchedule schedule_;
    SourceSnapshot snapshot_;
    std::filesystem::path root_;
    std::filesystem::path logPath_;
    std::string configuration_;
    std::string status_;
    std::string log_;
    bool automatic_ = false;
    GameplayReloadSchedule::Clock::time_point nextScan_{};
    GameplayBuildProcess process_;
};

} // namespace engine::editor
