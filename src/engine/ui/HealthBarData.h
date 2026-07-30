// game/ui/HealthBarData.h
#pragma once
#include <glm/glm.hpp>

struct HealthBarData {
    glm::vec2 screenPosition;
    int currentHP;
    int maxHP;
    int currentEnergy;
    int maxEnergy;
    int currentXP;
    int maxXP;
    bool showXP;
    bool isEnemy = false;
    int level = 1;
    float cellPx = 0.0f;
};
