// Shared finite-difference velocity/density simulation; engine coordinates are Y-up.
#ifdef WF_HLSL
static const uint WF_CELLS=262144u,WF_HEADER=64u;
#else
const uint WF_CELLS=262144u,WF_HEADER=64u;
#endif
uint windIndex(ivec3 p){return uint(p.x+p.y*64+p.z*4096);}
bool windInside(ivec3 p){return all(greaterThanEqual(p,ivec3(0,0,0)))&&all(lessThan(p,ivec3(64,64,64)));}
vec4 windLoad(ivec3 p,uint base){
    // Root buffer descriptors have no hardware range. Keep the address valid
    // even when a compiler evaluates both sides of a conditional expression.
    ivec3 safeCell=clamp(p,ivec3(0,0,0),ivec3(63,63,63));
    vec4 value=WF_READ(base+windIndex(safeCell));
    return windInside(p)?value:vec4(0,0,0,0);
}
vec4 windLinear(vec3 p,uint base){
    ivec3 a=ivec3(floor(p));vec3 f=fract(p);vec4 sum=vec4(0,0,0,0);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        float w=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y)*(z==0?1-f.z:f.z);
        sum+=windLoad(a+ivec3(x,y,z),base)*w;
    }return sum;
}
void windField(uint i){
    if(i>=WF_CELLS)return;
    ivec3 p=ivec3(int(i%64u),int((i/64u)%64u),int(i/4096u));
    uint current=WF_HEADER,previous=WF_HEADER+WF_CELLS,temp=WF_HEADER+WF_CELLS*2u;
#if WF_STAGE == 0
    WF_WRITE(previous+i,WF_PARAM(0).w>.5?vec4(0,0,0,0):WF_READ(current+i));
    if(i<WF_HEADER){WF_WRITE(i,WF_INPUT(i));}
#elif WF_STAGE == 1
    ivec3 q=p+ivec3(WF_PARAM(0).xyz);
    if(!windInside(q)||WF_PARAM(0).w>0.5){WF_WRITE(temp+i,vec4(0,0,0,0));return;}
    vec4 c=windLoad(q,current);float dt=WF_PARAM(1).w;
    if(dt<=0.0){WF_WRITE(temp+i,c);return;}
    vec4 l=windLoad(q-ivec3(1,0,0),current),r=windLoad(q+ivec3(1,0,0),current);
    vec4 b=windLoad(q-ivec3(0,1,0),current),t=windLoad(q+ivec3(0,1,0),current);
    vec4 n=windLoad(q-ivec3(0,0,1),current),f=windLoad(q+ivec3(0,0,1),current);
    vec4 dx=(r-l)*.5,dy=(t-b)*.5,dz=(f-n)*.5;
    vec3 gradient=vec3(dx.w,dy.w,dz.w);
    c.w-=dt*(dot(gradient,c.xyz)+(dx.x+dy.y+dz.z)*c.w);
    vec3 advected=windLinear(vec3(q)-dt*c.xyz,current).xyz;
    vec3 velocity=mix(c.xyz,advected,clamp(length(advected-c.xyz)/4.0,0.0,1.0));
    velocity+=dt*.73*(l.xyz+r.xyz+b.xyz+t.xyz+n.xyz+f.xyz-6.0*c.xyz)-.2*gradient;
    velocity=max(vec3(0,0,0),abs(velocity)-.001)*sign(velocity);
    c=vec4(clamp(velocity,vec3(-8,-8,-8),vec3(8,8,8)),clamp(max(0.0,c.w-.0254),0.0,1.6));
    if(p.x==0||p.x==63){c.yz=vec2(0,0);c.w=0;}
    if(p.y==0||p.y==63){c.xz=vec2(0,0);c.w=0;}
    if(p.z==0||p.z==63){c.xy=vec2(0,0);c.w=0;}
    WF_WRITE(temp+i,c);
#else
    vec4 c=WF_READ(temp+i);vec3 sum=vec3(0,0,0);
    vec3 world=WF_INPUT(0).xyz-16.0+(vec3(p)+.5)*.5;
    ivec3 cluster=p/4;uint row=2104u+uint(cluster.x+cluster.y*16+cluster.z*256)*9u;
    uint count=uint(WF_INPUT(row).x);
    if(WF_PARAM(1).w>0.0)for(uint j=1u;j<=count;++j){
        uint shape=64u+uint(WF_INPUT(row+j/4u)[j%4u])*4u;
        vec4 a=WF_INPUT(shape),b=WF_INPUT(shape+1u),anchor=WF_INPUT(shape+2u);
        vec3 axis=b.xyz-a.xyz;float len=dot(axis,axis);
        vec3 closest=a.xyz+axis*(len>1e-8?clamp(dot(world-a.xyz,axis)/len,0.0,1.0):0.0);
        vec3 radial=world-closest;float distance=length(radial);
        if(distance>=a.w)continue;
        vec3 direction=radial-anchor.xyz;
        if(len>1e-8){vec3 up=axis/sqrt(len);direction-=up*dot(direction,up);direction+=up*.01;}
        float magnitude=length(direction);if(magnitude<1e-6)continue;
        float envelope=1.0;
        if(WF_INPUT(shape+3u).x<.5)envelope=sqrt(clamp(anchor.w/.2,0.0,1.0))*(1.0-smoothstep(0.0,1.0,anchor.w));
        sum+=direction/magnitude*(1.0-distance/a.w)*.4*b.w*envelope;
    }
    vec3 edge=16.0-abs(world-WF_PARAM(1).xyz);
    float fade=clamp(min(edge.x,min(edge.y,edge.z))/4.0,0.0,1.0);fade*=fade;fade*=fade;
    c+=vec4(sum,min(.4,length(sum)))*fade;
    WF_WRITE(current+i,c);
#endif
}
