"""Independent contact-consumer arithmetic against native linear-HDR readbacks.
The fixture isolates direct specular and translucent-leaf diffuse lighting.
Tolerance covers compact normal/HDR storage, not display-image resemblance.
Run: python ContactConsumerOracle.py <TestContactComputeNative output for one API>
"""
from pathlib import Path
import numpy as np,json,sys
def unit(x):return x/np.maximum(np.linalg.norm(x,axis=-1,keepdims=True),1e-12)
def leaf(P):
    A=P/'42-consumer-leaf-off';B=P/'43-consumer-leaf-on'
    m=json.loads((B/'metadata.json').read_text());w,h=m['width'],m['height']
    off=np.fromfile(A/'color.bin',np.float32).reshape(h,w,4)
    on=np.fromfile(B/'color.bin',np.float32).reshape(h,w,4)
    ids=np.fromfile(B/'identity.bin',np.uint32).reshape(h,w)
    packed=np.fromfile(B/'normal.bin',np.uint32).reshape(h,w)
    n=unit(np.stack([(packed&1023)/1023,((packed>>10)&1023)/1023,((packed>>20)&1023)/1023],axis=-1)*2-1)
    depth=np.fromfile(B/'plane-7.bin',np.float32).reshape(h,w,4)[...,0]
    fy,fx=np.mgrid[:h,:w];clip=np.stack([(fx+.5)/w*2-1,1-(fy+.5)/h*2,depth*2-1,np.ones((h,w))],axis=-1)
    p=np.array(m['parameters']);pos=clip@p[4:8].T;pos=pos[...,:3]/pos[...,3,None]
    view=unit(p[10,:3]-pos);n=np.where((n*view).sum(-1)[...,None]<0,-n,n)
    cone=np.fromfile(B/'plane-3.bin',np.float32).reshape(h,w,4)
    axis=unit(cone[...,:3]*2-1);cosine=1-cone[...,3]
    align=(axis*n).sum(-1)
    visibility=np.clip((.1829369962*cosine*align+.5-.5*cosine**2)*align+.5-.56*cosine,0,1)
    visibility=np.where(cosine<.000001,1,visibility)
    ao=.75+.25*visibility # source translucency=1, diffuse leaf dim=.75
    albedo=.5
    a=2.0404*albedo-.3324;b=.6417-4.7951*albedo;c=2.7552*albedo+.6903
    bounce=np.clip(np.maximum(ao,((a*ao+b)*ao+c)*ao),0,1)
    light=unit(np.array([-.4,.8,.4]));nl=(n*light).sum(-1)
    diffuse=albedo*(np.maximum(nl,0)/np.pi+np.maximum(-nl,0)*.16)
    expected=diffuse*(1-bounce)
    actual=(off[...,:3]-on[...,:3]).mean(-1)
    err=abs(expected-actual)[ids==3]
    row={'pixels':int((ids==3).sum()),'max':float(err.max()),'mean':float(err.mean()),'mean_actual':float(actual[ids==3].mean()),'mean_expected':float(expected[ids==3].mean())}
    row['tolerance']=0.000244140625
    assert row['max']<=row['tolerance'],row
    return row

def specular(P):
    A=P/'36-consumer-specular-off';B=P/'37-consumer-specular-on'
    m=json.loads((B/'metadata.json').read_text());w,h=m['width'],m['height']
    off=np.fromfile(A/'color.bin',np.float32).reshape(h,w,4);on=np.fromfile(B/'color.bin',np.float32).reshape(h,w,4)
    ids=np.fromfile(B/'identity.bin',np.uint32).reshape(h,w);n=np.zeros((h,w,3))
    for i,normal in [(1,[0,1,0]),(2,[1,0,0]),(3,[0,0,1])]:n[ids==i]=normal
    depth=np.fromfile(B/'plane-7.bin',np.float32).reshape(h,w,4)[...,0]
    fy,fx=np.mgrid[:h,:w];clip=np.stack([(fx+.5)/w*2-1,1-(fy+.5)/h*2,depth*2-1,np.ones((h,w))],axis=-1)
    p=np.array(m['parameters']);pos=clip@p[4:8].T;pos=pos[...,:3]/pos[...,3,None]
    view=unit(p[10,:3]-pos);n=np.where((n*view).sum(-1)[...,None]<0,-n,n)
    cone=np.fromfile(B/'plane-3.bin',np.float32).reshape(h,w,4);axis=unit(cone[...,:3]*2-1)
    c=1-cone[...,3];r=.45
    refl=unit((2*np.sum(n*view,axis=-1)[...,None]*n-view)*(1-.75*r*r)+n*.75*r*r)
    lobe=np.arccos(2**(-3.321929931640625*r*r));aperture=np.arccos(c)
    sep=np.arccos(np.clip((axis*refl).sum(-1),-1,1));offset=-(1-np.clip((n*view).sum(-1),0,1))**3
    sep=np.clip(sep+offset,0,np.pi)/(1-offset/np.pi)
    inner=abs(aperture-lobe);outer=aperture+lobe
    t=np.clip((sep-inner)/(np.maximum(outer,inner+.000001)-inner),0,1);overlap=1-t*t*(3-2*t)
    visibility=np.clip(overlap*(1-np.maximum(c,np.cos(lobe)))/(1-np.cos(lobe)),0,1)
    visibility=np.where(cone[...,3]>=.999999,1,visibility)
    expected=off[...,:3]*(.5+.5*visibility[...,None]);err=abs(expected-on[...,:3])[ids>0]
    row={'pixels':int((ids>0).sum()),'max':float(err.max()),'mean':float(err.mean())}
    row['tolerance']=6.103515625e-05
    assert row['max']<=row['tolerance'],row
    return row

if __name__=='__main__':
    root=Path(sys.argv[1])
    result={'leaf':leaf(root),'directSpecular':specular(root)}
    for mode in ['direct','point','specular','ambient','emission','leaf']:
        records=json.loads((root/'records.json').read_text())
        pair=[r for r in records if r['name']=='consumer-'+mode+'-off']
        assert len(pair)==1,mode
        a=np.fromfile(Path(pair[0]['directory'])/'color.bin',np.float32).reshape(-1,4)
        assert float(a[:,:3].max())>.01,('Blank consumer control',mode)
    (root/'consumer-oracle-report.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
    print('CONTACT CONSUMER ORACLE PASS')
