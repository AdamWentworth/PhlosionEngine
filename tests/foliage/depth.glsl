void computeFixture(uint i,out vec4 a,out vec4 b) {
    float d=4.0+float(i),offset=-.25*float(i%4u),perspective=float(i%2u);
    vec3 camera=vec3(2.0,-3.0,5.0),forward=vec3(.6,0.0,.8);
    vec3 samplePosition=camera+forward*d+vec3(-.8,0.0,.6)*3.0+vec3(0.0,4.0,0.0);
    vec3 projection=perspective>.5?vec3(1.002,-.2002,1.0):vec3(.02,-.7,0.0);
    vec3 moved=worldFoliageDisplacedPosition(samplePosition,camera,forward,perspective,offset);
    float original=worldFoliagePositionDepth(samplePosition,camera,forward,projection);
    float reconstructed=worldFoliagePositionDepth(moved,camera,forward,projection);
    a=vec4(reconstructed,worldFoliageProjectedDepth(original,offset,projection),original,reconstructed>=0.0?1.0:0.0);
    b=vec4(dot(moved-camera,forward),dot(moved-camera,vec3(-.8,0.0,.6)),
           moved.y-camera.y,original);
}
