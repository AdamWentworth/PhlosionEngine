// UIManager.cpp

#include "engine/ui/UIManager.h"

#include <memory>
#include <mutex>

#include "engine/utils/Shader.h"
#include "engine/ui/Card.h" // for Card::shutdownSharedGL()
#include "engine/utils/Log.h"

namespace {
    std::mutex s_mutex;
    std::unique_ptr<Shader> s_cardShader;
    bool s_initialized = false;
}

void UIManager::init() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized) return;

    // Create shader. Any failures should throw from Shader ctor, surfacing early.
    // NOTE: this assumes init() is called on the thread that owns a valid GL context.
    s_cardShader = std::make_unique<Shader>(
        "assets/shaders/ui/card.vert",
        "assets/shaders/ui/card.frag"
    );

    s_initialized = true;
    LOG_INFO_T("UI", "Card shader initialized.");
}

Shader* UIManager::getCardShader() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_initialized ? s_cardShader.get() : nullptr;
}

void UIManager::drawCard(const ui::Rect& /*rect*/, const std::string& /*imagePath*/, Shader* /*shader*/) {
    // Stub: implement textured quad draw as needed.
}

void UIManager::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);

    // Cleanup shared Card GL resources while the GL context is still alive.
    // Safe to call multiple times if Card implementation is idempotent.
    Card::shutdownSharedGL();

    if (s_cardShader) {
        s_cardShader.reset();
        LOG_INFO_T("UI", "Card shader destroyed.");
    }
    s_initialized = false;
}

