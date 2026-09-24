#include "BattlePreview.h"
#include "ExplorationPresentation.h"
#include "engine/core/Environment.h"
#include "engine/editor/EditorProjectPlugin.h"

#include <chrono>

namespace {
using namespace engine;
bool battleNavigationEnabled(const editor::EditorSdlGameInputContext& c) {
    return c.windowFocused && c.activeGameViewport && c.viewportFocused &&
           !c.textEditing && c.viewport.width > 0 && c.viewport.height > 0;
}
class Runtime final : public editor::IEditorProjectRuntime {
public:
    bool open(const editor::EditorProjectOpenContext&,std::string* outError) override {
        try {
        automation_=env::get("PHLOSION_EXPLORATION_AUTOMATION").value_or("0")=="1";
        const auto caseName=env::get("PHLOSION_EXPLORATION_CASE").value_or("walk");
        battleMode_=caseName=="outskirt-battle";
        encounterMode_=caseName=="outskirt-encounter";
        if(battleMode_) battle_=std::make_unique<exploration_fixture::BattlePreview>(
            !automation_ || env::get("PHLOSION_BATTLE_AUDIO_OUTPUT").value_or("0")=="1");
        else {
            session_=std::make_unique<exploration_fixture::ExplorationSession>(caseName,automation_,exploration_fixture::configuredCameraSettings());
            session_->setTrace(env::get("PHLOSION_EXPLORATION_TRACE").value_or(""));
            presentation_=std::make_unique<exploration_fixture::ExplorationPresentation>(
                session_->caseName()=="outskirt" || encounterMode_, session_->encounterScene());
        }
        if(outError)outError->clear();
        return true;
        } catch(const std::exception& exception) {
            if(outError)*outError=exception.what();
            battle_.reset();
            session_.reset();
            presentation_.reset();
            return false;
        }
    }
    void prewarm(IRenderBackend& renderer,const editor::EditorProjectCameraContext&) override {renderer.prewarmWorldRenderAssets();}
    void update(float) override {
        if (!battle_) return;
        const auto now = std::chrono::steady_clock::now();
        const float delta = cameraUpdateTime_ == std::chrono::steady_clock::time_point{}
            ? 0.0f : std::chrono::duration<float>(now - cameraUpdateTime_).count();
        cameraUpdateTime_ = now;
        battle_->navigationFrame(delta);
    }
    void render(const editor::EditorProjectRenderContext& c) override {
        if(!c.renderer)return;
        if(battle_)battle_->draw(*c.renderer,battleTicks_,c.surfaceWidth,c.surfaceHeight,false);
        else presentation_->draw(*c.renderer,*session_,c.surfaceWidth,c.surfaceHeight);
    }
    editor::EditorProjectStats stats() const override {
        if(battle_ || (session_ && (session_->caseName()=="outskirt" || encounterMode_))) {
            editor::EditorProjectStats result{.sceneCount=1};
            std::size_t assetIndex=0;
            for(const auto& item : battle_ ? battle_->imports() : presentation_->imports()) {
                result.materialCount += item.at("materials").get<std::uint32_t>();
                const std::uint32_t instances =
                    battle_ && !battle_->usesAuthoredCast() && assetIndex == 1 ? 4u : 1u;
                result.drawClassCount += item.at("primitives").get<std::uint32_t>()*instances;
                result.visibleTriangleCount += item.at("indices").get<std::uint64_t>()/3*instances;
                ++assetIndex;
            }
            return result;
        }
        return {.sceneCount=1,.materialCount=10,.drawClassCount=14,.visibleTriangleCount=168};
    }
    const char* status() const noexcept override {
        if (encounterMode_) return "Outskirt: WASD / left stick move; Shift / L3 run; E / A talk to Willie nearby. "
            "Battle: arrows / D-pad choose, Enter / A confirm, Escape / B back; WASD / RMB camera; R reset.";
        if (battleMode_ && battle_ && battle_->usesAuthoredCast())
            return "Battle camera: WASD move, Q/E down/up, RMB look, Shift fast, "
                   "wheel dolly, R reset.";
        if (battleMode_)
            return "Provisional Outskirt battle camera and four Wes actor stand-ins.";
        return "WASD / left stick move; Shift / L3 run; RMB / right stick orbit; "
               "wheel / bumpers zoom; R / Back reset; Tab / Start select pad.";
    }
    std::size_t gamePreviewCount() const noexcept override {return 1;}
    editor::EditorProjectGamePreview gamePreview(std::size_t) const noexcept override {
        if (encounterMode_) return {"exploration","Outskirt Stand: Wes and Rider Willie (temporary)",
            "Prototypes",status(),"fixture/scene"};
        if (battleMode_) {
            const bool cast = battle_ && battle_->usesAuthoredCast();
            return {"battle", cast ? "Wes vs Rider Willie (temporary)" :
                    "Outskirt battle (provisional)",
                    cast ? "Prototypes" : "Samples", status(), "fixture/scene"};
        }
        if(session_ && session_->caseName()=="outskirt")return {"exploration","Outskirt Stand (provisional)","Samples",status(),"fixture/scene"};
        return {"exploration","Public exploration course","Samples",status(),"fixture/scene"};
    }
    bool initializeGamePreview(const editor::EditorProjectGamePreviewContext&,std::string*) override {ready_=true;return true;}
    bool selectGamePreview(const char* id,std::string*) override {
        if(std::string(id)!=(battleMode_?"battle":"exploration"))return false;
        if(selected_)resetGamePreview();
        selected_=true;return true;
    }
    bool gamePreviewReady() const noexcept override {return ready_;}
    bool selectAssetPreview(const char*,const char*,std::string* outError) override {
        if(outError)*outError="The temporary exploration fixture does not render cooked model previews.";
        return false;
    }
    void resetGamePreview() override {
        if (encounterMode_) {
            if (battle_) { battle_->release(); battle_.reset(); }
            session_->reset(); battleTicks_=0;cameraUpdateTime_={};lastOutcome_="reset";
        } else if(battle_){battle_->reset();battleTicks_=0;cameraUpdateTime_={};}
        else session_->reset();
    }
    void fixedUpdateGamePreview(float dt) override {
        if(std::abs(dt-exploration_fixture::fixedDelta)>.00001f)throw std::runtime_error("Fixture requires 60 Hz ticks");
        if (encounterMode_) {
            if (battle_) {
                if (battle_->complete()) {
                    const auto state = battle_->state(battleTicks_, battleViewport_);
                    lastOutcome_ = battle_->usesRules() ? state["rules"]["outcome"].get<std::string>() :
                        state["encounter"]["cancelled"].get<bool>() ? "cancelled" :
                        state["encounter"]["skipped"].get<bool>() ? "skipped" : "completed";
                    battle_->release();battle_.reset();battleTicks_=0;cameraUpdateTime_={};
                    session_->suspendInput();
                } else ++battleTicks_;
            } else {
                session_->tick();
                if (session_->takeEncounterRequest()) {
                    presentation_->release();
                    battle_=std::make_unique<exploration_fixture::BattlePreview>(
                        !automation_ || env::get("PHLOSION_BATTLE_AUDIO_OUTPUT").value_or("0")=="1");
                    ++encounterCount_;battleTicks_=0;cameraUpdateTime_={};
                    session_->suspendInput();
                }
            }
        } else if(battle_)++battleTicks_;else session_->tick();
    }
    void renderGamePreview(const editor::EditorProjectRenderContext& c) override {
        if(!c.renderer)return;++frames_;c.renderer->beginWorldSceneColorPass(c.surfaceWidth,c.surfaceHeight);
        if(battle_)battle_->draw(*c.renderer,battleTicks_,c.surfaceWidth,c.surfaceHeight);
        else presentation_->draw(*c.renderer,*session_,c.surfaceWidth,c.surfaceHeight);
        c.renderer->endWorldSceneColorPass();
        if(battle_)battle_->drawOverlay(*c.renderer,c.surfaceWidth,c.surfaceHeight);
        const int capture=std::stoi(env::get("PHLOSION_BACKEND_SCREENSHOT_FRAME").value_or("160"));
        if(frames_==capture) {
            if(encounterMode_)exploration_fixture::writeEncounterHandshake(env::get("PHLOSION_EXPLORATION_HANDSHAKE").value_or(""),"editor",
                env::get("PHLOSION_EXPLORATION_API").value_or(c.renderer->backendId()),*session_,*presentation_,
                battle_.get(),battleTicks_,encounterCount_,lastOutcome_,*c.renderer,c.surfaceWidth,c.surfaceHeight,frames_,battleViewport_);
            else if(battle_)exploration_fixture::writeBattleHandshake(env::get("PHLOSION_EXPLORATION_HANDSHAKE").value_or(""),"editor",
                env::get("PHLOSION_EXPLORATION_API").value_or(c.renderer->backendId()),*battle_,*c.renderer,battleTicks_,c.surfaceWidth,c.surfaceHeight,frames_,battleViewport_);
            else exploration_fixture::writeHandshake(env::get("PHLOSION_EXPLORATION_HANDSHAKE").value_or(""),"editor",
                env::get("PHLOSION_EXPLORATION_API").value_or(c.renderer->backendId()),*session_,*presentation_,*c.renderer,c.surfaceWidth,c.surfaceHeight,frames_);
        }
    }
    void begin(const editor::EditorSdlGameInputContext& c){
        if(battle_){
            battleViewport_={c.viewport.x,c.viewport.y,c.viewport.width,c.viewport.height};
            battle_->setNavigationEnabled(battleNavigationEnabled(c) &&
                (!battle_->usesRules() || editor::gameInputEnabled(c)));
        }
        else session_->begin(c);
    }
    void event(const editor::EditorSdlGameInputContext& c,const SDL_Event& e){
        if(session_ && !battle_){session_->event(c,e);return;}
        if(!battle_)return;
        battle_->setNavigationEnabled(battleNavigationEnabled(c) &&
            (!battle_->usesRules() || editor::gameInputEnabled(c)));
        const bool inside = e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP
            ? editor::gamePointerInside(c,e.button.x,e.button.y)
            : e.type == SDL_MOUSEMOTION
                ? editor::gamePointerInside(c,e.motion.x,e.motion.y)
                : c.viewportHovered;
        battle_->navigationEvent(e,c.windowId,inside);
        if(battleNavigationEnabled(c)&&(!battle_->usesRules()||editor::gameInputEnabled(c))&&
           e.type==SDL_KEYDOWN&&e.key.repeat==0&&
           e.key.keysym.sym==SDLK_r){
            if(battle_->usesRules()){battle_->reset();battleTicks_=0;cameraUpdateTime_={};}
            else resetGamePreview();
            return;
        }
        if(!editor::gameInputEnabled(c))return;
        if(battle_->usesRules()) {
            if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT&&inside)
                battle_->menuClick(e.button.x-c.viewport.x,e.button.y-c.viewport.y,
                                   c.viewport.width,c.viewport.height);
            if(e.type==SDL_KEYDOWN&&e.key.repeat==0) {
                if(e.key.keysym.sym==SDLK_UP)battle_->menuNavigate(-1);
                if(e.key.keysym.sym==SDLK_DOWN)battle_->menuNavigate(1);
                if(e.key.keysym.sym==SDLK_RETURN||e.key.keysym.sym==SDLK_SPACE)battle_->menuConfirm();
                if(e.key.keysym.sym==SDLK_ESCAPE)battle_->menuBack();
            }
            if(e.type==SDL_CONTROLLERBUTTONDOWN) {
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP)battle_->menuNavigate(-1);
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)battle_->menuNavigate(1);
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_A)battle_->menuConfirm();
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_B)battle_->menuBack();
            }
            return;
        }
        if(e.type==SDL_KEYDOWN&&e.key.repeat==0){
            if(e.key.keysym.sym==SDLK_ESCAPE)battle_->cancel();
            if(e.key.keysym.sym==SDLK_SPACE)battle_->skip();
        }
        if(e.type==SDL_CONTROLLERBUTTONDOWN){
            if(e.cbutton.button==SDL_CONTROLLER_BUTTON_B)battle_->cancel();
            if(e.cbutton.button==SDL_CONTROLLER_BUTTON_A)battle_->skip();
        }
    }
    void end(const editor::EditorSdlGameInputContext& c){
        if(session_ && !battle_)session_->end(c);
        if(battle_)battle_->setNavigationEnabled(battleNavigationEnabled(c) &&
            (!battle_->usesRules() || editor::gameInputEnabled(c)));
    }
private:
    std::unique_ptr<exploration_fixture::ExplorationSession> session_;
    std::unique_ptr<exploration_fixture::ExplorationPresentation> presentation_;
    std::unique_ptr<exploration_fixture::BattlePreview> battle_;
    std::array<int,4> battleViewport_{0,0,0,0};
    bool automation_=false,ready_=false,selected_=false,battleMode_=false,encounterMode_=false;
    int frames_=0,battleTicks_=0,encounterCount_=0;
    std::string lastOutcome_="none";
    std::chrono::steady_clock::time_point cameraUpdateTime_{};
};
editor::IEditorProjectRuntime* create(){return new Runtime;}
void destroy(editor::IEditorProjectRuntime* r){delete r;}
const editor::EditorProjectPluginContract contract{editor::kEditorProjectPluginAbiVersion,sizeof(editor::EditorProjectPluginContract),editor::kEditorProjectPluginLayoutFingerprint,PHLOSION_EDITOR_BUILD_CONFIGURATION,editor::kEditorProjectPluginCompilerAbi,&create,&destroy};
const editor::EditorSdlGameInputExtension extension{1,sizeof(editor::EditorSdlGameInputExtension),editor::kEditorSdlGameInputFingerprint,
    [](editor::IEditorProjectRuntime* r,const editor::EditorSdlGameInputContext* c){static_cast<Runtime*>(r)->begin(*c);},
    [](editor::IEditorProjectRuntime* r,const editor::EditorSdlGameInputContext* c,const SDL_Event* e){static_cast<Runtime*>(r)->event(*c,*e);},
    [](editor::IEditorProjectRuntime* r,const editor::EditorSdlGameInputContext* c){static_cast<Runtime*>(r)->end(*c);}};
}
PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorProjectPluginContract* phlosionEditorProjectPluginContract(){return &contract;}
PHLOSION_EDITOR_PROJECT_EXPORT const engine::editor::EditorSdlGameInputExtension* phlosionEditorSdlGameInput(){return &extension;}
