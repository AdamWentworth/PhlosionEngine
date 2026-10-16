#pragma once
#include <algorithm>
#include <cmath>

namespace engine::render {
// Optional per-view optical bloom. Reset at the start of each world color pass;
// editor chrome and overlays composed afterward are unaffected.
class IWorldBloom {
public:
    virtual ~IWorldBloom() = default;
    void setWorldBloomStrength(float strength) {
        worldBloomStrength_ = std::isfinite(strength) ? std::clamp(strength,0.f,1.f) : 0.f;
    }
protected:
    float worldBloomStrength_ = 0;
};
}
