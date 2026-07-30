// src/engine/core/Updatable.h
#pragma once

class Updatable {
public:
    virtual ~Updatable() = default;
    virtual void update(float deltaTime) = 0;
};
