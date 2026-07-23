// src/engine/core/GameLoop.h
#pragma once

struct GameContext;
struct InputEvent;

/*
    GameLoop:
    - Engine-facing interface for the game/app layer.
    - The host loop drives the game via these callbacks.
    - SDL types are NOT used here.
*/
class GameLoop {
public:
    virtual ~GameLoop() = default;

    virtual void init(GameContext& ctx) = 0;
    virtual void handleEvent(const InputEvent& event) = 0;

    virtual void fixedUpdate(float dt) = 0;
    virtual void render(int drawableW, int drawableH) = 0;

    virtual void shutdown() = 0;
};
