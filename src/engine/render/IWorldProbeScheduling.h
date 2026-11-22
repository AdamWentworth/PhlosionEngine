#pragma once
#include <cstdint>
#include <span>
namespace engine::render {
class IWorldProbeScheduling {
public:
    virtual ~IWorldProbeScheduling()=default;
    // Called once before admission. Retired slot storage is reused on the same
    // ordered graphics queue; resources/descriptors stay alive until normal retirement.
    virtual bool reserveWorldProbes(std::span<const std::uint64_t>,float blendDelta)=0;
    virtual bool worldProbePublished(std::uint64_t) const=0;
    virtual bool worldProbeBlending(std::uint64_t) const=0;
    // Executes bounded dependent mip/filter work; returns true only after publication.
    virtual bool filterWorldProbe(std::uint64_t,unsigned jobs)=0;
};
}
