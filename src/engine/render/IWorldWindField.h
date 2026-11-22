#pragma once
#include "engine/render/WorldWindController.h"
#include <vector>
#include <glm/glm.hpp>
namespace engine::render {
struct WorldWindImpulse {
    glm::vec3 a{},b{},anchor{};
    float radius=1,intensity=1,age=0,lifetime=1;
    bool persistent=false;
};
struct WorldWindShelter {glm::vec3 low{},high{};float transmission=.15f,wake=8;};
struct WorldWindFieldInput {
    std::uint64_t owner=0;
    WorldWindSnapshot wind;
    glm::vec3 camera{},forward{0,0,-1};
    std::vector<WorldWindImpulse> impulses;
    std::vector<WorldWindShelter> shelters;
    unsigned quality=1;
};
inline constexpr unsigned windFieldCells=64*64*64,windFieldHeader=64;
inline constexpr unsigned windFieldWords=windFieldHeader+windFieldCells*3;
inline constexpr unsigned windShapeRows=64,windClusterRows=windShapeRows+510*4;
struct WorldWindFieldPlan {
    glm::vec3 center{},snapped{};
    std::vector<std::array<float,4>> input;
    unsigned shapes=0,rejected=0,overflow=0;
};
struct WorldWindFieldHistory {
    std::uint64_t generation=0,updates=0;
    double seconds=-1;
    glm::vec3 snapped{};
    std::array<std::array<float,4>,64> header{};
};
struct WorldWindFieldUpdate {
    WorldWindFieldPlan plan;
    std::vector<std::array<std::array<float,4>,2>> steps;
    bool reset=false;
};
WorldWindFieldUpdate updateWorldWindField(const WorldWindFieldInput&,const WorldWindFieldHistory&);
void commitWorldWindField(WorldWindFieldHistory&,const WorldWindFieldInput&,const WorldWindFieldUpdate&);
WorldWindFieldPlan planWorldWindField(const WorldWindFieldInput&);
struct WorldWindFieldDiagnostic {
    unsigned steps=0,shapes=0,overflow=0;
    double seconds=0;
    std::uint64_t bytes=0,owner=0,updates=0;
    std::vector<std::array<float,4>> field;
};
class IWorldWindField {
public:
    virtual ~IWorldWindField()=default;
    // Once per resource scope per backend frame; that scope's views share the result.
    // Transitions may render outgoing and incoming scopes in the same frame.
    virtual void prepareWorldWindField(const WorldWindFieldInput&)=0;
    virtual void clearWorldWindField()=0;
    virtual bool captureWorldWindField()=0;
    virtual WorldWindFieldDiagnostic readWorldWindField()=0;
};
}
