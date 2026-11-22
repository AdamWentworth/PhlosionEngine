// Buffered directional visibility and its indirect-light consumers. The legacy
// inline horizon integral below remains available for orthographic/debug views.
// Matrix convention is OpenGL clip space; the sampled native depth is [0,1].
#if !defined(CONTACT_BUFFER_FETCH) || !defined(CONTACT)
bool worldContactFiltering() { return false; }
vec4 worldContactFilter(vec2 pixel) { return vec4(0.0,0.0,0.0,1.0); }
#endif
#ifndef CONTACT
bool worldContactPrepass() { return false; }
vec4 worldContactVisibility(vec3 position,vec3 normal,vec3 camera) { return vec4(normal,1.0); }
float worldContactDiffuseVisibility(vec4 contact,vec3 normal,vec3 view) { return 1.0; }
float worldContactSpecularVisibility(vec4 contact,vec3 normal,vec3 view,float roughness) { return 1.0; }
#else
#ifndef CONTACT_LOOP
#define CONTACT_LOOP
#endif
bool worldContactPrepass() { return CONTACT(8).w<-.5; }
vec4 contactTransform(vec4 p,int offset) {
    return vec4(dot(CONTACT(offset),p),dot(CONTACT(offset+1),p),dot(CONTACT(offset+2),p),dot(CONTACT(offset+3),p));
}
vec3 contactPosition(vec2 uv,float depth) {
    vec4 p=contactTransform(vec4(uv*2.0-1.0,depth*2.0-1.0,1.0),4);
    return p.xyz/p.w;
}
#ifdef CONTACT_BUFFER_FETCH
#include "world_contact_filter.glsl"
#endif
float worldContactDiffuseVisibility(vec4 contact,vec3 normal,vec3 view) {
    if(CONTACT(9).w<.5)return contact.w;
    if(dot(normal,view)<0.0)normal=-normal;
    float cosine=1.0-contact.w;
    if(cosine<.000001)return 1.0;
    float alignment=dot(contact.xyz,normal);
    return clamp((.1829369962*cosine*alignment+.5-.5*cosine*cosine)*alignment+.5-.56*cosine,0.0,1.0);
}
vec4 worldContactVisibility(vec3 position,vec3 normal,vec3 camera) {
    if(CONTACT(8).w<.5 || ENV_SUN.w<.5) return vec4(normal,1.0);
    vec3 view=normalize(camera-position);
    if(dot(normal,view)<0.0)normal=-normal;
#ifdef CONTACT_BUFFER_FETCH
    if(CONTACT(9).w>.5)return contactReconstruct(position,normal);
#endif
    vec4 clip=contactTransform(vec4(position,1.0),0);
    if(clip.w<=.001) return vec4(normal,1.0);
    vec2 uv=clip.xy/clip.w*.5+.5;
    vec2 dimensions=CONTACT(8).xy;
    float radius=CONTACT(8).z;
    float visibility=0.0;
    vec3 moment=vec3(0.0,0.0,0.0);
    CONTACT_LOOP for(int slice=0;slice<8;++slice) {
        float phi=(float(slice)+.5)*.3926990817;
        vec2 direction=vec2(cos(phi),sin(phi));
        vec3 tangent=contactPosition(uv+direction/dimensions,clip.z/clip.w*.5+.5)-position;
        tangent-=view*dot(tangent,view);
        float metresPerPixel=max(length(tangent),.000001);
        tangent/=metresPerPixel;
        vec3 axis=normalize(cross(tangent,view));
        vec3 projected=normal-axis*dot(normal,axis);
        float projectedLength=length(projected);
        if(projectedLength<.00001)continue;
        float n=atan(dot(projected,tangent),dot(projected,view));
        vec2 screenRadius=direction/dimensions*(radius/metresPerPixel);
        vec2 horizon=vec2(-1.0,-1.0);
        CONTACT_LOOP for(int side=0;side<2;++side) CONTACT_LOOP for(int stepIndex=0;stepIndex<4;++stepIndex) {
            float fraction=(float(stepIndex)+1.0)/4.0;
            vec2 sampleUv=uv+screenRadius*(side==0?1.0:-1.0)*fraction*fraction;
            if(sampleUv.x<0.0 || sampleUv.y<0.0 || sampleUv.x>=1.0 || sampleUv.y>=1.0)continue;
            // Y inversion can round an in-range UV to the first uncopied row.
            vec2 pixel=clamp(floor(vec2(sampleUv.x,1.0-sampleUv.y)*dimensions),
                vec2(0.0,0.0),dimensions-vec2(1.0,1.0));
            float depth=CONTACT_DEPTH(pixel);
            if(depth>=.999999)continue;
            vec2 center=(pixel+.5)/dimensions;center.y=1.0-center.y;
            vec3 delta=contactPosition(center,depth)-position-normal*CONTACT(9).x;
            float distanceToSample=length(delta);
            if(distanceToSample<.0001 || distanceToSample>=radius)continue;
            float cosine=dot(delta,view)/distanceToSample;
            cosine=mix(cosine,-1.0,smoothstep(radius*.8,radius,distanceToSample));
            if(side==0)horizon.x=max(horizon.x,cosine);else horizon.y=max(horizon.y,cosine);
        }
        float h0=n+max(-acos(clamp(horizon.y,-1.0,1.0))-n,-1.5707963268);
        float h1=n+min( acos(clamp(horizon.x,-1.0,1.0))-n, 1.5707963268);
        visibility+=projectedLength*.25*(2.0*cos(n)+2.0*(h0+h1)*sin(n)-cos(2.0*h0-n)-cos(2.0*h1-n));
        // First moment of the visible solid angle in this slice. Unlike the
        // scalar diffuse integral, its direction distinguishes opposite blockers.
        moment+=view*.5*(sin(h0)*sin(h0)+sin(h1)*sin(h1))+
            tangent*(.5*(h0+h1)-.25*(sin(2.0*h0)+sin(2.0*h1)));
    }
    vec3 bent=dot(moment,moment)>.000001?normalize(moment):normal;
    return vec4(bent,clamp(visibility*.125,0.0,1.0));
}
float worldContactSpecularVisibility(vec4 contact,vec3 normal,vec3 view,float roughness) {
    if(CONTACT(8).w<.5 || ENV_SUN.w<.5 || contact.w>=.999999) return 1.0;
    if(dot(normal,view)<0.0)normal=-normal;
    // Jimenez et al., Eq. 18: convert cosine-weighted visibility to a cone.
    float visibilityCosine=CONTACT(9).w>.5?1.0-contact.w:sqrt(max(1.0-contact.w,0.0));
    float r=clamp(roughness,.04,1.0);
    vec3 reflection=normalize(mix(reflect(-view,normal),normal,.75*r*r));
    float reflectionCosine=exp2(-3.321929931640625*r*r);
    float aperture=acos(visibilityCosine);
    float lobe=acos(reflectionCosine);
    float separation=acos(clamp(dot(contact.xyz,reflection),-1.0,1.0));
    float grazing=1.0-clamp(dot(normal,view),0.0,1.0);
    float threshold=CONTACT(10).w<-.5?CONTACT(22).x:-1.0;
    float offset=threshold*grazing*grazing*grazing;
    separation=clamp(separation+offset,0.0,3.1415926536)/(1.0-offset/3.1415926536);
    float inner=abs(aperture-lobe),outer=aperture+lobe;
    float overlap=1.0-smoothstep(inner,max(outer,inner+.000001),separation);
    return clamp(overlap*(1.0-max(visibilityCosine,reflectionCosine))/
        max(1.0-reflectionCosine,.000001),0.0,1.0);
}
#endif


// Raw diffuse visibility still drives the BRDF coefficients. Strength and leaf
// translucency apply separately to indirect and direct lighting; never emission.
bool worldContactPrepared() {
#ifdef CONTACT
    return CONTACT(8).w>.5 && CONTACT(10).w<-.5 && ENV_SUN.w>.5;
#else
    return false;
#endif
}
vec4 worldContactResponse(float diffuse,float specular,float translucency) {
#ifdef CONTACT
    if(worldContactPrepared()) {
        float d=mix(diffuse,1.0,CONTACT(22).y*clamp(translucency,0.0,1.0));
        float s=mix(specular,1.0,CONTACT(22).z*clamp(translucency,0.0,1.0));
        vec4 strength=CONTACT(23);
        return max(vec4(0.0,0.0,0.0,0.0),mix(vec4(1.0,1.0,1.0,1.0),vec4(d,s,d,s),strength));
    }
#endif
    return vec4(diffuse,specular,1.0,1.0);
}
