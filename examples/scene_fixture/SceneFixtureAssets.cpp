#include "SceneFixtureAssets.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace engine::scene_fixture {
namespace {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Authored content tables
// ---------------------------------------------------------------------------

struct VariantRigSpec {
    float shoulderX;
    float shoulderY;
    float elbowLocalY;
};

const VariantRigSpec kRig[2] = {
    {0.42f, 0.88f, -0.29f},
    {0.44f, 0.96f, -0.34f},
};

const VariantRigSpec& rigSpec(SceneVariant variant) {
    return kRig[static_cast<std::size_t>(variant)];
}

struct VariantGeometry {
    glm::vec3 torsoMin;
    glm::vec3 torsoMax;
    glm::vec3 headMin;
    glm::vec3 headMax;
    glm::vec3 leftArmMin;
    glm::vec3 leftArmMax;
    glm::vec3 propCenter;
    glm::vec3 propHalf;
    float propRotationDegrees;
};

const VariantGeometry kGeometry[2] = {
    {{-0.34f, 0.16f, -0.18f},
     {0.34f, 0.96f, 0.18f},
     {-0.22f, 1.02f, -0.20f},
     {0.22f, 1.44f, 0.20f},
     {-0.56f, 0.30f, -0.12f},
     {-0.36f, 0.90f, 0.12f},
     {-1.90f, 0.22f, 0.55f},
     {0.20f, 0.20f, 0.20f},
     35.0f},
    {{-0.42f, 0.14f, -0.20f},
     {0.42f, 1.06f, 0.20f},
     {-0.20f, 1.12f, -0.18f},
     {0.20f, 1.52f, 0.18f},
     {-0.64f, 0.32f, -0.14f},
     {-0.44f, 1.00f, 0.14f},
     {-1.50f, 0.22f, 0.75f},
     {0.22f, 0.22f, 0.22f},
     70.0f},
};

const VariantGeometry& geometry(SceneVariant variant) {
    return kGeometry[static_cast<std::size_t>(variant)];
}

const AuthoredMaterial kMaterials[2][5] = {
    {{"Body", "body.png", {0.78f, 0.86f, 1.00f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.10f, 0.85f},
     {"HeadLamp", nullptr, {0.16f, 0.17f, 0.22f, 1.0f}, {0.90f, 0.60f, 0.20f}, 0.0f, 0.55f},
     {"Floor", "floor.png", {0.92f, 0.96f, 0.90f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.95f},
     {"Wall", "wall.png", {0.62f, 0.70f, 0.78f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.90f},
     {"Prop", "prop.png", {1.00f, 0.82f, 0.62f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.05f, 0.70f}},
    {{"Body", "body.png", {1.00f, 0.62f, 0.42f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.20f, 0.70f},
     {"HeadLamp", nullptr, {0.24f, 0.26f, 0.30f, 1.0f}, {0.90f, 0.60f, 0.20f}, 0.0f, 0.50f},
     {"Floor", "floor.png", {0.85f, 0.90f, 0.95f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.90f},
     {"Wall", "wall.png", {0.45f, 0.55f, 0.70f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.85f},
     {"Prop", "prop.png", {0.95f, 0.95f, 0.65f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.10f, 0.60f}},
};

const AuthoredMap kMaps[2][4] = {
    {{"body.png", {150u, 170u, 200u, 255u}, {80u, 110u, 170u, 255u}},
     {"floor.png", {120u, 130u, 120u, 255u}, {86u, 94u, 88u, 255u}},
     {"wall.png", {96u, 110u, 132u, 255u}, {58u, 66u, 84u, 255u}},
     {"prop.png", {210u, 150u, 70u, 255u}, {150u, 96u, 40u, 255u}}},
    {{"body.png", {200u, 120u, 90u, 255u}, {60u, 90u, 150u, 255u}},
     {"floor.png", {96u, 102u, 110u, 255u}, {60u, 64u, 70u, 255u}},
     {"wall.png", {140u, 120u, 90u, 255u}, {80u, 70u, 60u, 255u}},
     {"prop.png", {120u, 200u, 150u, 255u}, {70u, 120u, 90u, 255u}}},
};

constexpr int kMapSize = 8;

// Fixed node and mesh layout of the authored document.
constexpr int kNodeCharacter = 0;
constexpr int kNodeLeftArm = 1;
constexpr int kNodeShoulder = 2;
constexpr int kNodeElbow = 3;
constexpr int kNodeTorso = 4;
constexpr int kNodeHead = 5;
constexpr int kNodeRightArm = 6;
constexpr int kNodeFloor = 7;
constexpr int kNodeWall = 8;
constexpr int kNodeProp = 9;

constexpr int kMeshTorso = 0;
constexpr int kMeshHead = 1;
constexpr int kMeshLeftArm = 2;
constexpr int kMeshRightArm = 3;
constexpr int kMeshFloor = 4;
constexpr int kMeshWall = 5;
constexpr int kMeshProp = 6;

constexpr int kMaterialBody = 0;
constexpr int kMaterialHeadLamp = 1;
constexpr int kMaterialFloor = 2;
constexpr int kMaterialWall = 3;
constexpr int kMaterialProp = 4;

// ---------------------------------------------------------------------------
// Authored geometry
// ---------------------------------------------------------------------------

struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::u8vec4> joints;
    std::vector<glm::vec4> weights;
    std::vector<std::uint16_t> indices;
};

void addQuad(MeshData& mesh,
             const glm::vec3& a,
             const glm::vec3& b,
             const glm::vec3& c,
             const glm::vec3& d,
             const glm::vec3& normal) {
    const std::uint16_t base = static_cast<std::uint16_t>(mesh.positions.size());
    mesh.positions.push_back(a);
    mesh.positions.push_back(b);
    mesh.positions.push_back(c);
    mesh.positions.push_back(d);
    for (int index = 0; index < 4; ++index) {
        mesh.normals.push_back(normal);
    }
    mesh.uvs.emplace_back(0.0f, 0.0f);
    mesh.uvs.emplace_back(1.0f, 0.0f);
    mesh.uvs.emplace_back(1.0f, 1.0f);
    mesh.uvs.emplace_back(0.0f, 1.0f);
    mesh.indices.push_back(base + 0u);
    mesh.indices.push_back(base + 1u);
    mesh.indices.push_back(base + 2u);
    mesh.indices.push_back(base + 0u);
    mesh.indices.push_back(base + 2u);
    mesh.indices.push_back(base + 3u);
}

MeshData makeBox(const glm::vec3& minimum, const glm::vec3& maximum) {
    MeshData mesh;
    const float x0 = minimum.x;
    const float y0 = minimum.y;
    const float z0 = minimum.z;
    const float x1 = maximum.x;
    const float y1 = maximum.y;
    const float z1 = maximum.z;
    addQuad(mesh, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
            {0.0f, 0.0f, 1.0f});
    addQuad(mesh, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0},
            {0.0f, 0.0f, -1.0f});
    addQuad(mesh, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0},
            {-1.0f, 0.0f, 0.0f});
    addQuad(mesh, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1},
            {1.0f, 0.0f, 0.0f});
    addQuad(mesh, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0},
            {0.0f, 1.0f, 0.0f});
    addQuad(mesh, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
            {0.0f, -1.0f, 0.0f});
    return mesh;
}

MeshData makeQuadMesh(const glm::vec3& a,
                      const glm::vec3& b,
                      const glm::vec3& c,
                      const glm::vec3& d,
                      const glm::vec3& normal) {
    MeshData mesh;
    addQuad(mesh, a, b, c, d, normal);
    return mesh;
}

MeshData makeLimbMesh(SceneVariant variant) {
    const VariantRigSpec& rig = rigSpec(variant);
    const float y0 = rig.shoulderY;
    const float y1 = rig.shoulderY + rig.elbowLocalY;
    const float y2 = y1 + rig.elbowLocalY;
    const float x0 = rig.shoulderX - 0.10f;
    const float x1 = rig.shoulderX + 0.10f;
    MeshData mesh;
    const std::array<float, 3> rows{y0, y1, y2};
    const std::array<std::array<float, 2>, 3> rowWeights{
        std::array<float, 2>{1.0f, 0.0f},
        std::array<float, 2>{0.5f, 0.5f},
        std::array<float, 2>{0.0f, 1.0f}};
    for (std::size_t row = 0u; row < rows.size(); ++row) {
        for (int side = 0; side < 2; ++side) {
            mesh.positions.emplace_back(side == 0 ? x0 : x1, rows[row], 0.0f);
            mesh.normals.emplace_back(0.0f, 0.0f, 1.0f);
            mesh.uvs.emplace_back(side == 0 ? 0.0f : 1.0f,
                                  static_cast<float>(row) * 0.5f);
            mesh.joints.emplace_back(0u, 1u, 0u, 0u);
            mesh.weights.emplace_back(rowWeights[row][0], rowWeights[row][1],
                                      0.0f, 0.0f);
        }
    }
    mesh.indices = {0u, 1u, 3u, 0u, 3u, 2u, 2u, 3u, 5u, 2u, 5u, 4u};
    return mesh;
}

std::vector<glm::vec3> boxCorners(const glm::vec3& minimum,
                                  const glm::vec3& maximum) {
    std::vector<glm::vec3> corners;
    corners.reserve(8u);
    for (int index = 0; index < 8; ++index) {
        corners.emplace_back((index & 1) != 0 ? maximum.x : minimum.x,
                             (index & 2) != 0 ? maximum.y : minimum.y,
                             (index & 4) != 0 ? maximum.z : minimum.z);
    }
    return corners;
}

// The authored floor lies entirely in front of the character (higher z is
// closer to the camera), so it projects below the character and never shares a
// row with the measured character silhouette.
glm::vec3 floorExtentMinimum() { return {-3.2f, 0.0f, 0.10f}; }
glm::vec3 floorExtentMaximum() { return {3.2f, 0.0f, 1.80f}; }
// The authored wall stands behind and to the left of the character, so the
// background remains visible around the character silhouette.
float wallMinX() { return -3.2f; }
float wallMaxX() { return -2.0f; }
float wallZ() { return -1.7f; }
float wallHeight() { return 2.4f; }

glm::mat4 propModelMatrix(SceneVariant variant) {
    const VariantGeometry& content = geometry(variant);
    const glm::mat4 rotation = glm::rotate(
        glm::mat4(1.0f), glm::radians(content.propRotationDegrees),
        glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::translate(glm::mat4(1.0f), content.propCenter) * rotation;
}

// ---------------------------------------------------------------------------
// Binary/JSON authoring helpers
// ---------------------------------------------------------------------------

class BinaryWriter {
public:
    std::size_t alignTo4() {
        while (bytes_.size() % 4u != 0u) {
            bytes_.push_back(0u);
        }
        return bytes_.size();
    }

    void addU16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    }

    void addF32(float value) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) {
            bytes_.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFu));
        }
    }

    void addVec2(const glm::vec2& value) {
        addF32(value.x);
        addF32(value.y);
    }

    void addVec3(const glm::vec3& value) {
        addF32(value.x);
        addF32(value.y);
        addF32(value.z);
    }

    void addVec4(const glm::vec4& value) {
        addF32(value.x);
        addF32(value.y);
        addF32(value.z);
        addF32(value.w);
    }

    void addU8Vec4(const glm::u8vec4& value) {
        bytes_.push_back(value.x);
        bytes_.push_back(value.y);
        bytes_.push_back(value.z);
        bytes_.push_back(value.w);
    }

    void addMat4Columns(const glm::mat4& value) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                addF32(value[column][row]);
            }
        }
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

struct DocumentBuilder {
    BinaryWriter binary;
    json bufferViews = json::array();
    json accessors = json::array();

    std::size_t addView(std::size_t offset, std::size_t length) {
        bufferViews.push_back(
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}});
        return bufferViews.size() - 1u;
    }

    std::size_t addAccessor(std::size_t view,
                            int componentType,
                            std::size_t count,
                            const char* type) {
        accessors.push_back({{"bufferView", view},
                             {"componentType", componentType},
                             {"count", count},
                             {"type", type}});
        return accessors.size() - 1u;
    }
};

struct MeshAccessors {
    std::size_t positions = 0u;
    std::size_t normals = 0u;
    std::size_t uvs = 0u;
    std::size_t joints = 0u;
    std::size_t weights = 0u;
    std::size_t indices = 0u;
};

MeshAccessors addMesh(DocumentBuilder& builder, const MeshData& mesh) {
    MeshAccessors out;
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(-std::numeric_limits<float>::max());
    const std::size_t positionOffset = builder.binary.alignTo4();
    for (const glm::vec3& value : mesh.positions) {
        builder.binary.addVec3(value);
        minimum = glm::min(minimum, value);
        maximum = glm::max(maximum, value);
    }
    const std::size_t positionView = builder.addView(
        positionOffset, builder.binary.bytes().size() - positionOffset);
    out.positions = builder.addAccessor(positionView, 5126,
                                        mesh.positions.size(), "VEC3");
    builder.accessors[out.positions]["min"] = {minimum.x, minimum.y, minimum.z};
    builder.accessors[out.positions]["max"] = {maximum.x, maximum.y, maximum.z};

    const std::size_t normalOffset = builder.binary.alignTo4();
    for (const glm::vec3& value : mesh.normals) {
        builder.binary.addVec3(value);
    }
    const std::size_t normalView = builder.addView(
        normalOffset, builder.binary.bytes().size() - normalOffset);
    out.normals = builder.addAccessor(normalView, 5126, mesh.normals.size(),
                                      "VEC3");

    const std::size_t uvOffset = builder.binary.alignTo4();
    for (const glm::vec2& value : mesh.uvs) {
        builder.binary.addVec2(value);
    }
    const std::size_t uvView = builder.addView(
        uvOffset, builder.binary.bytes().size() - uvOffset);
    out.uvs = builder.addAccessor(uvView, 5126, mesh.uvs.size(), "VEC2");

    if (!mesh.joints.empty()) {
        const std::size_t jointOffset = builder.binary.alignTo4();
        for (const glm::u8vec4& value : mesh.joints) {
            builder.binary.addU8Vec4(value);
        }
        const std::size_t jointView = builder.addView(
            jointOffset, builder.binary.bytes().size() - jointOffset);
        out.joints = builder.addAccessor(jointView, 5121, mesh.joints.size(),
                                         "VEC4");

        const std::size_t weightOffset = builder.binary.alignTo4();
        for (const glm::vec4& value : mesh.weights) {
            builder.binary.addVec4(value);
        }
        const std::size_t weightView = builder.addView(
            weightOffset, builder.binary.bytes().size() - weightOffset);
        out.weights = builder.addAccessor(weightView, 5126,
                                          mesh.weights.size(), "VEC4");
    }

    const std::size_t indexOffset = builder.binary.alignTo4();
    for (const std::uint16_t value : mesh.indices) {
        builder.binary.addU16(value);
    }
    const std::size_t indexView = builder.addView(
        indexOffset, builder.binary.bytes().size() - indexOffset);
    out.indices = builder.addAccessor(indexView, 5123, mesh.indices.size(),
                                      "SCALAR");
    return out;
}

bool writeBytes(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes,
                std::string* outError) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (outError != nullptr) {
            *outError = "could not open scene fixture input for writing: " +
                        path.string();
        }
        return false;
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        if (outError != nullptr) {
            *outError = "could not write scene fixture input: " + path.string();
        }
        return false;
    }
    return true;
}

bool writeText(const std::filesystem::path& path,
               const std::string& text,
               std::string* outError) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (outError != nullptr) {
            *outError = "could not open scene fixture document: " + path.string();
        }
        return false;
    }
    output << text;
    if (!output) {
        if (outError != nullptr) {
            *outError = "could not write scene fixture document: " + path.string();
        }
        return false;
    }
    return true;
}

std::string stagingToken() {
    static std::mt19937_64 generator([] {
        std::random_device device;
        std::uint64_t seed = (static_cast<std::uint64_t>(device()) << 32u) ^
                             static_cast<std::uint64_t>(device());
        seed ^= static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return seed;
    }());
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(generator()));
    return std::string(buffer);
}

bool validPrefix(const std::string& prefix) {
    if (prefix.empty() || prefix.size() > 64u) {
        return false;
    }
    return std::all_of(prefix.begin(), prefix.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' ||
               character == '_';
    });
}

}  // namespace

// ---------------------------------------------------------------------------
// Authored rig (analytic expectations)
// ---------------------------------------------------------------------------

glm::mat4 authoredShoulderLocal(SceneVariant variant) {
    const VariantRigSpec& rig = rigSpec(variant);
    return glm::translate(glm::mat4(1.0f),
                          glm::vec3(rig.shoulderX, rig.shoulderY, 0.0f));
}

glm::mat4 authoredElbowLocal(SceneVariant variant, float poseBlend) {
    const float blend = std::clamp(poseBlend, 0.0f, 1.0f);
    const float angle = glm::radians(AuthoredRig::kPoseAngleDegrees) * blend;
    const glm::mat4 rotation =
        glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::translate(glm::mat4(1.0f),
                          glm::vec3(0.0f, rigSpec(variant).elbowLocalY, 0.0f)) *
           rotation;
}

glm::mat4 authoredShoulderGlobal(SceneVariant variant, float poseBlend) {
    (void)poseBlend;
    return authoredShoulderLocal(variant);
}

glm::mat4 authoredElbowGlobal(SceneVariant variant, float poseBlend) {
    return authoredShoulderGlobal(variant, poseBlend) *
           authoredElbowLocal(variant, poseBlend);
}

glm::mat4 authoredJointInverseBind(SceneVariant variant, int joint) {
    const glm::mat4 global = joint == 0 ? authoredShoulderGlobal(variant, 0.0f)
                                        : authoredElbowGlobal(variant, 0.0f);
    return glm::inverse(global);
}

glm::mat4 authoredJointPalette(SceneVariant variant,
                               int joint,
                               float poseBlend) {
    const glm::mat4 global = joint == 0
                                 ? authoredShoulderGlobal(variant, poseBlend)
                                 : authoredElbowGlobal(variant, poseBlend);
    return global * authoredJointInverseBind(variant, joint);
}

glm::mat4 authoredArmMeshNodeMatrix() {
    return glm::translate(
        glm::mat4(1.0f), glm::vec3(AuthoredRig::kArmMeshNodeOffsetX, 0.0f, 0.0f));
}

const std::vector<AuthoredLimbVertex>& authoredLimbVertices(
    SceneVariant variant) {
    static const std::vector<AuthoredLimbVertex> kVariantA = [] {
        std::vector<AuthoredLimbVertex> vertices;
        const VariantRigSpec& rig = kRig[0];
        const std::array<float, 3> rows{
            rig.shoulderY, rig.shoulderY + rig.elbowLocalY,
            rig.shoulderY + rig.elbowLocalY * 2.0f};
        const std::array<std::array<float, 2>, 3> rowWeights{
            std::array<float, 2>{1.0f, 0.0f},
            std::array<float, 2>{0.5f, 0.5f},
            std::array<float, 2>{0.0f, 1.0f}};
        for (std::size_t row = 0u; row < rows.size(); ++row) {
            for (int side = 0; side < 2; ++side) {
                AuthoredLimbVertex vertex;
                vertex.restPosition = glm::vec3(
                    side == 0 ? rig.shoulderX - 0.10f : rig.shoulderX + 0.10f,
                    rows[row], 0.0f);
                vertex.joints = {0, 1, 0, 0};
                vertex.weights = {rowWeights[row][0], rowWeights[row][1], 0.0f,
                                  0.0f};
                vertex.uv = glm::vec2(side == 0 ? 0.0f : 1.0f,
                                      static_cast<float>(row) * 0.5f);
                vertices.push_back(vertex);
            }
        }
        return vertices;
    }();
    static const std::vector<AuthoredLimbVertex> kVariantB = [] {
        std::vector<AuthoredLimbVertex> vertices;
        const VariantRigSpec& rig = kRig[1];
        const std::array<float, 3> rows{
            rig.shoulderY, rig.shoulderY + rig.elbowLocalY,
            rig.shoulderY + rig.elbowLocalY * 2.0f};
        const std::array<std::array<float, 2>, 3> rowWeights{
            std::array<float, 2>{1.0f, 0.0f},
            std::array<float, 2>{0.5f, 0.5f},
            std::array<float, 2>{0.0f, 1.0f}};
        for (std::size_t row = 0u; row < rows.size(); ++row) {
            for (int side = 0; side < 2; ++side) {
                AuthoredLimbVertex vertex;
                vertex.restPosition = glm::vec3(
                    side == 0 ? rig.shoulderX - 0.12f : rig.shoulderX + 0.12f,
                    rows[row], 0.0f);
                vertex.joints = {0, 1, 0, 0};
                vertex.weights = {rowWeights[row][0], rowWeights[row][1], 0.0f,
                                  0.0f};
                vertex.uv = glm::vec2(side == 0 ? 0.0f : 1.0f,
                                      static_cast<float>(row) * 0.5f);
                vertices.push_back(vertex);
            }
        }
        return vertices;
    }();
    return variant == SceneVariant::A ? kVariantA : kVariantB;
}

const std::vector<std::uint16_t>& authoredLimbIndices() {
    static const std::vector<std::uint16_t> kIndices{
        0u, 1u, 3u, 0u, 3u, 2u, 2u, 3u, 5u, 2u, 5u, 4u};
    return kIndices;
}

glm::vec3 authoredLimbSkinnedPosition(SceneVariant variant,
                                      std::size_t vertex,
                                      float poseBlend) {
    const AuthoredLimbVertex& authored = authoredLimbVertices(variant).at(vertex);
    glm::vec3 position(0.0f);
    for (int influence = 0; influence < 4; ++influence) {
        const float weight = authored.weights[static_cast<std::size_t>(influence)];
        if (weight <= 0.0f) {
            continue;
        }
        const int joint = authored.joints[static_cast<std::size_t>(influence)];
        const glm::vec4 transformed =
            authoredJointPalette(variant, joint, poseBlend) *
            glm::vec4(authored.restPosition, 1.0f);
        position += glm::vec3(transformed) * weight;
    }
    return position;
}

std::vector<glm::vec3> authoredTorsoPoints(SceneVariant variant) {
    return boxCorners(geometry(variant).torsoMin, geometry(variant).torsoMax);
}

std::vector<glm::vec3> authoredHeadPoints(SceneVariant variant) {
    return boxCorners(geometry(variant).headMin, geometry(variant).headMax);
}

std::vector<glm::vec3> authoredLeftArmPoints(SceneVariant variant) {
    return boxCorners(geometry(variant).leftArmMin, geometry(variant).leftArmMax);
}

std::vector<glm::vec3> authoredLimbPoints(SceneVariant variant, float poseBlend) {
    std::vector<glm::vec3> points;
    // The authored limb points are exactly the analytic skinned positions of
    // the authored limb vertices, in authored vertex order.
    const std::size_t count = authoredLimbVertices(variant).size();
    points.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        points.push_back(
            authoredLimbSkinnedPosition(variant, index, poseBlend));
    }
    return points;
}

std::vector<glm::vec3> authoredCharacterPoints(SceneVariant variant,
                                               float poseBlend) {
    std::vector<glm::vec3> points = authoredTorsoPoints(variant);
    const std::vector<glm::vec3> head = authoredHeadPoints(variant);
    points.insert(points.end(), head.begin(), head.end());
    const std::vector<glm::vec3> leftArm = boxCorners(
        geometry(variant).leftArmMin, geometry(variant).leftArmMax);
    points.insert(points.end(), leftArm.begin(), leftArm.end());
    const std::vector<glm::vec3> limb = authoredLimbPoints(variant, poseBlend);
    points.insert(points.end(), limb.begin(), limb.end());
    return points;
}

std::vector<glm::vec3> authoredFloorPoints() {
    const glm::vec3 minimum = floorExtentMinimum();
    const glm::vec3 maximum = floorExtentMaximum();
    return {{minimum.x, minimum.y, minimum.z},
            {maximum.x, minimum.y, minimum.z},
            {maximum.x, minimum.y, maximum.z},
            {minimum.x, minimum.y, maximum.z}};
}

std::vector<glm::vec3> authoredWallPoints(SceneVariant variant) {
    (void)variant;
    return {{wallMinX(), 0.0f, wallZ()},
            {wallMaxX(), 0.0f, wallZ()},
            {wallMaxX(), wallHeight(), wallZ()},
            {wallMinX(), wallHeight(), wallZ()}};
}

const AuthoredProbe& authoredProbe() {
    static const AuthoredProbe kProbe{
        {1.30f, 1.60f, 0.30f},
        {0.36f, 0.22f},
        {0.50f, 0.75f, 0.25f, 1.0f},
        {"probe.png", {200u, 120u, 40u, 255u}, {60u, 160u, 220u, 255u}},
        2};
    return kProbe;
}

std::vector<glm::vec3> authoredProbePoints() {
    const AuthoredProbe& probe = authoredProbe();
    const glm::vec3& center = probe.center;
    const glm::vec2 half = probe.halfExtent;
    return {{center.x - half.x, center.y - half.y, center.z},
            {center.x + half.x, center.y - half.y, center.z},
            {center.x + half.x, center.y + half.y, center.z},
            {center.x - half.x, center.y + half.y, center.z}};
}

std::vector<std::uint8_t> authoredProbePixels() {
    return authoredMapPixels(authoredProbe().map);
}

std::array<std::uint8_t, 3> authoredProbeExpectedPixel(std::uint8_t red,
                                                       std::uint8_t green,
                                                       std::uint8_t blue) {
    const AuthoredProbe& probe = authoredProbe();
    const auto srgbToLinear = [](std::uint8_t byte) {
        const float value = static_cast<float>(byte) / 255.0f;
        return value <= 0.04045f ? value / 12.92f
                                 : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    const auto linearToSrgbByte = [](float value) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        const float encoded =
            clamped <= 0.0031308f
                ? clamped * 12.92f
                : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(
            std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
    };
    const float source[3] = {srgbToLinear(red), srgbToLinear(green),
                             srgbToLinear(blue)};
    return {linearToSrgbByte(source[0] * probe.baseColorFactor[0]),
            linearToSrgbByte(source[1] * probe.baseColorFactor[1]),
            linearToSrgbByte(source[2] * probe.baseColorFactor[2])};
}

const char* sceneVariantName(SceneVariant variant) {
    return variant == SceneVariant::A ? "a" : "b";
}

const AuthoredMaterial& authoredMaterial(SceneVariant variant,
                                         const std::string& name) {
    const auto& table = kMaterials[static_cast<std::size_t>(variant)];
    for (const AuthoredMaterial& material : table) {
        if (name == material.name) {
            return material;
        }
    }
    return table[0];
}

const AuthoredMap& authoredMap(SceneVariant variant, const std::string& name) {
    const auto& table = kMaps[static_cast<std::size_t>(variant)];
    for (const AuthoredMap& map : table) {
        if (name == map.fileName) {
            return map;
        }
    }
    return table[0];
}

std::vector<std::uint8_t> authoredMapPixels(const AuthoredMap& map) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(kMapSize) * static_cast<std::size_t>(kMapSize) *
            4u,
        255u);
    for (int y = 0; y < kMapSize; ++y) {
        for (int x = 0; x < kMapSize; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * kMapSize +
                 static_cast<std::size_t>(x)) *
                4u;
            const std::array<std::uint8_t, 4>& half =
                x * 2 < kMapSize ? map.left : map.right;
            for (std::size_t channel = 0u; channel < 4u; ++channel) {
                pixels[offset + channel] = half[channel];
            }
        }
    }
    return pixels;
}

// ---------------------------------------------------------------------------
// Staging directory
// ---------------------------------------------------------------------------

SceneStagingDirectory::SceneStagingDirectory(std::string prefix) {
    if (!validPrefix(prefix)) {
        error_ = "staging directory prefix must be one safe path component";
        return;
    }
    const std::filesystem::path root = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::filesystem::path candidate =
            root / (prefix + "-" + stagingToken());
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            path_ = candidate;
            return;
        }
        if (error && error != std::errc::file_exists) {
            error_ = "could not create a staging directory: " + error.message();
            return;
        }
    }
    error_ = "no free staging directory name was available";
}

SceneStagingDirectory::~SceneStagingDirectory() {
    release();
}

SceneStagingDirectory::SceneStagingDirectory(SceneStagingDirectory&& other) noexcept
    : path_(std::move(other.path_)), error_(std::move(other.error_)) {
    other.path_.clear();
}

SceneStagingDirectory& SceneStagingDirectory::operator=(
    SceneStagingDirectory&& other) noexcept {
    if (this != &other) {
        release();
        path_ = std::move(other.path_);
        error_ = std::move(other.error_);
        other.path_.clear();
    }
    return *this;
}

void SceneStagingDirectory::release() noexcept {
    if (path_.empty()) {
        return;
    }
    // Removes only the directory this instance created; never a glob.
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    path_.clear();
}

std::filesystem::path SceneStagingDirectory::file(
    const std::string& name) const {
    if (path_.empty() || name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        return {};
    }
    return path_ / name;
}

bool SceneStagingDirectory::contains(const std::filesystem::path& candidate) const {
    if (path_.empty() || candidate.empty()) {
        return false;
    }
    const std::filesystem::path owned = path_.lexically_normal();
    const std::filesystem::path normal = candidate.lexically_normal();
    auto ownedPart = owned.begin();
    auto candidatePart = normal.begin();
    while (ownedPart != owned.end()) {
        if (candidatePart == normal.end() || *ownedPart != *candidatePart) {
            return false;
        }
        ++ownedPart;
        ++candidatePart;
    }
    return true;
}

const char* sceneDocumentFileName() {
    return "scene_fixture.gltf";
}

// ---------------------------------------------------------------------------
// Authored document
// ---------------------------------------------------------------------------

bool stageSceneDocument(SceneVariant variant,
                        bool emissionOff,
                        const std::filesystem::path& directory,
                        std::string* outError) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        if (outError != nullptr) {
            *outError = "staging directory does not exist: " + directory.string();
        }
        return false;
    }

    const VariantGeometry& content = geometry(variant);
    const VariantRigSpec& rig = rigSpec(variant);

    const std::array<MeshData, 7> meshes{
        makeBox(content.torsoMin, content.torsoMax),
        makeBox(content.headMin, content.headMax),
        makeBox(content.leftArmMin, content.leftArmMax),
        makeLimbMesh(variant),
        makeQuadMesh({floorExtentMinimum().x, 0.0f, floorExtentMinimum().z},
                     {floorExtentMinimum().x, 0.0f, floorExtentMaximum().z},
                     {floorExtentMaximum().x, 0.0f, floorExtentMaximum().z},
                     {floorExtentMaximum().x, 0.0f, floorExtentMinimum().z},
                     {0.0f, 1.0f, 0.0f}),
        makeQuadMesh({wallMinX(), 0.0f, wallZ()},
                     {wallMaxX(), 0.0f, wallZ()},
                     {wallMaxX(), wallHeight(), wallZ()},
                     {wallMinX(), wallHeight(), wallZ()},
                     {0.0f, 0.0f, 1.0f}),
        makeBox(content.propCenter - content.propHalf,
                content.propCenter + content.propHalf),
    };

    DocumentBuilder builder;
    std::array<MeshAccessors, 7> accessors{};
    for (std::size_t index = 0u; index < meshes.size(); ++index) {
        accessors[index] = addMesh(builder, meshes[index]);
    }

    // Inverse bind matrices for the two authored joints.
    const std::size_t inverseBindOffset = builder.binary.alignTo4();
    builder.binary.addMat4Columns(authoredJointInverseBind(variant, 0));
    builder.binary.addMat4Columns(authoredJointInverseBind(variant, 1));
    const std::size_t inverseBindView = builder.addView(
        inverseBindOffset,
        builder.binary.bytes().size() - inverseBindOffset);
    const std::size_t inverseBindAccessor =
        builder.addAccessor(inverseBindView, 5126, 2u, "MAT4");

    // Animation: identity at t=0, authored bend at t=1.
    const std::size_t timeOffset = builder.binary.alignTo4();
    builder.binary.addF32(0.0f);
    builder.binary.addF32(AuthoredRig::kPoseTimeSec);
    const std::size_t timeView = builder.addView(
        timeOffset, builder.binary.bytes().size() - timeOffset);
    const std::size_t timeAccessor =
        builder.addAccessor(timeView, 5126, 2u, "SCALAR");
    builder.accessors[timeAccessor]["min"] = {0.0f};
    builder.accessors[timeAccessor]["max"] = {AuthoredRig::kPoseTimeSec};

    const std::size_t rotationOffset = builder.binary.alignTo4();
    builder.binary.addVec4(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const float halfAngle = glm::radians(AuthoredRig::kPoseAngleDegrees) * 0.5f;
    builder.binary.addVec4(
        glm::vec4(0.0f, 0.0f, std::sin(halfAngle), std::cos(halfAngle)));
    const std::size_t rotationView = builder.addView(
        rotationOffset, builder.binary.bytes().size() - rotationOffset);
    const std::size_t rotationAccessor =
        builder.addAccessor(rotationView, 5126, 2u, "VEC4");

    json document;
    document["asset"] = {{"version", "2.0"},
                         {"generator", "phlosion-scene-fixture"}};
    document["buffers"] = json::array({json{
        {"uri", "scene_fixture.bin"},
        {"byteLength", builder.binary.bytes().size()}}});
    document["bufferViews"] = builder.bufferViews;
    document["accessors"] = builder.accessors;

    // PNG maps: one image per authored material map.
    json images = json::array();
    json textures = json::array();
    std::vector<std::string> mapFiles;
    const auto& materialTable = kMaterials[static_cast<std::size_t>(variant)];
    for (const AuthoredMaterial& material : materialTable) {
        if (material.image == nullptr) {
            continue;
        }
        const AuthoredMap& map = authoredMap(variant, material.image);
        const std::vector<std::uint8_t> pixels = authoredMapPixels(map);
        const std::filesystem::path mapPath = directory / map.fileName;
        const int wrote = stbi_write_png(mapPath.string().c_str(), kMapSize,
                                         kMapSize, 4, pixels.data(),
                                         kMapSize * 4);
        if (wrote == 0) {
            if (outError != nullptr) {
                *outError = std::string("the PNG writer failed for map ") +
                            map.fileName;
            }
            return false;
        }
        images.push_back({{"name", map.fileName}, {"uri", map.fileName}});
        textures.push_back(
            {{"source", images.size() - 1u}, {"sampler", 0}});
        mapFiles.emplace_back(map.fileName);
    }
    document["samplers"] = json::array({json{
        {"magFilter", 9729},
        {"wrapS", 10497}, {"wrapT", 10497}}});
    document["images"] = images;
    document["textures"] = textures;

    const auto textureIndexFor = [&mapFiles](const char* fileName) -> std::size_t {
        for (std::size_t index = 0u; index < mapFiles.size(); ++index) {
            if (mapFiles[index] == fileName) {
                return index;
            }
        }
        return 0u;
    };

    json materials = json::array();
    for (const AuthoredMaterial& material : materialTable) {
        json entry;
        entry["name"] = material.name;
        json pbr;
        if (material.image != nullptr) {
            pbr["baseColorTexture"] = {{"index", textureIndexFor(material.image)}};
        }
        pbr["baseColorFactor"] = {material.baseColorFactor[0],
                                  material.baseColorFactor[1],
                                  material.baseColorFactor[2],
                                  material.baseColorFactor[3]};
        pbr["metallicFactor"] = material.metallicFactor;
        pbr["roughnessFactor"] = material.roughnessFactor;
        entry["pbrMetallicRoughness"] = pbr;
        const float emissiveScale = emissionOff ? 0.0f : 1.0f;
        entry["emissiveFactor"] = {material.emissiveFactor[0] * emissiveScale,
                                   material.emissiveFactor[1] * emissiveScale,
                                   material.emissiveFactor[2] * emissiveScale};
        materials.push_back(std::move(entry));
    }
    document["materials"] = materials;

    struct MeshSlot {
        int mesh;
        int material;
        bool skinned;
    };
    const std::array<MeshSlot, 7> meshSlots{{
        {kMeshTorso, kMaterialBody, false},
        {kMeshHead, kMaterialHeadLamp, false},
        {kMeshLeftArm, kMaterialBody, false},
        {kMeshRightArm, kMaterialBody, true},
        {kMeshFloor, kMaterialFloor, false},
        {kMeshWall, kMaterialWall, false},
        {kMeshProp, kMaterialProp, false},
    }};
    json meshArray = json::array();
    for (const MeshSlot& slot : meshSlots) {
        const MeshAccessors& accessor =
            accessors[static_cast<std::size_t>(slot.mesh)];
        json attributes;
        attributes["POSITION"] = accessor.positions;
        attributes["NORMAL"] = accessor.normals;
        attributes["TEXCOORD_0"] = accessor.uvs;
        if (slot.skinned) {
            attributes["JOINTS_0"] = accessor.joints;
            attributes["WEIGHTS_0"] = accessor.weights;
        }
        json primitive;
        primitive["attributes"] = attributes;
        primitive["indices"] = accessor.indices;
        primitive["material"] = slot.material;
        meshArray.push_back(
            {{"name", "Mesh" + std::to_string(slot.mesh)},
             {"primitives", json::array({primitive})}});
    }
    document["meshes"] = meshArray;

    document["skins"] = json::array({json{
        {"name", "ArmSkin"},
        {"joints", {kNodeShoulder, kNodeElbow}},
        {"inverseBindMatrices", inverseBindAccessor}}});

    document["animations"] = json::array({json{
        {"name", "ArmBend"},
        {"samplers",
         json::array({json{{"input", timeAccessor},
                           {"interpolation", "LINEAR"},
                           {"output", rotationAccessor}}})},
        {"channels",
         json::array({json{{"sampler", 0},
                           {"target",
                            {{"node", kNodeElbow}, {"path", "rotation"}}}}})}}});

    const float propHalfAngle =
        glm::radians(content.propRotationDegrees) * 0.5f;
    document["nodes"] = json::array(
        {json{{"name", "Character"},
              {"children",
               {kNodeLeftArm, kNodeShoulder, kNodeTorso, kNodeHead,
                kNodeRightArm}}},
         json{{"name", "LeftArm"}, {"mesh", kMeshLeftArm}},
         json{{"name", "Shoulder"},
              {"translation", {rig.shoulderX, rig.shoulderY, 0.0}},
              {"children", {kNodeElbow}}},
         json{{"name", "Elbow"},
              {"translation", {0.0, rig.elbowLocalY, 0.0}}},
         json{{"name", "Torso"}, {"mesh", kMeshTorso}},
         json{{"name", "Head"}, {"mesh", kMeshHead}},
         json{{"name", "RightArm"},
              {"mesh", kMeshRightArm},
              {"skin", 0},
              {"translation", {AuthoredRig::kArmMeshNodeOffsetX, 0.0, 0.0}}},
         json{{"name", "Floor"}, {"mesh", kMeshFloor}},
         json{{"name", "Wall"}, {"mesh", kMeshWall}},
         json{{"name", "Prop"},
              {"mesh", kMeshProp},
              {"translation",
               {content.propCenter.x, content.propCenter.y, content.propCenter.z}},
              {"rotation",
               {0.0, std::sin(propHalfAngle), 0.0, std::cos(propHalfAngle)}}}});
    document["scenes"] = json::array({json{
        {"name", "Scene"},
        {"nodes", {kNodeCharacter, kNodeFloor, kNodeWall, kNodeProp}}}});
    document["scene"] = 0;

    if (!writeBytes(directory / "scene_fixture.bin", builder.binary.bytes(),
                    outError)) {
        return false;
    }
    if (!writeText(directory / sceneDocumentFileName(), document.dump(2),
                   outError)) {
        return false;
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

bool stageMalformedDocument(const std::filesystem::path& directory,
                            std::string* outError) {
    const std::filesystem::path document = directory / sceneDocumentFileName();
    // A deliberately malformed replacement: the production loader must reject
    // it and leave the previous published scene untouched.
    const std::string malformed =
        "{ \"asset\": { \"version\": \"2.0\" }, \"meshes\": [ { } }";
    return writeText(document, malformed, outError);
}

bool stageProbeDocument(const std::filesystem::path& directory,
                        std::string* outError) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        if (outError != nullptr) {
            *outError = "probe staging directory does not exist: " +
                        directory.string();
        }
        return false;
    }
    const AuthoredProbe& probe = authoredProbe();
    const glm::vec2 half = probe.halfExtent;
    MeshData mesh = makeQuadMesh({-half.x, -half.y, 0.0f},
                                 {half.x, -half.y, 0.0f},
                                 {half.x, half.y, 0.0f},
                                 {-half.x, half.y, 0.0f},
                                 {0.0f, 0.0f, 1.0f});
    DocumentBuilder builder;
    const MeshAccessors accessors = addMesh(builder, mesh);

    const std::vector<std::uint8_t> pixels = authoredProbePixels();
    const std::filesystem::path mapPath = directory / probe.map.fileName;
    const int wrote = stbi_write_png(mapPath.string().c_str(), kMapSize, kMapSize,
                                     4, pixels.data(), kMapSize * 4);
    if (wrote == 0) {
        if (outError != nullptr) {
            *outError = "the PNG writer failed for the albedo inspection map";
        }
        return false;
    }

    json document;
    document["asset"] = {{"version", "2.0"},
                         {"generator", "phlosion-scene-fixture-probe"}};
    document["buffers"] = json::array({json{
        {"uri", "probe_fixture.bin"},
        {"byteLength", builder.binary.bytes().size()}}});
    document["bufferViews"] = builder.bufferViews;
    document["accessors"] = builder.accessors;
    document["samplers"] = json::array({json{
        {"magFilter", 9729},
        {"wrapS", 10497}, {"wrapT", 10497}}});
    document["images"] = json::array(
        {json{{"name", probe.map.fileName}, {"uri", probe.map.fileName}}});
    document["textures"] =
        json::array({json{{"source", 0}, {"sampler", 0}}});
    document["materials"] = json::array({json{
        {"name", "ProbeAlbedo"},
        {"pbrMetallicRoughness",
         {{"baseColorTexture", {{"index", 0}}},
          {"baseColorFactor",
           {probe.baseColorFactor[0], probe.baseColorFactor[1],
            probe.baseColorFactor[2], probe.baseColorFactor[3]}},
          {"metallicFactor", 0.0},
          {"roughnessFactor", 1.0}}}}});
    document["meshes"] = json::array({json{
        {"name", "ProbeQuad"},
        {"primitives", json::array({json{
             {"attributes",
              {{"POSITION", accessors.positions},
               {"NORMAL", accessors.normals},
               {"TEXCOORD_0", accessors.uvs}}},
             {"indices", accessors.indices},
             {"material", 0}}})}}});
    document["nodes"] = json::array({json{
        {"name", "Probe"},
        {"mesh", 0},
        {"translation", {probe.center.x, probe.center.y, probe.center.z}}}});
    document["scenes"] =
        json::array({json{{"name", "ProbeScene"}, {"nodes", {0}}}});
    document["scene"] = 0;

    if (!writeBytes(directory / "probe_fixture.bin", builder.binary.bytes(),
                    outError)) {
        return false;
    }
    if (!writeText(directory / "probe_fixture.gltf", document.dump(2),
                   outError)) {
        return false;
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

}  // namespace engine::scene_fixture
