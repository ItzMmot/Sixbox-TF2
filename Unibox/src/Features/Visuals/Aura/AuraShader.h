#pragma once
// Mask-driven screen-space coating. s0 is actual model coverage, s1 its soft field.
inline constexpr char AuraPixelShader[] = R"HLSL(
#ifndef AURA_MODE
#define AURA_MODE 0
#endif
sampler2D ModelMask : register(s0);
#if AURA_MODE >= 14
float4 Step : register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 // Nine-tap Gaussian, five bilinear fetches; separable horizontal/vertical passes.
 float2 d=Step.xy*.25;
 float v=tex2D(ModelMask,uv).r*.227027027;
 v+=tex2D(ModelMask,uv+d*1.384615385).r*.316216216;
 v+=tex2D(ModelMask,uv-d*1.384615385).r*.316216216;
 v+=tex2D(ModelMask,uv+d*3.230769231).r*.070270270;
 v+=tex2D(ModelMask,uv-d*3.230769231).r*.070270270;
 return float4(v,v,v,1);
}
#else
sampler2D SoftMask : register(s1);
float4 Meta : register(c0); // seconds, animation speed, noise scale, quality
float4 Core : register(c1);
float4 Edge : register(c2);
float4 Look : register(c3); // opacity, intensity, interior fill, outer strength
float4 Wind : register(c4); // strength, frequency, screen direction, rise velocity
float4 Detail : register(c5); // turbulence, softness, flicker, halo
float4 Motion : register(c6); // smoothed x/y bend, pulse, swirl
float4 Shape : register(c7); // coating radius in UV x/y, aspect ratio, coverage mode
float hash(float2 p) {return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float noise(float2 p) {
 float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
}
float fbm(float2 p) {
 float v=noise(p)*.55;p=p*2.03+17.1;
 v+=noise(p)*.275;p=p*2.01-8.4;v+=noise(p)*.1375;
 if(Meta.w>.5) {p=p*2.04+2.8;v+=noise(p)*.06875;}
 if(Meta.w>1.5) {p=p*2.02-6.1;v+=noise(p)*.034375;}
 return v;
}
float3 spectrum(float h) {return .5+.5*cos(6.2831853*(h+float3(0,.333333,.666667)));}
float4 main(float2 uv:TEXCOORD0):COLOR0 {
 float solid=tex2D(ModelMask,uv).r;
 float soft=tex2D(SoftMask,uv).r;
 // Reject empty screen regions before the procedural texture work.
 float reach=tex2D(SoftMask,uv+float2(0,Shape.y*.55)).r;
 if(max(solid,max(soft,reach))<.0001) discard;
 float t=Meta.x*Meta.y;
 float2 p=uv*float2(Shape.z,1)*Meta.z*8;
 float gust=sin(t*Wind.y+p.y*.45)*Wind.x*.012;
 float2 bend=Motion.xy*1.4+float2(cos(Wind.z),sin(Wind.z))*gust;
 float2 q=p+float2(-t*.15,-t*(.12+Wind.w*.012));
 q+=float2(sin(p.y*.7-t),cos(p.x*.5+t))*(Motion.w*.25);
 float n=fbm(q+float2(fbm(q*.65+t*.08),0)*Detail.x);
 float2 warp=float2((n-.5)*Detail.x*.7, .12+n*.38)*(1+Wind.w*.004);
 float advected=tex2D(SoftMask,uv+Shape.xy*(warp+bend)).r;
 float field=max(soft*.5,advected);
 float rim=pow(saturate(field*3),1/max(Detail.y,.1))*(1-solid);
 float envelope=saturate(rim*Look.w);
 float pattern=saturate(.25+n*1.2);
 float heat=saturate(n*.9+soft*.55);
 float3 color=lerp(Edge.rgb,Core.rgb,heat);
#if AURA_MODE == 0
 pattern=pow(saturate(n*1.35+.1),1.5);
 color=lerp(Edge.rgb,Core.rgb,saturate(n*1.3+soft*.3));
#elif AURA_MODE == 1
 // Hologram: horizontal raster lines and a broad travelling scan bar.
 float scan=pow(.5+.5*sin(p.y*18-t*4),5);
 float sweep=pow(saturate(1-abs(frac(uv.y*.8-t*.12)-.5)*14),3);
 float dropout=step(.96,hash(float2(floor(p.y*8),floor(t*5))));
 pattern=(.035+scan*.9+sweep*.9)*(1-dropout*.85);
 color=lerp(Edge.rgb,Core.rgb,saturate(scan+sweep));
 envelope*=.15+scan+sweep;
#elif AURA_MODE == 2
 float2 cell=q*float2(3,4);float2 f=frac(cell)-.5;
 float spark=pow(saturate(1-length(f*float2(3,1.5))),5)*step(.65,hash(floor(cell)));
 pattern=saturate(spark*3+n*.12);color=lerp(Edge.rgb,Core.rgb,spark);
#elif AURA_MODE == 3
 float band=pow(saturate(1-abs(sin(n*20+t))),4);
 pattern=.15+band;color=lerp(Edge.rgb,Core.rgb,band);
#elif AURA_MODE == 4
 // Jagged bolts, re-routed on discrete frames, with narrow hot cores.
 float2 bolt=p*.55;float segment=bolt.y*4;
 float jitter=lerp(hash(float2(floor(bolt.x),floor(segment)+floor(t*12))),
     hash(float2(floor(bolt.x),floor(segment)+1+floor(t*12))),frac(segment));
 float distance=abs(frac(bolt.x)-(.2+jitter*.6));
 float arc=exp(-distance*75), corona=exp(-distance*12);
 pattern=.025+arc*2+corona*.2;color=lerp(Edge.rgb,Core.rgb,saturate(arc*2));
#elif AURA_MODE == 5
 float ribbon=pow(.5+.5*sin(p.x*3+sin(p.y*.7+t)*3+n*3),4);
 pattern=.15+ribbon;color=lerp(Edge.rgb,Core.rgb,.5+.5*sin(p.y*.4+n*2));
#elif AURA_MODE == 6
 float lattice=min(abs(sin((p.x+p.y)*4+n*2)),abs(sin((p.x-p.y)*4-n*2)));
 pattern=.1+pow(saturate(1-lattice*5),3);color=lerp(Edge.rgb,Core.rgb,pattern);
#elif AURA_MODE == 7
 // Circuit: Manhattan traces, junction rings and travelling signal pulses.
 float2 grid=p*.85,cell=floor(grid),f=frac(grid)-.5;
 f.x*=hash(cell)>.5 ? -1:1;f.y*=hash(cell+19)>.5 ? -1:1;
 float horizontal=(1-smoothstep(.025,.065,abs(f.y)))*step(f.x,0);
 float vertical=(1-smoothstep(.025,.065,abs(f.x)))*step(0,f.y);
 float trace=max(horizontal,vertical);
 float node=1-smoothstep(.015,.04,abs(length(f)-.11));
 float phase=frac(t*.6+hash(cell+7));
 float2 position=phase<.5 ? float2(phase-.5,0):float2(0,phase-.5);
 float signal=pow(saturate(1-length(f-position)*18),2);
 pattern=.025+trace*.9+node*.55+signal*2;
 color=lerp(Edge.rgb,Core.rgb,saturate(trace*.85+signal));
 envelope*=.12+trace+signal;
#elif AURA_MODE == 8
 // Hex shield: nearest centre in two offset hexagonal lattices.
 float2 grid=p*.65,spacing=float2(1,1.7320508);
 float2 a=(frac(grid/spacing)-.5)*spacing;
 float2 b=(frac((grid-spacing*.5)/spacing)-.5)*spacing;
 float2 local=dot(a,a)<dot(b,b) ? a:b;
 float2 id=grid-local;
 float d=max(abs(local.x),dot(abs(local),float2(.5,.8660254)));
 float border=1-smoothstep(.015,.055,abs(d-.5));
 float panel=pow(.5+.5*sin(t*3+dot(id,float2(1.2,2.3))),8);
 pattern=.035+border*1.25+panel*.55;
 color=lerp(Edge.rgb,Core.rgb,saturate(border+panel));
 envelope*=.15+border+panel*.5;
#elif AURA_MODE == 9
 float2 centre=(uv-.5)*float2(Shape.z,1);
 float spiral=pow(.5+.5*sin(atan2(centre.y,centre.x)*5-length(centre)*Meta.z*30+t*4),4);
 pattern=.04+spiral*1.25;color=lerp(Edge.rgb,Core.rgb,spiral);
#elif AURA_MODE == 10
 // Radar: a rotating beam with a fading wake, range rings and crosshairs.
 float2 radar=(uv-.5)*float2(Shape.z,1);
 float angle=atan2(radar.y,radar.x);
 float sweep=pow(1-frac((angle+3.14159265)/6.2831853-t*.12),6);
 float rings=1-smoothstep(.05,.2,abs(sin(length(radar)*Meta.z*19)));
 float axes=1-smoothstep(.002,.007,min(abs(radar.x),abs(radar.y)));
 pattern=.06+sweep*1.8+rings*.8+axes*.5;
 color=lerp(Edge.rgb,Core.rgb,saturate(sweep+rings*.85+axes*.6));
 envelope*=.15+sweep*1.5+rings*.5;
#elif AURA_MODE == 11
 // Digital rain: independently falling columns of changing 3x5 glyphs.
 float2 grid=p*float2(1.6,3);float column=floor(grid.x);
 float speed=.8+hash(float2(column,3))*1.2;
 float head=frac(grid.y*.09-t*.28*speed+hash(float2(column,9)));
 float2 glyphCell=float2(column,floor(grid.y)+floor(t*6*speed));
 float2 f=frac(grid),pixel=floor(f*float2(3,5));
 float glyph=step(.45,hash(pixel+glyphCell*7))*step(abs(f.x-.5),.36)*step(abs(f.y-.5),.42);
 float tail=pow(1-head,3), tip=step(.93,head);
 pattern=.015+glyph*(tail*1.7+tip*1.5);
 color=lerp(Edge.rgb,Core.rgb,saturate(tail+tip));
 envelope*=.1+glyph*(tail+tip);
#elif AURA_MODE == 12
 // Stylised chrome: contour-derived normals and broad moving reflection bands.
 float2 e=Shape.xy*.13;
 float dx=tex2D(SoftMask,uv+float2(e.x,0)).r-tex2D(SoftMask,uv-float2(e.x,0)).r;
 float dy=tex2D(SoftMask,uv+float2(0,e.y)).r-tex2D(SoftMask,uv-float2(0,e.y)).r;
 float3 normal=normalize(float3(-dx*4,-dy*4,.35));
 float reflected=.5+.5*sin(p.y*.5+normal.x*2.5+normal.y*1.5+sin(t*.6+p.x*.4)*.8);
 float band=smoothstep(.25,.65,reflected);
 float specular=pow(saturate(dot(normal,normalize(float3(.5,-.5,1)))),24);
 pattern=.45+specular*1.5+band*.9;
 color=lerp(Edge.rgb,Core.rgb,saturate(band*.85+specular));
 envelope*=.65;
#elif AURA_MODE == 13
 color=lerp(Edge.rgb,Core.rgb,n)*spectrum(p.y*.05+n*.4-t*.1)*1.5;
#endif
 float interior=solid*Look.z;
 if(Shape.w>.5 && Shape.w<1.5) interior=0; // Outline
 if(Shape.w>1.5) envelope=0; // Fill
 float halo=pow(saturate(soft*2),.8)*(1-solid)*Detail.w*.2;
 if(Shape.w>1.5) halo=0;
 float pulse=1-Motion.z*.35+sin(t*3)*Motion.z*.35;
 float flicker=1-Detail.z*.25+noise(float2(floor(t*18),0))*Detail.z*.5;
 float alpha=saturate(((interior+envelope)*pattern+halo)*Look.x*pulse*flicker*lerp(Edge.a,Core.a,heat));
 return float4(max(color*Look.y,0),alpha);
}
#endif
)HLSL";
