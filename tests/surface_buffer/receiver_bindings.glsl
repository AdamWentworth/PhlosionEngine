#define CLUSTER_FORWARD_FALLBACK false
#ifndef WORLD_GI_PRIVATE
#define WORLD_GI_PRIVATE
#endif
WORLD_GI_PRIVATE uint receiverCase;
vec4 fixtureCluster(int row) {
    if(row==4)return vec4(64.0,64.0,0.0,receiverCase==0u?0.0:1.0);
    if(row==0)return vec4(1.0,0.0,0.0,0.0);
    if(row==1)return vec4(0.0,1.0,0.0,0.0);
    if(row==2)return vec4(0.0,0.0,1.0,0.0);
    return vec4(0.0,0.0,0.0,receiverCase==9u?-1.0:1.0);
}
#define WORLD_GI_NEAR_ONLY
#define WORLD_GI_DATA(i) vec4(1.0,1.0,1.0,1.0)
#define WORLD_GI_RESULT(i) vec4(1.0,1.0,1.0,1.0)
#define ENV_SUN vec4(0.0,1.0,0.0,1.0)
#define ENV_BOUNCE(i) vec4(2.0,2.0,2.0,2.0)
#define WORLD_GI_FORWARD (receiverCase==10u)
#define CLUSTER_VIEW(i) fixtureCluster(i)
#define CLUSTER_DEPTH(p) (receiverCase==6u?.6:.5)
#define STORED_ALBEDO(p) vec4(0.0,0.0,0.0,receiverCase==2u?2.0/3.0:(receiverCase==3u?1.0/3.0:(receiverCase==14u?1.0:0.0)))
#define STORED_NORMAL(p) vec4(.5,1.0,.5,receiverCase==4u?0.0:(receiverCase==5u?1.0:2.0/3.0))
#define WORLD_GI_RECEIVER(p) ((p).y>=64?vec4(.75,.5,.25,1.0):vec4(.25,.5,.75,receiverCase==11u?0.0:1.0))
