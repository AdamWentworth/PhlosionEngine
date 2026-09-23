#include "UiFocusNavigation.h"

#include "engine/input/InputActions.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace engine::ui {
namespace {

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

void succeed(std::string* error) {
    if (error) error->clear();
}

bool validItems(std::span<const UiFocusItem> items, std::string* error) {
    if (items.size() > 128u) return fail(error, "too many UI focus items");
    for (std::size_t i = 0; i < items.size(); ++i) {
        const UiFocusItem& item = items[i];
        if (item.id.empty() || item.id.size() > 128u ||
            item.id.find('\0') != std::string::npos ||
            item.rect.w <= 0 || item.rect.h <= 0)
            return fail(error, "invalid UI focus item");
        for (std::size_t j = 0; j < i; ++j) {
            if (items[j].id == item.id)
                return fail(error, "duplicate UI focus item ID");
        }
    }
    return true;
}

std::size_t indexOf(std::span<const UiFocusItem> items,
                    std::string_view id) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].enabled && items[i].id == id) return i;
    }
    return items.size();
}

glm::dvec2 center(const ::ui::Rect& rect) {
    return {static_cast<double>(rect.x) + rect.w * 0.5,
            static_cast<double>(rect.y) + rect.h * 0.5};
}

} // namespace

UiFocusNavigator::UiFocusNavigator() : owner_(std::this_thread::get_id()) {}

UiFocusNavigator::~UiFocusNavigator() {
    if (owner_ != std::this_thread::get_id()) std::terminate();
}

void UiFocusNavigator::checkOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("UiFocusNavigator used outside its owner thread");
}

bool UiFocusNavigator::route(std::span<const UiFocusItem> items,
                             const UiControls& controls, float deltaSeconds,
                             bool active, UiRouteResult& out,
                             std::string* error) {
    checkOwner();
    if (!validItems(items, error)) return false;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f ||
        deltaSeconds > 0.25f ||
        !std::isfinite(controls.direction.x) ||
        !std::isfinite(controls.direction.y) ||
        std::abs(controls.direction.x) > 1.0f ||
        std::abs(controls.direction.y) > 1.0f)
        return fail(error, "invalid UI frame delta or direction");

    std::string nextFocus = focusedId_;
    if (indexOf(items, nextFocus) == items.size()) {
        nextFocus.clear();
        for (const UiFocusItem& item : items) {
            if (item.enabled) {
                nextFocus = item.id;
                break;
            }
        }
    }
    Direction nextHeld = heldDirection_;
    float nextElapsed = repeatElapsed_;
    bool nextRepeating = repeating_;
    bool nextNeedsNeutral = needsNeutral_;
    UiRouteResult result;
    result.blocksGameplay = active;

    const auto move = [&](Direction direction) {
        const std::size_t source = indexOf(items, nextFocus);
        if (source == items.size()) return;
        const glm::dvec2 origin = center(items[source].rect);
        std::size_t best = items.size();
        double bestScore = (std::numeric_limits<double>::infinity)();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i == source || !items[i].enabled) continue;
            const glm::dvec2 offset = center(items[i].rect) - origin;
            const double primary = direction == Direction::Left ? -offset.x :
                direction == Direction::Right ? offset.x :
                direction == Direction::Up ? -offset.y : offset.y;
            if (primary <= 0.0) continue;
            const double lateral = direction == Direction::Left ||
                direction == Direction::Right ? std::abs(offset.y) :
                                              std::abs(offset.x);
            const double score = primary + 2.0 * lateral;
            if (score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best != items.size()) nextFocus = items[best].id;
    };

    if (!active) {
        nextHeld = Direction::None;
        nextElapsed = 0.0f;
        nextRepeating = false;
        nextNeedsNeutral = true;
    } else if (nextNeedsNeutral) {
        if (std::abs(controls.direction.x) < 0.5f &&
            std::abs(controls.direction.y) < 0.5f &&
            !controls.acceptDown && !controls.acceptPressed &&
            !controls.cancelDown && !controls.cancelPressed)
            nextNeedsNeutral = false;
    } else {
        Direction direction = Direction::None;
        const float x = controls.direction.x;
        const float y = controls.direction.y;
        if (std::abs(x) >= 0.5f || std::abs(y) >= 0.5f) {
            if (std::abs(x) >= std::abs(y))
                direction = x < 0.0f ? Direction::Left : Direction::Right;
            else
                direction = y < 0.0f ? Direction::Up : Direction::Down;
        }
        if (direction == Direction::None) {
            nextHeld = Direction::None;
            nextElapsed = 0.0f;
            nextRepeating = false;
        } else if (direction != nextHeld) {
            move(direction);
            nextHeld = direction;
            nextElapsed = 0.0f;
            nextRepeating = false;
        } else {
            nextElapsed += deltaSeconds;
            float threshold = nextRepeating ? 0.12f : 0.35f;
            while (nextElapsed >= threshold) {
                move(direction);
                nextElapsed -= threshold;
                nextRepeating = true;
                threshold = 0.12f;
            }
        }
        result.cancelRequested = controls.cancelPressed;
        if (!result.cancelRequested && controls.acceptPressed &&
            !nextFocus.empty())
            result.activatedId = nextFocus;
    }

    result.focusedId = nextFocus;
    result.focusChanged = nextFocus != focusedId_;
    focusedId_ = std::move(nextFocus);
    heldDirection_ = nextHeld;
    repeatElapsed_ = nextElapsed;
    repeating_ = nextRepeating;
    needsNeutral_ = nextNeedsNeutral;
    out = std::move(result);
    succeed(error);
    return true;
}

bool UiFocusNavigator::routeActions(
    std::span<const UiFocusItem> items,
    const engine::input::InputActions& actions,
    const UiActionNames& names, float deltaSeconds, bool active,
    UiRouteResult& out, std::string* error) {
    checkOwner();
    if (names.move.empty() || names.accept.empty() || names.cancel.empty() ||
        names.move == names.accept || names.move == names.cancel ||
        names.accept == names.cancel)
        return fail(error, "invalid UI action names");
    engine::input::ActionState move;
    engine::input::ActionState accept;
    engine::input::ActionState cancel;
    if (!actions.state(names.move, move))
        return fail(error, "UI move action is unavailable");
    if (!actions.state(names.accept, accept))
        return fail(error, "UI accept action is unavailable");
    if (!actions.state(names.cancel, cancel))
        return fail(error, "UI cancel action is unavailable");
    return route(items, {move.value, accept.down, accept.pressed,
                         cancel.down, cancel.pressed},
                 deltaSeconds, active, out, error);
}

bool UiFocusNavigator::setFocus(std::span<const UiFocusItem> items,
                                std::string_view id, std::string* error) {
    checkOwner();
    if (!validItems(items, error)) return false;
    const std::size_t index = indexOf(items, id);
    if (index == items.size())
        return fail(error, "UI focus item is unavailable");
    focusedId_ = items[index].id;
    heldDirection_ = Direction::None;
    repeatElapsed_ = 0.0f;
    repeating_ = false;
    needsNeutral_ = true;
    succeed(error);
    return true;
}

void UiFocusNavigator::reset() {
    checkOwner();
    focusedId_.clear();
    heldDirection_ = Direction::None;
    repeatElapsed_ = 0.0f;
    repeating_ = false;
    needsNeutral_ = true;
}

} // namespace engine::ui
