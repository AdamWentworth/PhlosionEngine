#pragma once
#include <string_view>

// Optional composition boundary: retain scene color, discard the active world's
// depth. Subsequent opaque actors establish depth for effects in the new layer.
class IWorldDepthControl {
public:
    virtual ~IWorldDepthControl() = default;
    virtual bool clearWorldDepth() = 0;
    // Copy the active world's depth before transparent effects. The copy remains
    // immutable until the next capture; bind it with WorldTextureData::lightProjectionKey
    // set to snapshotTextureKey (no CPU pixels), preserving the shared material ABI.
    // Native normalized depth; fetch by pixel coordinate.
    virtual bool captureWorldDepthSnapshot() { return false; }
    static constexpr const char* snapshotTextureKey = "phlosion://world/depth-snapshot";
    static bool isSnapshotTexture(const char* key) {
        return key && std::string_view(key) == snapshotTextureKey;
    }
};
