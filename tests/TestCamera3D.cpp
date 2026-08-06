#include "engine/render/Camera3D.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>

bool test_camera_view_plane_pan_contract(std::string& outFail) {
    Camera3D camera(45.0f, 1.0f, 0.1f, 100.0f);
    camera.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera.lookAt(glm::vec3(0.0f));

    const glm::vec3 oldOffset =
        camera.getPosition() - camera.getTarget();
    camera.panViewPlane(10.0f, 20.0f, 0.1f);

    const glm::vec3 expectedTarget(-1.0f, 2.0f, 0.0f);
    if (glm::distance(camera.getTarget(), expectedTarget) > 0.0001f) {
        outFail =
            "view-plane pan did not translate horizontally and vertically in camera space";
        return false;
    }
    const glm::vec3 newOffset =
        camera.getPosition() - camera.getTarget();
    if (glm::distance(oldOffset, newOffset) > 0.0001f) {
        outFail =
            "view-plane pan changed the camera-to-target orbit offset";
        return false;
    }
    return true;
}
