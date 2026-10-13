// Material 249: depth-tested slices integrate a texture-modulated 3D density.
// local is ellipsoid space; step is normalized integration thickness.
vec4 worldVolumeColor(vec3 local, float seed, float time, vec4 color, float step) {
    float envelope = max(0.0, 1.0-dot(local,local));
    if (envelope <= 0.0) discard;
    vec3 warp = vec3(sin(local.y*3.1+time+seed),
                     sin(local.z*2.7-time*.75+seed*2.0),
                     sin(local.x*3.3+time*.5-seed))*.12;
    vec3 p = (local+warp)*.43+.5;
    float textureDensity = (VOLUME_SAMPLE(p.xy)+VOLUME_SAMPLE(p.yz)+VOLUME_SAMPLE(p.zx))/3.0;
    float billows = sin(local.x*5.1+seed+time)+sin(local.y*6.3-time*.5)+cos(local.z*5.7+seed);
    float detail = .12+.88*smoothstep(-.3,1.5,billows);
    float density = envelope*envelope*textureDensity*detail;
    float alpha = 1.0-exp(-max(0.0,color.a)*density*step*.6);
    return vec4(color.rgb*(.85+.15*clamp(local.y*.5+.5,0.0,1.0)),alpha);
}
