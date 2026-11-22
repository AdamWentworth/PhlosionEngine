void computeFixture(uint i,out vec4 first,out vec4 second) {
    receiverCase=i%16u;
    vec3 p=vec3(receiverCase==8u?2.0:0.0,0.0,receiverCase==15u?.000002:0.0);
    vec3 n=normalize(vec3(receiverCase==12u?.001:(receiverCase==13u?1.0:0.0),receiverCase==7u?-1.0:1.0,0.0));
    vec4 value;
    bool valid=worldIndirectPreparedReceiver(p,n,value);
    first=vec4(valid?1.0:0.0,value.xyz);
    second=worldIndirectNear(p,n);
}
