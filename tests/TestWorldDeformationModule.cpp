#include "engine/render/WorldDeformation.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
using namespace engine::render;
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    auto module=LoadLibraryA(argv[1]);if(!module)return 2;
    const auto create=reinterpret_cast<void(*)(WorldDeformation*)>(GetProcAddress(module,"makeDeformation"));
    if(!create)return 3;
    WorldDeformation first,second;create(&first);create(&second);
    WorldDeformationWeak live=first,expired=second;
    second.reset();
    if(!FreeLibrary(module))return 4;
    if(first->values()[0]!=42 || !expired.expired())return 5;
    auto retained=live.lock();first.reset();
    if(!retained || retained->values()[0]!=42)return 6;
    retained.reset();
    if(!live.expired())return 7;
    live.reset();expired.reset();
    std::cout<<"Module unload preserves owned deformations and safely retires weak residency.\n";
}
