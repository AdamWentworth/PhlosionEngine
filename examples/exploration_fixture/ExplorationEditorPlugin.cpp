#include "BattlePreview.h"
#include "ExplorationPresentation.h"
#include "engine/core/Environment.h"
#include "engine/editor/EditorProjectPlugin.h"

namespace {
using namespace engine;
class Runtime final : public editor::IEditorProjectRuntime {
public:
    bool open(const editor::EditorProjectOpenContext&,std::string*) override {
        automation_=env::get("PHLOSION_EXPLORATION_AUTOMATION").value_or("0")=="1";
        const auto caseName=env::get("PHLOSION_EXPLORATION_CASE").value_or("walk");
        battleMode_=caseName=="outskirt-battle";
        if(battleMode_) battle_=std::make_unique<exploration_fixture::BattlePreview>();
        else {
            session_=std::make_unique<exploration_fixture::ExplorationSession>(caseName,automation_,exploration_fixture::configuredCameraSettings());
            session_->setTrace(env::get("PHLOSION_EXPLORATION_TRACE").value_or(""));
            presentation_=std::make_unique<exploration_fixture::ExplorationPresentation>(session_->caseName()=="outskirt");
        }
        return true;
    }
    void prewarm(IRenderBackend& renderer,const editor::EditorProjectCameraContext&) override {renderer.prewarmWorldRenderAssets();}
    void update(float) override {}
    void render(const editor::EditorProjectRenderContext& c) override {
        if(!c.renderer)return;
        if(battle_)battle_->draw(*c.renderer,battleTicks_,c.surfaceWidth,c.surfaceHeight);
        else presentation_->draw(*c.renderer,*session_,c.surfaceWidth,c.surfaceHeight);
    }
    editor::EditorProjectStats stats() const override {
        if(battle_ || (session_ && session_->caseName()=="outskirt")) {
            editor::EditorProjectStats result{.sceneCount=1};
            std::size_t assetIndex=0;
            for(const auto& item : battle_ ? battle_->imports() : presentation_->imports()) {
                result.materialCount += item.at("materials").get<std::uint32_t>();
                const std::uint32_t instances=battle_ && assetIndex==1 ? 4u : 1u;
                result.drawClassCount += item.at("primitives").get<std::uint32_t>()*instances;
                result.visibleTriangleCount += item.at("indices").get<std::uint64_t>()/3*instances;
                ++assetIndex;
            }
            return result;
        }
        return {.sceneCount=1,.materialCount=10,.drawClassCount=14,.visibleTriangleCount=168};
    }
    const char* status() const noexcept override {return battleMode_ ? "Provisional Outskirt battle camera and four Wes actor stand-ins." : "WASD / left stick move; Shift / L3 run; RMB / right stick orbit; wheel / bumpers zoom; R / Back reset; Tab / Start select pad.";}
    std::size_t gamePreviewCount() const noexcept override {return 1;}
    editor::EditorProjectGamePreview gamePreview(std::size_t) const noexcept override {
        if(battleMode_)return {"battle","Outskirt battle (provisional)","Samples",status(),"fixture/scene"};
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
    void resetGamePreview() override {if(battle_){battle_->reset();battleTicks_=0;}else session_->reset();}
    void fixedUpdateGamePreview(float dt) override {
        if(std::abs(dt-exploration_fixture::fixedDelta)>.00001f)throw std::runtime_error("Fixture requires 60 Hz ticks");
        if(battle_)++battleTicks_;else session_->tick();
    }
    void renderGamePreview(const editor::EditorProjectRenderContext& c) override {
        if(!c.renderer)return;++frames_;c.renderer->beginWorldSceneColorPass(c.surfaceWidth,c.surfaceHeight);
        if(battle_)battle_->draw(*c.renderer,battleTicks_,c.surfaceWidth,c.surfaceHeight);
        else presentation_->draw(*c.renderer,*session_,c.surfaceWidth,c.surfaceHeight);
        c.renderer->endWorldSceneColorPass();
        if(battle_)battle_->drawOverlay(*c.renderer,c.surfaceWidth,c.surfaceHeight);
        const int capture=std::stoi(env::get("PHLOSION_BACKEND_SCREENSHOT_FRAME").value_or("160"));
        if(frames_==capture) {
            if(battle_)exploration_fixture::writeBattleHandshake(env::get("PHLOSION_EXPLORATION_HANDSHAKE").value_or(""),"editor",
                env::get("PHLOSION_EXPLORATION_API").value_or(c.renderer->backendId()),*battle_,*c.renderer,battleTicks_,c.surfaceWidth,c.surfaceHeight,frames_,battleViewport_);
            else exploration_fixture::writeHandshake(env::get("PHLOSION_EXPLORATION_HANDSHAKE").value_or(""),"editor",
                env::get("PHLOSION_EXPLORATION_API").value_or(c.renderer->backendId()),*session_,*presentation_,*c.renderer,c.surfaceWidth,c.surfaceHeight,frames_);
        }
    }
    void begin(const editor::EditorSdlGameInputContext& c){
        if(battle_)battleViewport_={c.viewport.x,c.viewport.y,c.viewport.width,c.viewport.height};
        else session_->begin(c);
    }
    void event(const editor::EditorSdlGameInputContext& c,const SDL_Event& e){
        if(session_){session_->event(c,e);return;}
        if(!battle_||!editor::gameInputEnabled(c))return;
        if(e.type==SDL_KEYDOWN&&e.key.repeat==0){
            if(e.key.keysym.sym==SDLK_ESCAPE)battle_->cancel();
            if(e.key.keysym.sym==SDLK_SPACE)battle_->skip();
            if(e.key.keysym.sym==SDLK_r)resetGamePreview();
        }
        if(e.type==SDL_CONTROLLERBUTTONDOWN){
            if(e.cbutton.button==SDL_CONTROLLER_BUTTON_B)battle_->cancel();
            if(e.cbutton.button==SDL_CONTROLLER_BUTTON_A)battle_->skip();
        }
    }
    void end(const editor::EditorSdlGameInputContext& c){if(session_)session_->end(c);}
private:
    std::unique_ptr<exploration_fixture::ExplorationSession> session_;
    std::unique_ptr<exploration_fixture::ExplorationPresentation> presentation_;
    std::unique_ptr<exploration_fixture::BattlePreview> battle_;
    std::array<int,4> battleViewport_{0,0,0,0};
    bool automation_=false,ready_=false,selected_=false,battleMode_=false;
    int frames_=0,battleTicks_=0;
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
