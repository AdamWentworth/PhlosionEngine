// ACES RRT/ODT mathematics adapted from Academy ACES 1.0.3.
// Copyright 2013 Academy of Motion Picture Arts and Sciences.
// See licenses/ACES-1.0.3.txt for license and warranty terms.
float displaySpline(float x,int kind) {
    float a,b,c,lowY,highY,slope;
    const float rlo[6]=float[6](-4.,-4.,-3.1573765773,-.4852499958,1.8477324706,1.8477324706);
    const float rhi[6]=float[6](-.7185482425,2.0810307172,3.6681241237,4.,4.,4.);
    const float olo[10]=float[10](-1.6989700043,-1.6989700043,-1.4779,-1.2291,-0.8648,-0.448,0.00518,0.4511080334,0.9113744414,0.9113744414);
    const float ohi[10]=float[10](0.491091168449,0.871391306351,1.16623441006,1.40302580755,1.53396612972,1.60563306486,1.64937489932,1.67527786053,1.6879569296,1.687873339);
    if(kind==0){a=log(.18*exp2(-15.))/log(10.);b=log(.18)/log(10.);c=log(.18*exp2(18.))/log(10.);lowY=-4.;highY=4.;slope=0.;}
    else{a=log(0.0018038330238608902)/log(10.);b=log(4.799999999717199)/log(10.);c=log(3245.05403183297)/log(10.);lowY=log(.02)/log(10.);highY=log(48.)/log(10.);slope=.04;}
    float lx=log(x>0.?x:exp2(-14.))/log(10.);
    if(lx<=a)return pow(10.,lowY);
    if(lx>=c)return pow(10.,highY+(lx-c)*slope);
    bool lower=lx<b;float k=float(kind==0?3:7)*(lx-(lower?a:b))/((lower?b:c)-(lower?a:b));
    int j=int(k);float t=k-float(j),v0,v1,v2;
    if(kind==0){v0=lower?rlo[j]:rhi[j];v1=lower?rlo[j+1]:rhi[j+1];v2=lower?rlo[j+2]:rhi[j+2];}
    else{v0=lower?olo[j]:ohi[j];v1=lower?olo[j+1]:ohi[j+1];v2=lower?olo[j+2]:ohi[j+2];}
    return pow(10.,.5*v0*(1.-t)*(1.-t)+v1*(.5+t-t*t)+.5*v2*t*t);
}
vec3 displayAces(vec3 v) {
    v=vec3(dot(v,vec3(.43963298,.38298870,.17737832)),
           dot(v,vec3(.08977644,.81343943,.09678413)),
           dot(v,vec3(.01754117,.11154655,.87091228)));
    float peak=max(v.r,max(v.g,v.b)),small=min(v.r,min(v.g,v.b));
    float sat=(max(peak,1e-7)-max(small,1e-7))/max(peak,.01);
    float yc=(v.r+v.g+v.b+1.75*sqrt(max(0.,v.b*(v.b-v.g)+v.g*(v.g-v.r)+v.r*(v.r-v.b))))/3.;
    float sx=(sat-.4)/.2,st=max(1.-abs(sx/2.),0.),gain=.025*(1.+sign(sx)*(1.-st*st));
    v*=1.+(yc<=.08*2./3.?gain:yc>=.16?0.:gain*(.08/yc-.5));
    float hue=(peak==small)?0.:ATAN2(1.73205080757*(v.g-v.b),2.*v.r-v.g-v.b)*57.2957795131;
    float u=abs(hue)/(135./4.),weight=u<1.?(4.-6.*u*u+3.*u*u*u)/4.:u<2.?pow(2.-u,3.)/4.:0.;
    v.r+=weight*sat*(.03-v.r)*.18;
    v=clamp(v,0.,65504.);
    v=clamp(vec3(dot(v,vec3(1.4514393161,-.2365107469,-.2149285693)),
                 dot(v,vec3(-.0765537734,1.1762296998,-.0996759264)),
                 dot(v,vec3(.0083161484,-.0060324498,.9977163014))),0.,65504.);
    // Preserve the connected shader's matrix convention.
    vec3 weights=vec3(.2722287168,.6740817658,.0536895174);
    float luminance=dot(v,weights);v=v*.96+vec3(luminance,luminance,luminance)*.04;
    v=vec3(displaySpline(v.r,0),displaySpline(v.g,0),displaySpline(v.b,0));
    v=vec3(displaySpline(v.r,1),displaySpline(v.g,1),displaySpline(v.b,1));
    v=max(v,vec3(.02,.02,.02));
    return max(vec3(dot(v,vec3(1.70505154,-.62179068,-.08325840)),
                    dot(v,vec3(-.13025714,1.14080268,-.01054853)),
                    dot(v,vec3(-.02400328,-.12896877,1.15297178)))/48.,vec3(0.,0.,0.));
}
vec3 displayGrade(vec3 v) {
    for(int c=0;c<3;++c)v[c]=v[c]<p5.w?v[c]*(1.-p4[c])+p4[c]*p5.w:
        v[c]<=p6.w?(v[c]-p5.w)*(1.+p5[c])+p5.w:(v[c]-p6.w)*(1.+p6[c])+p6.w;
    v=pow(max(p2.xyz*(v+p0.xyz*(vec3(1.,1.,1.)-v))+p3.xyz,vec3(0.,0.,0.)),p1.xyz);
    v=(v-vec3(p4.w,p4.w,p4.w))*p7.x+vec3(p4.w,p4.w,p4.w);
    v=vec3(dot(v,vec3(.8164966,-.4082483,-.4082483)),
           dot(v,vec3(0.,.70710677,-.70710677)),
           dot(v,vec3(.5352037,1.0548190,.1420281)));
    float sn=sin(p7.z),cs=cos(p7.z);v=vec3(v.x*cs-v.y*sn,v.x*sn+v.y*cs,v.z);
    v=vec3(dot(v,vec3(.84630,-.37265,.57735)),
           dot(v,vec3(-.37844,.33446,.57735)),dot(v,vec3(-.37844,-1.07975,.57735)));
    float l=dot(v,vec3(.299,.587,.114));
    return mix(vec3(l,l,l),v,p7.y)+vec3(p3.w,p3.w,p3.w);
}
