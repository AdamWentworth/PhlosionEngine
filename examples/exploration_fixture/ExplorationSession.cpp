#include "ExplorationSession.h"
#include "engine/core/Environment.h"
#include "engine/render/gltf/GltfSceneAsset.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace engine::exploration_fixture {
namespace {
void require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
nlohmann::json vec(glm::vec3 v) { return {v.x, v.y, v.z}; }
OutskirtEncounterScene loadEncounterScene() {
    const auto path = env::get("PHLOSION_OUTSKIRT_ENCOUNTER_JSON");
    require(path && !path->empty(), "Outskirt encounter requires PHLOSION_OUTSKIRT_ENCOUNTER_JSON");
    std::ifstream file(*path);
    require(bool(file), "Cannot read Outskirt encounter scene: " + *path);
    const auto scene = nlohmann::json::parse(file);
    require(scene.at("schema") == "pokemon-cyberpunk-outskirt-encounter-v2", "Unexpected Outskirt encounter schema");
    OutskirtEncounterScene result;
    auto position = [](const nlohmann::json& value) {
        require(value.is_array() && value.size() == 3, "Encounter position needs three coordinates");
        return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    };
    result.playerSpawn = position(scene.at("wes_spawn"));
    result.willieSpawn = position(scene.at("willie_spawn"));
    result.willieYawRadians = glm::radians(scene.at("willie_yaw_degrees").get<float>());
    result.interactionRadius = scene.at("interaction_radius").get<float>();
    result.willieModel = scene.at("willie_model").get<std::string>();
    result.willieIdleAnimation = scene.at("willie_idle_animation").get<std::string>();
    const auto& collision = scene.at("collision");
    const auto& meshNodes = collision.at("mesh_nodes");
    require(meshNodes.is_array() && !meshNodes.empty(), "Outskirt collision needs mesh nodes");
    std::unordered_set<std::string> names;
    for (const auto& value : meshNodes) {
        const auto name = value.get<std::string>();
        require(!name.empty() && names.insert(name).second,
                "Outskirt collision mesh names must be unique");
        result.collisionMeshNodes.push_back(name);
    }
    const auto& boxes = collision.at("boxes");
    require(boxes.is_array(), "Outskirt collision boxes must be an array");
    for (const auto& value : boxes) {
        CourseBox box;
        box.name = value.at("name").get<std::string>();
        box.center = position(value.at("center"));
        box.half = position(value.at("half_extents"));
        box.pitch = glm::radians(value.value("pitch_degrees", 0.0f));
        box.sensor = value.value("sensor", false);
        require(!box.name.empty() && names.insert(box.name).second,
                "Outskirt collision names must be unique");
        require(std::isfinite(box.center.x) && std::isfinite(box.center.y) &&
                std::isfinite(box.center.z) && std::isfinite(box.half.x) &&
                std::isfinite(box.half.y) && std::isfinite(box.half.z) &&
                std::isfinite(box.pitch) && glm::all(glm::greaterThan(box.half, glm::vec3(0))),
                "Invalid Outskirt collision box");
        result.collisionBoxes.push_back(box);
    }
    require(std::isfinite(result.interactionRadius) && result.interactionRadius > 0.5f && result.interactionRadius < 5.0f,
            "Invalid Outskirt interaction radius");
    require(std::isfinite(result.playerSpawn.x) && std::isfinite(result.playerSpawn.y) && std::isfinite(result.playerSpawn.z) &&
            std::isfinite(result.willieSpawn.x) && std::isfinite(result.willieSpawn.y) && std::isfinite(result.willieSpawn.z) &&
            std::isfinite(result.willieYawRadians), "Invalid Outskirt placement");
    const auto root = env::get("PHLOSION_BATTLE_CAST_ROOT");
    require(root && !root->empty(), "Outskirt encounter requires PHLOSION_BATTLE_CAST_ROOT");
    result.willieModel = *root + "/" + result.willieModel;
    return result;
}
void addOutskirtSurfaceCollision(collision::CollisionWorld& world,
                                 const std::vector<std::string>& names) {
    using namespace render::gltf::scene;
    const auto path = env::get("PHLOSION_OUTSKIRT_ENVIRONMENT_GLB");
    require(path && !path->empty(), "Outskirt collision requires PHLOSION_OUTSKIRT_ENVIRONMENT_GLB");
    std::shared_ptr<const GltfSceneAsset> asset;
    std::string error;
    const bool loaded = loadGltfSceneAsset(*path, asset, &error);
    require(loaded, error);
    GltfScenePose pose;
    const bool sampled = sampleGltfScenePose(asset, 0, kBindPoseClip, pose, &error);
    require(sampled, error);
    for (std::size_t meshIndex = 0; meshIndex < names.size(); ++meshIndex) {
        const GltfScenePrimitive* selected = nullptr;
        for (const auto& primitive : asset->primitives) {
            if (asset->nodes.at(static_cast<std::size_t>(primitive.nodeIndex)).name == names[meshIndex]) {
                require(selected == nullptr, "duplicate Outskirt collision node: " + names[meshIndex]);
                selected = &primitive;
            }
        }
        require(selected != nullptr, "missing Outskirt collision node: " + names[meshIndex]);
        const glm::mat4& transform = pose.global.at(static_cast<std::size_t>(selected->nodeIndex));
        std::vector<glm::vec3> vertices;
        vertices.reserve(selected->vertices.size());
        for (const auto& vertex : selected->vertices)
            vertices.emplace_back(transform * glm::vec4(vertex.x, vertex.y, vertex.z, 1.0f));
        std::vector<std::uint32_t> indices;
        indices.reserve(selected->indices.size());
        std::size_t degenerate = 0;
        for (std::size_t index = 0; index < selected->indices.size(); index += 3) {
            const auto a = selected->indices[index];
            const auto b = selected->indices[index + 1];
            const auto c = selected->indices[index + 2];
            const glm::vec3 cross = glm::cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            if (glm::dot(cross, cross) <= 1.0e-12f) {
                ++degenerate;
                continue;
            }
            indices.insert(indices.end(), {a, b, c});
        }
        require(!indices.empty() && degenerate * 10 <= selected->indices.size() / 3,
                names[meshIndex] + ": excessive degenerate collision triangles (" +
                std::to_string(degenerate) + ")");
        collision::StaticColliderDesc desc;
        desc.userData = meshIndex + 1;
        collision::ColliderId collider;
        const bool added = world.addTriangleMesh(desc, vertices, indices, collider, &error);
        require(added, names[meshIndex] + ": " + error);
    }
}
}
const std::vector<CourseBox>& course() {
    static const std::vector<CourseBox> boxes{
        {"floor", {0,-.25f,0}, {12,.25f,12}, 0, 0},
        {"thin-wall", {0,1.4f,-1}, {2,1.4f,.08f}, 0, 1},
        {"back-wall", {0,1,-9}, {9,1,.2f}, 0, 2},
        {"ramp", {-5,.50f,-3}, {1.4f,.2f,2}, glm::radians(20.0f), 3},
        {"steep", {-8,.70f,-3}, {1,.18f,1.3f}, glm::radians(60.0f), 4},
        {"step-one", {5,.10f,-1}, {1,.10f,.3f}, 0, 5},
        {"step-two", {5,.20f,-1.6f}, {1,.20f,.3f}, 0, 5},
        {"step-three", {5,.30f,-2.2f}, {1,.30f,.3f}, 0, 5},
        {"ledge", {5,.4f,-3.0f}, {1,.4f,.5f}, 0, 5},
        {"sensor", {3,.45f,3}, {1,.45f,1}, 0, 6, true},
        {"landmark-west", {-10,1.5f,-6}, {.5f,1.5f,.5f}, 0, 4},
        {"landmark-east", {8,1.5f,-6}, {.5f,1.5f,.5f}, 0, 3}
    };
    return boxes;
}
const std::vector<CourseBox>& outskirtCourse() {
    // Gameplay trigger and train-side blockers stay authored. The center gap
    // leaves the recovered ramp and cabin threshold traversable.
    static const std::vector<CourseBox> boxes{
        {"approach", {-2,.2f,1.0f}, {1.5f,.2f,1.0f}, 0, 6, true},
        {"train-right", {4.5f,1.5f,-3.15f}, {4.8f,1.5f,4.0f}, 0, 1},
        {"train-left", {-3.2f,1.5f,-3.15f}, {.5f,1.5f,4.0f}, 0, 1}
    };
    return boxes;
}
const std::vector<std::string>& cases() {
    static const std::vector<std::string> names{"walk","run","wall-blocked","ramp","stairs","trigger-active","camera-obstructed","recovered-reset"}; return names;
}
const std::vector<std::string>& supplementalCases() {static const std::vector<std::string> names{"steep","ledge-drop"};return names;}
glm::vec3 caseSpawn(const std::string& n) {
    if (n == "outskirt-encounter") return {-2,.9f,3};
    if (n == "outskirt") return {-2,.9f,3};
    if (n == "steep") return {-8,.9f,-1};
    if (n == "ledge-drop") return {5,1.705f,-3};
    if (n == "run") return {-5,.9f,6};
    if (n == "wall-blocked") return {0,.9f,1};
    if (n == "ramp") return {-5,.9f,0};
    if (n == "stairs") return {5,.9f,0};
    if (n == "trigger-active") return {3,.9f,6};
    if (n == "camera-obstructed" || n == "recovered-reset") return {0,.9f,-3};
    if (n == "walk") return {0,.9f,4};
    throw std::runtime_error("Unknown exploration case: " + n);
}
camera::ExplorationCameraSettings cameraSettings() {
    camera::ExplorationCameraSettings s; s.pivotOffset = {0,.45f,0}; s.desiredDistance = 7;
    s.initialPitchRadians = .48f; s.followHalfLifeSeconds = .08f; s.farPlane = 100; return s;
}
camera::ExplorationCameraSettings configuredCameraSettings() {
    auto settings=cameraSettings();
    auto read=[](const char* name,float& out){if(const auto value=env::get(name);value&&!value->empty()){
        std::size_t consumed=0;out=std::stof(*value,&consumed);if(consumed!=value->size()||!std::isfinite(out))throw std::runtime_error(std::string("Invalid camera setting: ")+name);
    }};
    read("PHLOSION_EXPLORATION_CAMERA_DISTANCE",settings.desiredDistance);
    read("PHLOSION_EXPLORATION_CAMERA_PITCH",settings.initialPitchRadians);
    read("PHLOSION_EXPLORATION_CAMERA_YAW",settings.initialYawRadians);
    return settings;
}
ExplorationSession::ExplorationSession(std::string name, bool automation, camera::ExplorationCameraSettings settings)
    : case_(std::move(name)), automation_(automation), rig_(settings) {
    using namespace input;
    std::string error;
    require(actions_.define({"move", ActionKind::Axis2D, {KeyBinding{InputEvent::Key::W,{0,-1}},
        KeyBinding{InputEvent::Key::S,{0,1}},KeyBinding{InputEvent::Key::A,{-1,0}},KeyBinding{InputEvent::Key::D,{1,0}},StickBinding{GamepadStick::Left}}}, &error),error);
    require(actions_.define({"run", ActionKind::Button, {KeyBinding{InputEvent::Key::LShift},ButtonBinding{GamepadButton::LeftStick}}},&error),error);
    require(actions_.define({"reset", ActionKind::Button,{KeyBinding{InputEvent::Key::R},ButtonBinding{GamepadButton::Back}}},&error),error);
    require(actions_.define({"interact", ActionKind::Button,{KeyBinding{InputEvent::Key::E},ButtonBinding{GamepadButton::South}}},&error),error);
    require(actions_.define({"look", ActionKind::Axis2D,{StickBinding{GamepadStick::Right}}},&error),error);
    require(actions_.define({"zoom", ActionKind::Axis1D,{ButtonBinding{GamepadButton::LeftShoulder,{-1,0}},ButtonBinding{GamepadButton::RightShoulder,{1,0}}}},&error),error);
    actions_.setEnabled(false);
    if (case_ == "outskirt-encounter") encounterScene_ = loadEncounterScene();
    const bool outskirt = case_ == "outskirt" || encounterScene_.has_value();
    if (outskirt) {
        static const std::vector<std::string> fallbackMeshNodes{
            "S1_out_mesh_1.001", "S1_out_mesh_007",
            "S1_out_mesh_0.002", "S1_out_mesh_094"};
        addOutskirtSurfaceCollision(world_, encounterScene_ ?
                                    encounterScene_->collisionMeshNodes : fallbackMeshNodes);
    }
    std::uint64_t id = outskirt ? 100 : 1;
    const auto& boxes = encounterScene_ ? encounterScene_->collisionBoxes :
                        outskirt ? outskirtCourse() : course();
    for (const auto& box : boxes) {
        collision::StaticColliderDesc d; d.position = box.center; d.rotation = glm::angleAxis(box.pitch,glm::vec3(1,0,0)); d.sensor = box.sensor; d.userData = id++;
        collision::ColliderId collider; require(world_.addBox(d,box.half,collider,&error),error);
    }
    require(world_.createCharacter({},encounterScene_ ? encounterScene_->playerSpawn : caseSpawn(case_),playerId_,&error),error);
    require(world_.createCharacter({}, encounterScene_ ? encounterScene_->willieSpawn :
        case_ == "outskirt" ? glm::vec3(-5,.9f,3) : glm::vec3(-1,.9f,3),npcId_,&error),error);
    reset(false); resets_ = 0;
}
void ExplorationSession::setTrace(const std::string& path) {
    if (!path.empty()) { trace_.open(path); if (!trace_) throw std::runtime_error("Cannot write exploration trace"); record("open"); }
}
void ExplorationSession::begin(const editor::EditorSdlGameInputContext& c) {
    ++inputFrames_;
    if (!input_) input_ = std::make_unique<input::SdlGameInput>(actions_,c.windowId,!automation_&&!c.synthetic);
    context_ = c;
    if (c.viewport.width > 0 && c.viewport.height > 0) aspect_ = static_cast<float>(c.viewport.width) / static_cast<float>(c.viewport.height);
    input_->begin(c);
}
void ExplorationSession::event(const editor::EditorSdlGameInputContext& c, const SDL_Event& e) { context_ = c; if (input_) input_->event(c,e); }
void ExplorationSession::end(const editor::EditorSdlGameInputContext& c) { context_ = c; if (input_) input_->end(c); record("input-frame"); }
input::ActionState ExplorationSession::action(const char* name) const { input::ActionState s; std::string e; require(actions_.state(name,s,&e),e); return s; }
void ExplorationSession::reset(bool safe) {
    std::string error;
    actions_.setEnabled(false);
    if (input_) { input_->takeLookDelta(); input_->takeZoomDelta(); }
    // Recreate characters to clear their trigger history; immutable authored rig settings survive.
    world_.removeCharacter(playerId_); world_.removeCharacter(npcId_);
    require(world_.createCharacter({},encounterScene_ ? encounterScene_->playerSpawn :
        safe && case_ != "outskirt" ? caseSpawn("walk") : caseSpawn(case_),playerId_,&error),error);
    require(world_.createCharacter({}, encounterScene_ ? encounterScene_->willieSpawn :
        case_ == "outskirt" ? glm::vec3(-5,.9f,3) : glm::vec3(-1,.9f,3),npcId_,&error),error);
    require(world_.characterState(playerId_,playerState_,&error),error);
    require(world_.characterState(npcId_,npcState_,&error),error);
    require(rig_.reset(playerState_.center,aspect_,pose_,&world_,&error),error);
    sensorActive_ = false; encounterRequested_ = false; npcStall_ = 0; npcWait_ = 0; ++resets_;
    record("reset");
}
void ExplorationSession::npcTick() {
    if (case_ == "outskirt" || encounterScene_) return;
    std::string error;
    if (npcWait_ > 0) {
        if (--npcWait_ == 0) { require(world_.teleportCharacter(npcId_,{-1,.9f,3},&error),error); ++npcRetries_; }
        require(world_.stepCharacter(npcId_,{},npcState_,&error),error); return;
    }
    const auto before = npcState_.center;
    const glm::vec2 waypoint(-1,-4);
    const glm::vec2 toWaypoint=waypoint-glm::vec2(npcState_.center.x,npcState_.center.z);
    const auto intent=glm::length(toWaypoint)>.1f?glm::normalize(toWaypoint):glm::vec2(0);
    require(world_.stepCharacter(npcId_,{intent,true},npcState_,&error),error);
    if (glm::length(glm::vec2(npcState_.center.x-before.x,npcState_.center.z-before.z)) < .005f) ++npcStall_; else npcStall_ = 0;
    if (npcStall_ == 10) { ++npcBlocked_; npcWait_ = 25; npcStall_ = 0; }
}
void ExplorationSession::tick() {
    if (!input_) return;
    if (automation_ && !encounterScene_ && ticks_ >= targetTick) return;
    actions_.advanceFrame(); ++ticks_;
    if (action("reset").pressed) { reset(); record("tick"); return; }
    glm::vec2 movement; std::string error;
    require(camera::cameraRelativeMove(action("move").value,pose_.yawRadians,movement,&error),error);
    require(world_.stepCharacter(playerId_,{movement,action("run").down},playerState_,&error),error);
    if (encounterScene_ && action("interact").pressed && canInteract()) encounterRequested_ = true;
    if (playerState_.center.y < -8) {reset();record("tick");return;}
    collision::CharacterTriggerSample sample; require(world_.sampleCharacterTriggers(playerId_,sample,{},&error),error);
    sensorActive_ = !sample.overlaps.empty();
    for (const auto& e : sample.events) {
        if (e.kind == collision::TriggerEventKind::Enter) ++enters_;
        if (e.kind == collision::TriggerEventKind::Stay) ++stays_;
        if (e.kind == collision::TriggerEventKind::Exit) ++exits_;
    }
    npcTick();
    camera::ExplorationCameraFrame frame; frame.targetPosition = playerState_.center; frame.aspectRatio = aspect_; frame.deltaSeconds = fixedDelta;
    frame.lookRateRadiansPerSecond = action("look").value * glm::vec2(-1.6f,1.3f); frame.zoomRate = action("zoom").value.x * 3;
    if (input_) { frame.lookDeltaRadians = input_->takeLookDelta(); frame.zoomDelta = input_->takeZoomDelta(); }
    require(rig_.step(frame,pose_,&world_,&error),error); record("tick");
}
nlohmann::json ExplorationSession::state() const {
    nlohmann::json devices = nlohmann::json::array(); bool selected = false;
    if (input_) for (const auto& d : input_->devices()) {
        const bool active = actions_.selectedController() == d.id; selected |= active;
        devices.push_back({{"name",d.name},{"selected",active}});
    }
    const auto move = action("move");
    return {{"tick",ticks_},{"input_frame",inputFrames_},{"case",case_},{"player",vec(playerState_.center)},{"velocity",vec(playerState_.velocity)},
        {"ground",static_cast<int>(playerState_.groundState)},{"support",playerState_.support ? playerState_.support->userData : 0},
        {"npc",vec(npcState_.center)},{"npc_blocked",npcBlocked_},{"npc_retries",npcRetries_},
        {"sensor",sensorActive_},{"can_interact",canInteract()},{"encounter_requested",encounterRequested_},
        {"enter",enters_},{"stay",stays_},{"exit",exits_},{"resets",resets_},
        {"camera",{{"position",vec(pose_.position)},{"pivot",vec(pose_.pivot)},{"yaw",pose_.yawRadians},{"pitch",pose_.pitchRadians},
            {"desired",pose_.desiredDistance},{"distance",pose_.actualDistance},{"obstructed",pose_.obstructed},{"aspect",pose_.aspectRatio}}},
        {"viewport",{context_.viewport.x,context_.viewport.y,context_.viewport.width,context_.viewport.height}},
        {"input",{{"move",{move.value.x,move.value.y}},{"run",action("run").down},{"enabled",actions_.enabled()},
            {"focus",context_.windowFocused},{"active",context_.activeGameViewport},{"playing",context_.playing},{"paused",context_.paused},
            {"viewport_focus",context_.viewportFocused},{"hover",context_.viewportHovered},{"text",context_.textEditing},
            {"synthetic",context_.synthetic},{"background_controller_input",SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,SDL_FALSE)==SDL_TRUE},
            {"selected",selected},{"devices",devices}}}};
}
bool ExplorationSession::canInteract() const {
    if (!encounterScene_) return false;
    const glm::vec2 delta(playerState_.center.x - npcState_.center.x,
                          playerState_.center.z - npcState_.center.z);
    return glm::dot(delta, delta) <= encounterScene_->interactionRadius * encounterScene_->interactionRadius;
}
bool ExplorationSession::takeEncounterRequest() {
    const bool requested = encounterRequested_;
    encounterRequested_ = false;
    return requested;
}
void ExplorationSession::suspendInput() { actions_.setEnabled(false); }
void ExplorationSession::record(const char* kind) { if (trace_) { auto s = state(); s["kind"] = kind; trace_ << s.dump() << '\n'; trace_.flush(); } }

nlohmann::json replayScript(const std::string& name) {
    (void)caseSpawn(name); using json = nlohmann::json; json events = json::array();
    auto add = [&](int frame, const char* type, nlohmann::json extra) { extra["frame"] = frame; extra["type"] = type; events.push_back(extra); };
    auto key = [&](int f, const char* k, bool d) { add(f,"key",{{"key",k},{"down",d}}); };
    add(1,"attach",json::object()); add(2,"button",{{"index",6},{"value",1}}); add(3,"button",{{"index",6},{"value",0}});
    if (name == "camera-obstructed") {
        add(11,"axis",{{"index",2},{"value",12000}}); add(31,"axis",{{"index",2},{"value",0}});
    } else if (name == "recovered-reset") {
        add(8,"mouse",{{"down",true}}); add(9,"motion",{{"x",10},{"y",-5}}); add(10,"wheel",{{"y",1}});
        add(11,"state",{{"focus",false}}); add(12,"motion",{{"x",500},{"y",500}});
        add(13,"state",{{"focus",true},{"hover",false}}); add(14,"mouse",{{"down",false}});
        add(15,"state",{{"hover",true}});add(15,"axis",{{"index",0},{"value",32767}}); add(16,"state",{{"text",true}});
        add(17,"state",{{"text",false}}); add(18,"state",{{"active",false}}); add(19,"state",{{"active",true}});
        add(20,"state",{{"paused",true}}); add(22,"state",{{"paused",false},{"viewport_focus",false}});
        add(23,"state",{{"viewport_focus",true}});add(23,"axis",{{"index",0},{"value",0}}); add(24,"detach",json::object()); add(25,"attach",json::object());
        add(26,"axis",{{"index",0},{"value",32767}}); add(27,"axis",{{"index",0},{"value",0}});
        key(30,"R",true); key(31,"R",false);
    } else if (name == "ledge-drop") {
        key(11,"D",true);key(71,"D",false);
    } else if (name == "steep") {
        key(11,"W",true);key(11,"Left Shift",true);key(101,"W",false);key(101,"Left Shift",false);
    } else if (name == "trigger-active") {
        key(11,"W",true); key(11,"Left Shift",true);
        key(71,"W",false); key(71,"S",true); key(91,"S",false); key(91,"Left Shift",false);
    } else {
        key(11,"W",true); if (name == "run") key(11,"Left Shift",true);
        const int stop = (name == "wall-blocked" || name == "trigger-active") ? 101 : 71;
        key(stop,"W",false); if (name == "run") key(stop,"Left Shift",false);
    }
    return {{"schema","phlosion-sdl-replay-v1"},{"events",events}};
}
} // namespace engine::exploration_fixture
