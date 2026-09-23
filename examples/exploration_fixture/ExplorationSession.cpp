#include "ExplorationSession.h"
#include "engine/core/Environment.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <stdexcept>

namespace engine::exploration_fixture {
namespace {
void require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
nlohmann::json vec(glm::vec3 v) { return {v.x, v.y, v.z}; }
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
    // Provisional collision proxy in glTF Y-up coordinates, based on the
    // recovered S1_out geometry. It is intentionally separate from the mesh.
    static const std::vector<CourseBox> boxes{
        {"forecourt", {0,-.25f,0}, {55,.25f,55}, 0, 0},
        {"stand-and-train", {2.7f,2.0f,-3.2f}, {6.5f,2.0f,4.0f}, 0, 1},
        {"approach", {-2,.2f,1.0f}, {1.5f,.2f,1.0f}, 0, 6, true}
    };
    return boxes;
}
const std::vector<std::string>& cases() {
    static const std::vector<std::string> names{"walk","run","wall-blocked","ramp","stairs","trigger-active","camera-obstructed","recovered-reset"}; return names;
}
const std::vector<std::string>& supplementalCases() {static const std::vector<std::string> names{"steep","ledge-drop"};return names;}
glm::vec3 caseSpawn(const std::string& n) {
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
    require(actions_.define({"look", ActionKind::Axis2D,{StickBinding{GamepadStick::Right}}},&error),error);
    require(actions_.define({"zoom", ActionKind::Axis1D,{ButtonBinding{GamepadButton::LeftShoulder,{-1,0}},ButtonBinding{GamepadButton::RightShoulder,{1,0}}}},&error),error);
    actions_.setEnabled(false);
    std::uint64_t id = 1;
    for (const auto& box : case_ == "outskirt" ? outskirtCourse() : course()) {
        collision::StaticColliderDesc d; d.position = box.center; d.rotation = glm::angleAxis(box.pitch,glm::vec3(1,0,0)); d.sensor = box.sensor; d.userData = id++;
        collision::ColliderId collider; require(world_.addBox(d,box.half,collider,&error),error);
    }
    require(world_.createCharacter({},caseSpawn(case_),playerId_,&error),error);
    require(world_.createCharacter({}, case_ == "outskirt" ? glm::vec3(-5,.9f,3) : glm::vec3(-1,.9f,3),npcId_,&error),error);
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
    require(world_.createCharacter({},safe && case_ != "outskirt" ? caseSpawn("walk") : caseSpawn(case_),playerId_,&error),error);
    require(world_.createCharacter({}, case_ == "outskirt" ? glm::vec3(-5,.9f,3) : glm::vec3(-1,.9f,3),npcId_,&error),error);
    require(world_.characterState(playerId_,playerState_,&error),error);
    require(world_.characterState(npcId_,npcState_,&error),error);
    require(rig_.reset(playerState_.center,aspect_,pose_,&world_,&error),error);
    sensorActive_ = false; npcStall_ = 0; npcWait_ = 0; ++resets_;
    record("reset");
}
void ExplorationSession::npcTick() {
    if (case_ == "outskirt") return;
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
    if (automation_ && ticks_ >= targetTick) return;
    actions_.advanceFrame(); ++ticks_;
    if (action("reset").pressed) { reset(); record("tick"); return; }
    glm::vec2 movement; std::string error;
    require(camera::cameraRelativeMove(action("move").value,pose_.yawRadians,movement,&error),error);
    require(world_.stepCharacter(playerId_,{movement,action("run").down},playerState_,&error),error);
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
        {"sensor",sensorActive_},{"enter",enters_},{"stay",stays_},{"exit",exits_},{"resets",resets_},
        {"camera",{{"position",vec(pose_.position)},{"pivot",vec(pose_.pivot)},{"yaw",pose_.yawRadians},{"pitch",pose_.pitchRadians},
            {"desired",pose_.desiredDistance},{"distance",pose_.actualDistance},{"obstructed",pose_.obstructed},{"aspect",pose_.aspectRatio}}},
        {"viewport",{context_.viewport.x,context_.viewport.y,context_.viewport.width,context_.viewport.height}},
        {"input",{{"move",{move.value.x,move.value.y}},{"run",action("run").down},{"enabled",actions_.enabled()},
            {"focus",context_.windowFocused},{"active",context_.activeGameViewport},{"playing",context_.playing},{"paused",context_.paused},
            {"viewport_focus",context_.viewportFocused},{"hover",context_.viewportHovered},{"text",context_.textEditing},
            {"synthetic",context_.synthetic},{"background_controller_input",SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,SDL_FALSE)==SDL_TRUE},
            {"selected",selected},{"devices",devices}}}};
}
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
