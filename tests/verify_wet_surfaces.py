"""Independent double-precision wet-material oracle for native readbacks."""
import argparse,json,math
from pathlib import Path

def sat(x):return min(1.,max(0.,x))
def texture(index,u,v):
    size=16 if index==2 else 8
    qx=u*size-.5;qy=v*size-.5;ix=math.floor(qx);iy=math.floor(qy);fx=qx-ix;fy=qy-iy
    def texel(x,y):
        x%=size;y%=size
        return [(x*29+y*7+index*11)%256,(x*13+y*31+63+index*17)%256,
                (x*19+y*23+91+index*31)%256,110+(x*17+y*11)%146]
    a,b,c,d=texel(ix,iy),texel(ix+1,iy),texel(ix,iy+1),texel(ix+1,iy+1)
    return [((a[k]*(1-fx)+b[k]*fx)*(1-fy)+(c[k]*(1-fx)+d[k]*fx)*fy)/255 for k in range(4)]

def expected(case,pixel):
    mode=case['case'];pattern='pattern' in mode;wall=mode.startswith('wall');x,y=pixel
    x=(2*(x+.5)/128-1)*2;z=(2*(y+.5)/128-1)*2
    geometric=[0.,1.,0.];position=[x,0.,z]
    if mode=='slope':geometric=[0.,math.cos(math.pi/12),-math.sin(math.pi/12)];position[1]=z*math.tan(math.pi/12)
    if wall:geometric=[0.,0.,1.];position=[x,-z,0.]
    distance=case['camera'][2]-position[2] if wall else case['camera'][1]-position[1]
    color=[.5,.4,.3];dry=color[:];normal=geometric[:];rough=.8;reactive=0.
    metal=.8 if mode=='metal' else 0.
    moisture,puddles,rain=case['moisture'],case['puddles'],case['rain']
    if mode in ('character','weapon','roof','interior','car','water') or moisture+puddles+rain==0:
        return color,normal,rough,reactive
    exposure=1.
    if mode=='interior-edge':
        q=position[0]+4;cell=math.floor(q)-1;f=q-math.floor(q)
        exposure=1-((1-f)*(cell>=4)+f*(cell+1>=4))
    if mode=='car-edge':
        q=(position[0]+6)/12*8-.5;cell=math.floor(q);f=q-cell
        exposure=1-((1-f)*(cell>=4)+f*(cell+1>=4))
    if mode=='water-shallow':
        height=32768/65535*128-64
        exposure=1-sat(height/.4+.4)
    moisture*=exposure;puddles*=exposure;rain*=exposure
    porosity=rough**3*(1-metal);accumulated=0.;water=0.
    if distance<=120:
        fade=1-distance/120
        wet=sat(geometric[1]-1+1.5*moisture)
        colorMask=(1-porosity)**5
        color=[c*math.exp(-c*(1-colorMask)*wet*fade*5)for c in color]
        rough*=math.exp(-colorMask*wet*fade*5);accumulated=wet
        if mode!='vehicle':
            mask=(texture(0,position[0]/5,-position[2]/5)[3]*texture(0,position[0]/25,-position[2]/25)[3] if pattern else (230/255)**2)*sat((geometric[1]-1+.04*puddles)*25)
            puddleWet=sat((mask-.3)*2.5);water=sat((mask-.45)*15)
            color=[c*(1-puddleWet*porosity*(1-accumulated)*fade)for c in color]
            rough*=(1-puddleWet*fade)*(1-water*fade)
            normal=[a+(b-a)*water**2*fade for a,b in zip(normal,geometric)]
            accumulated=sat(accumulated+max(puddleWet,water))
    if distance<=15:
        fade=1-distance/15
        frame=math.floor((case['time']%1)*16)
        uv=(((position[0]*1.5)%1+(frame%4))*.25,((-position[2]*1.5)%1-math.floor(frame/4))*.25)
        tex=texture(2,*uv) if pattern else [150/255,115/255,178/255,1]
        normal[0]+=(tex[0]*2-1)*rain*.3*water*fade
        normal[2]-=(tex[1]*2-1)*rain*.3*water*fade
        reactive+=tex[2]*water*fade
    if distance<=5:
        fade=1-distance/5;tex=texture(0,position[0],-position[2]) if pattern else [128/255,179/255,204/255,230/255]
        mask=tex[2]*sat((geometric[1]-.75)*10)*rain
        phase=1-((case['time']*.4+tex[0])%1)
        dropWater=sat((phase-.85)/.15)*mask;dropWet=sat(tex[1]-1+2*moisture)*mask
        color=[c*(1-max(dropWater,dropWet)*porosity*(1-accumulated)*fade)for c in color]
        rough*=1-dropWater*fade
        normal=[a+(b-a)*dropWater*fade for a,b in zip(normal,geometric)]
        reactive+=dropWater*fade
    if distance<=30:
        a=b=[180/255,110/255,128/255,1]
        if pattern and wall:
            # At a front-facing wall Y projection dominates. The diagonal dither
            # sometimes selects XY/YX, which have the same coordinates here.
            d=((pixel[0]+.5+(pixel[1]+.5)*2-1.5+case['frame']%16)/5)%1
            n=((171/71)*(pixel[0]+.5)+(231/71)*(pixel[1]+.5))%1
            threshold=(5*d+n)/6*(1-2/255)+2/255
            diagonal=sat(((math.sqrt(.5)-.5)*2-threshold)/.001)
            u=position[0]*.75*(1-diagonal+diagonal/1.4142);v=position[1]*.25
            a=texture(1,u,v+case['time']*.1);b=texture(1,u*.33,v*.33+case['time']*.015)
        wave=.5+.5*math.sin(2*math.pi*((a[2]-b[2]+case['time']*.25)%1))
        weight=wave**2*sat((1-abs(geometric[1])-.2)*2)*(1-distance/30)*moisture*rain
        rough*=1-weight
        normal[0]+=(a[0]*2-1+b[0]*2-1)*weight
        normal[2]-=(a[1]*2-1+b[1]*2-1)*weight
    rough=max(rough,min(.8,metal*.1))
    length=math.sqrt(sum(n*n for n in normal));normal=[n/length for n in normal]
    if mode in ('emissive','emissive-soft'):
        blend=1 if mode=='emissive' else .5
        color=[c+(d-c)*blend for c,d in zip(color,dry)]
    return color,normal,rough,sat(reactive)

def verify(path):
    report=json.loads(path.read_text());maximum=dict(albedo=0.,normal=0.,roughness=0.,reactive=0.)
    failures=[]
    for case in report['cases']:
        for sample in case['samples']:
            expectedColor,expectedNormal,rough,reactive=expected(case,sample['pixel'])
            color=[((sample['albedo']>>(10*i)&1023)/1023)**2 for i in range(3)]
            normal=[(sample['normal']>>(10*i)&1023)/1023*2-1 for i in range(3)]
            errors=dict(albedo=max(abs(a-b)for a,b in zip(color,expectedColor)),
                        normal=max(abs(a-b)for a,b in zip(normal,expectedNormal)),
                        roughness=abs((sample['material']>>24)/255-rough),
                        reactive=abs((sample['metadata']>>24)/255-reactive))
            for key,value in errors.items():maximum[key]=max(maximum[key],value)
            # RGBA8 hardware bilinear interpolation has finite subtexel precision.
            # The two sampled alphas feed a 15x contrast threshold, then UNORM8
            # roughness/reactivity. Keep the constant-mask arithmetic gate strict;
            # allow four final UNORM8 steps only for this filtered threshold test.
            tolerance=dict(albedo=.003,normal=.004,roughness=.003,reactive=.003)
            if 'pattern' in case['case']:tolerance.update(roughness=4/255,reactive=4/255)
            if sample['id']!=11 or any(v>tolerance[k] for k,v in errors.items()):
                failures.append(dict(case=case['case'],pixel=sample['pixel'],errors=errors,normal=normal,expectedNormal=expectedNormal))
    return dict(api=report['api'],cases=len(report['cases']),samples=sum(len(x['samples'])for x in report['cases']),
                maximum=maximum,failures=failures,passed=not failures)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('inputs',type=Path,nargs='+');p.add_argument('--output',type=Path)
    a=p.parse_args();report=[verify(path)for path in a.inputs]
    if len(a.inputs)>1:
        # Same-device backend contract: independent API images must agree exactly.
        inputs=[json.loads(path.read_text()) for path in a.inputs]
        reference=inputs[0]['cases']
        for result,data in zip(report,inputs):
            result['crossBackendIdentical']=data['cases']==reference
            result['passed']=result['passed'] and result['crossBackendIdentical']
    text=json.dumps(report,indent=2);print(text)
    if a.output:a.output.write_text(text+'\n')
    raise SystemExit(0 if all(x['passed']for x in report)else 1)
