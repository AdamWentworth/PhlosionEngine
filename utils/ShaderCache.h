// src/engine/utils/ShaderCache.h
#pragma once

#include "engine/utils/Shader.h"
#include <memory>
#include <string>
#include <unordered_map>

// Engine-owned shader program cache (no globals).
class ShaderCache {
public:
    std::shared_ptr<Shader> get(const std::string& vert, const std::string& frag);
    void clear();

private:
    std::unordered_map<std::string, std::shared_ptr<Shader>> cache;

    static std::string makeKey(const std::string& v, const std::string& f) {
        return v + "::" + f;
    }
};
