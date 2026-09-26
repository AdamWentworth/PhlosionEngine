#define SDL_MAIN_HANDLED
#include "ExplorationPresentation.h"
#include "BattleRulesOverlay.h"
#include "engine/editor/EditorProjectPlugin.h"
#include "engine/input/SdlInputReplay.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>

using namespace engine;
namespace {
#define CHECK(x) do { if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+" " #x); } while(false)
void extension() {
    editor::EditorSdlGameInputExtension e{1,sizeof(editor::EditorSdlGameInputExtension),editor::kEditorSdlGameInputFingerprint,
        [](editor::IEditorProjectRuntime*,const editor::EditorSdlGameInputContext*){},
        [](editor::IEditorProjectRuntime*,const editor::EditorSdlGameInputContext*,const SDL_Event*){},
        [](editor::IEditorProjectRuntime*,const editor::EditorSdlGameInputContext*){}};
    std::string error;CHECK(editor::validateEditorSdlGameInput(&e,error));CHECK(!editor::validateEditorSdlGameInput(nullptr,error));
    e.version=2;CHECK(!editor::validateEditorSdlGameInput(&e,error));e.version=1;
    e.structureSize=8;CHECK(!editor::validateEditorSdlGameInput(&e,error));e.structureSize=sizeof(e);
    e.layoutFingerprint^=1;CHECK(!editor::validateEditorSdlGameInput(&e,error));e.layoutFingerprint^=1;
    e.event=nullptr;CHECK(!editor::validateEditorSdlGameInput(&e,error));
    CHECK(editor::kEditorProjectPluginAbiVersion==34);
}
editor::EditorSdlGameInputContext context() {return {42,true,true,true,false,true,true,false,true,{20,30,640,360}};}
void battleHudHitboxes() {
    nlohmann::json state={{"screen","command"},{"options",nlohmann::json::array({
        {{"label","FIGHT"}},{{"label","POKEMON"}},{{"label","ITEM"}},{{"label","CALL"}}
    })}};
    for(const auto [width,height]:{std::pair{832,468},std::pair{840,360}}) {
        const float s=std::min({1.0f,height/400.0f,width/640.0f});
        const float left=38*s, top=height-121*s;
        const float commandScale=std::min({1.0f,height/480.0f,width/640.0f});
        for(int index=0;index<4;++index) {
            const int x=static_cast<int>((index>=2 ? 261 : 121)*commandScale);
            const int y=static_cast<int>(height-(480-(index%2 ? 406 : 378))*commandScale);
            CHECK(exploration_fixture::battleRulesOptionAt(state,x,y,width,height)==index);
        }
        state["screen"]="move";
        state["options"]=nlohmann::json::array({
            {{"label","BITE"}},{{"label","QUICK ATTACK"}},
            {{"label","SNARL"}},{{"label","TACKLE"}},{{"label","BACK"}}
        });
        for(int index=0;index<4;++index) {
            const int x=static_cast<int>((index>=2 ? 310 : 145)*commandScale);
            const int y=static_cast<int>(height-(480-(index%2 ? 406 : 378))*commandScale);
            CHECK(exploration_fixture::battleRulesOptionAt(state,x,y,width,height)==index);
        }
        CHECK(exploration_fixture::battleRulesOptionAt(state,
              static_cast<int>(520*commandScale),
              static_cast<int>(height-90*commandScale),width,height)==-1);
        state["screen"]="command";
        state["options"]=nlohmann::json::array({
            {{"label","FIGHT"}},{{"label","POKEMON"}},{{"label","ITEM"}},{{"label","CALL"}}
        });
        CHECK(exploration_fixture::battleRulesOptionAt(state,
              static_cast<int>(350*commandScale),
              static_cast<int>(height-90*commandScale),width,height)==-1);
        CHECK(exploration_fixture::battleRulesOptionAt(state,width/2,20,width,height)==-1);
        state["screen"]="target";
        state["party"]=nlohmann::json::array({
            {{"name","Umbreon"}},{{"name","Espeon"}},
            {{"name","Zigzagoon"}},{{"name","Poochyena"}}
        });
        state["options"]=nlohmann::json::array({
            {{"label","Zigzagoon 52/52"}},{{"label","Poochyena 55/55"}}
        });
        const float t=std::min({1.0f,height/480.0f,width/640.0f});
        CHECK(exploration_fixture::battleRulesOptionAt(state,
              static_cast<int>(width-(640-378)*t+31*t),
              static_cast<int>(48*t+31*t),width,height)==0);
        CHECK(exploration_fixture::battleRulesOptionAt(state,
              static_cast<int>(width-(640-402)*t+31*t),
              static_cast<int>(114*t+31*t),width,height)==1);
        CHECK(exploration_fixture::battleRulesOptionAt(state,static_cast<int>(left+49*s),
              static_cast<int>(top+48*s),width,height)==-1);
        state["options"]=nlohmann::json::array({
            {{"label","Umbreon 83/83"}},{{"label","Espeon 85/85"}}
        });
        CHECK(exploration_fixture::battleRulesOptionAt(state,
              static_cast<int>((37+194)*t),
              static_cast<int>((48+31)*t),width,height)==0);
        CHECK(exploration_fixture::battleRulesOptionAt(state,
              static_cast<int>((13+194)*t),
              static_cast<int>((114+31)*t),width,height)==1);
        state["screen"]="command";
        state["options"]=nlohmann::json::array({
            {{"label","FIGHT"}},{{"label","POKEMON"}},{{"label","ITEM"}},{{"label","CALL"}}
        });
    }
}
void policy() {
    for(int w:{1,7,63,641,1200})for(int h:{1,9,100,361,800})for(auto ratio:{glm::ivec2(16,9),glm::ivec2(21,9)}) {
        const auto r=editor::fitGameViewport(w,h,ratio.x,ratio.y);
        CHECK(r.width<=w&&r.height<=h&&r.width*ratio.y==r.height*ratio.x);
        CHECK(r.x>=0&&r.y>=0&&2*r.x+r.width<=w&&2*r.y+r.height<=h);
    }
    auto c=context();CHECK(editor::gameInputEnabled(c));
    for(int gate=0;gate<7;++gate){auto d=c;switch(gate){case 0:d.windowFocused=false;break;case 1:d.activeGameViewport=false;break;case 2:d.playing=false;break;case 3:d.paused=true;break;case 4:d.viewportFocused=false;break;case 5:d.textEditing=true;break;default:d.viewport.width=0;break;}CHECK(!editor::gameInputEnabled(d));}
    CHECK(editor::gamePointerInside(c,20,30));CHECK(!editor::gamePointerInside(c,660,30));
    input::InputActions actions;CHECK(actions.define({"reset",input::ActionKind::Button,{input::KeyBinding{InputEvent::Key::R}}}));
    input::SdlGameInput input(actions,42,false);input.begin(c);
    SDL_Event key{};key.type=SDL_KEYDOWN;key.key.windowID=42;key.key.keysym.sym=SDLK_r;input.event(c,key);
    key.type=SDL_KEYUP;input.event(c,key);input.end(c); // No tick this render frame.
    input.begin(c);input.end(c);actions.advanceFrame();input::ActionState state;CHECK(actions.state("reset",state));CHECK(state.pressed&&state.released&&!state.down);
    actions.advanceFrame();CHECK(actions.state("reset",state));CHECK(!state.pressed&&!state.released); // Catch-up tick consumes nothing twice.
    key.type=SDL_KEYDOWN;key.key.repeat=1;input.event(c,key);input.event(c,key);actions.advanceFrame();CHECK(actions.state("reset",state));CHECK(!state.down&&!state.pressed);
    key.key.repeat=0;key.key.windowID=99;input.event(c,key);actions.advanceFrame();CHECK(actions.state("reset",state));CHECK(!state.down);
    SDL_Event mouse{};mouse.type=SDL_MOUSEBUTTONDOWN;mouse.button.windowID=42;mouse.button.button=SDL_BUTTON_RIGHT;mouse.button.x=40;mouse.button.y=40;input.event(c,mouse);
    mouse={};mouse.type=SDL_MOUSEMOTION;mouse.motion.windowID=42;mouse.motion.x=50;mouse.motion.y=50;mouse.motion.xrel=20;input.event(c,mouse);
    input.begin(c);input.end(c);CHECK(std::abs(input.takeLookDelta().x+.12f)<.0001f);CHECK(input.takeLookDelta()==glm::vec2(0));
    c.paused=true;input.begin(c);CHECK(!input.dragging());CHECK(input.takeLookDelta()==glm::vec2(0));
    c.paused=false;input.begin(c);input.event(c,mouse);CHECK(input.takeLookDelta()==glm::vec2(0));
    SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=42;wheel.wheel.y=1;input.event(c,wheel);input.event(c,wheel);
    input.begin(c);input.end(c);CHECK(input.takeZoomDelta()==-1);CHECK(input.takeZoomDelta()==0);
    input.event(c,wheel);c.textEditing=true;input.begin(c);CHECK(input.takeZoomDelta()==0);
    c.textEditing=false;c.viewportHovered=false;input.begin(c);input.event(c,wheel);CHECK(input.takeZoomDelta()==0);
    c.viewportHovered=true;
    auto settings=exploration_fixture::cameraSettings();settings.desiredDistance=9;settings.initialPitchRadians=.3f;
    exploration_fixture::ExplorationSession configured("walk",true,settings);configured.reset();
    CHECK(configured.settings().desiredDistance==9&&configured.settings().initialPitchRadians==.3f);
    CHECK(configured.cameraPose().desiredDistance==9&&configured.cameraPose().pitchRadians==.3f);
    exploration_fixture::ExplorationSession freePlay("walk",false);freePlay.begin(c);
    SDL_Event move{};move.type=SDL_KEYDOWN;move.key.windowID=42;move.key.keysym.sym=SDLK_d;freePlay.event(c,move);
    for(int tick=0;tick<900;++tick){freePlay.begin(c);freePlay.end(c);freePlay.tick();}
    CHECK(freePlay.state().at("resets")==1&&std::abs(freePlay.player().center.x)<.001f&&std::abs(freePlay.player().center.z-4)<.001f);
}
nlohmann::json run(const std::string& name,const std::filesystem::path& dir) {
    const auto script=dir/(name+".json");{std::ofstream out(script);out<<exploration_fixture::replayScript(name).dump(2);}
    input::SdlInputReplay replay(script);exploration_fixture::ExplorationSession session(name,true);auto c=context();
    session.setTrace((dir/(name+".jsonl")).string());
    for(int f=0;f<150;++f) {
        replay.frame(c);session.begin(c);SDL_Event e{};while(SDL_PollEvent(&e))session.event(c,e);session.end(c);
        if(c.playing&&!c.paused)session.tick();
    }
    const auto state=session.state();CHECK(session.ticks()==120);return state;
}
void malformed(const std::filesystem::path& dir) {
    const auto path=dir/"bad.json";
    for(const auto& events : std::vector<nlohmann::json>{
        {{{"frame",2},{"type","attach"}},{{"frame",1},{"type","detach"}}},
        {{{"frame",1},{"type","axis"},{"index",0},{"value",100}}},
        {{{"frame",1},{"type","state"},{"focus",3}}},
        {{{"frame",1},{"type","motion"},{"x",1e100},{"y",0}}},
        {{{"frame",1},{"type","surprise"}}}}) {
        {std::ofstream out(path);out<<nlohmann::json{{"schema","phlosion-sdl-replay-v1"},{"events",events}};}
        bool rejected=false;try{input::SdlInputReplay replay(path);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    }
    {std::ofstream out(path);out<<std::string(1024*1024+1,' ');}
    bool oversized=false;try{input::SdlInputReplay replay(path);}catch(const std::exception&){oversized=true;}CHECK(oversized);
    nlohmann::json nested=0;for(int i=0;i<20;++i)nested=nlohmann::json{{"nested",nested}};
    {std::ofstream out(path);out<<nested;}
    bool deep=false;try{input::SdlInputReplay replay(path);}catch(const std::exception&){deep=true;}CHECK(deep);
    const auto parseEvents=[&](const nlohmann::json& events,bool accept){
        {std::ofstream out(path);out<<nlohmann::json{{"schema","phlosion-sdl-replay-v1"},{"events",events}};}
        bool parsed=false;try{input::SdlInputReplay replay(path);parsed=true;}catch(const std::exception&){}
        CHECK(parsed==accept);
    };
    for(const std::uint64_t large:{std::numeric_limits<std::uint64_t>::max(),
                                 static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())+1u}) {
        parseEvents({{{"frame",1},{"type","motion"},{"x",large},{"y",0}}},false);
        parseEvents({{{"frame",1},{"type","attach"}},{{"frame",2},{"type","axis"},{"index",0},{"value",large}}},false);
        parseEvents({{{"frame",1},{"type","attach"}},{{"frame",2},{"type","axis"},{"index",large},{"value",0}}},false);
    }
    parseEvents({{{"frame",1},{"type","motion"},{"x",-1000},{"y",1000}},
                 {{"frame",1},{"type","attach"}},{{"frame",2},{"type","axis"},{"index",0},{"value",-32768}},
                 {{"frame",2},{"type","axis"},{"index",5},{"value",32767}}},true);
}
}
int main(int argc,char** argv) {
    try {
        battleHudHitboxes();
        CHECK(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)==0);
        const int ambientDevices=SDL_NumJoysticks();
        const auto dir=argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/("phlosion-exploration-"+std::to_string(SDL_GetPerformanceCounter()));
        std::filesystem::create_directories(dir);extension();policy();malformed(dir);
        nlohmann::json states;
        for(const auto& name:exploration_fixture::cases()){states[name]=run(name,dir);std::cout<<name<<" "<<states[name].dump()<<'\n';}
        for(const auto& name:exploration_fixture::supplementalCases()){states[name]=run(name,dir);std::cout<<name<<" "<<states[name].dump()<<'\n';}
        {std::ofstream out(dir/"states.json");out<<states.dump(2);}
        CHECK(std::abs(states["walk"]["player"][2].get<float>()-2.0f)<.08f);
        CHECK(std::abs(states["run"]["player"][2].get<float>()-1.5f)<.08f);
        CHECK(states["wall-blocked"]["player"][2].get<float>()>-.6f);
        CHECK(states["ramp"]["player"][1].get<float>()>1.1f);
        CHECK(states["stairs"]["player"][1].get<float>()>1.2f);
        CHECK(states["trigger-active"]["sensor"].get<bool>());
        CHECK(states["camera-obstructed"]["camera"]["distance"].get<float>()<2);
        CHECK(states["recovered-reset"]["camera"]["distance"].get<float>()>6.9f);
        CHECK(states["recovered-reset"]["resets"]==1);
        CHECK(states["walk"]["npc_blocked"].get<int>()>=1&&states["walk"]["npc_retries"].get<int>()>=1);
        CHECK(states["steep"]["player"][2].get<float>()>-2.4f&&states["steep"]["player"][1].get<float>()<1.1f);
        CHECK(std::abs(states["ledge-drop"]["player"][0].get<float>()-7)<.01f&&std::abs(states["ledge-drop"]["player"][1].get<float>()-.905f)<.01f);
        // The import path remains CPU-only; drawing is native-host qualification.
        {exploration_fixture::ExplorationPresentation presentation;CHECK(presentation.imports().size()==10);}
        CHECK(SDL_NumJoysticks()==ambientDevices);CHECK((SDL_WasInit(SDL_INIT_GAMECONTROLLER)&SDL_INIT_GAMECONTROLLER)!=0);
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS);std::cout<<"Exploration contracts PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
