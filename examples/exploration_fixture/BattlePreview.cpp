#include "BattlePreview.h"

#include "engine/core/Environment.h"
#include "engine/render/Camera3D.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL_loadso.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace engine::exploration_fixture {
namespace {
using namespace render::gltf::scene;
namespace fs = std::filesystem;

struct ActorSlot {
    const char* name;
    glm::vec3 position;
};

constexpr std::array<ActorSlot, 4> slots{{
    {"ally-left", {-3.2f, 0.0f, 5.0f}},
    {"ally-right", {-0.8f, 0.0f, 5.0f}},
    {"opponent-left", {-3.2f, 0.0f, 2.0f}},
    {"opponent-right", {-0.8f, 0.0f, 2.0f}},
}};

enum NavigationKey : std::size_t {
    Forward, Backward, Left, Right, Down, Up, Fast
};

void require(bool passed, const std::string& error) {
    if (!passed) throw std::runtime_error(error);
}

nlohmann::json vec(glm::vec3 value) {
    return {value.x, value.y, value.z};
}

camera::BattleCameraShot shot(float yaw, float pitch, float fov) {
    camera::BattleCameraShot value;
    value.yawRadians = yaw;
    value.pitchRadians = pitch;
    value.verticalFovDegrees = fov;
    value.marginFraction = .15f;
    value.minDistance = 5.0f;
    value.maxDistance = 20.0f;
    value.farPlane = 2500.0f;
    return value;
}

std::array<camera::BattleCameraTarget, 6> targets() {
    std::array<camera::BattleCameraTarget, 6> result{};
    for (std::size_t index = 0; index < slots.size(); ++index)
        result[index] = {slots[index].position + glm::vec3(0.0f, .9f, 0.0f), .9f};
    return result;
}
} // namespace

BattlePreview::BattlePreview(bool audioOutput)
    : hudAtlas_(env::get("PHLOSION_BATTLE_HUD_PNG").value_or("")),
      vfxPlugin_(env::get("PHLOSION_BATTLE_VFX_PLUGIN").value_or("")),
      audioOutput_(mixer_) {
    const auto fieldPath = env::get("PHLOSION_OUTSKIRT_BATTLE_GLB");
    const auto wesPath = env::get("PHLOSION_OUTSKIRT_WES_GLB");
    const auto castScene = env::get("PHLOSION_BATTLE_SCENE_JSON");
    const auto castRoot = env::get("PHLOSION_BATTLE_CAST_ROOT");
    const auto pokemonRoot = env::get("PHLOSION_BATTLE_POKEMON_ROOT");
    require(fieldPath && !fieldPath->empty(),
            "Battle preview requires PHLOSION_OUTSKIRT_BATTLE_GLB");
    require(static_cast<bool>(castScene) == static_cast<bool>(castRoot),
            "Battle cast needs both PHLOSION_BATTLE_SCENE_JSON and PHLOSION_BATTLE_CAST_ROOT");
    std::string error;
    const bool loadedField = loadGltfSceneAsset(*fieldPath, field_, &error);
    require(loadedField, error);
    const bool posedField = sampleGltfScenePose(field_, 0.0f, kBindPoseClip,
                                                fieldPose_, &error);
    require(posedField, error);
    if (castScene) {
        std::ifstream input(*castScene);
        require(static_cast<bool>(input), "Cannot open battle cast scene: " + *castScene);
        nlohmann::json document;
        input >> document;
        require(document.at("schema") == "pokemon-cyberpunk-outskirt-battle-preview-v1" &&
                document.at("actors").is_array() && document.at("actors").size() == 6u,
                "Battle cast scene must contain six ordered actors");
        constexpr std::array<const char*, 6> expectedSlots{
            "ally-left", "ally-right", "opponent-left", "opponent-right",
            "ally-trainer", "opponent-trainer"};
        for (std::size_t index = 0; index < castActors_.size(); ++index) {
            const auto& authored = document["actors"][index];
            auto& actor = castActors_[index];
            actor.slot = authored.at("slot").get<std::string>();
            require(actor.slot == expectedSlots[index], "Unexpected battle cast slot: " + actor.slot);
            actor.model = authored.value("model", authored.value("native_object", std::string{}));
            if (authored.contains("native_object")) {
                actor.nativeObject = authored.at("native_object").get<std::string>();
                actor.nativeSha256 = authored.at("native_sha256").get<std::string>();
                actor.nativeStats = authored.at("native_stats");
                require(actor.nativeSha256.size() == 64u,
                        "Native battle Pokemon needs a SHA-256 identity");
                for (const char* key : {"primitives", "vertices", "indices", "materials", "clips"})
                    require(actor.nativeStats.at(key).get<std::uint32_t>() > 0u,
                            std::string("Invalid native battle model stat: ") + key);
                require(pokemonRoot && !pokemonRoot->empty(),
                        "Native battle Pokemon need PHLOSION_BATTLE_POKEMON_ROOT");
                const fs::path nativeRelative(actor.nativeObject);
                require(!nativeRelative.empty() && !nativeRelative.is_absolute() &&
                        std::find(nativeRelative.begin(), nativeRelative.end(), fs::path("..")) == nativeRelative.end(),
                        "Native battle Pokemon path must be relative");
                actor.nativeObject = (fs::path(*pokemonRoot) / nativeRelative).string();
                require(fs::is_regular_file(actor.nativeObject),
                        "Missing native battle Pokemon: " + actor.nativeObject);
            }
            actor.displayName = authored.at("display_name").get<std::string>();
            const auto position = authored.at("position").get<std::array<float, 3>>();
            actor.position = {position[0], position[1], position[2]};
            const auto offset = authored.value("model_offset", std::array<float, 3>{0.0f, 0.0f, 0.0f});
            actor.modelOffset = {offset[0], offset[1], offset[2]};
            actor.yawRadians = glm::radians(authored.at("yaw_degrees").get<float>());
            actor.scale = authored.value("scale", 1.0f);
            actor.focusHeight = authored.at("focus_height").get<float>();
            actor.idleAnimation = authored.at("idle_animation").get<std::string>();
            actor.gestureAnimation = authored.value("gesture_animation", std::string{});
            require(std::isfinite(actor.position.x) && std::isfinite(actor.position.y) &&
                    std::isfinite(actor.position.z) && std::isfinite(actor.yawRadians) &&
                    std::isfinite(actor.modelOffset.x) && std::isfinite(actor.modelOffset.y) &&
                    std::isfinite(actor.modelOffset.z) && std::isfinite(actor.scale) &&
                    actor.scale > 0.0f && actor.scale < 4.0f &&
                    actor.focusHeight > 0.0f && actor.focusHeight < 4.0f,
                    "Invalid battle cast placement");
            if (actor.nativeObject.empty()) {
                const fs::path relative(actor.model);
                require(!relative.empty() && !relative.is_absolute() &&
                        std::find(relative.begin(), relative.end(), fs::path("..")) == relative.end(),
                        "Battle cast model must be relative to the private cast root");
                const auto path = (fs::path(*castRoot) / relative).string();
                require(loadGltfSceneAsset(path, castModels_[index], &error), error);
                auto findClip = [&](const std::string& name) {
                    const auto& animations = castModels_[index]->animations;
                    const auto found = std::find_if(animations.begin(), animations.end(),
                        [&](const auto& clip) { return clip.name == name && clip.durationSec > 0.0f; });
                    require(found != animations.end(), "Missing battle cast clip " + name);
                    return static_cast<int>(found - animations.begin());
                };
                castIdleClips_[index] = findClip(actor.idleAnimation);
                if (!actor.gestureAnimation.empty()) {
                    require(index == 4u, "Only the ally trainer can drive the preview gesture");
                    castGestureClip_ = findClip(actor.gestureAnimation);
                }
            }
        }
        require(castGestureClip_ >= 0, "Battle cast needs Wes's gesture animation");
        if (std::any_of(castActors_.begin(), castActors_.end(),
                        [](const CastActor& actor) { return !actor.nativeObject.empty(); })) {
            const auto pluginPath = env::get("PHLOSION_BATTLE_POKEMON_PLUGIN");
            require(pluginPath && !pluginPath->empty(),
                    "Native battle Pokemon need PHLOSION_BATTLE_POKEMON_PLUGIN");
#ifdef _WIN32
            // LoadLibraryEx searches next to the staged plugin for its native
            // dependencies; SDL_LoadObject uses the process search path.
            battlePokemonLibrary_ = LoadLibraryExW(
                fs::path(*pluginPath).wstring().c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            const auto loadError = GetLastError();
#else
            battlePokemonLibrary_ = SDL_LoadObject(pluginPath->c_str());
#endif
            if (!battlePokemonLibrary_) {
#ifdef _WIN32
                throw std::runtime_error("Battle Pokemon plugin load failed (Windows " +
                    std::to_string(loadError) + "): " + *pluginPath);
#else
                throw std::runtime_error("Battle Pokemon plugin load failed: " +
                    *pluginPath + ": " + SDL_GetError());
#endif
            }
#ifdef _WIN32
            const auto symbol = GetProcAddress(
                static_cast<HMODULE>(battlePokemonLibrary_),
                "phlosionBattlePokemonPluginCreate");
#else
            const auto symbol = SDL_LoadFunction(battlePokemonLibrary_,
                "phlosionBattlePokemonPluginCreate");
#endif
            if (!symbol) {
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(battlePokemonLibrary_));
#else
                SDL_UnloadObject(battlePokemonLibrary_);
#endif
                battlePokemonLibrary_ = nullptr;
                throw std::runtime_error("Battle Pokemon plugin entry point missing");
            }
            const auto create = reinterpret_cast<render::CreateBattlePokemonPlugin>(symbol);
            const bool created = create(render::battlePokemonPluginAbi, &battlePokemonApi_);
            if (!created || battlePokemonApi_.abi != render::battlePokemonPluginAbi ||
                battlePokemonApi_.size != sizeof(render::BattlePokemonPluginApi) ||
                !battlePokemonApi_.context || !battlePokemonApi_.render ||
                !battlePokemonApi_.lastError || !battlePokemonApi_.destroy) {
                if (battlePokemonApi_.context && battlePokemonApi_.destroy)
                    battlePokemonApi_.destroy(battlePokemonApi_.context);
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(battlePokemonLibrary_));
#else
                SDL_UnloadObject(battlePokemonLibrary_);
#endif
                battlePokemonLibrary_ = nullptr;
                throw std::runtime_error("Battle Pokemon plugin ABI mismatch");
            }
        }
        castEnabled_ = true;
    } else {
        require(wesPath && !wesPath->empty(),
                "Battle preview requires PHLOSION_OUTSKIRT_WES_GLB");
        require(loadGltfSceneAsset(*wesPath, wes_, &error), error);
        require(wes_->animations.size() > 8u &&
                wes_->animations[1].durationSec > 0.0f &&
                wes_->animations[8].durationSec > 0.0f,
                "Battle preview needs Wes's idle and gesture clips");
    }
    std::array<float, 9600> tone{};
    for (std::size_t index = 0; index < tone.size(); ++index) {
        const float time = static_cast<float>(index) / audio::sampleRate;
        const float falloff = 1.0f - static_cast<float>(index) / tone.size();
        tone[index] = .2f * falloff * std::sin(2.0f * 3.14159265f * 330.0f * time);
    }
    require(audio::AudioClip::create(audio::sampleRate, 1u, tone, impactClip_, &error), error);
    if (audioOutput && !audioOutput_.start(&error))
        std::cerr << "[BattlePreview] Audio device unavailable; using null sink: " << error << '\n';
    if (env::get("PHLOSION_EXPLORATION_AUTOMATION").value_or("0") == "1") {
        cameraProbe_ = env::get("PHLOSION_BATTLE_CAMERA_PROBE").value_or("0") == "1";
        if (cameraProbe_) {
            const auto aspect = env::get("PHLOSION_BATTLE_CAMERA_PROBE_ASPECT").value_or("");
            require(aspect == "16x9" || aspect == "21x9",
                    "Battle camera probe needs a 16x9 or 21x9 aspect");
            cameraProbeAspectNumerator_ = aspect == "16x9" ? 16 : 21;
        }
        automationAction_ = env::get("PHLOSION_BATTLE_ACTION").value_or("");
        if (!automationAction_.empty()) {
            require(automationAction_ == "cancel" || automationAction_ == "skip",
                    "Invalid battle automation action");
            automationActionTick_ = std::stoi(env::get("PHLOSION_BATTLE_ACTION_TICK").value_or("-1"));
            require(automationActionTick_ > 0 && automationActionTick_ < 150,
                    "Invalid battle automation action tick");
        }
    }
}

BattlePreview::~BattlePreview() {
    cancel();
    release();
    if (battlePokemonLibrary_) {
        if (battlePokemonApi_.context && battlePokemonApi_.destroy)
            battlePokemonApi_.destroy(battlePokemonApi_.context);
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(battlePokemonLibrary_));
#else
        SDL_UnloadObject(battlePokemonLibrary_);
#endif
    }
}

void BattlePreview::release() {
    if (scopes_ && scope_) {
        if (!scopes_->requestWorldResourceScopeRelease(scope_)) std::terminate();
        scope_ = 0;
        scopes_ = nullptr;
    }
}

void BattlePreview::dispatch(BattleCue cue) {
    if (cue == BattleCue::ImpactSound) {
        audio::AudioVoiceId voice = 0;
        audio::AudioPlayOptions options;
        options.bus = audio::AudioBus::Sfx;
        options.spatial = true;
        options.emitterPosition = castEnabled_ ? castActors_[3].position : slots[3].position;
        std::string error;
        require(mixer_.play(impactClip_, options, voice, &error), error);
    } else if (cue == BattleCue::ImpactEffect) {
        activeEffect_ = true;
        const auto target = castEnabled_ ? castActors_[3].position : slots[3].position;
        vfxPlugin_.spawnImpact(target.x, target.y + .9f, target.z);
    } else if (cue == BattleCue::EffectStop) {
        activeEffect_ = false;
        vfxPlugin_.stopAll();
    } else if (cue == BattleCue::Cleanup) {
        audioOutput_.stopAll();
        vfxPlugin_.stopAll();
        lastAudioPeak_ = 0.0f;
        camera_.reset();
        activeEffect_ = false;
        phase_ = "complete";
    }
}

void BattlePreview::reset() {
    sequence_.reset();
    audioOutput_.stopAll();
    vfxPlugin_.stopAll();
    mixer_.reset();
    camera_.reset();
    freeCamera_.reset();
    navigationKeys_.fill(false);
    pendingLook_ = {};
    pendingDolly_ = 0.0f;
    navigationSeconds_ = 0.0f;
    rightMouseDown_ = false;
    cameraProbeApplied_ = false;
    activeEffect_ = false;
    lastAudioPeak_ = 0.0f;
    phase_ = "wide";
}

void BattlePreview::setNavigationEnabled(bool enabled) {
    navigationEnabled_ = enabled;
    if (!enabled) {
        navigationKeys_.fill(false);
        rightMouseDown_ = false;
        pendingLook_ = {};
        pendingDolly_ = 0.0f;
        navigationSeconds_ = 0.0f;
    }
}

void BattlePreview::navigationEvent(const SDL_Event& event, Uint32 windowId,
                                    bool pointerInside) {
    if (!navigationEnabled_) return;
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
        event.key.windowID == windowId) {
        const bool down = event.type == SDL_KEYDOWN;
        switch (event.key.keysym.sym) {
        case SDLK_w: navigationKeys_[Forward] = down; break;
        case SDLK_s: navigationKeys_[Backward] = down; break;
        case SDLK_a: navigationKeys_[Left] = down; break;
        case SDLK_d: navigationKeys_[Right] = down; break;
        case SDLK_q: navigationKeys_[Down] = down; break;
        case SDLK_e: navigationKeys_[Up] = down; break;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: navigationKeys_[Fast] = down; break;
        default: break;
        }
    } else if ((event.type == SDL_MOUSEBUTTONDOWN ||
                event.type == SDL_MOUSEBUTTONUP) &&
               event.button.windowID == windowId &&
               event.button.button == SDL_BUTTON_RIGHT) {
        rightMouseDown_ = event.type == SDL_MOUSEBUTTONDOWN && pointerInside;
    } else if (event.type == SDL_MOUSEMOTION &&
               event.motion.windowID == windowId && rightMouseDown_ &&
               pointerInside) {
        pendingLook_ += glm::vec2(event.motion.xrel, -event.motion.yrel) * .006f;
        pendingLook_ = glm::clamp(pendingLook_, glm::vec2(-.4f), glm::vec2(.4f));
    } else if (event.type == SDL_MOUSEWHEEL &&
               event.wheel.windowID == windowId && pointerInside) {
        pendingDolly_ = glm::clamp(pendingDolly_ +
            static_cast<float>(event.wheel.y), -8.0f, 8.0f);
    }
}

void BattlePreview::navigationFrame(float deltaSeconds) {
    if (navigationEnabled_)
        navigationSeconds_ = glm::clamp(deltaSeconds, 0.0f, 0.05f);
}

void BattlePreview::cancel() {
    for (const auto cue : sequence_.cancel()) dispatch(cue);
}

void BattlePreview::skip() {
    for (const auto cue : sequence_.skip()) dispatch(cue);
}

void BattlePreview::frameCamera(int tick, float aspect) {
    std::string error;
    auto all = targets();
    if (castEnabled_) {
        for (std::size_t index = 0; index < castActors_.size(); ++index)
            all[index] = {castActors_[index].position +
                          glm::vec3(0.0f, castActors_[index].focusHeight, 0.0f),
                          castActors_[index].focusHeight};
    }
    const std::array<camera::BattleCameraTarget, 2> first{all[0], all[2]};
    const std::array<camera::BattleCameraTarget, 2> second{all[1], all[3]};
    const std::span<const camera::BattleCameraTarget> wide(all.data(), castEnabled_ ? 6u : 4u);
    camera_.reset();
    auto wideShot = shot(.10f, castEnabled_ ? .25f : .38f, 48.0f);
    if (castEnabled_) wideShot.focusOffset.y = -.4f;
    const bool opened = camera_.cut(wideShot, wide, aspect, cameraPose_, &error);
    require(opened, error);
    phase_ = "wide";
    for (int current = 1; current <= tick; ++current) {
        if (current == 30) {
            const bool blended = camera_.blendTo(shot(-.48f, .25f, 42.0f), first,
                                                 aspect, .5f, cameraPose_, &error);
            require(blended, error);
            phase_ = "blend";
        }
        if (current == 60) {
            const bool cut = camera_.cut(shot(.52f, .28f, 42.0f), second,
                                         aspect, cameraPose_, &error);
            require(cut, error);
            phase_ = "counter";
        }
        if (current == 90) {
            const bool shaken = camera_.startShake({.12f, 8.0f, .3f, 17u}, &error);
            require(shaken, error);
            phase_ = "impact";
        }
        const auto active = current < 30 ? wide
                           : current < 60 ? std::span<const camera::BattleCameraTarget>(first)
                                          : std::span<const camera::BattleCameraTarget>(second);
        const bool stepped = camera_.step(active, aspect, battleFixedDelta,
                                          false, cameraPose_, &error);
        require(stepped, error);
        if (current == 110) phase_ = "settle";
    }
}

void BattlePreview::draw(IRenderBackend& renderer, int tick, int width, int height,
                         bool cameraProbeAllowed) {
    if (tick < 0 || width <= 0 || height <= 0) throw std::runtime_error("Invalid battle preview frame");
    if (!scope_) {
        scopes_ = dynamic_cast<IWorldResourceScope*>(&renderer);
        require(scopes_ != nullptr, "Managed battle resources required");
        scope_ = scopes_->createWorldResourceScope(256u * 1024u * 1024u);
        require(scope_ != 0, "Could not create battle GPU scope");
    }
    require(dynamic_cast<IWorldResourceScope*>(&renderer) == scopes_,
            "Battle renderer changed without release");
    if (sequence_.state().active)
        frameCamera(tick, static_cast<float>(width) / height);
    if (cameraProbe_ && cameraProbeAllowed && !cameraProbeApplied_ &&
        tick >= 5 && width * 9 == height * cameraProbeAspectNumerator_) {
        BattleFreeCameraInput probe;
        probe.look = {.12f, -.03f};
        probe.dolly = .7f;
        freeCamera_.step(cameraPose_, probe, 0.0f);
        cameraProbeApplied_ = true;
    }
    BattleFreeCameraInput navigation;
    if (navigationEnabled_) {
        navigation.move = {static_cast<float>(navigationKeys_[Right]) -
                               static_cast<float>(navigationKeys_[Left]),
                           static_cast<float>(navigationKeys_[Up]) -
                               static_cast<float>(navigationKeys_[Down]),
                           static_cast<float>(navigationKeys_[Forward]) -
                               static_cast<float>(navigationKeys_[Backward])};
        navigation.look = pendingLook_;
        navigation.dolly = pendingDolly_;
        navigation.fast = navigationKeys_[Fast];
    }
    freeCamera_.step(cameraPose_, navigation, navigationSeconds_);
    pendingLook_ = {};
    pendingDolly_ = 0.0f;
    navigationSeconds_ = 0.0f;
    if (freeCamera_.active())
        cameraPose_ = freeCamera_.pose(static_cast<float>(width) / height);
    std::string error;
    require(mixer_.setListener(cameraPose_.position, cameraPose_.right, &error), error);
    while (sequence_.state().active && sequence_.state().tick < tick) {
        for (const auto cue : sequence_.advance()) dispatch(cue);
        if (sequence_.state().tick == automationActionTick_) {
            if (automationAction_ == "cancel") cancel();
            else skip();
        }
        if (!audioOutput_.pump(800u, &error)) {
            require(error.starts_with("SDL audio queue failed:"), error);
            std::cerr << "[BattlePreview] Audio device lost; using null sink: " << error << '\n';
        }
        lastAudioPeak_ = audioOutput_.lastPeak();
        vfxPlugin_.step(battleFixedDelta);
    }
    Camera3D camera(cameraPose_.verticalFovDegrees,
                    static_cast<float>(width) / height,
                    cameraPose_.nearPlane, cameraPose_.farPlane);
    camera.setPosition(cameraPose_.position);
    camera.lookAt(cameraPose_.lookTarget, cameraPose_.up);
    const glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
    GltfSceneDrawOptions options;
    std::copy_n(glm::value_ptr(viewProjection), 16, options.viewProjectionMatrix.begin());
    options.surfaceWidth = width;
    options.surfaceHeight = height;
    options.cameraPosition = {cameraPose_.position.x, cameraPose_.position.y,
                              cameraPose_.position.z};
    options.cameraTarget = {cameraPose_.lookTarget.x, cameraPose_.lookTarget.y,
                            cameraPose_.lookTarget.z};
    options.cameraForward = {cameraPose_.forward.x, cameraPose_.forward.y,
                             cameraPose_.forward.z};
    options.resourceScope = scope_;
    options.materialMode = 2;
    GltfSceneDrawStats stats;
    const bool drewField = drawGltfSceneAsset(renderer, *field_, fieldPose_,
                                               options, stats, &error);
    require(drewField, error);
    if (castEnabled_) {
        // Backends may read skin palettes after this call returns.
        auto& actorPoses = castPoses_;
        for (std::size_t index = 0; index < castActors_.size(); ++index) {
            if (!castActors_[index].nativeObject.empty()) continue;
            const auto& model = castModels_[index];
            const int idleClip = castIdleClips_[index];
            const float sampleTime = std::fmod(tick * battleFixedDelta + index * .17f,
                                                model->animations[idleClip].durationSec);
            bool sampled = false;
            if (index == 4u && sequence_.state().active && tick >= 110 && tick < 119) {
                render::animation::PoseBlendRequest recovery;
                recovery.baseClipIndex = castGestureClip_;
                recovery.baseTimeSec = std::fmod((tick - 60) * battleFixedDelta,
                    model->animations[castGestureClip_].durationSec);
                recovery.targetClipIndex = idleClip;
                recovery.targetTimeSec = sampleTime;
                recovery.crossfadeWeight = (tick - 109) / 9.0f;
                sampled = sampleGltfSceneBlendedPose(model, recovery, actorPoses[index], &error);
            } else if (index == 4u && sequence_.state().gestureClip == 8) {
                render::animation::PoseBlendRequest gesture;
                gesture.baseClipIndex = idleClip;
                gesture.baseTimeSec = sampleTime;
                gesture.targetClipIndex = castGestureClip_;
                gesture.targetTimeSec = std::fmod((tick - 60) * battleFixedDelta,
                    model->animations[castGestureClip_].durationSec);
                gesture.crossfadeWeight = std::min(1.0f, (tick - 59) / 9.0f);
                sampled = sampleGltfSceneBlendedPose(model, gesture, actorPoses[index], &error);
            } else {
                sampled = sampleGltfScenePose(model, sampleTime, idleClip,
                                              actorPoses[index], &error);
            }
            require(sampled, error);
        }
        for (std::size_t index = 0; index < castActors_.size(); ++index) {
            if (!castActors_[index].nativeObject.empty()) continue;
            const auto& actor = castActors_[index];
            const glm::mat4 placement = glm::translate(glm::mat4(1.0f), actor.position) *
                glm::rotate(glm::mat4(1.0f), actor.yawRadians,
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(actor.scale)) *
                glm::translate(glm::mat4(1.0f), actor.modelOffset);
            std::copy_n(glm::value_ptr(placement), 16, options.instanceTransform.begin());
            require(drawGltfSceneAsset(renderer, *castModels_[index], actorPoses[index],
                                       options, stats, &error), error);
        }
        for (std::size_t index = 0; index < castActors_.size(); ++index) {
            const auto& actor = castActors_[index];
            if (actor.nativeObject.empty()) continue;
            render::BattlePokemonFrame frame;
            frame.renderer = &renderer;
            frame.assetId = actor.slot.c_str();
            frame.phloPath = actor.nativeObject.c_str();
            frame.animation = actor.idleAnimation.c_str();
            frame.animationTime = tick * battleFixedDelta + index * .17f;
            std::copy_n(glm::value_ptr(actor.position), 3, frame.position);
            frame.yawRadians = actor.yawRadians;
            frame.scale = actor.scale;
            std::copy_n(glm::value_ptr(cameraPose_.position), 3, frame.cameraPosition);
            std::copy_n(glm::value_ptr(cameraPose_.lookTarget), 3, frame.cameraTarget);
            std::copy_n(glm::value_ptr(cameraPose_.up), 3, frame.cameraUp);
            frame.verticalFovDegrees = cameraPose_.verticalFovDegrees;
            frame.nearPlane = cameraPose_.nearPlane;
            frame.farPlane = cameraPose_.farPlane;
            frame.width = width;
            frame.height = height;
            if (!battlePokemonApi_.render(battlePokemonApi_.context, &frame))
                throw std::runtime_error(std::string("Native battle Pokemon render failed: ") +
                    battlePokemonApi_.lastError(battlePokemonApi_.context));
        }
    } else {
        const float duration = wes_->animations[1].durationSec;
        auto& actorPoses = standInPoses_;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            const float sampleTime = std::fmod(tick * battleFixedDelta + index * .17f,
                                               duration);
            bool sampled = false;
            if (index == 1 && sequence_.state().active && tick >= 110 && tick < 119) {
                render::animation::PoseBlendRequest recovery;
                recovery.baseClipIndex = 8;
                recovery.baseTimeSec = std::fmod((tick - 60) * battleFixedDelta,
                                                 wes_->animations[8].durationSec);
                recovery.targetClipIndex = 1;
                recovery.targetTimeSec = sampleTime;
                recovery.crossfadeWeight = (tick - 109) / 9.0f;
                sampled = sampleGltfSceneBlendedPose(wes_, recovery, actorPoses[index], &error);
            } else if (index == 1 && sequence_.state().gestureClip == 8) {
                render::animation::PoseBlendRequest gesture;
                gesture.baseClipIndex = 1;
                gesture.baseTimeSec = sampleTime;
                gesture.targetClipIndex = 8;
                gesture.targetTimeSec = std::fmod((tick - 60) * battleFixedDelta,
                                                   wes_->animations[8].durationSec);
                gesture.crossfadeWeight = std::min(1.0f, (tick - 59) / 9.0f);
                sampled = sampleGltfSceneBlendedPose(wes_, gesture, actorPoses[index], &error);
            } else {
                sampled = sampleGltfScenePose(wes_, sampleTime, 1,
                                              actorPoses[index], &error);
            }
            require(sampled, error);
        }
        for (std::size_t index = 0; index < slots.size(); ++index) {
            const glm::mat4 placement = glm::translate(glm::mat4(1.0f), slots[index].position) *
                glm::rotate(glm::mat4(1.0f), index < 2 ? 0.0f : 3.14159265f,
                            glm::vec3(0.0f, 1.0f, 0.0f));
            std::copy_n(glm::value_ptr(placement), 16, options.instanceTransform.begin());
            require(drawGltfSceneAsset(renderer, *wes_, actorPoses[index],
                                       options, stats, &error), error);
        }
    }
    BattleVfxFrame effectFrame;
    effectFrame.renderer = &renderer;
    effectFrame.viewProjection = glm::value_ptr(viewProjection);
    effectFrame.cameraPosition[0] = cameraPose_.position.x;
    effectFrame.cameraPosition[1] = cameraPose_.position.y;
    effectFrame.cameraPosition[2] = cameraPose_.position.z;
    effectFrame.cameraRight[0] = cameraPose_.right.x;
    effectFrame.cameraRight[1] = cameraPose_.right.y;
    effectFrame.cameraRight[2] = cameraPose_.right.z;
    effectFrame.cameraUp[0] = cameraPose_.up.x;
    effectFrame.cameraUp[1] = cameraPose_.up.y;
    effectFrame.cameraUp[2] = cameraPose_.up.z;
    effectFrame.cameraForward[0] = cameraPose_.forward.x;
    effectFrame.cameraForward[1] = cameraPose_.forward.y;
    effectFrame.cameraForward[2] = cameraPose_.forward.z;
    effectFrame.width = width;
    effectFrame.height = height;
    vfxPlugin_.render(effectFrame);
}

void BattlePreview::drawOverlay(IRenderBackend& renderer, int width, int height) const {
    if (!sequence_.state().hudVisible) return;
    hudAtlas_.draw(renderer, width, height);
    std::array<IRenderBackend::DebugQuad, 10> quads{};
    std::size_t count = 0;
    const float panelWidth = hudAtlas_.available() ? 150.0f : std::min(180.0f, width * .24f);
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const float x = hudAtlas_.available()
            ? (index % 2 == 0 ? 37.0f : width - 187.0f)
            : (index < 2 ? 12.0f : width - panelWidth - 12.0f);
        const float y = hudAtlas_.available()
            ? (index < 2 ? height - 39.0f : 83.0f)
            : height - 50.0f + (index % 2) * 18.0f;
        const float pad = hudAtlas_.available() ? 2.0f : 3.0f;
        quads[count++] = {x, y, panelWidth, hudAtlas_.available() ? 10.0f : 12.0f,
                          .04f, .06f, .09f, 1.0f};
        quads[count++] = {x + pad, y + pad,
                          (panelWidth - pad * 2.0f) * sequence_.state().health[index], 6.0f,
                          index < 2 ? .15f : .8f, index < 2 ? .8f : .55f,
                          .25f, 1.0f};
    }
    if (!vfxPlugin_.available() && activeEffect_ && sequence_.state().effectVisible)
        quads[count++] = {width * .5f + 80.0f, height * .5f - 45.0f,
                          14.0f, 14.0f, 1.0f, .75f, .2f, 1.0f};
    renderer.drawDebugQuads(quads.data(), count, width, height);
}

nlohmann::json BattlePreview::imports() const {
    nlohmann::json result = nlohmann::json::array();
    auto append = [&](const auto& asset) {
        result.push_back({{"identity", asset->sourceIdentity},
                          {"primitives", asset->stats.primitiveCount},
                          {"vertices", asset->stats.vertexCount},
                          {"indices", asset->stats.indexCount},
                          {"materials", asset->stats.materialCount},
                          {"clips", asset->stats.clipCount}});
    };
    append(field_);
    if (castEnabled_) {
        for (std::size_t index = 0; index < castModels_.size(); ++index) {
            if (castModels_[index]) append(castModels_[index]);
            else {
                auto entry = castActors_[index].nativeStats;
                entry["identity"] = "phlo:" + castActors_[index].nativeSha256;
                entry["format"] = "native-phlo";
                result.push_back(std::move(entry));
            }
        }
    } else {
        append(wes_);
    }
    return result;
}

nlohmann::json BattlePreview::state(int tick, std::array<int, 4> viewport) const {
    nlohmann::json actors = nlohmann::json::array();
    if (castEnabled_) {
        for (const auto& actor : castActors_)
            actors.push_back({{"slot", actor.slot}, {"display_name", actor.displayName},
                              {"model", actor.model}, {"position", vec(actor.position)},
                              {"presentation", actor.nativeObject.empty()
                                  ? "gltf" : "native-phlo"},
                              {"yaw_radians", actor.yawRadians},
                              {"scale", actor.scale}, {"model_offset", vec(actor.modelOffset)},
                              {"focus_height", actor.focusHeight}});
    } else {
        for (const auto& actor : slots)
            actors.push_back({{"slot", actor.name}, {"position", vec(actor.position)},
                              {"stand_in", "wes"}});
    }
    const auto& encounter = sequence_.state();
    camera::BattleCameraPose rigPose;
    const bool cameraActive = camera_.pose(rigPose);
    return {{"tick", tick}, {"phase", phase_}, {"viewport", viewport},
            {"camera_mode", freeCamera_.active() ? "free" : "scripted"},
            {"encounter", {{"tick", encounter.tick}, {"active", encounter.active},
                           {"camera_active", cameraActive},
                           {"cancelled", encounter.cancelled}, {"skipped", encounter.skipped},
                           {"hud_visible", encounter.hudVisible},
                           {"effect_visible", encounter.effectVisible},
                           {"vfx_active", vfxPlugin_.available()
                                               ? vfxPlugin_.activeCount() > 0 : activeEffect_},
                           {"vfx_adapter", vfxPlugin_.available()},
                           {"gesture_clip", encounter.gestureClip},
                           {"health", encounter.health},
                           {"audio_voices", mixer_.activeVoices()},
                           {"audio_device_ready", audioOutput_.deviceReady()},
                           {"audio_peak", lastAudioPeak_}}},
            {"camera", {{"position", vec(cameraPose_.position)},
                         {"target", vec(cameraPose_.lookTarget)},
                         {"forward", vec(cameraPose_.forward)},
                         {"right", vec(cameraPose_.right)},
                         {"up", vec(cameraPose_.up)},
                         {"distance", cameraPose_.distance},
                         {"aspect", cameraPose_.aspectRatio},
                         {"fov", cameraPose_.verticalFovDegrees},
                         {"blend", cameraPose_.blendProgress},
                         {"shake", vec(cameraPose_.shakeOffset)}}},
            {"actors", actors}};
}

void writeBattleHandshake(const std::string& path, const char* host,
                          const std::string& requested, const BattlePreview& preview,
                          IRenderBackend& renderer, int tick, int width, int height,
                          int renderFrame, std::array<int, 4> viewport) {
    if (path.empty()) return;
    nlohmann::json result{{"schema", "phlosion-battle-preview-v1"},
                          {"host", host}, {"requested_api", requested},
                          {"api", renderer.backendId()},
                          {"gpu", renderer.activeGpuName()},
                          {"gpu_discrete", renderer.activeGpuIsDiscrete()},
                          {"width", width}, {"height", height},
                          {"render_frame", renderFrame},
                          {"state", preview.state(tick, viewport)},
                          {"imports", preview.imports()}};
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot write battle handshake");
    output << result.dump(2);
}

} // namespace engine::exploration_fixture
