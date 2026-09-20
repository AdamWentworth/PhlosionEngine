#include "FixtureSession.h"

#include <stdexcept>
#include <thread>

namespace engine::fixture {
namespace {

using engine::assets::phlosion::ArchiveKind;
using engine::assets::phlosion::ArchiveSessionState;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

} // namespace

FixtureSession::FixtureSession(FixtureCase fixtureCase)
    : fixtureCase_(fixtureCase) {}

FixtureSession::~FixtureSession() = default;

bool FixtureSession::pumpCandidate(
    bool expectReplacement,
    std::string* outError) {
    const auto deadline =
        std::chrono::steady_clock::now() + kPreparationTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        session_->pump();
        switch (session_->state()) {
        case ArchiveSessionState::Idle:
            return fail(
                outError,
                std::string("fixture session returned to idle before ") +
                    (expectReplacement ? "replacement" : "initial") +
                    " preparation finished");
        case ArchiveSessionState::Loading:
            break;
        case ArchiveSessionState::Staged:
        case ArchiveSessionState::Failed:
        case ArchiveSessionState::Cancelled:
            if (outError) {
                outError->clear();
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return fail(
        outError,
        std::string("fixture session timed out after ") +
            std::to_string(kPreparationTimeout.count()) +
            "ms during " +
            (expectReplacement ? "replacement" : "initial") +
            " preparation");
}

bool FixtureSession::prepare(std::string* outError) {
    if (prepared_) {
        return fail(outError, "fixture session was already prepared");
    }

    store_ = std::make_shared<FixtureAssetStore>();
    addFixtureDependency(*store_);
    for (const FixtureContentVariant variant :
         {FixtureContentVariant::A,
          FixtureContentVariant::B,
          FixtureContentVariant::MissingDependency}) {
        FixtureBundle bundle = buildFixtureBundle(variant);
        store_->add(bundle.virtualPath, std::move(bundle.archiveBytes));
    }

    session_ = std::make_unique<
        engine::assets::phlosion::PhlosionArchiveSession>(
        store_,
        fixtureAssetResolver);
    std::string sessionError;
    if (!session_->begin(kFixtureRootA, ArchiveKind::Scene, &sessionError)) {
        return fail(
            outError,
            "fixture initial activation was rejected: " + sessionError);
    }
    if (!pumpCandidate(false, outError)) {
        return false;
    }
    if (session_->state() != ArchiveSessionState::Staged) {
        return fail(
            outError,
            "fixture initial activation did not stage scene A: " +
                session_->error());
    }
    const std::shared_ptr<const engine::assets::phlosion::ArchiveSnapshot>
        staged = session_->staged();
    if (!staged) {
        return fail(outError, "fixture initial activation staged no snapshot");
    }
    auto content = std::make_shared<FixtureContent>();
    if (!validateFixtureSnapshot(*staged, *content, outError)) {
        session_->discard();
        return false;
    }
    if (!session_->activate()) {
        return fail(outError, "fixture initial activation could not publish scene A");
    }
    activeContent_ = std::move(content);
    activationCount_ = 1;
    prepared_ = true;
    if (outError) {
        outError->clear();
    }
    return true;
}

bool FixtureSession::attemptReplacement(std::string* outError) {
    if (!prepared_ || !activeContent_) {
        return fail(outError, "fixture replacement requires an active scene");
    }
    if (!fixtureCaseReplaces(fixtureCase_)) {
        if (outError) {
            outError->clear();
        }
        return true;
    }

    transition_ = FixtureTransition{};
    transition_.attempted = true;
    transition_.requestedRoot = fixtureCaseReplacementRoot(fixtureCase_);

    const std::shared_ptr<const FixtureContent> previous = activeContent_;
    std::string sessionError;
    if (!session_->begin(
            transition_.requestedRoot,
            ArchiveKind::Scene,
            &sessionError)) {
        transition_.failed = true;
        transition_.outcome = "begin_rejected";
        transition_.error = sessionError;
        return fail(
            outError,
            "fixture replacement begin was rejected: " + sessionError);
    }
    if (!pumpCandidate(true, outError)) {
        transition_.failed = true;
        transition_.outcome = "timed_out";
        transition_.error = outError ? *outError : std::string{};
        return false;
    }

    if (session_->state() == ArchiveSessionState::Staged) {
        const std::shared_ptr<const engine::assets::phlosion::ArchiveSnapshot>
            staged = session_->staged();
        auto content = std::make_shared<FixtureContent>();
        std::string validationError;
        if (!staged || !validateFixtureSnapshot(*staged, *content, &validationError)) {
            session_->discard();
            transition_.failed = true;
            transition_.outcome = "validation_failed";
            transition_.error = validationError;
            return fail(
                outError,
                "fixture replacement content failed validation: " +
                    validationError);
        }
        if (!session_->activate()) {
            transition_.failed = true;
            transition_.outcome = "activation_rejected";
            transition_.error = "activate() returned false in Staged";
            return fail(
                outError,
                "fixture replacement could not activate staged content");
        }
        activeContent_ = std::move(content);
        activationCount_ = 2;
        transition_.succeeded = true;
        transition_.outcome = "activated";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    if (session_->state() == ArchiveSessionState::Failed) {
        transition_.failed = true;
        transition_.outcome = "graph_failed";
        transition_.error = session_->error();
        session_->discard();
        if (activeContent_ != previous ||
            activeContent_->contentId != previous->contentId) {
            return fail(
                outError,
                "a failed fixture replacement changed the active content");
        }
        if (!fixtureCaseExpectsReplacementFailure(fixtureCase_)) {
            return fail(
                outError,
                "fixture replacement failed unexpectedly: " +
                    transition_.error);
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }

    transition_.failed = true;
    transition_.outcome = "cancelled";
    transition_.error = session_->error();
    return fail(
        outError,
        "fixture replacement was cancelled: " + transition_.error);
}

const FixtureContent& FixtureSession::activeContent() const {
    if (!activeContent_) {
        throw std::logic_error("fixture session has no active content");
    }
    return *activeContent_;
}

std::size_t FixtureSession::archiveFileCount() const noexcept {
    return activeContent_ ? activeContent_->archiveFileCount : 0u;
}

} // namespace engine::fixture
