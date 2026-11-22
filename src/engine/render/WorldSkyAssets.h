#pragma once
#include <span>
namespace engine::render {
// Authored procedural defaults packed for the ordinary material texture binding.
void prepareCelestialAtlas(std::span<unsigned char> atlas);
}
