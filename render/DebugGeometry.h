#pragma once

#include <cmath>
#include <vector>

#include "engine/render/IRenderBackend.h"

namespace engine::render::debug {

struct Vertex2D {
    float x = 0.0f;
    float y = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

inline void appendQuadAsTriangles(const IRenderBackend::DebugQuad& q,
                                  std::vector<Vertex2D>& out) {
    const Vertex2D a{q.x, q.y, q.r, q.g, q.b, q.a};
    const Vertex2D b{q.x + q.w, q.y, q.r, q.g, q.b, q.a};
    const Vertex2D c{q.x + q.w, q.y + q.h, q.r, q.g, q.b, q.a};
    const Vertex2D d{q.x, q.y + q.h, q.r, q.g, q.b, q.a};
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
    out.push_back(a);
    out.push_back(c);
    out.push_back(d);
}

inline bool appendLineAsTriangles(const IRenderBackend::DebugLine& line,
                                  std::vector<Vertex2D>& out) {
    const float thickness = (line.thickness > 0.0f) ? line.thickness : 0.0f;
    if (thickness <= 0.0f) return false;

    const float dx = line.x2 - line.x1;
    const float dy = line.y2 - line.y1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0001f) return false;

    const float half = thickness * 0.5f;
    const float nx = (-dy / len) * half;
    const float ny = (dx / len) * half;

    const Vertex2D a{line.x1 + nx, line.y1 + ny, line.r, line.g, line.b, line.a};
    const Vertex2D b{line.x2 + nx, line.y2 + ny, line.r, line.g, line.b, line.a};
    const Vertex2D c{line.x2 - nx, line.y2 - ny, line.r, line.g, line.b, line.a};
    const Vertex2D d{line.x1 - nx, line.y1 - ny, line.r, line.g, line.b, line.a};

    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
    out.push_back(a);
    out.push_back(c);
    out.push_back(d);
    return true;
}

inline void appendTriangle(const IRenderBackend::DebugTriangle& tri,
                           std::vector<Vertex2D>& out) {
    out.push_back(Vertex2D{tri.x1, tri.y1, tri.r, tri.g, tri.b, tri.a});
    out.push_back(Vertex2D{tri.x2, tri.y2, tri.r, tri.g, tri.b, tri.a});
    out.push_back(Vertex2D{tri.x3, tri.y3, tri.r, tri.g, tri.b, tri.a});
}

} // namespace engine::render::debug
