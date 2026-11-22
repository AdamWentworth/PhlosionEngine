
#if CLUSTER_PASS == 2
CLUSTER_SHARED uint classMask;
#endif
void clusteredMain(uvec3 group,uvec3 lane) {
#if CLUSTER_PASS == 0
    uvec3 cell=group*8u+lane;
    if(cell.x==0u&&cell.y==0u&&cell.z==0u){WORK(0)=0u;WORK(1)=0u;}
    if(cell.x<clusterCoarseX()&&cell.y<clusterCoarseY()&&cell.z<8u)
        WORK(clusterCoarseIndex(cell))=clusterCull(cell,4u,1048575u);
#elif CLUSTER_PASS == 1
    uvec3 cell=group*8u+lane;
    if(cell.x<clusterTilesX()&&cell.y<clusterTilesY()&&cell.z<32u)
        WORK(clusterFineIndex(cell))=clusterCull(cell,1u,WORK(clusterCoarseIndex(cell/4u)));
#elif CLUSTER_PASS == 2
    uint threadIndex=lane.y*8u+lane.x;
    if(threadIndex==0u)classMask=0u;
    CLUSTER_BARRIER;
    for(uint j=0u;j<4u;++j) {
        ivec2 p=ivec2(group.xy*16u+lane.xy+uvec2(j&1u,j>>1u)*8u);
        if(p.x<int(CP(8).x)&&p.y<int(CP(8).y)){
            STORE_DIFFUSE(p,vec4(0.0,0.0,0.0,0.0));STORE_SPECULAR(p,vec4(0.0,0.0,0.0,0.0));
            if(clusterEligible(p))CLUSTER_CLASS_OR(1u<<clusterClass(p));
        }
    }
    CLUSTER_BARRIER;
    if(threadIndex==0u)for(uint c=0u;c<2u;++c)if((classMask&(1u<<c))!=0u) {
        uint slot;CLUSTER_APPEND(c,slot);
        WORK(clusterListOffset()+c*clusterTilesX()*clusterTilesY()+slot)=group.y*clusterTilesX()+group.x;
    }
#elif CLUSTER_PASS == 3
    for(uint c=0u;c<2u;++c) {
        ARGS(c*3u)=(WORK(c)+63u)/64u;ARGS(c*3u+1u)=64u;ARGS(c*3u+2u)=1u;
    }
#else
    uint listIndex=group.x*64u+group.y,c=uint(CLUSTER_PASS-4);
    if(listIndex>=WORK(c))return;
    uint tile=WORK(clusterListOffset()+c*clusterTilesX()*clusterTilesY()+listIndex);
    uvec2 tilePosition=uvec2(tile%clusterTilesX(),tile/clusterTilesX())*16u;
    for(uint j=0u;j<4u;++j)clusterShade(ivec2(tilePosition+lane.xy+uvec2(j&1u,j>>1u)*8u));
#endif
}
