// src/engine/utils/ShaderCache.cpp
#include "engine/utils/ShaderCache.h"

std::shared_ptr<Shader> ShaderCache::get(const std::string& vert, const std::string& frag) {
    const std::string key = makeKey(vert, frag);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    auto shader = std::make_shared<Shader>(vert.c_str(), frag.c_str());
    cache.emplace(key, shader);
    return shader;
}

void ShaderCache::clear() {
    cache.clear();
}
