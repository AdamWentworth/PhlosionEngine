// UIManager.h
#pragma once

#include <string>
#include "engine/ui/Rect.h"

class Shader;

namespace UIManager {
    // Initialize UI resources (idempotent, thread-safe).
    // Can be called again after shutdown().
    void init();

    // Returns the shader used for drawing cards (owned by UIManager).
    // Returns nullptr if init() hasn't been called or shutdown() has run.
    Shader* getCardShader();

    // Draw a card rect with optional image (stub).
    void drawCard(const ui::Rect& rect, const std::string& imagePath, Shader* shader);

    // Tear down UI resources created in init().
    // Safe to call multiple times.
    void shutdown();
}
