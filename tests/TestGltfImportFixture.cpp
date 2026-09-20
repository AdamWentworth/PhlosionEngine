// tests/TestGltfImportFixture.cpp
//
// Narrow regression for the shared glTF fixture helper. The helper must own a
// unique directory acquired atomically beneath the system temporary directory,
// must never delete a path it did not create, and must reject unsafe path
// components. This is test-support verification only; no production code and no
// glTF import behavior is involved.
//
// Every sentinel path used here is created by this test with a unique,
// atomically reserved name. The test never inspects, writes or removes an
// unrelated fixed global path.

#include "gltf_import/GltfImportTestSupport.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using gltf_import_contract::FixtureDirectory;
using gltf_import_contract::expect;
using gltf_import_contract::fixtureNameSuffix;

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    return output.good();
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string text;
    std::getline(input, text);
    return text;
}

// A uniquely named directory reserved by this test for the duration of one
// case. It removes exactly the directory it created, including on early
// returns, and nothing else.
class SentinelDirectory {
public:
    SentinelDirectory() = default;

    // The prefix is unique per acquisition, and <temp>/<prefix> is deliberately
    // the fixed-name path a regressed helper would predelete, so creating it
    // here with content makes that regression observable.
    static SentinelDirectory acquire(const std::string& basePrefix) {
        SentinelDirectory sentinel;
        const std::filesystem::path root = FixtureDirectory::temporaryRoot();
        for (int attempt = 0; attempt < 64; ++attempt) {
            std::string prefix = basePrefix + fixtureNameSuffix();
            const std::filesystem::path candidate = root / prefix;
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                sentinel.prefix_ = std::move(prefix);
                sentinel.path_ = candidate;
                sentinel.owned_ = true;
                return sentinel;
            }
            if (error) {
                sentinel.error_ = error.message();
                return sentinel;
            }
        }
        sentinel.error_ = "no free sentinel directory name was available";
        return sentinel;
    }

    SentinelDirectory(const SentinelDirectory&) = delete;
    SentinelDirectory& operator=(const SentinelDirectory&) = delete;

    SentinelDirectory(SentinelDirectory&& other) noexcept
        : path_(std::move(other.path_)),
          prefix_(std::move(other.prefix_)),
          error_(std::move(other.error_)),
          owned_(other.owned_) {
        other.owned_ = false;
    }

    SentinelDirectory& operator=(SentinelDirectory&& other) noexcept {
        if (this != &other) {
            release();
            path_ = std::move(other.path_);
            prefix_ = std::move(other.prefix_);
            error_ = std::move(other.error_);
            owned_ = other.owned_;
            other.owned_ = false;
        }
        return *this;
    }

    ~SentinelDirectory() { release(); }

    bool ready() const { return owned_; }

    const std::string& prefix() const { return prefix_; }

    const std::filesystem::path& path() const { return path_; }

    const std::string& error() const { return error_; }

private:
    void release() {
        if (!owned_) {
            return;
        }
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        owned_ = false;
    }

    std::filesystem::path path_;
    std::string prefix_;
    std::string error_;
    bool owned_ = false;
};

// Creates the task-owned sentinel directory and its marker before any
// FixtureDirectory exists.
bool acquireSentinel(const std::string& basePrefix,
                     const std::string& markerContent,
                     SentinelDirectory& outSentinel,
                     std::filesystem::path& outMarker,
                     std::string& outFail) {
    outSentinel = SentinelDirectory::acquire(basePrefix);
    if (!expect(outSentinel.ready(),
                "task-owned sentinel directory was not created: " +
                    outSentinel.error(),
                outFail)) {
        return false;
    }
    outMarker = outSentinel.path() / "sentinel.txt";
    return expect(writeTextFile(outMarker, markerContent),
                  "sentinel marker could not be written", outFail);
}

bool caseSimultaneousInstancesAndSentinels(std::string& outFail) {
    SentinelDirectory sentinel;
    std::filesystem::path sentinelMarker;
    if (!acquireSentinel("phlosion-gltf-fixture-sentinel-", "sentinel", sentinel,
                         sentinelMarker, outFail)) {
        return false;
    }
    const std::filesystem::path sentinelPath = sentinel.path();
    const std::string prefix = sentinel.prefix();
    const std::filesystem::path temporaryRoot = FixtureDirectory::temporaryRoot();

    std::filesystem::path firstPath;
    FixtureDirectory second(prefix);
    if (!expect(second.ready(),
                "second fixture directory was not created: " + second.error(),
                outFail)) {
        return false;
    }
    const std::filesystem::path secondPath = second.path();
    {
        FixtureDirectory first(prefix);
        if (!expect(first.ready(), "first fixture directory was not created: " +
                                       first.error(),
                    outFail)) {
            return false;
        }
        firstPath = first.path();
        if (!expect(firstPath != secondPath,
                    "simultaneous instances must use distinct directories",
                    outFail)) {
            return false;
        }
        if (!expect(firstPath.parent_path() == temporaryRoot &&
                        secondPath.parent_path() == temporaryRoot,
                    "fixture directories must resolve beneath the system "
                    "temporary directory: " +
                        firstPath.parent_path().string() + " vs " +
                        temporaryRoot.string(),
                    outFail)) {
            return false;
        }
        if (!expect(firstPath != sentinelPath && secondPath != sentinelPath,
                    "an instance must not reuse the sentinel prefix directory",
                    outFail)) {
            return false;
        }
        if (!expect(first.writeText("marker-a.txt", "A") &&
                        second.writeText("marker-b.txt", "B"),
                    "instance markers could not be written", outFail)) {
            return false;
        }
        if (!expect(std::filesystem::exists(first.file("marker-a.txt")) &&
                        std::filesystem::exists(second.file("marker-b.txt")),
                    "both markers must exist while both instances live",
                    outFail)) {
            return false;
        }
        if (!expect(std::filesystem::exists(sentinelMarker) &&
                        readTextFile(sentinelMarker) == "sentinel",
                    "creating instances must not disturb the sentinel directory",
                    outFail)) {
            return false;
        }
    }

    if (!expect(!std::filesystem::exists(firstPath),
                "a destroyed instance must clean up its own directory", outFail)) {
        return false;
    }
    if (!expect(std::filesystem::exists(second.file("marker-b.txt")) &&
                    readTextFile(second.file("marker-b.txt")) == "B",
                "the surviving instance's marker must outlive the other's "
                "cleanup",
                outFail)) {
        return false;
    }
    if (!expect(std::filesystem::exists(sentinelMarker) &&
                    readTextFile(sentinelMarker) == "sentinel",
                "sentinel content must survive instance creation and cleanup",
                outFail)) {
        return false;
    }
    return true;
}

bool caseParallelInstances(std::string& outFail) {
    constexpr std::size_t kInstanceCount = 8u;
    SentinelDirectory sentinel;
    std::filesystem::path sentinelMarker;
    if (!acquireSentinel("phlosion-gltf-parallel-sentinel-", "sentinel", sentinel,
                         sentinelMarker, outFail)) {
        return false;
    }
    const std::string prefix = sentinel.prefix();

    std::vector<std::unique_ptr<FixtureDirectory>> instances(kInstanceCount);
    std::vector<std::thread> threads;
    threads.reserve(kInstanceCount);
    for (std::size_t index = 0; index < kInstanceCount; ++index) {
        threads.emplace_back([&instances, &prefix, index] {
            instances[index] = std::make_unique<FixtureDirectory>(prefix);
            if (instances[index]->ready()) {
                instances[index]->writeText(
                    "marker-" + std::to_string(index) + ".txt",
                    std::to_string(index));
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    bool allReady = true;
    std::vector<std::filesystem::path> paths;
    paths.reserve(kInstanceCount);
    for (std::size_t index = 0; index < kInstanceCount; ++index) {
        allReady = allReady && instances[index] != nullptr &&
                   instances[index]->ready();
        if (instances[index] != nullptr && instances[index]->ready()) {
            paths.push_back(instances[index]->path());
        }
    }
    if (!expect(allReady && paths.size() == kInstanceCount,
                "every parallel fixture directory must be created", outFail)) {
        return false;
    }
    std::vector<std::filesystem::path> uniquePaths = paths;
    std::sort(uniquePaths.begin(), uniquePaths.end());
    uniquePaths.erase(std::unique(uniquePaths.begin(), uniquePaths.end()),
                      uniquePaths.end());
    if (!expect(uniquePaths.size() == kInstanceCount,
                "parallel instances with one prefix must not share directories",
                outFail)) {
        return false;
    }
    for (std::size_t index = 0; index < kInstanceCount; ++index) {
        const std::filesystem::path marker =
            instances[index]->file("marker-" + std::to_string(index) + ".txt");
        if (!expect(!marker.empty() && std::filesystem::exists(marker),
                    "parallel marker " + std::to_string(index) + " must exist",
                    outFail)) {
            return false;
        }
    }

    instances.clear();
    for (const std::filesystem::path& path : paths) {
        if (!expect(!std::filesystem::exists(path),
                    "parallel instance directory " + path.string() +
                        " must be removed with its owner",
                    outFail)) {
            return false;
        }
    }
    if (!expect(std::filesystem::exists(sentinelMarker) &&
                    readTextFile(sentinelMarker) == "sentinel",
                "sentinel content must survive parallel instance cleanup",
                outFail)) {
        return false;
    }
    return true;
}

bool caseUnsafeComponentsRejected(std::string& outFail) {
    const char* unsafePrefixes[] = {"", ".", "..", "../escape", "a/b", "a\\b",
                                    "with space", "colon:name", "upper..dot"};
    for (const char* prefix : unsafePrefixes) {
        FixtureDirectory rejected(prefix);
        if (!expect(!rejected.ready(),
                    "unsafe prefix \"" + std::string(prefix) +
                        "\" must not create a directory",
                    outFail)) {
            return false;
        }
        if (!expect(!rejected.error().empty() && rejected.path().empty() &&
                        rejected.file("fixture.bin").empty(),
                    "an unsafe prefix must report an error and expose no path",
                    outFail)) {
            return false;
        }
    }

    FixtureDirectory directory("phlosion-gltf-fixture-names");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }

    // A task-owned traversal target replaces an assumption about an unrelated
    // global temp path: refusing the relative name must leave this content
    // untouched.
    SentinelDirectory target;
    std::filesystem::path targetMarker;
    if (!acquireSentinel("phlosion-gltf-traversal-target-", "keep", target,
                         targetMarker, outFail)) {
        return false;
    }
    const std::string traversal =
        "../" + target.path().filename().string() + "/sentinel.txt";

    const char* unsafeNames[] = {"../escape.txt", "sub/dir.bin", "..", "",
                                 "with space.bin", traversal.c_str()};
    for (const char* name : unsafeNames) {
        if (!expect(!directory.writeText(name, "overwritten"),
                    "unsafe child name \"" + std::string(name) +
                        "\" must be refused",
                    outFail)) {
            return false;
        }
    }
    if (!expect(readTextFile(targetMarker) == "keep",
                "a refused relative traversal must leave the task-owned target "
                "content unchanged",
                outFail)) {
        return false;
    }
    if (!expect(std::filesystem::is_empty(directory.path()),
                "refused names must not create entries inside the fixture "
                "directory",
                outFail)) {
        return false;
    }
    return true;
}

}  // namespace

bool test_gltf_import_fixture_contract(std::string& outFail) {
    outFail.clear();
    if (!caseSimultaneousInstancesAndSentinels(outFail)) {
        return false;
    }
    if (!caseParallelInstances(outFail)) {
        return false;
    }
    if (!caseUnsafeComponentsRejected(outFail)) {
        return false;
    }
    return true;
}
