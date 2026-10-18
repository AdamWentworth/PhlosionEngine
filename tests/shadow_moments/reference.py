"""Independent scalar float64 oracle for the public shadow moment fixtures."""
from pathlib import Path
import math

def depth(kind,x,y):
    if kind==0: return .6
    if kind==1: return .2 if x<8 else .8
    if kind==2: return .2 if y<8 else .8
    if kind==3: return .1+.025*x+.015*y
    if kind==4: return 0
    if kind==5: return 1
    if kind==6: return .95 if x==8 else .05
    return .1 if (x+y)%2==0 else .9

def radial(d,x,y):
    return math.sqrt((.12*x-.84)**2+(-.1*y+.7)**2+1)/(1.1+7*d)

def horizontal(kind,x,y,taps):
    values=[]
    for dx in range(-(taps//2),taps//2+1):
        bx,by=min(15,max(0,x+dx)),min(15,max(0,y))
        cached=.18 if kind==4 and bx<8 else 1
        values.append(min(radial(depth(kind,bx,by),x+dx,y),cached))
    return sum(values)/taps,sum(v*v for v in values)/taps

def unorm(v):
    return math.floor(min(1,max(0,v))*65535+.5)/65535

def visibility(m,receiver,opacity):
    if receiver<=m[0]: return 1
    variance=max(m[1]-m[0]**2,.00001)
    p=variance/(variance+(receiver-m[0])**2)
    return 1+opacity*(min(1,max(0,(p-.04)*1.0416667461395264))-1)

def main():
    result=[]
    for case in range(32):
        taps=[1,3,7,15][case//8]
        x,y=[(0,0),(15,15),(8,8),(8,8)][case%4]
        h=horizontal(case%8,x,y,taps)
        rows=[tuple(map(unorm,horizontal(case%8,x,min(15,max(0,y+dy)),taps))) for dy in range(-(taps//2),taps//2+1)]
        v=tuple(sum(row[c] for row in rows)/taps for c in range(2))
        result.extend((*h,*v,radial(.6,x,y),visibility(v,.1,1),visibility(v,.5,1),visibility(v,.8,.25)))
    path=Path(__file__).with_name('expected.inc')
    path.write_text('\n'.join(', '.join(format(v,'.17g') for v in result[i:i+8])+',' for i in range(0,len(result),8))+'\n')

if __name__=='__main__': main()
