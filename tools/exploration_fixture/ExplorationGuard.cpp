#define SDL_MAIN_HANDLED
#include "ExplorationPresentation.h"
#include "FixtureImageGuard.h"
#include "SceneFixtureGuard.h"
#include "engine/input/SdlInputReplay.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <regex>

namespace {
using json=nlohmann::json;
using namespace engine::exploration_fixture;
using namespace engine::fixture;
using engine::scene_fixture::verification::sha256FileHex;
std::filesystem::path runningGuard;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
json read(const std::filesystem::path& p){std::ifstream in(p);check(bool(in),"Missing JSON: "+p.string());json j;in>>j;return j;}
std::string textFile(const std::filesystem::path& p){check(std::filesystem::file_size(p)<16*1024*1024,"Text evidence too large");std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
glm::vec3 vec(const json& a){check(a.is_array()&&a.size()==3,"Malformed vector");glm::vec3 v(a[0].get<float>(),a[1].get<float>(),a[2].get<float>());check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"Nonfinite vector");return v;}
bool near(float a,float b,float tolerance=.00001f){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance;}
void same(const json& a,const json& b,const std::string& path="state"){
    if(a.is_number()&&b.is_number()){check(std::isfinite(a.get<double>())&&std::isfinite(b.get<double>())&&std::abs(a.get<double>()-b.get<double>())<=.00002,"State differs: "+path);return;}
    check(a.type()==b.type(),"State type differs: "+path);
    if(a.is_object()){check(a.size()==b.size(),"State shape differs");for(auto i=a.begin();i!=a.end();++i){check(b.contains(i.key()),"State key missing");same(i.value(),b.at(i.key()),path+"/"+i.key());}}
    else if(a.is_array()){check(a.size()==b.size(),"State array differs: "+path);for(std::size_t i=0;i<a.size();++i)same(a[i],b[i],path+"/"+std::to_string(i));}
    else check(a==b,"State differs: "+path);
}
json semantic(json s){s.erase("input_frame");s.erase("viewport");s.erase("kind");s["camera"].erase("aspect");s["input"].erase("devices");return s;}
void stateOracle(const std::string& name,const json& s,int width,int height){
    check(s.at("tick").is_number_integer()&&s.at("input_frame").is_number_integer(),"Tick/frame must be integers");
    check(s.at("case")==name&&s.at("tick")==targetTick,"Wrong case or actual tick");
    const auto p=vec(s.at("player"));check(near(p.x,name=="ledge-drop"?7:caseSpawn(name).x,.001f)||name=="recovered-reset","Player in wrong lane");
    float z=0,y=.905f;int support=1;
    if(name=="steep"){check(p.z>-2.4f&&p.z<-2.0f,"Nonwalkable slope was crossed or never approached");z=p.z;}
    else if(name=="ledge-drop")z=-3;
    else if(name=="walk")z=2;
    else if(name=="run")z=1.5f;
    else if(name=="wall-blocked")z=-1+.08f+.35f+.005f;
    else if(name=="ramp"){
        check(p.z>-1.95f&&p.z<-1.60f,"Ramp forward progress missing");z=p.z;support=4;
        y=.5f+.2f/std::cos(glm::radians(20.0f))-std::tan(glm::radians(20.0f))*(p.z+3)+.55f+.35f/std::cos(glm::radians(20.0f))+.005f;
    }else if(name=="stairs"){check(p.z>-1.9f&&p.z<-1.3f,"Stair progress missing");z=p.z;y=1.305f;support=7;}
    else if(name=="trigger-active")z=3;
    else if(name=="camera-obstructed")z=-3;
    else if(name=="recovered-reset"){z=4;check(near(p.x,0,.001f),"Reset X differs");}
    else throw std::runtime_error("Unknown case");
    check(near(p.z,z,.015f)&&near(p.y,y,.025f),"Player position fails independent distance/support oracle");
    check(s.at("ground")==0&&s.at("support")==support,"Wrong ground/support identity");
    check(s.at("resets")== (name=="recovered-reset"?1:0),"Wrong reset count");
    const bool sensor=name=="trigger-active";check(s.at("sensor")==sensor,"Wrong sensor membership");
    check(s.at("enter")== (sensor?2:0)&&s.at("exit")== (sensor?1:0),"Trigger Enter/Exit sequence missing");
    check(sensor?s.at("stay").get<int>()>=20:s.at("stay")==0,"Trigger Stay sequence missing");
    check(s.at("npc_blocked")==1&&s.at("npc_retries")==1,"NPC blocked/retry policy missing");
    const auto npc=vec(s.at("npc"));check(near(npc.x,-1,.001f)&&near(npc.y,.905f,.015f)&&near(npc.z,name=="recovered-reset"?2.325f:.225f,.015f),"NPC waypoint/blocked retry progress wrong");
    const auto& c=s.at("camera");const auto pivot=vec(c.at("pivot")),eye=vec(c.at("position"));
    check(glm::length(pivot-(p+glm::vec3(0,.45f,0)))<.035f,"Camera is not following the character");
    const float yaw=name=="camera-obstructed"?-((12000.0f/32767-.15f)/.85f)*1.6f*20/60:0;
    check(near(c.at("yaw"),yaw,.00002f)&&near(c.at("pitch"),.48f),"Wrong camera orbit/reset");
    const float distance=name=="camera-obstructed"?1.72f/(std::cos(.48f)*std::cos(yaw))-.03f:7;
    check(near(c.at("distance"),distance,.003f)&&near(c.at("desired"),7),"Camera obstruction/recovery distance wrong");
    check(c.at("obstructed")== (name=="camera-obstructed"),"Wrong obstruction state");
    const glm::vec3 boom(std::sin(yaw)*std::cos(.48f),std::sin(.48f),std::cos(yaw)*std::cos(.48f));
    check(glm::length(eye-pivot-boom*distance)<.003f,"Wrong camera view position");
    check(near(c.at("aspect"),static_cast<float>(width)/static_cast<float>(height)),"Wrong camera aspect");
    const auto& input=s.at("input");check(input.at("synthetic")==true&&input.at("enabled")==true&&input.at("background_controller_input")==true,"Not the qualified synthetic/background game route");
    check(input.at("selected")== (name!="recovered-reset"),"Controller selection/removal policy wrong");
    for(const auto& d:input.at("devices"))if(d.at("selected").get<bool>())check(d.at("name")=="Phlosion owned replay controller","Ambient device selected by automation");
}
void traceOracle(const std::filesystem::path& path,const json& finalState,int tailFrames=0){
    std::ifstream in(path);check(bool(in),"Missing trace");std::string line;int tick=0,lastFrame=0,lines=0;json last;
    bool focus=false,text=false,active=false,pause=false,viewportFocus=false,hover=false,shortCamera=false,zoom=false,selected=false,removed=false,ledgeSupport=false,falling=false;
    while(std::getline(in,line)){
        check(++lines<1000&&line.size()<65536,"Trace bounds exceeded");const auto s=json::parse(line);const auto kind=s.at("kind").get<std::string>();
        check(s.at("tick").is_number_integer()&&s.at("input_frame").is_number_integer(),"Malformed trace tick/frame");
        check(kind=="open"||kind=="reset"||kind=="input-frame"||kind=="tick","Unknown trace record");
        if(kind=="tick"){
            check(s.at("tick").get<int>()==++tick,"Nonsequential actual simulation tick");last=s;
            ledgeSupport|=s.at("support")==9;falling|=s.at("ground")==3&&vec(s.at("velocity")).y<-.1f;
            if(s.at("case")=="steep")check(vec(s.at("player")).z>-2.4f&&vec(s.at("player")).y<1.1f,"Steep slope traversal escaped its allowed region");
        }
        if(kind=="input-frame"){
            check(s.at("input_frame").get<int>()==++lastFrame,"Missing host input frame");
            const auto& g=s.at("input");const bool enabled=g.at("focus").get<bool>()&&g.at("active").get<bool>()&&g.at("playing").get<bool>()&&!g.at("paused").get<bool>()&&g.at("viewport_focus").get<bool>()&&!g.at("text").get<bool>();
            check(g.at("enabled")==enabled,"Input gate disagrees with host context");
            focus|=!g.at("focus").get<bool>();text|=g.at("text").get<bool>();active|=!g.at("active").get<bool>();pause|=g.at("paused").get<bool>();
            viewportFocus|=!g.at("viewport_focus").get<bool>();hover|=!g.at("hover").get<bool>();
            selected|=g.at("selected").get<bool>();removed|=selected&&!g.at("selected").get<bool>();
            if(s.at("case")=="recovered-reset"&&lastFrame>=17&&lastFrame<=23)
                check(s.at("input").at("move")==json::array({0.0,0.0}),"Held controller leaked through gate rearm");
        }
        shortCamera|=s.at("camera").at("distance").get<float>()<2;
        zoom|=s.at("camera").at("desired").get<float>()<6.6f;
    }
    check(tick==targetTick&&lastFrame>=140&&lastFrame==finalState.at("input_frame").get<int>()+tailFrames,"Incomplete trace/frozen capture period");same(semantic(last),semantic(finalState));
    if(finalState.at("case")=="recovered-reset")check(focus&&text&&active&&pause&&viewportFocus&&hover&&shortCamera&&zoom&&selected&&removed,"Recovery trace does not establish all gates, mouse zoom, hotplug and camera recovery");
    if(finalState.at("case")=="ledge-drop")check(ledgeSupport&&falling,"Ledge support-to-fall transition missing");
}
// Independent analytic box ray intersections and documented unlit display
// transfer. No captured pixel or producer landmark rectangle trains this oracle.
glm::vec3 display(glm::vec3 color){
    const glm::mat3 a(glm::vec3(.59719f,.076f,.0284f),glm::vec3(.35458f,.90834f,.13383f),glm::vec3(.04823f,.01566f,.83777f));
    const glm::mat3 b(glm::vec3(1.60475f,-.10208f,-.00327f),glm::vec3(-.53108f,1.10813f,-.07276f),glm::vec3(-.07367f,-.00605f,1.07602f));
    const auto v=a*(color*(1.15f/.6f));color=glm::clamp(b*((v*(v+.0245786f)-.000090537f)/(v*(.983729f*v+.432951f)+.238081f)),glm::vec3(0),glm::vec3(1));
    for(int k=0;k<3;++k)color[k]=(color[k]<=.0031308f?color[k]*12.92f:1.055f*std::pow(color[k],1/2.4f)-.055f)*255;
    return color;
}
struct RayBox {glm::vec3 center,half;float sine,cosine;};
float intersect(const RayBox& b,glm::vec3 eye,glm::vec3 ray){
    eye-=b.center;
    const float ey=b.cosine*eye.y+b.sine*eye.z,ez=-b.sine*eye.y+b.cosine*eye.z;
    const float ry=b.cosine*ray.y+b.sine*ray.z,rz=-b.sine*ray.y+b.cosine*ray.z;
    eye.y=ey;eye.z=ez;ray.y=ry;ray.z=rz;
    float lo=0,hi=1000;
    for(int k=0;k<3;++k){if(std::abs(ray[k])<1e-8f){if(std::abs(eye[k])>b.half[k])return 1000;continue;}
        float a=(-b.half[k]-eye[k])/ray[k],z=(b.half[k]-eye[k])/ray[k];if(a>z)std::swap(a,z);lo=std::max(lo,a);hi=std::min(hi,z);}
    return hi>=lo&&hi>.1f?lo:1000;
}
json contentOracle(const FixtureImage& image,const json& state,FixtureImage* synthetic=nullptr){
    const auto& c=state.at("camera");const auto eye=vec(c.at("position")),pivot=vec(c.at("pivot"));
    const auto forward=glm::normalize(pivot-eye),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0))),up=glm::cross(right,forward);
    std::vector<CourseBox> boxes=course();for(auto& b:boxes)if(b.sensor){b.center.y=.012f;b.half.y=.012f;b.color=state.at("sensor").get<bool>()?7:6;}
    boxes.push_back({"npc",vec(state.at("npc")),{.3f,.9f,.3f},0,9});boxes.push_back({"player",vec(state.at("player")),{.3f,.9f,.3f},0,state.at("sensor").get<bool>()?7:8});
    std::vector<RayBox> rayBoxes;for(const auto& b:boxes)rayBoxes.push_back({b.center,b.half,std::sin(b.pitch),std::cos(b.pitch)});
    const float tangent=std::tan(glm::radians(45.0f)*.5f),aspect=static_cast<float>(image.width)/static_cast<float>(image.height);
    auto rayAt=[&](int x,int y){return glm::normalize(forward+(2*(static_cast<float>(x)+.5f)/static_cast<float>(image.width)-1)*tangent*aspect*right+(1-2*(static_cast<float>(y)+.5f)/static_cast<float>(image.height))*tangent*up);};
    auto hit=[&](int x,int y){float distance=1000;int id=-1;const auto ray=rayAt(x,y);for(std::size_t i=0;i<rayBoxes.size();++i){const auto t=intersect(rayBoxes[i],eye,ray);if(t<distance){distance=t;id=static_cast<int>(i);}}return id;};
    if(synthetic){
        synthetic->width=image.width;synthetic->height=image.height;synthetic->rgba.resize(static_cast<std::size_t>(image.width)*image.height*4);
        for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){const int id=hit(x,y);const auto color=display(id<0?glm::vec3(.035f,.05f,.085f):colors()[boxes[static_cast<std::size_t>(id)].color]);
            const auto offset=(static_cast<std::size_t>(y)*image.width+x)*4;for(int channel=0;channel<3;++channel)synthetic->rgba[offset+channel]=static_cast<std::uint8_t>(std::lround(color[channel]));synthetic->rgba[offset+3]=255;
        }return json::object();
    }
    std::map<int,std::pair<int,int>> counts;
    for(int y=3;y<image.height-3;y+=3)for(int x=3;x<image.width-3;x+=3){
        const int id=hit(x,y);if(id!=hit(x-2,y)||id!=hit(x+2,y)||id!=hit(x,y-2)||id!=hit(x,y+2))continue;
        auto& count=counts[id];++count.first;const auto expected=display(id<0?glm::vec3(.035f,.05f,.085f):colors()[boxes[static_cast<std::size_t>(id)].color]);
        const auto offset=(static_cast<std::size_t>(y)*image.width+x)*4;bool correct=true;
        for(int channel=0;channel<3;++channel)correct&=std::abs(static_cast<float>(image.rgba[offset+channel])-expected[channel])<=4;
        if(correct)++count.second;
    }
    json result;int landmarks=0;for(const auto& [id,count]:counts){const std::string name=id<0?"sky":boxes[static_cast<std::size_t>(id)].name;
        if(count.first<10)continue;const double ratio=static_cast<double>(count.second)/count.first;result[name]={{"samples",count.first},{"correct",ratio}};
        check(ratio>=.97,"Expected content missing/wrong: "+name+" coverage="+std::to_string(ratio));if(id>0&&name!="player"&&name!="npc")++landmarks;
    }
    check(result.contains("player")&&result.at("player").at("samples").get<int>()>=25,"Absent/misplaced visible player");
    check(landmarks>=1&&result.contains("floor"),"Missing course landmarks/ground");return result;
}
std::filesystem::path hashed(const json& identity,const std::filesystem::path& root){
    const auto path=std::filesystem::weakly_canonical(identity.at("path").get<std::string>());
    if(!root.empty()){std::filesystem::path resolved;std::string error;check(engine::scene_fixture::verification::resolveWithinRoot(path.string(),root,resolved,error),error);}
    check(std::filesystem::file_size(path)==identity.at("bytes").get<std::uint64_t>(),"File size mismatch: "+path.string());std::string hash,error;check(sha256FileHex(path.string(),hash,&error),error);
    check(hash==identity.at("sha256").get<std::string>(),"SHA-256 mismatch: "+path.string());return path;
}
void damage(FixtureImage& image,const std::string& kind){
    if(kind=="mirror"){for(int y=0;y<image.height;++y)for(int x=0;x<image.width/2;++x)for(int k=0;k<4;++k)std::swap(image.rgba[(static_cast<std::size_t>(y)*image.width+x)*4+k],image.rgba[(static_cast<std::size_t>(y)*image.width+image.width-x-1)*4+k]);return;}
    const auto target=display(colors()[kind=="erase-player"?8:(kind=="erase-wall"?1:7)]),replacement=display(colors()[0]);
    for(std::size_t i=0;i<image.rgba.size();i+=4){bool replace=kind=="blank";
        if(!replace){replace=true;for(int c=0;c<3;++c)replace&=std::abs(static_cast<float>(image.rgba[i+c])-target[c])<=4;}
        if(replace)for(int c=0;c<3;++c)image.rgba[i+c]=kind=="blank"?0:static_cast<std::uint8_t>(std::lround(replacement[c]));
    }
}
void selftest(const std::filesystem::path& parent){
    check(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)==0,"SDL test reference failed");
    struct Quit {~Quit(){SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS);}} quit;
    const auto dir=parent/("guard-"+std::to_string(SDL_GetPerformanceCounter()));std::filesystem::create_directories(dir);
    auto names=cases();names.insert(names.end(),supplementalCases().begin(),supplementalCases().end());
    int rejected=0;auto reject=[&](auto f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,"Negative evidence unexpectedly accepted");++rejected;};
    for(const auto& name:names){
        std::cout<<"Evidence CPU case "<<name<<'\n';
        const auto script=dir/(name+".json"),trace=dir/(name+".jsonl");{std::ofstream out(script);out<<replayScript(name);}
        json state;
        {
            engine::input::SdlInputReplay replay(script);ExplorationSession session(name,true);session.setTrace(trace.string());
            engine::editor::EditorSdlGameInputContext c{42,true,true,true,false,true,true,false,true,{0,0,320,180}};
            for(int frame=0;frame<150;++frame){replay.frame(c);session.begin(c);SDL_Event e{};while(SDL_PollEvent(&e))session.event(c,e);session.end(c);if(c.playing&&!c.paused)session.tick();}state=session.state();
        }
        stateOracle(name,state,320,180);traceOracle(trace,state);
        FixtureImage dimensions{320,180,{}},image;contentOracle(dimensions,state,&image);contentOracle(image,state);
        auto bad=state;bad["tick"]=119;reject([&]{stateOracle(name,bad,320,180);});bad=state;bad["player"][0]=100;reject([&]{stateOracle(name,bad,320,180);});
        bad=state;bad["camera"]["yaw"]=1;reject([&]{stateOracle(name,bad,320,180);});bad=state;bad["sensor"]=!state.at("sensor").get<bool>();reject([&]{stateOracle(name,bad,320,180);});
        auto damaged=image;damage(damaged,"blank");reject([&]{contentOracle(damaged,state);});
        if(name=="walk")for(const std::string defect:{"erase-player","erase-wall","mirror"}){damaged=image;damage(damaged,defect);reject([&]{contentOracle(damaged,state);});}
        if(name=="trigger-active"){damaged=image;damage(damaged,"erase-sensor");reject([&]{contentOracle(damaged,state);});}
        contentOracle(image,state); // Pristine recovery after each group.
    }
    std::cout<<"Exploration evidence contracts PASS: "<<rejected<<" negative state/content cases\n";
}
void requireArgument(const std::string& command,const std::string& prefix,const std::string& expected) {
    // The process runner quotes whole arguments. Paths cannot contain quotes on Windows.
    const std::regex tokenPattern(R"token("[^"]*"|\S+)token");
    int matches=0;
    for(std::sregex_iterator i(command.begin(),command.end(),tokenPattern),end;i!=end;++i) {
        auto token=i->str();if(token.front()=='"'&&token.back()=='"')token=token.substr(1,token.size()-2);
        if(token.starts_with(prefix)){++matches;check(token==expected,"Wrong process argument: "+prefix);}
    }
    check(matches==1,"Missing/duplicate process argument: "+prefix);
}
void matrix(const std::filesystem::path& path){
    const auto m=read(path);const auto root=path.parent_path();const bool supplemental=m.at("schema")=="phlosion-exploration-supplemental-v1";
    check(supplemental||m.at("schema")=="phlosion-exploration-matrix-v1","Bad matrix schema");
    check(m.at("failures").empty(),"Producer recorded failures");check(m.at("captures").size()==(supplemental?12:96),"Required capture coverage differs");
    check(m.at("thresholds")==json{{"normalized_mae",.01},{"normalized_rmse",.08},{"changed_ratio",.05},{"channel_tolerance",8}},"Changed parity thresholds");
    const std::map<std::string,std::string> names{{"runtime","ExplorationFixtureRuntime.exe"},{"editor","PhlosionEditor.exe"},{"plugin","ExplorationFixtureEditorPlugin.dll"},{"guard","ExplorationFixtureGuard.exe"}};
    std::set<std::string> roles;std::map<std::string,json> binaries;for(const auto& b:m.at("binaries")){const std::string role=b.at("role");check(roles.insert(role).second&&names.contains(role),"Duplicate/unknown binary role");const auto binary=hashed(b,{});check(binary.filename()==names.at(role),"Wrong binary path");if(role=="guard")check(binary==runningGuard,"Wrong verification binary");binaries[role]=b;}check(roles.size()==4,"Missing binary provenance");
    const auto descriptor=hashed(m.at("project").at("descriptor"),root);
    const auto stagedPlugin=hashed(m.at("project").at("plugin"),root);
    check(descriptor==std::filesystem::weakly_canonical(root/"project/phlosion.project.json"),"Wrong staged project descriptor path");
    check(stagedPlugin==std::filesystem::weakly_canonical(root/"project/.phlosion/editor/Debug/ExplorationFixtureEditorPlugin.dll"),"Wrong staged plugin path");
    check(m.at("project").at("plugin").at("sha256")==binaries.at("plugin").at("sha256")&&
          m.at("project").at("plugin").at("bytes")==binaries.at("plugin").at("bytes"),"Staged/build plugin identity differs");
    const auto project=read(descriptor);
    check(project.at("schema_version")==2&&project.at("project_id")=="phlosion-exploration-fixture"&&
          project.at("editor_plugin")==json{{"library","ExplorationFixtureEditorPlugin"},{"directory",".phlosion/editor/{config}"}},"Wrong staged project/plugin descriptor");
    check(std::regex_match(m.at("baseline").get<std::string>(),std::regex("[0-9a-f]{40}")),"Missing source baseline");
    const auto sourceRoot=std::filesystem::weakly_canonical(m.at("source_root").get<std::string>());
    std::set<std::filesystem::path> sourcePaths;for(const auto& source:m.at("sources"))check(sourcePaths.insert(hashed(source,sourceRoot)).second,"Duplicate source provenance");
    for(const char* required:{"CMakeLists.txt","src/engine/editor/PhlosionEditorMain.cpp","src/engine/editor/EditorShell.cpp",
        "src/engine/editor/EditorShell.h","src/engine/editor/EditorSdlGameInput.h","src/engine/editor/EditorProjectPlugin.h",
        "src/engine/editor/EditorProjectPluginProbe.cpp","src/engine/input/InputActions.cpp","src/engine/input/InputActions.h",
        "src/engine/input/SdlActionInput.cpp","src/engine/input/SdlActionInput.h","src/engine/input/SdlGameInput.cpp",
        "src/engine/input/SdlGameInput.h","src/engine/input/SdlInputReplay.cpp","src/engine/input/SdlInputReplay.h",
        "src/engine/collision/CollisionWorld.cpp","src/engine/collision/CollisionWorld.h","src/engine/camera/ExplorationCamera.cpp",
        "src/engine/camera/ExplorationCamera.h","src/engine/platform/Window.cpp","src/engine/platform/Window.h",
        "examples/exploration_fixture/ExplorationEditorPlugin.cpp","examples/exploration_fixture/ExplorationPresentation.cpp",
        "examples/exploration_fixture/ExplorationPresentation.h","examples/exploration_fixture/ExplorationRuntimeMain.cpp",
        "examples/exploration_fixture/ExplorationSession.cpp","examples/exploration_fixture/ExplorationSession.h",
        "tools/exploration_fixture/ExplorationGuard.cpp","tools/exploration_fixture/run_capture_matrix.ps1",
        "tools/public_asset_fixture/FixtureProcess.psm1","tests/TestExplorationFixture.cpp","tests/TestEditorSdlExtensionPlugin.cpp",
        "tests/exploration_fixture/test_native_harness.ps1"})
        check(sourcePaths.contains(std::filesystem::weakly_canonical(sourceRoot/required)),std::string("Missing required source: ")+required);
    std::set<std::string> coverage,evidencePaths;std::map<std::string,FixtureImage> baselines;std::map<std::string,json> states;
    std::vector<std::pair<std::string,FixtureImage>> sheet;json results=json::array();
    for(const auto& row:m.at("captures")){
        const std::string name=row.at("case"),host=row.at("host"),api=row.at("api"),aspect=row.at("aspect");
        const auto& catalog=supplemental?supplementalCases():cases();check(std::find(catalog.begin(),catalog.end(),name)!=catalog.end(),"Unknown capture case");
        check((host=="runtime"||host=="editor")&&(api=="opengl"||api=="vulkan"||api=="d3d12")&&(aspect=="16:9"||aspect=="21:9"),"Unknown host/API/aspect");
        check(!supplemental||aspect=="16:9","Supplemental ratio differs");
        const std::string key=aspect+"/"+name,label=key+"/"+host+"/"+api;check(coverage.insert(label).second,"Duplicate capture");
        std::cout<<"Checking "<<label<<'\n';
        const auto png=hashed(row.at("png"),root),handshake=hashed(row.at("handshake"),root),trace=hashed(row.at("trace"),root);
        for(const char* role:{"png","handshake","trace","stdout","stderr","process"})check(evidencePaths.insert(row.at(role).at("path")).second,"Reused/stale evidence path");
        const auto script=hashed(row.at("script"),root);check(read(script)==replayScript(name),"Replay script differs from authored case");
        const auto stdoutPath=hashed(row.at("stdout"),root),stderrPath=hashed(row.at("stderr"),root);
        const auto stdoutText=textFile(stdoutPath),stderrText=textFile(stderrPath),logs=stdoutText+"\n"+stderrText;
        const auto process=read(hashed(row.at("process"),root));check(process.at("ExitCode")==0&&process.at("TimedOut")==false,"Process failed/timed out");
        if(host=="editor")requireArgument(process.at("CommandLine"),"--project=","--project="+descriptor.string());
        check(process.at("Stdout")==stdoutText&&process.at("Stderr")==stderrText,"Process/log evidence differs");
        check(process.at("CommandLine").get<std::string>().find("--input-replay="+script.string())!=std::string::npos,"Wrong input replay path");
        check(process.at("CommandLine").get<std::string>().find("--frames=164")!=std::string::npos&&process.at("CommandLine").get<std::string>().find("--hidden")!=std::string::npos,"Wrong host capture mode/bounds");
        const auto environment=read(hashed(row.at("environment"),root));
        check(environment.at("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS")=="1"&&environment.at("PHLOSION_EXPLORATION_AUTOMATION")=="1","Wrong isolated replay environment");
        check(process.at("CommandLine").get<std::string>().find(m.at("binaries")[host=="runtime"?0:1].at("path").get<std::string>())==0,"Wrong launched binary");
        const auto h=read(handshake);check(h.at("width").is_number_integer()&&h.at("height").is_number_integer()&&h.at("render_frame").is_number_integer(),"Malformed capture dimensions/frame");const int width=h.at("width"),height=h.at("height");
        check(width>=64&&height>=64&&width<=4096&&height<=4096,"Invalid surface");check(width*(aspect=="16:9"?9:9)==height*(aspect=="16:9"?16:21),"Aspect is not exact");
        check(h.at("schema")=="phlosion-exploration-v1"&&h.at("host")==host&&h.at("api")==api&&h.at("requested_api")==api&&h.at("frozen")==true,"Wrong host/backend/freeze");
        check(!h.at("gpu").get<std::string>().empty()&&h.at("imports").size()==10,"Missing GPU/import evidence");
        for(const auto& imported:h.at("imports"))check(imported.at("identity").get<std::string>().size()==16&&imported.at("primitives")==1&&imported.at("vertices").get<int>()>0,"Malformed import evidence");
        const std::string displayName=api=="opengl"?"OpenGL":(api=="vulkan"?"Vulkan":"D3D12");
        check(logs.find("[ParityContract]["+displayName+"] PASS")!=std::string::npos&&logs.find("compatibility mode")==std::string::npos,"Native backend contract missing/fallback");
        const std::regex pattern(R"(\[Screenshot\]\[(OpenGL|Vulkan|D3D12)\] WROTE (.+) size=(\d+)x(\d+) frame=(\d+))");
        std::sregex_iterator it(logs.begin(),logs.end(),pattern),end;check(it!=end,"Missing observed screenshot record");const auto record=*it;check(++it==end,"Extra screenshot records");
        check(record[1].str()==displayName&&std::filesystem::weakly_canonical(record[2].str())==png&&std::stoi(record[3])==row.at("window_width")&&std::stoi(record[4])==row.at("window_height")&&std::stoi(record[5])==160,"Observed screenshot record mismatch");
        check(h.at("render_frame")==160&&row.at("observed_frame")==160&&row.at("requested_frame")==160,"Wrong actual screenshot/render frame");
        check(h.at("state").at("input_frame")== (host=="editor"?159:160),"Host input/screenshot cadence mismatch");
        stateOracle(name,h.at("state"),width,height);traceOracle(trace,h.at("state"),4);
        FixtureImage raw;std::string error;check(loadFixturePng(png.string(),raw,&error),error);
        check(raw.width==row.at("window_width")&&raw.height==row.at("window_height"),"Wrong screenshot dimensions");
        int x=0,y=0;if(host=="editor"){
            const auto metrics=read(hashed(row.at("metrics"),root));check(metrics.at("renderer").at("backend")==api,"Editor backend mismatch");
            check(metrics.at("capture").at("frames").get<int>()>=164&&metrics.at("capture").at("viewport_width")==width&&metrics.at("capture").at("viewport_height")==height,"Wrong editor metrics/frame/viewport");
            x=h.at("state").at("viewport")[0];y=h.at("state").at("viewport")[1];
        }else check(raw.width==width&&raw.height==height,"Runtime surface mismatch");
        check(x>=0&&y>=0&&x+width<=raw.width&&y+height<=raw.height,"Invalid viewport rectangle");
        auto image=cropFixtureImage(raw,{x,y,width,height});auto content=contentOracle(image,h.at("state"));
        if(!states.contains(name))states[name]=semantic(h.at("state"));else same(states.at(name),semantic(h.at("state")));
        json comparison;
        if(!baselines.contains(key))baselines[key]=image;else {ImageCompareMetrics v;check(compareFixtureImages(baselines[key],image,8,v,&error),error);
            check(v.normalizedMae<=.01&&v.normalizedRmse<=.08&&v.changedPixelRatio<=.05,"Native image parity failed: "+label);
            comparison={{"mae",v.normalizedMae},{"rmse",v.normalizedRmse},{"changed",v.changedPixelRatio}};
        }
        sheet.emplace_back(label,std::move(image));results.push_back({{"label",label},{"content",content},{"comparison",comparison}});
    }
    const auto contact=buildFixtureContactSheet(sheet,6,320,180);std::string error;check(saveFixturePng((root/"contact-sheet.png").string(),contact,&error),error);
    std::ofstream out(root/"validation.json");out<<json{{"ok",true},{"captures",results}}.dump(2);std::cout<<"Exploration native matrix PASS ("<<results.size()<<" captures)\n";
}
}
int main(int argc,char** argv){try{
    runningGuard=std::filesystem::weakly_canonical(std::filesystem::absolute(argv[0]));
    check(argc==3,"Usage: ExplorationFixtureGuard scripts <directory> | matrix <manifest> | selftest <directory>");const std::string command=argv[1];
    if(command=="selftest"){selftest(argv[2]);return 0;}
    if(command=="blank"||command=="erase-player"||command=="erase-wall"||command=="erase-sensor"||command=="mirror"){
        FixtureImage image;std::string error;check(loadFixturePng(argv[2],image,&error),error);damage(image,command);check(saveFixturePng(argv[2],image,&error),error);return 0;
    }
    if(command=="scripts"){std::filesystem::create_directories(argv[2]);auto names=cases();names.insert(names.end(),supplementalCases().begin(),supplementalCases().end());for(const auto& name:names){std::ofstream out(std::filesystem::path(argv[2])/(name+".json"));out<<replayScript(name).dump(2);}return 0;}
    check(command=="matrix","Unknown command");matrix(argv[2]);return 0;
}catch(const std::exception& e){std::cerr<<"Exploration guard FAILED: "<<e.what()<<'\n';return 1;}}
