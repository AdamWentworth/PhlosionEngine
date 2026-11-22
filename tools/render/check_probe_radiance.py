"""Independent numerical check of native compact probe readbacks.
Usage: python check_probe_radiance.py PREFIX energy|gradient
Requires numpy (qualification tooling only). Does not import the production kernel generator.
"""
import sys,json,math
from pathlib import Path
import numpy as np
P=Path(sys.argv[1]);mode=sys.argv[2]
metadata=json.loads(Path(str(P)+".json").read_text())
bits=np.fromfile(str(P)+".bin",dtype="<u4").reshape(metadata["height"],metadata["width"])
def decode(bits):
    channels=[]
    for shift,mantissa in [(0,6),(11,6),(22,5)]:
        value=(bits>>shift)&((1<<(mantissa+5))-1)
        exponent=(value>>mantissa).astype(np.int32);fraction=(value&((1<<mantissa)-1)).astype(float)
        channels.append(np.where(exponent==0,np.ldexp(fraction,1-15-mantissa),
            np.ldexp(1+fraction/(1<<mantissa),exponent-15)))
        assert np.all(exponent!=31),"Nonfinite HDR value"
    return np.stack(channels,axis=-1)
def quantize(color):
    color=np.maximum(color,0)
    result=np.empty_like(color)
    for ch,m in enumerate((6,6,5)):
        exponent=np.floor(np.log2(np.maximum(color[...,ch],2**-14))).astype(int)
        unit=np.exp2(exponent-m)
        result[...,ch]=np.rint(color[...,ch]/unit)*unit
    return result
def ulp(color):
    return np.exp2(np.floor(np.log2(np.maximum(np.abs(color),2**-14)))-np.array([6,6,5]))
selection=metadata["selection"];offset=np.rint(np.array(selection[7][2:])*metadata["width"]).astype(int)
def face_image(face,level,prepared=False):
    size=128>>level
    rawOrigins=((0,0),(0,260),(0,392),(204,392),(312,392),(372,392))
    filteredOrigins=((0,0),(198,260),(102,392),(258,392),(342,392),(390,392))
    x,y=(filteredOrigins if prepared else rawOrigins)[level]
    x+=(face%3)*(size+2)+1;y+=(face//3)*(size+2)+1
    x+=offset[0];y+=offset[1]
    return decode(bits[y:y+size,x:x+size])
cubes=[np.stack([face_image(f,0) for f in range(6)])]
report={"mode":mode,"downsample":[],"filter":[],"faces":6,"levels":6}
if mode=="energy":
    target=quantize(np.array([3.,.4,.8]))
    for level in range(6):
        actual=np.stack([face_image(f,level,bool(level)) for f in range(6)])
        units=np.abs(actual-target)/ulp(target)
        error=float(units.max());report["filter"].append({"level":level,"maxUlp":error})
        assert error<=2,("Constant radiance energy changed",level,error)
else:
    for level in range(1,6):
        last=cubes[-1];size=last.shape[1]
        expected=quantize(last.reshape(6,size//2,2,size//2,2,3).mean(axis=(2,4)))
        actual=np.stack([face_image(f,level) for f in range(6)])
        error=float((np.abs(actual-expected)/ulp(expected)).max())
        report["downsample"].append({"level":level,"maxUlp":error})
        assert error<=3,("Box mip mismatch",level,error)
        cubes.append(expected)
    def direction(face,u,v):
        x=2*u-1;y=1-2*v
        out=((1,y,x),(-1,y,-x),(-x,1,-y),(-x,-1,y),(-x,y,1),(x,y,-1))[face]
        return np.array(out)/np.linalg.norm(out)
    def address(d):
        x,y,z=d;ax,ay,az=abs(x),abs(y),abs(z)
        if ax>=ay and ax>=az:f=0 if x>=0 else 1;u=(z if x>=0 else -z)/ax;v=y/ax
        elif ay>=az:f=2 if y>=0 else 3;u=-x/ay;v=(-z if y>=0 else z)/ay
        else:f=4 if z>=0 else 5;u=(-x if z>=0 else x)/az;v=y/az
        return f,.5+.5*u,.5-.5*v
    def sample(d,level):
        cube=cubes[level];size=cube.shape[1]
        f,u,v=address(d);x,y=u*size-.5,v*size-.5;ix,iy=math.floor(x),math.floor(y)
        fx,fy=x-ix,y-iy;out=np.zeros(3)
        for dx,dy,w in ((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),(0,1,(1-fx)*fy),(1,1,fx*fy)):
            tx,ty=ix+dx,iy+dy;tf=f
            if tx<0 or ty<0 or tx>=size or ty>=size:
                tf,tu,tv=address(direction(f,(tx+.5)/size,(ty+.5)/size))
                tx=min(size-1,max(0,math.floor(tu*size)));ty=min(size-1,max(0,math.floor(tv*size)))
            out+=cube[tf,ty,tx]*w
        return out
    def kernel(level):
        # Derive the deterministic Weyl sequence and accepted hemisphere,
        # then independently integrate the GGX PDF/solid-angle footprint.
        f=np.float32;g=f(1.324717957244746);a=f(1/g);b=f(1/f(g*g))
        r=f(f(level/5)**2);alpha=f(r*r);a2=f(alpha*alpha)
        target=(16,32,64,128,128)[level-1]
        for count in range(target,target*4):
            n=np.arange(count,dtype=np.float32)
            u=np.mod(f(.5)+a*n,1);v=np.mod(f(.5)+b*n,1)
            phi=f(2*math.pi)*u
            c=np.sqrt((f(1)-v)/(f(1)+(a2-f(1))*v));sn=np.sqrt(np.maximum(f(0),f(1)-c*c))
            dirs=np.stack((2*c*sn*np.cos(phi),2*c*sn*np.sin(phi),2*c*c-1),axis=1)
            mask=dirs[:,2]>=1e-4
            if int(mask.sum())>=target:break
        dirs=dirs[mask];c=c[mask]
        den=(c*a2-c)*c+f(1);pdf=(a2/(f(math.pi)*den*den))*.25
        lod=np.maximum(.5*np.log2((6*128*128)/(count*pdf*4*math.pi))+1,0)
        return dirs,lod
    for level in range(1,6):
        taps,lods=kernel(level);size=128>>level;worst=0.;rms=[]
        points=sorted(set((0,1,size//4,size//2,max(0,size-2),size-1)))
        for face in range(6):
            actual=face_image(face,level,True)
            for x,y in [(v,w) for v in points for w in points]:
                n=direction(face,(x+.5)/size,(y+.5)/size)
                z=1 if n[2]>=0 else -1;a=-1/(z+n[2])
                basis=np.array(((1+z*n[0]*n[0]*a,z*n[0]*n[1]*a,-z*n[0]),
                    (n[0]*n[1]*a,z+n[1]*n[1]*a,-n[1]),n))
                total=np.zeros(3)
                for tap,lod in zip(taps,lods):
                    d=tap@basis;l=float(np.clip(lod,0,5));lo=int(math.floor(l));hi=int(math.ceil(l))
                    total+=((1-l+lo)*sample(d,lo)+(l-lo)*sample(d,hi))*tap[2]
                expected=quantize(total/taps[:,2].sum())
                diff=np.abs(actual[y,x]-expected)/ulp(expected)
                worst=max(worst,float(diff.max()));rms.extend(diff)
        report["filter"].append({"level":level,"maxUlp":worst,"rmsUlp":float(np.sqrt(np.mean(np.square(rms)))),"samplesPerFace":len(points)**2})
        assert worst<=4,("GGX convolution differs from independent oracle",level,worst)
# Prepared guard texels must exactly reproduce the adjacent core sample.
# No changed energy/GGX threshold is allowed to compensate for a border error.
report["padding"]=[]
for level in range(6):
    for prepared in (False,True):
        if level==0 and prepared:continue
        size=128>>level
        cores=np.stack([face_image(f,level,prepared) for f in range(6)])
        origins=((0,0),(198,260),(102,392),(258,392),(342,392),(390,392)) if prepared else ((0,0),(0,260),(0,392),(204,392),(312,392),(372,392))
        worst=0
        for face in range(6):
            ox,oy=origins[level];ox+=offset[0]+(face%3)*(size+2);oy+=offset[1]+(face//3)*(size+2)
            border=decode(bits[oy:oy+size+2,ox:ox+size+2])
            for x,y in [(x,y) for y in range(size+2) for x in range(size+2) if x in (0,size+1) or y in (0,size+1)]:
                if mode=="energy":expected=cores[face,0,0]
                else:
                    tf,u,v=address(direction(face,(x-.5)/size,(y-.5)/size))
                    tx=min(size-1,max(0,math.floor(u*size)));ty=min(size-1,max(0,math.floor(v*size)))
                    expected=cores[tf,ty,tx]
                worst=max(worst,float((np.abs(border[y,x]-expected)/ulp(expected)).max()))
        report["padding"].append({"level":level,"prepared":prepared,"maxUlp":worst})
        assert worst==0,("Prepared seam border changed neighboring core",level,prepared,worst)
Path(str(P)+"-oracle.json").write_text(json.dumps(report,indent=2))
print(json.dumps(report))
