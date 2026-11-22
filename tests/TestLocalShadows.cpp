#include "engine/render/gltf/GltfSceneCulling.h"
#include "engine/render/IWorldLocalShadow.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace engine::render;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct ProbeRenderer final : IWorldLocalShadow {
    int begun = 0, ended = 0;
    void prewarmWorldLocalShadows() override {}
    void clearWorldLocalShadows() override {}
    bool beginWorldLocalShadow(std::uint32_t, const WorldDirectionalShadowView& view) override {
        check(validWorldDirectionalShadowView(view), "Invalid view reached renderer"); ++begun; return true;
    }
    bool endWorldLocalShadow() override { ++ended; return true; }
};
int main() try {

    WorldPointLight spot;spot.position={3,2,4};spot.radius=20;spot.intensity={3,4,5};
    spot.source.axis={0,0,-1};
    check(!pointLightShadowView(spot).enabled,"Omnidirectional source created a raster map");
    spot.source.cone=true;spot.shadow.angle=80;
    auto sv=pointLightShadowView(spot);
    check(validWorldDirectionalShadowView(sv),"Native spot shadow missing");
    auto sm=glm::make_mat4(sv.viewProjection.data());
    const double tangent=std::tan(80.*3.14159265358979323846/360.);
    for(auto point:{glm::vec3(3,2,3),glm::vec3(4,3,-2),glm::vec3(1,-1,-10)}) {
        auto h=sm*glm::vec4(point,1);auto q=glm::vec3(h)/h.w;
        const double z=4.-point.z,x=point.x-3.,y=point.y-2.;
        check(std::abs(q.x-x/(z*tangent))<2e-6&&std::abs(q.y-y/(z*tangent))<2e-6,"Spot projection differs from geometry");
        const double radial=h.w*sv.distanceMoments[3]*std::sqrt(1+tangent*tangent*(q.x*q.x+q.y*q.y));
        check(std::abs(radial-std::sqrt(x*x+y*y+z*z)/20.)<2e-6,"Spot radial distance differs");
    }
    spot.shadow.fadeDistance=10;spot.shadow.fadeRange=20;
    check(pointLightShadowView(spot,{3,2,14}).strength==1,"Full fade boundary");
    check(std::abs(pointLightShadowView(spot,{3,2,24}).strength-.5f)<1e-6,"Half fade boundary");
    check(!pointLightShadowView(spot,{3,2,34}).enabled,"Fully faded shadow still admitted");
    for(unsigned mode=0;mode<5;++mode){
        spot.shadow.softness=WorldShadowSoftness(mode);
        check(pointLightShadowView(spot).momentKernel==std::array<unsigned,5>{2,2,5,10,15}[mode],"Source softness mapping");
    }
    const auto fullSpot=pointLightShadowView(spot);
    spot.shadow.radius=3;
    const auto shortSpot=pointLightShadowView(spot);
    check(shortSpot.viewProjection==fullSpot.viewProjection && shortSpot.distanceMoments==fullSpot.distanceMoments,
        "Caster radius changed depth normalization");
    check(shortSpot.casterViewProjection!=fullSpot.casterViewProjection,"Caster range did not restrict query");
    const auto queryClip=glm::make_mat4(shortSpot.casterViewProjection.data())*glm::vec4(3,2,8,1);
    check(queryClip.z/queryClip.w>1,"Caster beyond authored query range was admitted");
    spot.shadow.radius=0;
    spot.participation&=~WorldLightShadows;check(!pointLightShadowView(spot).enabled,"Disabled shadow admitted");
    WorldLocalShadowSettings invalidSettings;invalidSettings.softness=WorldShadowSoftness(99);
    check(!validWorldLocalShadowSettings(invalidSettings),"Invalid softness accepted");
    const WorldRectLight light{{0,4,0},22,{4,0,0},{0,0,.3f},{5,2,8}};
    const auto view = rectLightShadowView(light);
    check(validWorldDirectionalShadowView(view), "Valid light did not produce a projection");
    const auto m = glm::make_mat4(view.viewProjection.data());
    const auto clip = [&](glm::vec3 p) { auto v=m*glm::vec4(p,1); return glm::vec3(v)/v.w; };
    check(std::abs(clip({0,4,0}).z+1)<1e-5f, "Near clip is not the emitting plane");
    check(clip({0,4.1f,0}).z < -1, "Housing behind the plane was not clipped");
    check(std::abs(clip({0,0,0}).x)<1e-6f && std::abs(clip({0,0,0}).y)<1e-6f &&
        std::abs(clip({0,0,0}).z)<1, "Ground receiver missing from frustum");
    check(std::abs(clip({3.99f,4,0}).x)<1, "Luminous plane edge missing");
    auto momentLight=light;momentLight.shadowFilter=WorldLocalShadowFilter::DistanceMoments;
    const auto momentView=rectLightShadowView(momentLight);
    check(validWorldDirectionalShadowView(momentView),"Moment projection rejected");
    const auto& mc=momentView.distanceMoments;
    for(const glm::vec3 point : {glm::vec3(0,0,0),glm::vec3(3,1,2),glm::vec3(-4,-3,1)}) {
        const auto projected=m*glm::vec4(point,1);
        const auto ndc=glm::vec3(projected)/projected.w;
        const float radial=1.f/((ndc.z*.5f+.5f)*mc[1]+mc[0])*std::sqrt(1+mc[2]*mc[2]*(ndc.x*ndc.x+ndc.y*ndc.y));
        const float distance=glm::length(glm::vec3(glm::lookAt(glm::vec3(0,4+4/3.7320508f,0),glm::vec3(0,4,0),glm::vec3(0,0,1))*glm::vec4(point,1)))*mc[3];
        check(std::abs(radial-distance)<2e-6f,"Filter and receiver radial distance differ");
    }
    auto badMoment=momentView;badMoment.distanceMoments[1]=0;
    check(!validWorldDirectionalShadowView(badMoment),"Invalid depth coefficients accepted");
    // Independent point projection and radial-distance oracle. Exercise the
    // source contract under rotation, all observed coverage angles, and a sphere.
    for (float fov : {120.f,150.f,160.f}) for (bool square : {false,true}) {
        auto finite=momentLight;finite.response=WorldRectLightResponse::FiniteLine;
        finite.position={3,-2,7};finite.halfU={2.4f,3.2f,0};
        finite.halfV=square?std::array<float,3>{0,0,4}:std::array<float,3>{0,0,.3f};
        const auto fv=rectLightShadowView(finite,fov);
        check(validWorldDirectionalShadowView(fv),"Finite source projection rejected");
        const glm::vec3 centre(3,-2,7),forward(.8f,-.6f,0),up(0,0,1),side(-.6f,-.8f,0);
        const double finiteTangent=std::tan(static_cast<double>(fov)*3.14159265358979323846/360.);
        const double shift=std::max(.05,(square?0.:3.7)/finiteTangent),far=shift+22;
        const glm::vec3 eye=centre-forward*static_cast<float>(shift);
        const auto fm=glm::make_mat4(fv.viewProjection.data());
        for (const glm::vec3 local : {glm::vec3(.2f,-.3f,1),glm::vec3(2,3,8),glm::vec3(-6,2,20)}) {
            const auto point=eye+side*local.x+up*local.y+forward*local.z;
            const auto p=fm*glm::vec4(point,1);const auto ndc=glm::vec3(p)/p.w;
            check(std::abs(ndc.x-local.x/(local.z*finiteTangent))<3e-6 &&
                std::abs(ndc.y-local.y/(local.z*finiteTangent))<3e-6,"Finite shadow UV differs from ray oracle");
            const auto& c=fv.distanceMoments;
            const double rayLength=std::sqrt(1+finiteTangent*finiteTangent*(ndc.x*ndc.x+ndc.y*ndc.y));
            const double depth=ndc.z*.5+.5,denominator=depth*c[1]+c[0];
            const double reconstructed=rayLength/denominator,expected=glm::length(local)/far;
            // Near-floor projections amplify float depth error at the far plane.
            // Bound that error by the reconstruction derivative, not source size.
            const double depthError=2*std::numeric_limits<float>::epsilon();
            const double errorBound=3e-6+depthError*std::abs(c[1])*rayLength/(denominator*denominator);
            check(std::abs(reconstructed-expected)<errorBound,"Finite shadow radial normalization differs");
            check(std::abs(p.w*c[3]*rayLength-expected)<3e-6,"Finite receiver distance differs from geometry");
        }
        const auto nearClip=fm*glm::vec4(centre,1);
        check(std::abs(nearClip.z/nearClip.w+1)<3e-5,"Finite source plane was not near-clipped");
        check(std::abs(fv.distanceMoments[3]-1/far)<1e-7,"Finite shadow range counts source size twice");
        finite.shadowFilter=WorldLocalShadowFilter::Pcf;
        check(rectLightShadowView(finite,fov).viewProjection==fv.viewProjection,"Filter choice changed projection");
    }
    for(float invalidFov : {-1.f,172.9f,180.f,std::numeric_limits<float>::quiet_NaN()})
        check(!rectLightShadowView(momentLight,invalidFov).enabled,"Invalid projection angle accepted");
    WorldLocalShadowAtlas momentAtlas;momentAtlas.enabled=true;
    check(momentAtlas.begin(15,momentView),"Moment pass rejected");momentAtlas.finish(momentView);
    check(momentAtlas.uniforms[94]==std::array<float,4>{1,mc[3],mc[2],-1.f/1024},"Moment receiver packing changed");
    auto invalid=light; invalid.range=0;
    check(!rectLightShadowView(invalid).enabled,"Disabled light cast a shadow");
    invalid=light;invalid.radiance={};
    check(!rectLightShadowView(invalid).enabled,"Off light cast a shadow");
    invalid=light;invalid.halfU[0]=std::numeric_limits<float>::quiet_NaN();
    check(!rectLightShadowView(invalid).enabled,"Nonfinite light accepted");
    WorldLocalShadowAtlas atlas;
    check(!atlas.begin(0,view),"Unprepared atlas accepted pass");
    check(atlas.directionalUv()[2]==1,"Disabled atlas changed sun coordinates");
    atlas.enabled=true;
    std::array<std::array<std::uint32_t,2>,8> tiles{};
    for(std::uint32_t i=0;i<8;++i) {
        check(atlas.begin(15-i,view),"Valid slot rejected");
        check(!atlas.begin(0,view),"Nested pass accepted");
        tiles[i]=atlas.offset();
        check(tiles[i][0]>=2048 && tiles[i][0]+1024<=4096 && tiles[i][1]+1024<=4096,"Tile overlaps sun or leaves atlas");
        for(std::uint32_t j=0;j<i;++j) check(tiles[i]!=tiles[j],"Tiles overlap");
        atlas.finish(view);
        check(atlas.uniforms[(15-i)*6+4][0]==1,"Receiver slot not enabled");
    }
    check(!atlas.begin(0,view),"Capacity exceeded");
    atlas.clear();
    check(atlas.enabled && atlas.uniforms==WorldLocalShadowUniforms{} && atlas.tileCount==0,"View clear retained stale shadows");
    check(!atlas.begin(worldLocalShadowSourceCapacity,view),"Invalid source index accepted");
    check(atlas.begin(0,view),"Reset cannot accept pass");atlas.finish(view);
    check(!atlas.begin(0,view),"Duplicate source used two tiles");
    WorldLocalShadowAtlas stable;stable.enabled=true;
    const std::array<std::uint32_t,8> first{1,2,3,4,5,6,7,8};
    stable.plan(first);
    std::array<std::uint32_t,8> original{};
    for(std::size_t i=0;i<first.size();++i) {
        original[i]=stable.slot(first[i]);check(stable.begin(first[i],view),"Planned pass rejected");stable.finish(view);
    }
    stable.clear();
    const std::array<std::uint32_t,8> replacement{0,2,3,4,5,6,7,8};
    stable.plan(replacement);
    check(stable.slot(0)==original[0],"New source did not use vacant slot");
    for(std::size_t i=1;i<first.size();++i)check(stable.slot(first[i])==original[i],"New early source displaced live cache");
    for(auto sourceId:replacement){check(stable.begin(sourceId,view),"Replacement source rejected");stable.finish(view);}
    stable.clear();
    const std::array<std::uint32_t,3> reordered{8,4,2};
    stable.plan(reordered);
    for(auto sourceId:reordered) {
        check(stable.slot(sourceId)==original[sourceId-1],"Reordering moved cached tile");
        check(stable.begin(sourceId,view),"Reordered source rejected");
        const auto xy=stable.offset();stable.finish(view);
        const auto uv=stable.uniforms[sourceId*6+5];
        check(uv[0]==xy[0]/4096.f&&uv[1]==xy[1]/4096.f,"Receiver lost stable tile");
    }
    stable.clear();bool invalidPlan=false;
    try{stable.plan(std::array<std::uint32_t,2>{4,4});}catch(const std::invalid_argument&){invalidPlan=true;}
    check(invalidPlan&&!stable.planned,"Invalid plan changed allocation");
    {
        using namespace engine::render::gltf::scene;
        GltfScenePrimitive p;p.skinIndex=0;p.boundsValid=true;p.boundsMin={-1,-1,-1};p.boundsMax={1,1,1};
        p.vertices.resize(1);p.vertices[0].weight0=.25f;p.vertices[0].weight1=.75f;p.vertices[0].joint1=1;
        std::array<glm::mat4,2> palette{
            glm::translate(glm::mat4(1),glm::vec3(3,0,0)),
            glm::rotate(glm::translate(glm::mat4(1),glm::vec3(-2,1,0)),.7f,glm::vec3(0,1,0))};
        const auto model=glm::translate(glm::mat4(1),glm::vec3(10,2,-5));
        const auto bounds=gltfSceneSkinnedBounds(p,model,palette);
        check(bounds.valid,"Valid animated bounds rejected");
        for(unsigned i=0;i<100;++i) {
            const float weight=float(i)/99;
            const glm::vec4 v(float(i%5)/2-1,float(i%7)/3-1,float(i%11)/5-1,1);
            const auto actual=model*(palette[0]*v*weight+palette[1]*v*(1-weight));
            for(unsigned axis=0;axis<3;++axis)
                check(actual[axis]>=bounds.boundsMin[axis]&&actual[axis]<=bounds.boundsMax[axis],
                    "Conservative pose bounds lost a blended vertex");
        }
        std::array<float,16> identity{};identity[0]=identity[5]=identity[10]=identity[15]=1;
        check(gltfSceneBoundsOutsideView(bounds,identity),"Distant animated caster admitted");
        const auto moved=gltfSceneSkinnedBounds(p,glm::mat4(1),palette);
        check(!gltfSceneBoundsOutsideView(moved,identity),"Crossing animated caster rejected");
        p.vertices[0].weight0=2;
        check(!gltfSceneSkinnedBounds(p,model,palette).valid,"Unsupported weights used unsafe bounds");
        p.vertices[0].weight0=.9995f;p.vertices[0].weight1=0;
        const auto residual=gltfSceneSkinnedBounds(p,glm::mat4(1),palette);
        check(residual.boundsMin[0]<=-1&&residual.boundsMax[0]>=1,"Residual bind position omitted");
    }
    ProbeRenderer renderer;
    WorldEnvironmentLighting environment; environment.rectLights.fill(light);
    int submitted = 0;
    drawWorldRectShadows(renderer,environment,[&](const auto&) { ++submitted; });
    check(submitted==0,"Disabled environment submitted shadows");
    environment.enabled=true;
    drawWorldRectShadows(renderer,environment,[&](const auto&) { ++submitted; });
    check(submitted==8 && renderer.begun==8 && renderer.ended==8,"Shadow budget/pass lifecycle changed");
    bool exception = false;
    try { drawWorldRectShadows(renderer,environment,[](const auto&) { throw std::runtime_error("draw failed"); }); }
    catch (const std::runtime_error&) { exception = true; }
    check(exception && renderer.begun==renderer.ended,"Draw failure left shadow pass open");
    environment.localShadowMapResolution=512;
    auto half=worldLocalShadowView(environment,0);
    check(half.enabled&&half.resolution==512,"Half-size local projection");
    WorldLocalShadowAtlas reduced;reduced.enabled=true;
    check(reduced.begin(0,half),"512px atlas admission");reduced.finish(half);
    check(reduced.uniforms[5][0]==.5f&&reduced.uniforms[5][2]==.125f,"512px receiver bounds");
    environment.localShadowMapResolution=0;
    check(!worldLocalShadowView(environment,0).enabled,"Off local shadows still admitted");
    environment.localShadowMapResolution=256;exception=false;
    try{worldLocalShadowView(environment,0);}catch(const std::invalid_argument&){exception=true;}
    check(exception,"Unsupported shadow size admitted");
    std::cout << "Local shadow projection, atlas isolation and lifecycle contracts passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
