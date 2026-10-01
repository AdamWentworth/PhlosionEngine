#pragma once

// Optional composition boundary: retain scene color, discard the active world's
// depth. Subsequent opaque actors establish depth for effects in the new layer.
class IWorldDepthControl {
public:
    virtual ~IWorldDepthControl() = default;
    virtual bool clearWorldDepth() = 0;
};
