#include "engine/render/IWorldLocalShadow.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace engine::render;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct ProbeRenderer final : IWorldLocalShadow {
    int begun = 0, ended = 0;
    void prewarmWorldLocalShadows() override {}
    void clearWorldLocalShadows() override {}
    bool beginWorldLocalShadow(std::uint32_t, const WorldDirectionalShadowView& view) override {
        check(validWorldDirectionalShadowView(view), "Invalid view reached renderer"); ++begun; return true;
    }
    bool endWorldLocalShadow() override { ++ended; return true; }
};
int main() try {
    const WorldRectLight light{{0,4,0},22,{4,0,0},{0,0,.3f},{5,2,8}};
    const auto view = rectLightShadowView(light);
    check(validWorldDirectionalShadowView(view), "Valid light did not produce a projection");
    const auto m = glm::make_mat4(view.viewProjection.data());
    const auto clip = [&](glm::vec3 p) { auto v=m*glm::vec4(p,1); return glm::vec3(v)/v.w; };
    check(std::abs(clip({0,4,0}).z+1)<1e-5f, "Near clip is not the emitting plane");
    check(clip({0,4.1f,0}).z < -1, "Housing behind the plane was not clipped");
    check(std::abs(clip({0,0,0}).x)<1e-6f && std::abs(clip({0,0,0}).y)<1e-6f &&
        std::abs(clip({0,0,0}).z)<1, "Ground receiver missing from frustum");
    check(std::abs(clip({3.99f,4,0}).x)<1, "Luminous plane edge missing");
    auto invalid=light; invalid.range=0;
    check(!rectLightShadowView(invalid).enabled,"Disabled light cast a shadow");
    invalid=light;invalid.radiance={};
    check(!rectLightShadowView(invalid).enabled,"Off light cast a shadow");
    invalid=light;invalid.halfU[0]=std::numeric_limits<float>::quiet_NaN();
    check(!rectLightShadowView(invalid).enabled,"Nonfinite light accepted");
    WorldLocalShadowAtlas atlas;
    check(!atlas.begin(0,view),"Unprepared atlas accepted pass");
    check(atlas.directionalUv()[2]==1,"Disabled atlas changed sun coordinates");
    atlas.enabled=true;
    std::array<std::array<std::uint32_t,2>,8> tiles{};
    for(std::uint32_t i=0;i<8;++i) {
        check(atlas.begin(15-i,view),"Valid slot rejected");
        check(!atlas.begin(0,view),"Nested pass accepted");
        tiles[i]=atlas.offset();
        check(tiles[i][0]>=2048 && tiles[i][0]+1024<=4096 && tiles[i][1]+1024<=4096,"Tile overlaps sun or leaves atlas");
        for(std::uint32_t j=0;j<i;++j) check(tiles[i]!=tiles[j],"Tiles overlap");
        atlas.finish(view);
        check(atlas.uniforms[(15-i)*6+4][0]==1,"Receiver slot not enabled");
    }
    check(!atlas.begin(0,view),"Capacity exceeded");
    atlas.clear();
    check(atlas.enabled && atlas.uniforms==WorldLocalShadowUniforms{} && atlas.tileCount==0,"View clear retained stale shadows");
    check(!atlas.begin(16,view),"Invalid source index accepted");
    check(atlas.begin(0,view),"Reset cannot accept pass");atlas.finish(view);
    check(!atlas.begin(0,view),"Duplicate source used two tiles");
    ProbeRenderer renderer;
    WorldEnvironmentLighting environment; environment.rectLights.fill(light);
    int submitted = 0;
    drawWorldRectShadows(renderer,environment,[&](const auto&) { ++submitted; });
    check(submitted==0,"Disabled environment submitted shadows");
    environment.enabled=true;
    drawWorldRectShadows(renderer,environment,[&](const auto&) { ++submitted; });
    check(submitted==8 && renderer.begun==8 && renderer.ended==8,"Shadow budget/pass lifecycle changed");
    bool exception = false;
    try { drawWorldRectShadows(renderer,environment,[](const auto&) { throw std::runtime_error("draw failed"); }); }
    catch (const std::runtime_error&) { exception = true; }
    check(exception && renderer.begun==renderer.ended,"Draw failure left shadow pass open");
    std::cout << "Local shadow projection, atlas isolation and lifecycle contracts passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
