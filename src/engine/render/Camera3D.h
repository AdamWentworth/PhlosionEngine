// Camera3D.h

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera3D {
public:
    Camera3D(float fovDeg, float aspect, float nearPlane, float farPlane);

    void setPosition(const glm::vec3& pos);
    void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));

    void move(const glm::vec3& delta);       // NEW
    void zoom(float delta);                  // NEW
    void panPlanar(float screenDx, float screenDy, float scale);

    // NEW: orbit camera position around its current target
    // yawDeltaRad: rotate around world up (Y)
    // pitchDeltaRad: rotate up/down
    void orbit(float yawDeltaRad, float pitchDeltaRad);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getDirection() const;
    glm::vec3 getPosition() const { return position; }
    glm::vec3 getTarget() const { return target; }
    void setAspectRatio(float aspect);

private:
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 upVector;

    float fov;
    float aspectRatio;
    float nearZ;
    float farZ;
};
