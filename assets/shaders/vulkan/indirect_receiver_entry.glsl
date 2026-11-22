// Same pixel center, inverse and evaluated normal as surface composition.
void indirectReceiver(ivec2 pixel) {
    if(pixel.x>=int(CP(8).x)||pixel.y>=int(CP(8).y))return;
    vec4 value=vec4(0.0,0.0,0.0,0.0),reverse=value;
    float depth=DEPTH_LOAD(pixel),tag=ALBEDO_LOAD(pixel).a;
    vec4 normal=NORMAL_LOAD(pixel);
    if(depth<.999999 && (tag<.16 || tag>.5) && normal.a>=.16 && normal.a<=.84) {
        vec2 uv=(vec2(pixel)+vec2(.5,.5))/CP(8).xy;
        vec4 clip=vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0);
        vec4 h=vec4(dot(CP(4),clip),dot(CP(5),clip),dot(CP(6),clip),dot(CP(7),clip));
        vec3 n=normal.xyz*2.0-1.0;n*=inversesqrt(max(dot(n,n),1e-12));
        value=worldIndirectNear(h.xyz/h.w,n);
        reverse=worldIndirectNear(h.xyz/h.w,-n);
    }
    STORE_INDIRECT(pixel,value);
    STORE_INDIRECT(pixel+ivec2(0,int(CP(8).y)),reverse);
}
