#include "engine/render/WorldMotionGpu.h"
#include <iostream>
#include <limits>
#include <string>

using namespace engine::render;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Fn> void rejects(Fn&& fn) {
    bool rejected=false;
    try { fn(); } catch(const std::exception&) { rejected=true; }
    check(rejected,"invalid capture was accepted");
}
WorldMotionView view(std::uint64_t frame, std::uint64_t id=1) {
    WorldMotionView v;v.identity=id;v.generation=1;v.frame=frame;v.width=1920;v.height=1080;return v;
}
WorldMotionObject object(std::uint64_t id, float x=0) {
    WorldMotionObject o;o.identity={id,0};o.revision=1;o.model[12]=x;return o;
}
bool near(float a,float b) { return std::abs(a-b)<1e-6f; }
void posesAndVisibility() {
    WorldMotionHistory h;
    std::array<float,16> palette=worldMotionIdentity;
    auto a=object(1,3),b=object(2,7);a.deformation=palette;a.matrixCount=1;
    { WorldMotionCapture frame(h,view(1));
      check(!h.submit(a).valid,"first frame used history");
      h.submit(b);frame.commit(); }
    // Reusing the same caller allocation cannot mutate the owned previous pose.
    palette[12]=9;a.model[12]=4;b.model[12]=8;
    { WorldMotionCapture frame(h,view(2));
      auto pb=h.submit(b),pa=h.submit(a);
      check(pa.valid&&pb.valid,"reordering invalidated identities");
      check(pa.previous->deformation[12]==0&&pa.previous->model[12]==3,"borrowed palette or model survived");
      check(pb.previous->model[12]==7,"reordered objects exchanged motion");
      frame.commit(); }
    { WorldMotionCapture frame(h,view(3));h.submit(b);frame.commit(); }
    { WorldMotionCapture frame(h,view(4));
      check(!h.submit(a).valid,"reappearing object reused stale pose");
      check(h.submit(b).valid,"visible object lost history");frame.commit(); }
    check(h.stats().objects==2&&h.stats().valid==1&&h.stats().deformed==1,"capture counters wrong");
    check(h.stats().deformationBytes==64,"palette byte accounting wrong");
}
void instancesAndLods() {
    WorldMotionHistory h;
    std::array<float,64> wind{};wind[0]=.2f;wind[51]=12;
    auto plant=object(1);plant.instance[12]=12;plant.instance[14]=-40;
    plant.deformation=wind;plant.matrixCount=4;plant.materialMode=238;
    plant.materialParameters[1]=1;
    auto lod=plant;lod.identity.part=1;
    { WorldMotionCapture frame(h,view(1));h.submit(plant);h.submit(lod);frame.commit(); }
    wind[0]=.8f;wind[51]=28;
    { WorldMotionCapture frame(h,view(2));
      auto p=h.submit(plant);
      check(p.valid&&p.previous->deformation[0]==.2f&&p.previous->deformation[51]==12,"previous wind lost");
      check(p.previous->instance[12]==12&&p.previous->instance[14]==-40,"instance transform lost");
      check(p.previous->materialParameters[1]==1,"vertex parameters lost");
      check(h.submit(lod).imageIdentity!=p.imageIdentity,"LODs share identity");
      frame.commit(); }
    // A geometry revision or palette layout change must reject the old shape.
    plant.revision=2;
    { WorldMotionCapture frame(h,view(3));check(!h.submit(plant).valid,"revision ignored");frame.commit(); }
    plant.deformation={};plant.matrixCount=0;
    { WorldMotionCapture frame(h,view(4));check(!h.submit(plant).valid,"layout change ignored");frame.commit(); }
}
void viewsAndFailure() {
    WorldMotionHistory h;
    auto o=object(1);
    { WorldMotionCapture frame(h,view(1));h.submit(o);frame.commit(); }
    auto auxiliary=view(1,2);auxiliary.viewProjection[12]=42;
    { WorldMotionCapture frame(h,auxiliary);h.submit(object(1,99));frame.commit(); }
    { WorldMotionCapture frame(h,view(2));
      check(h.previousViewProjection()[12]==0,"auxiliary camera replaced main view");
      check(h.submit(o).previous->model[12]==0,"auxiliary pose replaced main view");
      // Exception/early return path: destructor aborts this capture.
    }
    check(!h.active()&&h.objectCount(1)==1,"aborted frame promoted state");
    { WorldMotionCapture frame(h,view(3));check(!h.submit(o).valid,"failed frame gap reused history");frame.commit(); }
    auto resized=view(4);resized.width=1280;
    { WorldMotionCapture frame(h,resized);check(!h.submit(o).valid,"resize reused history");frame.commit(); }
    auto cut=resized;cut.frame=5;cut.reset=true;
    { WorldMotionCapture frame(h,cut);check(!h.submit(o).valid,"cut reused history");frame.commit(); }
    cut.reset=false;cut.frame=6;
    { WorldMotionCapture frame(h,cut);check(h.submit(o).valid,"history did not recover");frame.commit(); }
    cut.frame=7;cut.generation=2;
    { WorldMotionCapture frame(h,cut);check(!h.submit(o).valid,"scene generation reused history");frame.commit(); }
    h.release(1);check(h.objectCount(1)==0&&h.objectCount(2)==1,"release crossed views");
    h.clear();check(h.objectCount(2)==0,"clear retained history");
}
void invalidInputs() {
    WorldMotionHistory h;
    rejects([&]{h.submit(object(1));});
    auto bad=view(1);bad.viewProjection[0]=std::numeric_limits<float>::infinity();
    rejects([&]{h.begin(bad);});
    check(!h.active(),"invalid begin left capture active");
    h.begin(view(1));
    rejects([&]{h.begin(view(2));});
    rejects([&]{h.release(1);});
    rejects([&]{h.submit(object(0));});
    auto invalid=object(2);invalid.matrixCount=1;
    rejects([&]{h.submit(invalid);});
    invalid=object(2);invalid.model[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{h.submit(invalid);});
    h.submit(object(1));
    rejects([&]{h.submit(object(1));});
    h.abort();
    rejects([&]{h.begin(view(1));});
    h.begin(view(2));h.submit(object(1));h.commit();
    rejects([&]{h.commit();});
}
void reusablePackets() {
    WorldMotionPose pose;pose.model[12]=3;pose.instance[13]=7;
    pose.materialParameters[11]=9;pose.matrixCount=2;
    pose.deformation.resize(32);
    for(unsigned i=0;i<32;++i)pose.deformation[i]=float(i)+.25f;
    WorldMotionSample sample{&pose,0x1234abcdu,true};
    std::vector<std::array<float,4>> words;
    packWorldMotionDraw(worldMotionIdentity,{&sample,1},words);
    check(words.size()==26 && words[0][0]==1 && words[1][0]==2,
        "packet count/offset layout changed");
    check(words[5][0]==3 && words[9][1]==7 && words[16][3]==9 &&
        words[17]==std::array<float,4>{2,0,43981,70196} &&
        words[18][0]==.25f && words[25][3]==31.25f,
        "packet transform, identity, validity or palette layout changed");
    const auto expected=words;const auto* storage=words.data();const auto capacity=words.capacity();
    for(int i=0;i<10;++i) {
        packWorldMotionDraw(worldMotionIdentity,{},words);
        check(words.size()==1 && words[0]==std::array<float,4>{},
            "empty draw inherited previous pose");
        packWorldMotionDraw(worldMotionIdentity,{&sample,1},words);
        check(words==expected && words.data()==storage && words.capacity()==capacity,
            "packet buffer reallocates or loses data after reset");
    }
    auto invalid=sample;invalid.imageIdentity=0;
    rejects([&]{packWorldMotionDraw(worldMotionIdentity,{&invalid,1},words);});
    check(words==expected,"invalid packet damaged current data");
    pose.skinningMode=1;
    rejects([&]{packWorldMotionDraw(worldMotionIdentity,{&sample,1},words);});
    check(words==expected,"invalid palette damaged current data");
    pose.deformation.resize(64,2.f);
    packWorldMotionDraw(worldMotionIdentity,{&sample,1},words);
    check(words.size()==34 && words[17][1]==1 && words.back()[3]==2,
        "expanded palette did not resize packet correctly");
}
void externalPalettePackets() {
    WorldMotionPose a,b;a.matrixCount=1;a.deformation.resize(16,1.f);
    b.matrixCount=2;b.skinningMode=1;b.deformation.resize(64,2.f);
    std::array samples{WorldMotionSample{&a,7,true},WorldMotionSample{&b,8,false}};
    std::array<std::uint32_t,2> offsets{128,256};
    std::vector<std::array<float,4>> words;
    packWorldMotionDraw(worldMotionIdentity,samples,words,offsets);
    check(words.size()==35 && words[1]==std::array<float,4>{3,128,1,0} &&
        words[2]==std::array<float,4>{19,256,1,0} &&
        words[18]==std::array<float,4>{1,0,7,65536} &&
        words[34]==std::array<float,4>{2,1,8,0},"external palette motion layout");
    const auto saved=words;
    rejects([&]{packWorldMotionDraw(worldMotionIdentity,samples,words,std::span(offsets).first(1));});
    offsets[1]=3;
    rejects([&]{packWorldMotionDraw(worldMotionIdentity,samples,words,offsets);});
    check(words==saved,"invalid external offset damaged prior packet");
    WorldMotionHistory history;history.begin(view(1));
    auto o=object(1);o.matrixCount=1;o.deformation=a.deformation;history.submit(o);history.commit();
    const auto retained=history.deformationPalettes();
    check(retained.size()==1 && retained[0].size()==16 && retained[0][0]==1,"previous palette not retained");
    a.deformation[0]=9;
    history.begin(view(2));o.deformation=a.deformation;history.submit(o);
    check(retained[0][0]==1 && history.deformationPalettes()[0][0]==1,"pending pose overwrote prior data");
    history.abort();history.release(1);
    check(history.deformationPalettes().empty(),"released view retained palette");
}
void cpuPositionsAndInstanceIdentity() {
    WorldMotionHistory history;
    std::array<std::array<float,3>,2> vertices{{{1,2,3},{4,5,6}}};
    auto a=object(81);a.identity.instance=9;a.positions=vertices;
    auto b=a;b.identity.instance=17;b.model[12]=7;
    std::uint32_t first=0,second=0;
    {WorldMotionCapture capture(history,view(1));
     first=history.submit(a).imageIdentity;second=history.submit(b).imageIdentity;capture.commit();}
    vertices[0][0]=20;
    {WorldMotionCapture capture(history,view(2));
     auto previousB=history.submit(b),previousA=history.submit(a);
     check(previousA.valid && previousB.valid && previousA.imageIdentity==first &&
           previousB.imageIdentity==second && first!=second,"instance reorder exchanged history");
     check(previousA.previous->positions[0][0]==1 && previousB.previous->model[12]==7,
           "CPU positions borrowed caller data");
     std::vector<std::array<float,4>> words;
     packWorldMotionDraw(worldMotionIdentity,{&previousA,1},words);
     check(words.size()==21 && words[1][3]==18 && words[18][0]==2 &&
           words[19]==std::array<float,4>{1,2,3,1} && words[20]==std::array<float,4>{4,5,6,1},
           "CPU previous-position native packet");
     std::array<std::uint32_t,1> offsets{0};
     std::vector<std::array<float,4>> external;
     packWorldMotionDraw(worldMotionIdentity,{&previousA,1},external,offsets);
     check(external.size()==words.size() && external[1][3]==18 && external.back()==words.back(),
           "external palette dropped CPU positions");
     capture.commit();}
    check(history.stats().positionVertices==4 && history.stats().deformationBytes==48 &&
          history.stats().deformationChanged==2,"CPU history accounting");
    a.positions=std::span(vertices).first(1);
    {WorldMotionCapture capture(history,view(3));
     check(!history.submit(a).valid,"changed CPU topology reused positions");capture.commit();}
    vertices[0][0]=std::numeric_limits<float>::quiet_NaN();a.positions=vertices;
    {WorldMotionCapture capture(history,view(4));
     rejects([&]{history.submit(a);});}
    vertices[0][0]=0;a.matrixCount=1;
    std::array<float,16> palette=worldMotionIdentity;a.deformation=palette;
    {WorldMotionCapture capture(history,view(5));
     rejects([&]{history.submit(a);});}
}
void projection() {
    auto v=projectWorldMotion({.4f,.2f,0,1},{0,0,0,1},true);
    check(v.valid&&near(v.previousUvMinusCurrentUv[0],-.2f)&&near(v.previousUvMinusCurrentUv[1],.1f),
        "rigid/camera motion sign or top-left convention wrong");
    v=projectWorldMotion({.4f,.2f,0,2},{.2f,.1f,0,1},true);
    check(v.valid&&near(v.previousUvMinusCurrentUv[0],0)&&near(v.previousUvMinusCurrentUv[1],0),
        "perspective division missing");
    // Animation: current bone moves +.4 X; camera shifts -.1 X, net .3 X.
    v=projectWorldMotion({.3f,0,0,1},{0,0,-.5f,1},true);
    check(v.valid&&near(v.previousUvMinusCurrentUv[0],-.15f)&&near(v.previousDepth,.25f),
        "combined animation/camera or previous depth wrong");
    check(!projectWorldMotion({0,0,0,1},{0,0,0,1},false).valid,"missing history accepted");
    check(!projectWorldMotion({0,0,0,1},{0,0,0,-1},true).valid,"behind eye accepted");
    check(!projectWorldMotion({0,0,0,1},{0,0,-2,1},true).valid,"near plane crossing accepted");
    check(!projectWorldMotion({std::numeric_limits<float>::infinity(),0,0,1},{0,0,0,1},true).valid,
        "nonfinite clip accepted");
}
}
int main() {
    try {
        cpuPositionsAndInstanceIdentity();posesAndVisibility();instancesAndLods();viewsAndFailure();invalidInputs();projection();reusablePackets();externalPalettePackets();
        std::cout<<"Motion history: owned poses, identity/LOD, failure/reset isolation, projection contract passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
