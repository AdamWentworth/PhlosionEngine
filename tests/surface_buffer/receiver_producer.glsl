void computeFixture(uint i,out vec4 first,out vec4 second){
    producerCase=i;producerFront=vec4(-9.0,-9.0,-9.0,-9.0);producerBack=producerFront;
    indirectReceiver(ivec2(int(i%8u),int(i/8u)));
    first=producerFront;second=producerBack;
}
