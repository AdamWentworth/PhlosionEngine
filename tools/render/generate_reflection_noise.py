"""Regenerate the owned 64x64 progressive blue-noise ranks (offline NumPy tool)."""
from pathlib import Path
import numpy as np
SIZE=64
def ranks(seed):
    rng=np.random.default_rng(seed)
    coord=np.minimum(np.arange(SIZE),SIZE-np.arange(SIZE)).astype(float)
    kernel=np.exp(-(coord[:,None]**2+coord[None,:]**2)/(2*1.9**2))
    initial=np.zeros((SIZE,SIZE),dtype=bool)
    initial.ravel()[rng.choice(SIZE*SIZE,512,replace=False)]=True
    field=np.fft.ifft2(np.fft.fft2(initial)*np.fft.fft2(kernel)).real
    def influence(index):return np.roll(kernel,np.unravel_index(index,(SIZE,SIZE)),axis=(0,1))
    # Relax the initial binary pattern, then rank clusters/voids progressively.
    for iteration in range(100000):
        clustered=int(np.argmax(np.where(initial,field,-np.inf)))
        initial.ravel()[clustered]=False;field-=influence(clustered)
        empty=int(np.argmin(np.where(initial,np.inf,field)))
        initial.ravel()[empty]=True;field+=influence(empty)
        if empty==clustered:break
    else:raise RuntimeError("Blue-noise relaxation did not converge")
    result=np.zeros((SIZE,SIZE),dtype=np.uint16)
    points=initial.copy();energy=field.copy()
    for rank in range(511,-1,-1):
        point=int(np.argmax(np.where(points,energy,-np.inf)))
        result.ravel()[point]=rank;points.ravel()[point]=False;energy-=influence(point)
    points=initial.copy();energy=field.copy()
    for rank in range(512,SIZE*SIZE//2):
        point=int(np.argmin(np.where(points,np.inf,energy)))
        result.ravel()[point]=rank;points.ravel()[point]=True;energy+=influence(point)
    points=~points
    energy=np.fft.ifft2(np.fft.fft2(points)*np.fft.fft2(kernel)).real
    for rank in range(SIZE*SIZE//2,SIZE*SIZE):
        point=int(np.argmax(np.where(points,energy,-np.inf)))
        result.ravel()[point]=rank;points.ravel()[point]=False;energy-=influence(point)
    assert np.array_equal(np.sort(result.ravel()),np.arange(SIZE*SIZE))
    binary=(result<1024).astype(float)-.25
    power=abs(np.fft.fft2(binary))**2
    y,x=np.meshgrid(coord,coord)
    low=(x*x+y*y>0)&(x*x+y*y<=16)
    high=(x*x+y*y>100)&(x*x+y*y<400)
    ratio=float(power[low].mean()/power[high].mean())
    assert ratio<.2,ratio
    return result,ratio

def generate(path):
    # Independent frame/channel patterns avoid the old translated-frame correlation.
    frames=[];ratios=[]
    for frame in range(64):
        channels=[]
        for channel in range(2):
            rank,ratio=ranks(2077+frame*1009+channel*7919)
            channels.append(rank.astype(np.uint32)*16)
            ratios.append(ratio)
        frames.append(channels[0]|(channels[1]<<16))
    data=np.asarray(frames,dtype=np.uint32)
    lines=[','.join(str(int(v))+'u' for v in row) for row in data.reshape(-1,32)]
    text="""#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace engine::render {
// Owned independent 64x64x64 blue-noise ranks. Two UNORM16 channels are packed
// into RGBA8 (little-endian pairs); the shader decodes them before sampling.
// Regenerate with tools/render/generate_reflection_noise.py.
inline std::vector<unsigned char> worldReflectionBlueNoise() {
    static constexpr std::array<std::uint32_t,262144> ranks{{
"""+',\n'.join(lines)+"""
    }};
    std::vector<unsigned char> pixels(ranks.size()*4);
    for(std::size_t i=0;i<ranks.size();++i)
        for(unsigned byte=0;byte<4;++byte)pixels[i*4+byte]=static_cast<unsigned char>(ranks[i]>>(byte*8));
    return pixels;
}
}
"""
    path.write_text(text,encoding='utf-8')
    print(f"128 independent patterns; worst low/high frequency power {max(ratios):.6f}",flush=True)
if __name__=='__main__':
    generate(Path(__file__).resolve().parents[2]/'src/engine/render/WorldReflectionBlueNoise.h')
