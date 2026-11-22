"""Independent double-precision ConeAO stage oracle for native diagnostic exports.
Run on the GPU worker after TestContactComputeNative. Requires numpy.
The acceptance compares actual GPU storage, not the shader text or macro values.
"""
import json,sys
from pathlib import Path
import numpy as np
def unit(v,fallback=None):
    n=np.linalg.norm(v,axis=-1,keepdims=True)
    out=v/np.maximum(n,1e-30)
    if fallback is not None:out=np.where(n>1e-6,out,fallback)
    return out
def unorm(v):return np.rint(np.clip(v,0,1)*255)/255
def dot(a,b):return np.sum(a*b,axis=-1)
def fetch(a,x,y,clamp=False):
    h,w=a.shape[:2]
    data=a[np.clip(y,0,h-1),np.clip(x,0,w-1)]
    if clamp:return data
    mask=(x>=0)&(y>=0)&(x<w)&(y<h)
    return np.where(mask[...,None] if data.ndim>mask.ndim else mask,data,0)
def sample(a,uv,hardware=None):
    h,w=a.shape[:2];p=uv*np.array([w,h])-.5
    b=np.floor(p).astype(int);f=p-b
    if hardware is not None:
        # Bound hardware bilinear weights at the minimum eight fractional bits.
        # Both axes share these weights across previous depth/velocity/cone.
        f=np.where(np.array(hardware)>0,np.ceil(f*256),np.floor(f*256))/256
    if a.ndim==3:f=f[...,None,:]
    x,y=b[...,0],b[...,1];fx,fy=f[...,0],f[...,1]
    return ((1-fx)*fetch(a,x,y)+fx*fetch(a,x+1,y))*(1-fy)+((1-fx)*fetch(a,x,y+1)+fx*fetch(a,x+1,y+1))*fy
def position(uv,depth,inverse):
    a=np.concatenate([uv*np.array([2,-2])+np.array([-1,1]),(depth*2-1)[...,None],np.ones(depth.shape+(1,))],axis=-1)
    a=a@inverse.T
    return a[...,:3]/a[...,3,None]
def linear_world(uv,d,inv,eye,forward):
    ray=position(uv,np.full(d.shape,.5),inv)-eye
    return eye+ray*(d/np.maximum(dot(ray,forward),1e-8))[...,None]
def run(root):
    records=json.loads((root/'records.json').read_text())
    report=[];history={}
    for rec in records:
        directory=Path(rec['directory']);w,h=rec['width'],rec['height'];hw,hh=(w+1)//2,(h+1)//2
        planes=[]
        for i in range(8):
            shape=(h,w,4) if i in (3,7) else (hh,hw,4)
            planes.append(np.fromfile(directory/f'plane-{i}.bin',np.float32).reshape(shape).astype(float))
        packet=np.array(rec['parameters']);inv=packet[4:8];eye=packet[10,:3]
        right,up,forward=packet[11,:3],packet[12,:3],packet[13,:3]
        projection=np.array(rec['projection']).reshape(4,4).T
        # Camera packet must have an orthonormal orientation despite jitter.
        basis=np.stack([right,up,forward])
        assert np.max(abs(basis@basis.T-np.eye(3)))<2e-6,'jitter contaminated camera axes'
        y,x=np.mgrid[:hh,:hw];uv=np.stack([(x+.5)/hw,(y+.5)/hh],axis=-1)
        depth=planes[4][...,0];normals=planes[6];velocity=planes[5][...,:2]
        previous=history.get(rec['view'])
        valid=previous is not None and not rec['reset'] and rec['generation']==previous[0]['generation'] and rec['frame']==previous[0]['frame']+1 and rec['quality']==previous[0]['quality'] and (w,h)==(previous[0]['width'],previous[0]['height'])
        assert bool(packet[9,2])==valid,(rec['name'],'history lifecycle mismatch')
        metrics={}
        def compare(label,expected,actual,tolerance):
            delta=np.abs(expected-actual)
            metrics[label]={'max':float(delta.max()),'mean':float(delta.mean()),'p99':float(np.quantile(delta,.99)),'tolerance':tolerance}
            if delta.max()>tolerance:raise AssertionError((rec['name'],rec['frame'],label,metrics[label]))
        # Farthest native depth, gathered BL/BR/TR/TL; normals and motion follow the winner.
        full=planes[7][...,0]
        locations=[(np.minimum(x*2+dx,w-1),np.minimum(y*2+dy,h-1)) for dx,dy in [(0,1),(1,1),(1,0),(0,0)]]
        ds=np.stack([full[yy,xx] for xx,yy in locations]);winner=np.argmax(ds,axis=0)
        sx=np.choose(winner,[a[0] for a in locations]);sy=np.choose(winner,[a[1] for a in locations])
        selected_depth=np.max(ds,axis=0);selected_uv=np.stack([(sx+.5)/w,(sy+.5)/h],axis=-1)
        # Native shader contracts permit a fused denominator and a reciprocal
        # rounded within one float32 ULP. Check that bound, rather than allowing
        # an arbitrary image-error tolerance near the sharp depth-weight kernel.
        def native_linear(d):
            z=(d.astype(np.float32)*np.float32(2)-np.float32(1)).astype(float)
            denominator=(z*packet[7,2]+packet[7,3]).astype(np.float32)
            return np.float32(1)/denominator
        expected_depth=native_linear(selected_depth)
        depth_ulp=np.spacing(expected_depth).astype(float)
        compare('half-depth-ulp',abs(expected_depth-depth)/depth_ulp,np.zeros_like(depth),1.001)
        full_motion=np.fromfile(directory/'motion.bin',np.float32).reshape(h,w,4).astype(float)
        expected_motion=full_motion[sy,sx,:2].astype(np.float16).astype(float)
        compare('half-velocity',expected_motion,velocity,1e-7)
        packed=np.fromfile(directory/'normal.bin',np.uint32).reshape(h,w)
        full_normals=np.stack([(packed&1023)/1023,((packed>>10)&1023)/1023,((packed>>20)&1023)/1023],axis=-1)
        compare('half-normal',unorm(full_normals[sy,sx]),normals[...,:3],1.01/255)
        identity=np.fromfile(directory/'identity.bin',np.uint32).reshape(h,w)
        flags=np.zeros((hh,hw),np.uint32)
        for xx,yy in locations:
            if rec['leaf']:flags|=np.where(identity[yy,xx]==3,1,0).astype(np.uint32)
            if rec['suppressed']:flags|=np.where(identity[yy,xx]==2,2,0).astype(np.uint32)
        flags|=np.where(full_motion[sy,sx,3]<.5,4,0).astype(np.uint32)
        compare('half-flags',flags,np.rint(normals[...,3]*255),0)
        # Horizon: camera-space double precision, source depth-derived normals and gathered rays.
        def raw_reference(gather_direction):
            projection_offset=np.array([dot(packet[0,:3],forward),dot(packet[1,:3],forward)])
            def view_at(uvs,d):
                screen=uvs*[2,-2]+[-1,1]-projection_offset
                return np.concatenate([screen*np.array([packet[11,3],packet[12,3]])*d[...,None],-d[...,None]],axis=-1)
            def at(xx,yy):return view_at(np.stack([(xx+.5)/hw,(yy+.5)/hh],axis=-1),fetch(depth,xx,yy))
            center=at(x,y)
            left,rightp,above,below=at(x-1,y),at(x+1,y),at(x,y-1),at(x,y+1)
            dx=np.where((abs(fetch(depth,x-1,y)-depth)<abs(fetch(depth,x+1,y)-depth))[...,None],center-left,rightp-center)
            dy=np.where((abs(fetch(depth,x,y-1)-depth)<abs(fetch(depth,x,y+1)-depth))[...,None],center-above,below-center)
            geometric=unit(np.cross(dy,dx),np.array([0,0,1]))
            z=unit(center,np.array([0,0,-1]));a=unit(np.cross(np.array([0,1,0]),z),np.array([-1,0,0]));b=np.cross(z,a)
            radius=np.where(depth>50,1.4+8.6*np.clip((depth-50)/40,0,1),np.where(depth>15,.4+np.clip((depth-15)/10,0,1),.4))
            fade=np.where(depth>50,1.5+40*np.clip((depth-50)/150,0,1),np.where(depth>15,1.5,np.clip((depth-5)/10,0,1)))
            bayer=np.array([[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]],float)/16+1/32
            phase=(.5+rec['frame']/((1+np.sqrt(5))/2))%1
            compare('weyl-phase',np.array(phase),np.array(packet[9,1]),2e-6)
            rotation=np.where((flags&1)!=0,0,(bayer[y%4,x%4]+phase)%1)
            radial=np.where((flags&1)!=0,1,2*((bayer[x%4,y%4]+phase)%1))
            directions,steps=[(5,3),(10,6),(15,10)][rec['quality']]
            omega=np.zeros_like(depth);moment=np.zeros_like(center)
            for angle_index in range(directions):
                angle=(angle_index+.5+rotation)*2*np.pi/directions
                direction=np.cos(angle)[...,None]*a+np.sin(angle)[...,None]*b
                near=center+direction*(radius/steps)[...,None]
                projected=near[...,:2]/(-near[...,2,None]*[packet[11,3],packet[12,3]])+projection_offset
                step=projected*[.5,-.5]+.5-uv
                denom=dot(geometric,near)
                t=np.divide(dot(geometric,center),denom,out=np.full_like(depth,-1),where=abs(denom)>1e-8)
                plane=np.where(t<=0,1,dot(unit(t[...,None]*near-center,z),z))
                horizon=np.ones_like(depth)
                for j in range(steps):
                    point=uv+step*(radial+j)[...,None]
                    footprint=point*[hw,hh]-.5
                    # Texture gather is discontinuous at integral coordinates.
                    # Bound the float32 arithmetic footprint by eight coordinate
                    # ULPs; compare possible gathers, not a looser color threshold.
                    rounding=8*np.finfo(np.float32).eps*np.maximum(abs(footprint),1)
                    anchor=np.floor(footprint+rounding*gather_direction).astype(int);xx,yy=anchor[...,0],anchor[...,1]
                    far=np.maximum.reduce([fetch(depth,xx+ox,yy+oy,True) for ox,oy in [(0,0),(1,0),(0,1),(1,1)]])
                    delta=view_at(point,far)-center;distance=np.linalg.norm(delta,axis=-1)
                    sample_cos=dot(delta,z)/np.maximum(distance,1e-8)
                    mix_factor=np.clip(distance-fade,0,1)
                    sample_cos=sample_cos*(1-mix_factor)+plane*mix_factor
                    sample_cos=np.where(np.all((point>=0)&(point<=1),axis=-1),sample_cos,plane)
                    horizon=np.minimum(horizon,sample_cos)
                cosine=np.clip(horizon,-1,1);omega+=(cosine+1)/directions
                axis=np.polynomial.polynomial.polyval(cosine,[-.5376213133912192,.49405493145877283,.03581331526499383])
                length=np.polynomial.polynomial.polyval(cosine,[.9305141157425706,.8494448854988428,-.1042526565205432,-.059156878555752766,-.038348820643562175])
                moment+=(np.sqrt(np.maximum(0,1-axis*axis))[...,None]*direction+axis[...,None]*z)*length[...,None]
            bent=unit(moment,geometric)
            axis=bent*[1,-1,-1]
            suppressed=np.zeros_like(depth,dtype=bool)
            for ox,oy in [(0,0),(1,0),(0,1),(1,1),(2,1),(1,2),(2,2)]:
                suppressed|=(fetch(flags,x+ox,y+oy,True)&2)!=0
            return unorm(np.concatenate([axis*.5+.5,np.where(suppressed,1,omega)[...,None]],axis=-1))
        raw=raw_reference(np.array([0,0]))
        raw_delta=abs(raw-planes[0])
        if raw_delta.max()>2.01/255:
            alternatives=[raw_reference(np.array([dx,dy])) for dx in [-1,0,1] for dy in [-1,0,1]]
            minimum=np.minimum.reduce(alternatives);maximum=np.maximum.reduce(alternatives)
            outside=np.maximum(np.maximum(minimum-planes[0],planes[0]-maximum),0)
            compare('raw-gather-rounding-bound',outside,np.zeros_like(outside),2.01/255)
            metrics['raw-central']={'max':float(raw_delta.max()),'intervalMax':float((maximum-minimum).max()),
                                    'ambiguousPixels':int(np.count_nonzero(np.max(maximum-minimum,axis=-1)>2.01/255))}
        else:
            compare('raw-cone',raw,planes[0],2.01/255)
        # Filter oracle uses measured, quantized prior pass: this isolates each stage.
        n=unit(normals[...,:3]-.5,-forward);total=np.zeros_like(depth);summed=np.zeros((hh,hw,4))
        for oy in range(-1,3):
            for ox in range(-1,3):
                d=fetch(depth,x+ox,y+oy)
                nn=unit(fetch(normals[...,:3],x+ox,y+oy)-.5,n)
                weight=np.clip(1-30*(1-dot(nn,n)),0,1)/(abs(d-depth)+.00000457088)
                total+=weight;summed+=fetch(planes[0],x+ox,y+oy)*weight[...,None]
        filtered=summed/total[...,None]
        axis=unit(filtered[...,:3]-.5,np.array([0,0,-1]))
        axis=axis[...,0,None]*right-axis[...,1,None]*up+axis[...,2,None]*forward
        spatial=unorm(np.concatenate([axis*.5+.5,filtered[...,3,None]],axis=-1))
        compare('spatial',spatial,planes[1],1.01/255)
        center=linear_world(uv,depth,inv,eye,forward)
        def temporal_reference(hardware=None):
            current=planes[1];temporal=current.copy()
            if valid:
                old=previous[1];oldpacket=previous[2]
                reproject=uv+velocity
                oldd=sample(old[4][...,0],reproject,hardware)
                oldworld=linear_world(reproject,oldd,oldpacket[4:8],oldpacket[10,:3],oldpacket[13,:3])
                agreement=np.clip(1-dot(center-oldworld,center-oldworld),0,1)
                match=np.clip(1-256*np.linalg.norm(velocity-sample(old[5][...,:2],reproject,hardware),axis=-1),0,1)
                previous_cone=sample(old[2],reproject,hardware);axis=unit(current[...,:3]-.5,-forward);oldaxis=unit(previous_cone[...,:3]-.5,axis)
                neighborhood=[sample(current,uv+np.array([ox-.5,oy-.5])/[hw,hh]) for ox,oy in [(0,0),(1,0),(0,1),(1,1)]]
                minimum=np.minimum.reduce([p[...,3] for p in neighborhood]);maximum=np.maximum.reduce([p[...,3] for p in neighborhood])
                angular=np.minimum.reduce([dot(axis,unit(p[...,:3]-.5,axis)) for p in neighborhood])
                margin=.5*match*(maximum-minimum+.05);minimum-=margin;maximum+=margin;angular-=.5*match*(1-angular+.01)
                alignment=dot(axis,oldaxis);clamped=np.maximum(angular,alignment)
                tangent=unit(oldaxis-alignment[...,None]*axis+np.array([0,0,.0001]),right)
                clipped=unit(tangent*np.sqrt(np.maximum(0,1-clamped*clamped))[...,None]+axis*clamped[...,None],axis)
                weight=.9*agreement
                mixed=unit(axis*(1-weight[...,None])+clipped*weight[...,None],axis)
                scalar=current[...,3]*(1-weight)+np.clip(previous_cone[...,3],minimum,maximum)*weight
                eligible=np.all((reproject>.5/np.array([hw,hh]))&(reproject<1-.5/np.array([hw,hh])),axis=-1)&((flags&4)==0)
                temporal=np.where(eligible[...,None],unorm(np.concatenate([mixed*.5+.5,scalar[...,None]],axis=-1)),current)
            return temporal
        temporal=temporal_reference()
        temporal_delta=abs(temporal-planes[2])
        if temporal_delta.max()>1.01/255:
            alternatives=[temporal]+[temporal_reference([dx,dy]) for dx in [-1,1] for dy in [-1,1]]
            minimum=np.minimum.reduce(alternatives);maximum=np.maximum.reduce(alternatives)
            outside=np.maximum(np.maximum(minimum-planes[2],planes[2]-maximum),0)
            compare('temporal-sampler-bound',outside,np.zeros_like(outside),1.01/255)
            metrics['temporal-central']={'max':float(temporal_delta.max()),'intervalMax':float((maximum-minimum).max())}
        else:compare('temporal',temporal,planes[2],1.01/255)
        fy,fx=np.mgrid[:h,:w];fuv=np.stack([(fx+.5)/w,(fy+.5)/h],axis=-1)
        fulld=native_linear(full)
        coord=np.stack([(fx+.5)*.5-.5,(fy+.5)*.5-.5],axis=-1);anchor=np.floor(coord).astype(int);frac=coord-anchor
        def upsample(full_depth):
            total=np.zeros((h,w));summed=np.zeros((h,w,4))
            for ox,oy in [(0,0),(1,0),(0,1),(1,1)]:
                xx,yy=anchor[...,0]+ox,anchor[...,1]+oy
                weight=(frac[...,0] if ox else 1-frac[...,0])*(frac[...,1] if oy else 1-frac[...,1])
                weight/=abs(fetch(depth,xx,yy,True)-full_depth)+.00000457088
                total+=weight;summed+=fetch(planes[2],xx,yy,True)*weight[...,None]
            return unorm(summed/total[...,None])
        rounded=[upsample(d.astype(float)) for d in
                 [np.nextafter(fulld,np.float32(-np.inf)),fulld,np.nextafter(fulld,np.float32(np.inf))]]
        minimum=np.minimum.reduce(rounded);maximum=np.maximum.reduce(rounded)
        outside=np.maximum(np.maximum(minimum-planes[3],planes[3]-maximum),0)
        compare('upsample-rounding-bound',outside,np.zeros_like(outside),1.01/255)
        metrics['upsample-central']={'max':float(abs(rounded[1]-planes[3]).max()),
                                     'intervalMax':float((maximum-minimum).max())}
        report.append({'name':rec['name'],'frame':rec['frame'],'metrics':metrics})
        history[rec['view']]=(rec,planes,packet)
        print(rec['name'],rec['frame'],'PASS',flush=True)
    (root/'oracle-report.json').write_text(json.dumps(report,indent=2))
    print('CONTACT ORACLE PASS',len(report))
if __name__=='__main__':run(Path(sys.argv[1]))
