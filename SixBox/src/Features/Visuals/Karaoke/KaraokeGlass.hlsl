// SixBox DX9 adaptation of Ali A. Jafari's MIT Liquid-Glass-with-WebGL shader.
// Source and license: SixBox/include/ThirdParty/KaraokeGlass/. Gaussian pass and
// render-target flow adapted from Eray's MIT imgui-blur-effect.
sampler2D Image : register(s0);
float4 Rect : register(c0); // screen normalized origin and dimensions; blur: direction.xy
float4 Shape : register(c1); // panel width/height, corner radius, minimum dimension
float4 Lens : register(c2); // material, refraction, magnification, chromatic separation (px)
float4 Finish : register(c3); // frost grain, edge light, opacity, unused
float4 Tint : register(c4);

float4 blur(float2 uv:TEXCOORD0):COLOR0
{
    float4 color=tex2D(Image,uv)*.227027027;
    color+=(tex2D(Image,uv+Rect.xy*1.384615385)+tex2D(Image,uv-Rect.xy*1.384615385))*.316216216;
    color+=(tex2D(Image,uv+Rect.xy*3.230769231)+tex2D(Image,uv-Rect.xy*3.230769231))*.070270270;
    return color;
}
float sdf(float2 p,float2 b,float radius)
{
    float2 d=abs(p)-b+radius;
    return min(max(d.x,d.y),0.)+length(max(d,0.))-radius;
}
float random(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453123);}
float noise(float2 p)
{
    float2 i=floor(p),f=frac(p),u=f*f*(3.-2.*f);
    float a=random(i),b=random(i+float2(1,0)),c=random(i+float2(0,1)),d=random(i+float2(1,1));
    return lerp(a,b,u.x)+(c-a)*u.y*(1.-u.x)+(d-b)*u.x*u.y;
}
float4 glass(float2 uv:TEXCOORD0,float4 vertex:COLOR0):COLOR0
{
    float2 coord=uv*Shape.xy,center=Shape.xy*.5,p=coord-center;
    float distance=sdf(p,center,min(Shape.z,Shape.w*.5));
    // Rounded edges use the alpha mask below (avoids DX9 TEXKILL quirks).
    float inward=-distance/max(Shape.w,1.);
    float2 normal=normalize(p+float2(.0001,.0001));
    float edgeDistance=1.-saturate(inward/.32);
    float bend=1.-sqrt(max(0.,1.-edgeDistance*edgeDistance));
    float2 sampleCoord=center+p/max(1.,Lens.z);
    if(Lens.x>1.5) sampleCoord-=bend*normal*Shape.xy*Lens.y;
    float2 sampleUV=Rect.xy+sampleCoord/Shape.xy*Rect.zw;
    float2 chroma=Lens.x>1.5?normal*Lens.w/Shape.xy*Rect.zw:float2(0,0);
    float3 color=float3(tex2D(Image,saturate(sampleUV-chroma)).r,
        tex2D(Image,saturate(sampleUV)).g,tex2D(Image,saturate(sampleUV+chroma)).b);
    color+=(noise(coord/2.5)-.5)*Finish.x;
    float light=pow(saturate(dot(-normal,normalize(float2(-.6,-.8)))),2.);
    color+=light*Finish.y*edgeDistance+ (1.-smoothstep(0.,1.5,-distance))*Finish.y;
    color=lerp(color,Tint.rgb,Tint.a);
    return float4(saturate(color),Finish.z*saturate(-distance+.5))*vertex;
}

// Glyph mask for 2D text; extruded triangles carry a camera-space normal in UV.
sampler2D Glyph : register(s0);
sampler2D Scene : register(s1);
float4 text(float2 uv:TEXCOORD0,float2 screen:TEXCOORD1,float4 vertex:COLOR0):COLOR0
{
    float alpha=1.;float2 normal=uv;float edge=0.;
    if(Finish.w<.5)
    {
        alpha=tex2D(Glyph,uv).a;
        float2 dx=ddx(uv)*1.5,dy=ddy(uv)*1.5;
        float2 gradient=float2(tex2D(Glyph,uv-dx).a-tex2D(Glyph,uv+dx).a,
            tex2D(Glyph,uv-dy).a-tex2D(Glyph,uv+dy).a);
        edge=saturate(length(gradient)*2.);
        normal=normalize(gradient+float2(.00001,.00001))*edge;
    }
    else edge=saturate(length(normal));
    float2 bend=Lens.x>1.5?normal*Lens.y*Rect.xy:float2(0,0);
    float2 chroma=Lens.x>1.5?normal*Lens.w*Rect.xy:float2(0,0);
    float3 color=float3(tex2D(Scene,saturate(screen-bend-chroma)).r,tex2D(Scene,saturate(screen-bend)).g,tex2D(Scene,saturate(screen-bend+chroma)).b);
    color+=(noise(screen*Rect.zw/2.5)-.5)*Finish.x;
    color=lerp(color,vertex.rgb,Tint.a);
    color+=Finish.y*edge*.35;
    return float4(saturate(color),alpha*vertex.a*Finish.z);
}

// Explicit VS3 linkage avoids fixed-function/SWVP drivers losing TEXCOORD0
// when a Shader Model 3 pixel shader is attached to ImGui's XYZ vertex stream.
struct VertexInput {float3 pos:POSITION0;float4 color:COLOR0;float2 uv:TEXCOORD0;};
struct VertexOutput {float4 pos:POSITION0;float4 color:COLOR0;float2 uv:TEXCOORD0;float2 screen:TEXCOORD1;};
VertexOutput vertex(VertexInput input)
{
    VertexOutput output;
    output.pos=float4(input.pos.x*Rect.x+Rect.z,input.pos.y*Rect.y+Rect.w,0.,1.);
    output.color=input.color;output.uv=input.uv;output.screen=(input.pos.xy+.5)*float2(Rect.x*.5,-Rect.y*.5);return output;
}
