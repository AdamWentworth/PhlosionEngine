#ifndef WORLD_GI_PRIVATE
#define WORLD_GI_PRIVATE
#endif
WORLD_GI_PRIVATE uint producerCase;
WORLD_GI_PRIVATE vec4 producerFront,producerBack;
vec4 producerRow(int row) {
    if(row==8)return vec4(8.0,4.0,0.0,0.0);
    if(row==4)return vec4(1.0,0.0,0.0,0.0);
    if(row==5)return vec4(0.0,1.0,0.0,0.0);
    if(row==6)return vec4(0.0,0.0,1.0,0.0);
    return vec4(0.0,0.0,0.0,1.0);
}
vec4 worldIndirectNear(vec3 p,vec3 n){return vec4(p+n*2.0,.75);}
void producerStore(ivec2 p,vec4 value) {
    if(p.y>=4)producerBack=value;else producerFront=value;
}
#define CP(i) producerRow(i)
#define DEPTH_LOAD(p) (producerCase%4u==3u?1.0:.5)
#define ALBEDO_LOAD(p) vec4(0.0,0.0,0.0,producerCase%4u==1u?1.0/3.0:((producerCase&4u)!=0u?1.0:0.0))
#define NORMAL_LOAD(p) vec4(.5,.8,.9,producerCase%4u==2u?0.0:2.0/3.0)
#define STORE_INDIRECT(p,v) producerStore(p,v)
