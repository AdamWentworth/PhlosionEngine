#include "engine/editor/EditorGameplayReload.h"

#include <fstream>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

bool test_editor_gameplay_reload(std::string& outFail) {
    using namespace engine::editor;
    using namespace std::chrono_literals;
    const auto require = [](bool value, const char* detail) {
        if (!value) throw std::runtime_error(detail);
    };
    const auto root = std::filesystem::temp_directory_path() /
        ("Phlosion reload test " + std::to_string(GameplayReloadSchedule::Clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};
    try {
        // Save bursts debounce, and a revision compiled during another save is
        // never eligible to replace the running code. Failure must not spin.
        GameplayReloadSchedule schedule;
        const auto start = GameplayReloadSchedule::Clock::now();
        require(!schedule.shouldBuild(start, 1200, true), "Opening a project triggered compilation.");
        schedule.changed(start);
        schedule.changed(start + 900ms);
        require(!schedule.shouldBuild(start + 1500ms, 1200, true), "Save burst was not debounced.");
        require(schedule.shouldBuild(start + 2200ms, 1200, true), "Settled saves did not trigger a build.");
        schedule.started();
        schedule.changed(start + 2300ms);
        schedule.finished(true);
        require(!schedule.ready(), "Stale build was allowed to reload newer saves.");
        require(schedule.shouldBuild(start + 3600ms, 1200, true), "Save during build was lost.");
        schedule.started();
        schedule.finished(false);
        require(!schedule.ready() && !schedule.shouldBuild(start + 20s, 1200, true), "Failure caused an automatic retry loop.");
        schedule.requestBuild();
        require(schedule.shouldBuild(start + 20s, 1200, false), "Manual rebuild did not work with auto reload off.");
        schedule.started();
        schedule.finished(true);
        require(schedule.ready(), "Successful current build was not ready.");
        schedule.changed(start + 21s);
        require(!schedule.ready(), "Save between build and reload was ignored.");
        require(!schedule.shouldBuild(start + 23s, 1200, false), "Disabled auto reload compiled a save.");

        std::filesystem::create_directories(root / "src");
        std::ofstream(root / "project.json") << R"({"gameplay_reload":{
            "enabled":true,"build_directory":"build","target":"GameModule",
            "watch":["src"],"debounce_ms":1200}})";
        GameplayReloadConfig config;
        std::string error;
        require(loadGameplayReloadConfig(root / "project.json", config, error), "Reload config failed.");
        require(config.enabled && config.target == "GameModule", "Reload config values were lost.");
        std::ofstream(root / "src" / "Movement.cpp") << "first";
        SourceSnapshot before, after;
        require(scanGameplaySources(root, config, before, error), "Initial source scan failed.");
        std::ofstream(root / "src" / "screenshot.png") << "ignored";
        std::filesystem::create_directories(root / "src" / "build");
        std::ofstream(root / "src" / "build" / "generated.cpp") << "ignored";
        require(scanGameplaySources(root, config, after, error) && before == after,
            "Build outputs or images triggered compilation.");
        std::ofstream(root / "src" / "Movement.cpp") << "changed source";
        require(scanGameplaySources(root, config, after, error) && before != after, "Source save was missed.");
        before = after;
        std::ofstream(root / "src" / "New.h") << "new header";
        require(scanGameplaySources(root, config, after, error) && before != after, "New source file was missed.");
        before = after;
        std::filesystem::remove(root / "src" / "Movement.cpp");
        require(scanGameplaySources(root, config, after, error) && before != after, "Deleted source file was missed.");
        std::ofstream(root / "project.json") << R"({"gameplay_reload":{
            "build_directory":"build","target":"GameModule","watch":["../outside"]}})";
        require(!loadGameplayReloadConfig(root / "project.json", config, error), "Out-of-project watch root was accepted.");

        // Working bytes survive replacement of the normal linker output.
        const auto original = root / "GameModule.dll";
        std::ofstream(original) << "working version";
        auto copy = GameplayPluginCopy::create(original, error);
        require(copy && copy->path().parent_path() == original.parent_path(), "Module copy changed dependency directory.");
        const auto copyPath = copy->path();
        std::ofstream(original) << "new version";
        std::ifstream bytes(copyPath);
        std::string text{std::istreambuf_iterator<char>(bytes), {}};
        bytes.close();
        require(text == "working version", "Replacing build output overwrote rollback bytes.");
        copy.reset();
        require(!std::filesystem::exists(copyPath), "Unloaded module copy was not cleaned up.");

#if defined(_WIN32)
        wchar_t executablePath[32768]{};
        GetModuleFileNameW(nullptr, executablePath, 32768);
        const auto executable = std::filesystem::path(executablePath);
        GameplayBuildProcess process;
        const auto log = root / "build output.log";
        require(process.start(executable, {"--reload-child", "7", "failed"}, root, log, error), "Asynchronous child launch failed.");
        std::optional<int> code;
        const auto deadline = GameplayReloadSchedule::Clock::now() + 10s;
        while (!(code = process.poll()) && GameplayReloadSchedule::Clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        require(code == 7, "Child failure code did not reach the editor.");
        const std::string expected = "gameplay build \"quoted\" C:\\directory with spaces\\";
        require(process.start(executable, {"--reload-child", "0", expected}, root, log, error), "Child process could not restart after failure.");
        while (!(code = process.poll()) && GameplayReloadSchedule::Clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        require(code == 0, "Successful child process failed.");
        std::ifstream logStream(log);
        std::string logText{std::istreambuf_iterator<char>(logStream), {}};
        require(logText.find(expected) != std::string::npos, "Child argument quoting or compiler log output was lost.");
#endif
        return true;
    } catch (const std::exception& e) {
        outFail = e.what();
        return false;
    }
}
