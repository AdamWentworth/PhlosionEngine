#include "engine/vfx/portable/PortableEffect.h"

#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace vfx::portable {
namespace {

glm::vec3 vec3(const vfxlibrary::Vec3& v) { return {v[0], v[1], v[2]}; }

IRenderBackend::WorldMeshVertex vertex(const glm::vec3& point,
                                        const vfxlibrary::Vec2& uv,
                                        const vfxlibrary::Rgba& color) {
    IRenderBackend::WorldMeshVertex result;
    result.x = point.x;
    result.y = point.y;
    result.z = point.z;
    result.u = uv[0];
    result.v = uv[1];
    result.r = color[0];
    result.g = color[1];
    result.b = color[2];
    result.a = color[3];
    return result;
}

glm::vec3 transformPoint(const glm::mat4& world, const vfxlibrary::Vec3& point) {
    return glm::vec3(world * glm::vec4(vec3(point), 1.0f));
}

vfxlibrary::Rgba normalized(const vfxlibrary::Rgba8& value) {
    return {value[0] / 255.0f, value[1] / 255.0f,
            value[2] / 255.0f, value[3] / 255.0f};
}

int wrap(const std::string& value) {
    if (value == "repeat") return 10497;
    if (value == "mirror") return 33648;
    return 33071;
}

std::uint8_t materialMode(const std::string& shader) {
    if (shader == "intensity") return 250;
    if (shader == "intensity-separate") return 251;
    if (shader == "intensity-tev8") return 252;
    if (shader == "intensity-separate-tev8") return 253;
    if (shader == "rgba") return 255;
    throw std::runtime_error("Portable effect uses unsupported card shader");
}

void lowColor(IRenderBackend::WorldMeshVertex& vertex, const vfxlibrary::Rgba& low) {
    vertex.sourceUv1U = low[0];
    vertex.sourceUv1V = low[1];
    vertex.sourceUv2U = low[2];
}

glm::vec2 projectedDirection(const glm::vec3& center, const glm::vec3& direction,
    const glm::vec3& right, const glm::vec3& up, const float* viewProjection) {
    glm::vec2 result{glm::dot(direction, right), glm::dot(direction, up)};
    if (viewProjection) {
        const auto vp = glm::make_mat4(viewProjection);
        const auto p = vp * glm::vec4(center, 1);
        const auto d = vp * glm::vec4(direction, 0);
        const float horizontal = std::abs((vp * glm::vec4(right, 0)).x);
        const float vertical = std::abs((vp * glm::vec4(up, 0)).y);
        if (std::abs(p.w) > 1e-5f && horizontal > 1e-5f && vertical > 1e-5f)
            result = { (d.x * p.w - p.x * d.w) / horizontal,
                       (d.y * p.w - p.y * d.w) / vertical };
    }
    const float length = glm::length(result);
    return length > 1e-6f ? result / length : glm::vec2(0, 1);
}

Draw gradientLine(const glm::vec3& start, const glm::vec3& end,
    const vfxlibrary::Rgba& low, const vfxlibrary::Rgba& high, bool depth,
    const glm::vec3& right, const glm::vec3& up,
    const float* viewProjection, int height) {
    Draw draw;
    draw.depthTest = depth;
    draw.blend = vfxlibrary::Blend::Additive;
    if (glm::length(end - start) < 1e-6f) return draw;
    const auto direction = projectedDirection((start + end) * .5f, end - start,
                                               right, up, viewProjection);
    const auto side = right * -direction.y + up * direction.x;
    const auto vp = viewProjection ? glm::make_mat4(viewProjection) : glm::mat4(1);
    const float projectionY = std::abs((vp * glm::vec4(up, 0)).y);
    const auto halfWidth = [&](const glm::vec3& point) {
        return viewProjection && height > 0 && projectionY > 1e-5f
            ? std::abs((vp * glm::vec4(point, 1)).w) / (height * projectionY)
            : .001f;
    };
    // One pixel core, with half-pixel transparent fringes for smooth lines.
    for (float offset : {-1.0f, -.5f, .5f, 1.0f}) {
        auto a = low, b = high;
        if (std::abs(offset) > .5f) { a[3] = 0; b[3] = 0; }
        draw.vertices.push_back(vertex(start + side * (offset * 2 * halfWidth(start)), {0, 0}, a));
        draw.vertices.push_back(vertex(end + side * (offset * 2 * halfWidth(end)), {0, 0}, b));
    }
    for (std::uint32_t row = 0; row < 3; ++row) {
        const auto i = row * 2;
        draw.indices.insert(draw.indices.end(), {i, i+1, i+2, i+2, i+1, i+3});
    }
    return draw;
}

void appendQuad(Draw& draw, const std::array<glm::vec3, 4>& corners,
                const std::array<vfxlibrary::Vec2, 4>& uvs,
                const vfxlibrary::Rgba& color) {
    for (std::size_t i = 0; i < corners.size(); ++i)
        draw.vertices.push_back(vertex(corners[i], uvs[i], color));
    draw.indices = {0, 2, 1, 0, 3, 2};
}

std::array<glm::vec3, 4> planeCorners(const glm::vec3& center,
    const glm::vec3& right, const glm::vec3& up,
    float width, float height, float rotationDegrees) {
    const float angle = glm::radians(rotationDegrees);
    const float c = std::cos(angle), s = std::sin(angle);
    const std::array<glm::vec2, 4> local{{
        {-width * .5f, -height * .5f}, {width * .5f, -height * .5f},
        {width * .5f, height * .5f}, {-width * .5f, height * .5f}}};
    std::array<glm::vec3, 4> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = center + right * (local[i].x * c - local[i].y * s) +
                    up * (local[i].x * s + local[i].y * c);
    return result;
}

constexpr std::array<vfxlibrary::Vec2, 4> defaultUvs{{
    {0, 1}, {1, 1}, {1, 0}, {0, 0}}};

} // namespace

PortableEffect::PortableEffect(const std::filesystem::path& manifest)
    : effect_(vfxlibrary::load(manifest)) {
    for (const auto& style : effect_.cardStyles) {
        materialMode(style.shader);
        if (style.cameraRibbon || style.customVertices || style.cameraPosition || style.nearest ||
            (!style.longAxis.empty() && style.longAxis != "native-full-velocity-swept-card") ||
            (style.blend != "alpha_blend" && style.blend != "additive" && style.blend != "additive_alpha"))
            throw std::runtime_error("Portable effect uses unsupported card-stream style");
    }
    for (const auto& span : effect_.cardSpans) {
        for (const auto& packet : span.packets)
            if (packet.mode != 0 && packet.mode != 1 && packet.mode != 3 && packet.mode != 4)
                throw std::runtime_error("Portable effect uses unsupported card-stream packet");
        for (const auto& card : span.first)
            if (card.flags & ~7)
                throw std::runtime_error("Portable effect uses unsupported per-card vertex data");
    }
    for (const auto& mesh : effect_.meshes) {
        if (mesh.depthWrite || (mesh.cull != "back" && mesh.cull != "none") ||
            (mesh.wrapU != "clamp" && mesh.wrapU != "repeat" && mesh.wrapU != "mirror") ||
            (mesh.wrapV != "clamp" && mesh.wrapV != "repeat" && mesh.wrapV != "mirror"))
            throw std::runtime_error("Portable effect uses unsupported mesh material");
    }
    auto loadTexture = [&](const std::filesystem::path& path) {
        if (path.empty()) return;
        const std::string key = path.string();
        if (textures_.contains(key)) return;
        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load(key.c_str(), &width, &height, &channels, 4);
        if (!pixels || width <= 0 || height <= 0) {
            stbi_image_free(pixels);
            throw std::runtime_error("Could not decode portable effect texture: " + key);
        }
        Texture texture;
        texture.key = key;
        texture.width = width;
        texture.height = height;
        texture.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4u);
        stbi_image_free(pixels);
        textures_.emplace(key, std::move(texture));
    };
    for (const auto& mesh : effect_.meshes) loadTexture(mesh.texture);
    for (const auto& burst : effect_.bursts) loadTexture(burst.texture);
    for (const auto& material : effect_.materials) loadTexture(material.texture);
    for (const auto& style : effect_.cardStyles) loadTexture(style.texture);
    for (const auto& frame : effect_.frames)
        for (const auto& draw : frame)
            if (draw.kind == vfxlibrary::DrawKind::Card) {
                const auto& material = effect_.materials.at(static_cast<std::size_t>(draw.material));
                intensityTexture(material.texture, normalized(draw.high), normalized(draw.low));
            }
}

const PortableEffect::Texture& PortableEffect::texture(
    const std::filesystem::path& path) const {
    static const Texture white{"portable-effect-white", 1, 1, {255, 255, 255, 255}};
    if (path.empty()) return white;
    return textures_.at(path.string());
}

std::filesystem::path PortableEffect::intensityTexture(
    const std::filesystem::path& path, const vfxlibrary::Rgba& high,
    const vfxlibrary::Rgba& low) const {
    const auto byte = [](float value) {
        return static_cast<unsigned char>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    std::array<unsigned char, 8> endpoints{};
    for (int channel = 0; channel < 4; ++channel) {
        endpoints[channel] = byte(high[channel]);
        endpoints[channel + 4] = byte(low[channel]);
    }
    std::string key = path.string() + ":intensity";
    for (const auto component : endpoints) key += ':' + std::to_string(component);
    if (textures_.contains(key)) return key;
    const auto& source = texture(path);
    Texture baked{key, source.width, source.height, source.rgba};
    for (std::size_t pixel = 0; pixel < baked.rgba.size(); pixel += 4) {
        const float intensity = source.rgba[pixel + 3] / 255.0f;
        for (int channel = 0; channel < 3; ++channel)
            baked.rgba[pixel + channel] = static_cast<unsigned char>(std::lround(
                endpoints[channel + 4] * (1.0f - intensity) +
                endpoints[channel] * intensity));
        baked.rgba[pixel + 3] = static_cast<unsigned char>(std::lround(
            endpoints[3] * intensity));
    }
    textures_.emplace(key, std::move(baked));
    return key;
}

std::vector<Draw> PortableEffect::sample(std::int64_t timeMs,
    const glm::mat4& world, const glm::vec3& cameraRight,
    const glm::vec3& cameraUp, const float* viewProjection,
    int surfaceHeight) const {
    std::vector<Draw> draws;
    if (timeMs < 0 || timeMs >= effect_.durationMs) return draws;
    draws.reserve(effect_.meshes.size() + effect_.bursts.size() * 48u);

    for (const auto& mesh : effect_.meshes) {
        const auto sampled = effect_.sampleMesh(mesh, timeMs);
        if (sampled.vertices.empty() || sampled.triangles.empty() ||
            sampled.color[3] <= .001f) continue;
        Draw draw;
        draw.texture = mesh.texture;
        draw.blend = mesh.blend;
        draw.depthTest = mesh.depthTest;
        draw.wrapS = wrap(mesh.wrapU);
        draw.wrapT = wrap(mesh.wrapV);
        draw.materialMode = mesh.intensityMaterial ? 250 : 255;
        draw.vertices.reserve(sampled.vertices.size());
        for (std::size_t i = 0; i < sampled.vertices.size(); ++i) {
            vfxlibrary::Rgba color = sampled.color;
            if (!sampled.vertexColors.empty()) {
                const auto& source = sampled.vertexColors[i];
                for (int channel = 0; channel < 3; ++channel)
                    color[channel] = mesh.vertexColorMultiply
                        ? source[channel] * sampled.color[channel] : source[channel];
                color[3] *= source[3];
            } else if (mesh.texture.empty()) {
                color[3] *= .18f;
            }
            const vfxlibrary::Vec2 uv = sampled.uvs.empty()
                ? vfxlibrary::Vec2{0.0f, 0.0f} : sampled.uvs[i];
            glm::vec3 point = transformPoint(world, sampled.vertices[i]);
            if (mesh.cameraFacing) {
                const auto q = vec3(sampled.vertices[i]) - vec3(sampled.facingPivot);
                const int axis = sampled.facingNormalAxis;
                const float horizontal = axis == 0 ? q.z : q.x;
                const float vertical = axis == 1 ? q.z : q.y;
                const float depth = q[axis];
                const float scale = glm::length(glm::vec3(world[0]));
                point = transformPoint(world, sampled.facingPivot) + scale *
                    (cameraRight * horizontal + cameraUp * vertical +
                     glm::normalize(glm::cross(cameraRight, cameraUp)) * depth);
            }
            if (mesh.intensityMaterial) {
                for (int channel = 0; channel < 3; ++channel) color[channel] = sampled.high[channel];
            }
            auto output = vertex(point, uv, color);
            if (mesh.intensityMaterial) lowColor(output, sampled.low);
            draw.vertices.push_back(output);
        }
        draw.indices.reserve(sampled.triangles.size() * 3u);
        for (const auto& triangle : sampled.triangles) {
            // Source meshes use CCW fronts; Phlosion's world contract uses CW.
            draw.indices.push_back(static_cast<std::uint32_t>(triangle[0]));
            draw.indices.push_back(static_cast<std::uint32_t>(triangle[2]));
            draw.indices.push_back(static_cast<std::uint32_t>(triangle[1]));
        }
        draws.push_back(std::move(draw));
    }

    for (const auto& burst : effect_.bursts) {
        for (const auto& sampled : effect_.sampleBurst(burst, timeMs)) {
            if (!sampled.card || sampled.opacity <= .001f) continue;
            const auto& card = *sampled.card;
            const glm::vec3 center = transformPoint(world, card.anchor) +
                cameraRight * (sampled.offset[0] * card.worldScale) +
                cameraUp * (sampled.offset[1] * card.worldScale);
            const float radians = glm::radians(card.rotationDegrees);
            const float cosine = std::cos(radians), sine = std::sin(radians);
            const float halfWidth = card.width * card.worldScale * .5f;
            const float halfHeight = card.height * card.worldScale * .5f;
            Draw draw;
            draw.texture = burst.texture;
            draw.blend = vfxlibrary::Blend::Additive;
            draw.depthTest = false;
            const std::array<glm::vec2, 4> corners{{
                {-halfWidth, -halfHeight}, {halfWidth, -halfHeight},
                {halfWidth, halfHeight}, {-halfWidth, halfHeight}}};
            const std::array<vfxlibrary::Vec2, 4> uvs{{
                {0, 1}, {1, 1}, {1, 0}, {0, 0}}};
            for (std::size_t i = 0; i < 4; ++i) {
                const auto& corner = corners[i];
                const glm::vec3 position = center +
                    cameraRight * (corner.x * cosine - corner.y * sine) +
                    cameraUp * (corner.x * sine + corner.y * cosine);
                draw.vertices.push_back(vertex(position, uvs[i],
                    {1.0f, 1.0f, 1.0f, sampled.opacity}));
            }
            draw.indices = {0, 2, 1, 0, 3, 2};
            draws.push_back(std::move(draw));
        }
    }
    const glm::mat4 vp = viewProjection ? glm::make_mat4(viewProjection) : glm::mat4(1.0f);
    const float worldScale = glm::length(glm::vec3(world[0]));
    for (const auto& source : effect_.sample(timeMs)) {
        Draw draw;
        draw.depthTest = false;
        if (source.kind == vfxlibrary::DrawKind::GradientLine) {
            const glm::vec3 start = transformPoint(world, source.position);
            const glm::vec3 end = transformPoint(world, source.end);
            const glm::vec3 segment = end - start;
            const float x = glm::dot(segment, cameraRight);
            const float y = glm::dot(segment, cameraUp);
            const float projectedLength = std::sqrt(x * x + y * y);
            if (projectedLength < 1e-6f) continue;
            const glm::vec3 side = (-y * cameraRight + x * cameraUp) / projectedLength;
            const float clipW = std::abs((vp * glm::vec4(start, 1.0f)).w);
            const float halfWidth = viewProjection && surfaceHeight > 0
                ? std::max(0.00005f, clipW / (surfaceHeight * 2.4f))
                : 0.001f;
            const auto low = normalized(source.low), high = normalized(source.high);
            draw.vertices = {
                vertex(start - side * halfWidth, {0, 0}, low),
                vertex(start + side * halfWidth, {0, 1}, low),
                vertex(end + side * halfWidth, {1, 1}, high),
                vertex(end - side * halfWidth, {1, 0}, high)};
            draw.indices = {0, 2, 1, 0, 3, 2};
            draw.blend = vfxlibrary::Blend::Additive;
        } else {
            const auto& material = effect_.materials.at(static_cast<std::size_t>(source.material));
            const glm::vec3 center = transformPoint(world, source.position);
            glm::vec3 right = cameraRight, up = cameraUp;
            glm::vec3 sweep = glm::vec3(world * glm::vec4(vec3(source.sweep), 0.0f));
            if (material.shape == vfxlibrary::CardShape::VelocitySwept) {
                float dx = glm::dot(sweep, cameraRight);
                float dy = glm::dot(sweep, cameraUp);
                if (viewProjection) {
                    const glm::vec4 p = vp * glm::vec4(center, 1.0f);
                    const glm::vec4 d = vp * glm::vec4(sweep, 0.0f);
                    if (std::abs(p.w) > 1e-5f) {
                        dx = (d.x * p.w - p.x * d.w) / (p.w * p.w);
                        dy = (d.y * p.w - p.y * d.w) / (p.w * p.w);
                    }
                }
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 1e-6f) {
                    right = (cameraRight * dy - cameraUp * dx) / length;
                    up = (cameraRight * dx + cameraUp * dy) / length;
                }
            }
            const auto corners = planeCorners(center, right, up,
                source.width * worldScale, source.height * worldScale,
                material.shape == vfxlibrary::CardShape::Billboard
                    ? source.rotationDegrees : 0.0f);
            auto swept = corners;
            if (material.shape == vfxlibrary::CardShape::VelocitySwept) {
                swept[0] -= sweep * .5f; swept[1] -= sweep * .5f;
                swept[2] += sweep * .5f; swept[3] += sweep * .5f;
            }
            appendQuad(draw, swept, material.uvs, {1, 1, 1, 1});
            draw.texture = intensityTexture(material.texture,
                normalized(source.high), normalized(source.low));
            draw.blend = material.blend;
            draw.wrapS = wrap(material.wrapU);
            draw.wrapT = wrap(material.wrapV);
        }
        draws.push_back(std::move(draw));
    }
    const auto stream = effect_.sampleCardStream(timeMs);
    for (const auto& packet : stream.packets)
        for (int n = 0; n < packet.count; ++n) {
            const auto& card = stream.cards.at(static_cast<std::size_t>(packet.first + n));
            const auto& p = card.values;
            if (!(card.flags & 1) || p[0] <= .001f) continue;
            if (packet.mode == 3 || packet.mode == 4) {
                auto draw = gradientLine(transformPoint(world, {p[4], p[5], p[6]}),
                    transformPoint(world, {p[15], p[16], p[17]}),
                    {p[11], p[12], p[13], p[14]}, {p[7], p[8], p[9], p[10]},
                    packet.mode == 4, cameraRight, cameraUp, viewProjection, surfaceHeight);
                if (!draw.vertices.empty()) draws.push_back(std::move(draw));
                continue;
            }
            const auto& style = effect_.cardStyles.at(static_cast<std::size_t>(card.style));
            glm::vec3 localCenter = vec3(style.anchor);
            if (style.worldPosition) localCenter += glm::vec3(p[4], p[5], p[6]) * style.worldScale;
            const auto center = glm::vec3(world * glm::vec4(localCenter, 1));
            glm::vec3 right = cameraRight, up = cameraUp;
            if (style.worldPlane) {
                const auto normal = glm::normalize(vec3(style.normal));
                const auto localRight = glm::normalize(glm::cross(vec3(style.up), normal));
                right = glm::normalize(glm::mat3(world) * localRight);
                up = glm::normalize(glm::mat3(world) * glm::cross(normal, localRight));
            }
            float rotation = p[3];
            const bool keyDirection = style.alignDirection && (card.flags & 2);
            glm::vec3 direction = glm::mat3(world) *
                ((card.flags & 2) ? glm::vec3(p[15], p[16], p[17]) : vec3(style.direction));
            if (keyDirection || style.alignFixedDirection) {
                if (!keyDirection) direction = glm::mat3(world) * vec3(style.direction);
                const auto projected = projectedDirection(center, direction, cameraRight, cameraUp, viewProjection);
                rotation = glm::degrees(std::atan2(-projected.x, projected.y)) + style.axisRotation;
            }
            const bool swept = style.longAxis == "native-full-velocity-swept-card" && (card.flags & 2);
            if (swept) {
                const auto projected = projectedDirection(center, direction, cameraRight, cameraUp, viewProjection);
                right = cameraRight * projected.y - cameraUp * projected.x;
                up = cameraRight * projected.x + cameraUp * projected.y;
                rotation = 0;
            }
            const float width = style.width * p[1] * style.worldScale * worldScale;
            const float height = style.height * p[2] * style.worldScale * worldScale;
            if (packet.mode == 0 && (width <= .0001f || height <= .0001f)) continue;
            auto corners = planeCorners(center, right, up, width, height, rotation);
            if (swept) {
                const auto sweep = direction * (style.worldScale * .5f);
                corners[0] -= sweep; corners[1] -= sweep;
                corners[2] += sweep; corners[3] += sweep;
            }
            vfxlibrary::Rgba high{p[7], p[8], p[9], p[10]};
            vfxlibrary::Rgba low{p[11], p[12], p[13], p[14]};
            for (int channel = 0; channel < 4; ++channel) high[channel] *= style.tint[channel];
            for (int channel = 0; channel < 3; ++channel) low[channel] *= style.tint[channel];
            high[3] *= p[0] * style.opacity;
            Draw draw;
            appendQuad(draw, corners, style.customUvs ? style.uvs : defaultUvs, high);
            for (auto& output : draw.vertices) lowColor(output, low);
            draw.texture = style.texture;
            draw.materialMode = materialMode(style.shader);
            draw.blend = style.blend == "alpha_blend" ?
                vfxlibrary::Blend::Alpha : vfxlibrary::Blend::Additive;
            draw.depthTest = style.depthTest;
            draw.wrapS = wrap(style.wrapU);
            draw.wrapT = wrap(style.wrapV);
            draws.push_back(std::move(draw));
        }
    return draws;
}

void PortableEffect::render(IRenderBackend& renderer, std::int64_t timeMs,
    const glm::mat4& world, const glm::vec3& cameraRight,
    const glm::vec3& cameraUp, const float* viewProjection,
    int surfaceWidth, int surfaceHeight) const {
    if (!viewProjection || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    Draw batch;
    const auto flush = [&] {
        if (batch.vertices.empty()) return;
        const auto& image = texture(batch.texture);
        IRenderBackend::WorldTextureData material{};
        material.key = image.key.c_str();
        material.cacheKey = image.key.c_str();
        material.rgba = image.rgba.data();
        material.width = image.width;
        material.height = image.height;
        material.wrapS = batch.wrapS;
        material.wrapT = batch.wrapT;
        material.textureSrgb = 0u;
        material.alphaMode = 2u;
        material.blendMode = batch.blend == vfxlibrary::Blend::Additive ? 1u : 0u;
        material.depthTestEnabled = batch.depthTest ? 1u : 0u;
        material.materialMode = batch.materialMode;
        renderer.drawWorldIndexedMeshTextured(batch.vertices.data(), batch.vertices.size(),
            batch.indices.data(), batch.indices.size(), &material,
            viewProjection, surfaceWidth, surfaceHeight);
        batch = {};
    };
    for (const auto& draw : sample(timeMs, world, cameraRight, cameraUp,
                                   viewProjection, surfaceHeight)) {
        if (!batch.vertices.empty() && (batch.texture != draw.texture ||
            batch.blend != draw.blend || batch.depthTest != draw.depthTest ||
            batch.wrapS != draw.wrapS || batch.wrapT != draw.wrapT ||
            batch.materialMode != draw.materialMode)) flush();
        if (batch.vertices.empty()) {
            batch.texture = draw.texture;
            batch.blend = draw.blend;
            batch.depthTest = draw.depthTest;
            batch.wrapS = draw.wrapS;
            batch.wrapT = draw.wrapT;
            batch.materialMode = draw.materialMode;
        }
        const auto base = static_cast<std::uint32_t>(batch.vertices.size());
        batch.vertices.insert(batch.vertices.end(), draw.vertices.begin(), draw.vertices.end());
        for (const auto index : draw.indices) batch.indices.push_back(base + index);
    }
    flush();
}

} // namespace vfx::portable
