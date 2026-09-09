#include "engine/editor/EditorGameplayReload.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <system_error>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace engine::editor {
namespace {
bool relativePath(const std::filesystem::path& path) {
    if (path.empty() || path.has_root_path()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}

std::string readLogTail(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const auto size = static_cast<std::streamoff>(stream.tellg());
    const auto start = std::max<std::streamoff>(0, size - 16000);
    stream.seekg(start);
    std::string result{std::istreambuf_iterator<char>(stream), {}};
    if (start > 0) result = "[Earlier output is in the build log.]\n" + result;
    return result;
}

std::filesystem::path cmakeExecutable(const std::filesystem::path& build) {
    std::ifstream cache(build / "CMakeCache.txt");
    std::string line;
    constexpr std::string_view prefix = "CMAKE_COMMAND:INTERNAL=";
    while (std::getline(cache, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with(prefix)) return line.substr(prefix.size());
    }
    return {};
}

#if defined(_WIN32)
std::wstring quoteArgument(const std::wstring& arg) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif
} // namespace

bool loadGameplayReloadConfig(const std::filesystem::path& descriptor,
    GameplayReloadConfig& out, std::string& error) {
    out = {};
    try {
        std::ifstream stream(descriptor);
        const auto root = nlohmann::json::parse(stream);
        if (!root.contains("gameplay_reload")) { error.clear(); return true; }
        const auto& json = root.at("gameplay_reload");
        GameplayReloadConfig parsed;
        parsed.enabled = json.value("enabled", true);
        parsed.buildDirectory = json.at("build_directory").get<std::string>();
        parsed.target = json.at("target").get<std::string>();
        parsed.debounceMs = json.value("debounce_ms", 1200);
        for (const auto& path : json.at("watch")) {
            parsed.watchPaths.emplace_back(path.get<std::string>());
        }
        if (!relativePath(parsed.buildDirectory) || parsed.target.empty() ||
            parsed.target.front() == '-' || parsed.watchPaths.empty() ||
            parsed.debounceMs < 250 || parsed.debounceMs > 10000) {
            throw std::runtime_error("Invalid gameplay_reload build settings.");
        }
        for (const auto c : parsed.target) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
                throw std::runtime_error("Invalid gameplay_reload target name.");
        }
        for (const auto& path : parsed.watchPaths) {
            if (!relativePath(path)) throw std::runtime_error("Gameplay watch paths must stay inside the project.");
        }
        out = std::move(parsed);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool scanGameplaySources(const std::filesystem::path& root,
    const GameplayReloadConfig& config, SourceSnapshot& out, std::string& error) {
    SourceSnapshot scanned;
    try {
        const auto add = [&](const std::filesystem::path& path) {
            if (!std::filesystem::is_regular_file(path)) return;
            const auto ext = path.extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp" &&
                ext != ".c" && ext != ".cc" && ext != ".inl" &&
                ext != ".cmake" && path.filename() != "CMakeLists.txt") return;
            scanned.emplace(path, std::make_pair(std::filesystem::last_write_time(path),
                std::filesystem::file_size(path)));
        };
        for (const auto& relative : config.watchPaths) {
            const auto path = root / relative;
            if (!std::filesystem::exists(path)) continue; // Includes deleted watch roots.
            if (!std::filesystem::is_directory(path)) { add(path); continue; }
            for (auto it = std::filesystem::recursive_directory_iterator(path);
                 it != std::filesystem::recursive_directory_iterator(); ++it) {
                if (it->is_directory()) {
                    const auto name = it->path().filename().string();
                    if (name == ".git" || name == ".phlosion" || name == "build" ||
                        name.starts_with("build-") || it->is_symlink()) it.disable_recursion_pending();
                } else if (!it->is_symlink()) add(it->path());
            }
        }
        out = std::move(scanned);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false; // A partial scan must never be treated as a mass deletion.
    }
}

void GameplayReloadSchedule::changed(Clock::time_point now) {
    ++revision_;
    dirty_ = true;
    ready_ = false;
    lastChange_ = now;
}
bool GameplayReloadSchedule::shouldBuild(Clock::time_point now, int debounceMs, bool automatic) const {
    return !building_ && !ready_ && (manual_ || (automatic && dirty_ &&
        now - lastChange_ >= std::chrono::milliseconds(debounceMs)));
}
void GameplayReloadSchedule::requestBuild() { manual_ = true; }
void GameplayReloadSchedule::started() {
    building_ = true;
    ready_ = dirty_ = manual_ = false;
    buildRevision_ = revision_;
}
void GameplayReloadSchedule::finished(bool success) {
    building_ = false;
    ready_ = success && buildRevision_ == revision_ && !manual_;
    // Failed revisions are not retried until another save or explicit rebuild.
}
void GameplayReloadSchedule::consumed() { ready_ = false; }

struct GameplayBuildProcess::Impl {
#if defined(_WIN32)
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    ~Impl() {
        if (job) CloseHandle(job);
        if (process) CloseHandle(process);
    }
#endif
};
GameplayBuildProcess::GameplayBuildProcess() : impl_(std::make_unique<Impl>()) {}
GameplayBuildProcess::~GameplayBuildProcess() = default;
bool GameplayBuildProcess::start(const std::filesystem::path& executable,
    const std::vector<std::string>& arguments, const std::filesystem::path& workingDirectory,
    const std::filesystem::path& logPath, std::string& error) {
#if defined(_WIN32)
    if (impl_->process) { error = "A gameplay build is already running."; return false; }
    auto next = std::make_unique<Impl>();
    next->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!next->job || !SetInformationJobObject(next->job, JobObjectExtendedLimitInformation,
            &limits, sizeof(limits))) {
        error = "Could not create the gameplay build job."; return false;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(logPath.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) { error = "Could not open the gameplay build log."; return false; }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = startup.hStdError = output;
    startup.hStdInput = input;
    PROCESS_INFORMATION process{};
    std::wstring command = quoteArgument(executable.wstring());
    for (const auto& arg : arguments) command += L" " + quoteArgument(std::filesystem::path(arg).wstring());
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startup, &process);
    const DWORD createError = GetLastError();
    CloseHandle(output);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (!created) { error = "Could not start CMake (Windows error " + std::to_string(createError) + ")."; return false; }
    if (!AssignProcessToJobObject(next->job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        error = "Could not attach the gameplay build to its editor job.";
        return false;
    }
    next->process = process.hProcess;
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    impl_ = std::move(next);
    error.clear();
    return true;
#else
    error = "Automatic gameplay compilation is currently supported on Windows.";
    return false;
#endif
}
std::optional<int> GameplayBuildProcess::poll() {
#if defined(_WIN32)
    if (!impl_->process || WaitForSingleObject(impl_->process, 0) != WAIT_OBJECT_0) return {};
    DWORD code = 1;
    GetExitCodeProcess(impl_->process, &code);
    impl_ = std::make_unique<Impl>();
    return static_cast<int>(code);
#else
    return {};
#endif
}

GameplayPluginCopy::~GameplayPluginCopy() {
    std::error_code ignored;
    if (!path_.empty()) std::filesystem::remove(path_, ignored);
}
std::shared_ptr<GameplayPluginCopy> GameplayPluginCopy::create(
    const std::filesystem::path& source, std::string& error) {
    static std::atomic<unsigned> serial{0};
#if defined(_WIN32)
    const auto processId = GetCurrentProcessId();
#else
    const auto processId = getpid();
#endif
    auto result = std::make_shared<GameplayPluginCopy>();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    result->path_ = source.parent_path() / (".live-" + std::to_string(processId) + "-" +
        std::to_string(stamp) + "-" + std::to_string(++serial) + source.extension().string());
    std::error_code ec;
    if (!std::filesystem::copy_file(source, result->path_, std::filesystem::copy_options::none, ec)) {
        error = "Could not copy the gameplay module for reload: " + ec.message();
        return {};
    }
    error.clear();
    return result;
}

void EditorGameplayReload::configure(const std::filesystem::path& descriptor,
    const std::string& configuration, bool allowAutomatic) {
    root_ = descriptor.parent_path();
    configuration_ = configuration;
    if (!loadGameplayReloadConfig(descriptor, config_, status_)) {
        status_ = "Gameplay reload disabled: " + status_;
        return;
    }
    if (!available()) return;
    automatic_ = config_.enabled && allowAutomatic;
    std::string error;
    if (!scanGameplaySources(root_, config_, snapshot_, error)) {
        config_ = {};
        status_ = "Gameplay reload disabled: " + error;
        return;
    }
    // Each editor owns its log; multiple editor windows cannot truncate it.
#if defined(_WIN32)
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    logPath_ = root_ / ".phlosion" / ("gameplay-build-" + std::to_string(pid) + ".log");
    status_ = automatic_ ? "Auto Reload: watching gameplay source saves." : "Auto Reload is off.";
    std::cerr << "[Gameplay Reload] " << status_ << '\n';
}

void EditorGameplayReload::tick() {
    if (!available()) return;
    const auto now = GameplayReloadSchedule::Clock::now();
    // Recheck immediately before replacing a module, including saves after
    // compilation completed but before the next frame's reload.
    if (now >= nextScan_ || schedule_.ready()) {
        SourceSnapshot current;
        std::string error;
        if (!scanGameplaySources(root_, config_, current, error)) {
            status_ = "Waiting to read gameplay sources: " + error;
            return;
        }
        if (current != snapshot_) {
            snapshot_ = std::move(current);
            schedule_.changed(now);
            status_ = schedule_.building() ? "Building gameplay; another save is queued." :
                automatic_ ? "Gameplay changed; waiting for saves to settle..." :
                "Gameplay changed. Enable Auto Reload or choose Rebuild Gameplay.";
        }
        nextScan_ = now + std::chrono::milliseconds(500);
        if (schedule_.building()) log_ = readLogTail(logPath_);
    }
    if (schedule_.building()) {
        if (const auto exitCode = process_.poll()) {
            schedule_.finished(*exitCode == 0);
            log_ = readLogTail(logPath_);
            status_ = *exitCode != 0 ? "Gameplay build failed. Current version kept; see compiler output below." :
                schedule_.ready() ? "Reloading gameplay... The preview will reset." :
                "Build finished; newer saves are queued.";
            std::cerr << "[Gameplay Reload] build exit=" << *exitCode << " " << status_ << '\n';
        }
    }
    if (schedule_.shouldBuild(now, config_.debounceMs, automatic_)) {
        schedule_.started();
        const auto build = root_ / config_.buildDirectory;
        const auto cmake = cmakeExecutable(build);
        std::string error;
        std::error_code ec;
        std::filesystem::create_directories(logPath_.parent_path(), ec);
        if (cmake.empty() || ec || !process_.start(cmake,
                {"--build", build.string(), "--config", configuration_, "--target", config_.target, "--parallel", "4"},
                root_, logPath_, error)) {
            schedule_.finished(false);
            status_ = "Gameplay build could not start: " +
                (cmake.empty() ? "configure the project's CMake build directory first." : ec ? ec.message() : error);
        } else {
            status_ = "Building gameplay... You can keep using the editor.";
            log_.clear();
        }
        std::cerr << "[Gameplay Reload] " << status_ << '\n';
    }
}

void EditorGameplayReload::reloaded(bool success, const std::string& detail) {
    schedule_.consumed();
    status_ = success ? "Gameplay reloaded. Preview reset; press Play when ready." :
        "Gameplay reload failed. " + detail;
    std::cerr << "[Gameplay Reload] " << status_ << '\n';
}
void EditorGameplayReload::setAutomatic(bool value) {
    automatic_ = value;
    if (!building() && !ready()) status_ = value ?
        "Auto Reload: watching gameplay source saves." : "Auto Reload is off. Use Gameplay > Rebuild Gameplay to apply code changes.";
}
} // namespace engine::editor
