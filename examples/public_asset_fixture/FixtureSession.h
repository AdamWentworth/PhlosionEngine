#pragma once

#include "FixtureData.h"

#include <chrono>
#include <memory>
#include <string>

namespace engine::fixture {

// One candidate transition in the fixture lifecycle. A failed replacement
// keeps the previously active content; hosts assert that through
// FixtureSession::activeContent().
struct FixtureTransition {
    bool attempted = false;
    bool succeeded = false;
    bool failed = false;
    std::string requestedRoot;
    std::string outcome;
    std::string error;
};

// Owns the synthetic provider, the real PhlosionArchiveSession and the decoded
// active content. Deferred replacement keeps the same session and cache
// ownership as the initial activation.
class FixtureSession {
public:
    explicit FixtureSession(FixtureCase fixtureCase);
    ~FixtureSession();

    FixtureSession(const FixtureSession&) = delete;
    FixtureSession& operator=(const FixtureSession&) = delete;
    FixtureSession(FixtureSession&&) = delete;
    FixtureSession& operator=(FixtureSession&&) = delete;

    // Builds the deterministic archives, stages A through the real archive
    // session, validates the decoded fixture content and activates it. This
    // runs to completion within a finite timeout or fails explicitly.
    bool prepare(std::string* outError = nullptr);

    // Activates A first (during prepare) and then attempts the case's
    // replacement through the same session. A failure that the case expects
    // preserves the active content and returns true.
    bool attemptReplacement(std::string* outError = nullptr);

    FixtureCase fixtureCase() const noexcept {
        return fixtureCase_;
    }

    const FixtureContent& activeContent() const;
    const FixtureTransition& transition() const noexcept {
        return transition_;
    }
    bool preparationComplete() const noexcept {
        return prepared_;
    }
    std::size_t archiveFileCount() const noexcept;

    // Number of times the active content was published, including the initial
    // activation. Case A and the failed replacement publish once; the valid
    // replacement publishes twice.
    int activationCount() const noexcept {
        return activationCount_;
    }

    static constexpr std::chrono::milliseconds kPreparationTimeout{20000};

private:
    bool pumpCandidate(
        bool expectReplacement,
        std::string* outError);

    FixtureCase fixtureCase_;
    std::shared_ptr<FixtureAssetStore> store_;
    std::unique_ptr<engine::assets::phlosion::PhlosionArchiveSession> session_;
    std::shared_ptr<const FixtureContent> activeContent_;
    FixtureTransition transition_;
    bool prepared_ = false;
    int activationCount_ = 0;
};

} // namespace engine::fixture
