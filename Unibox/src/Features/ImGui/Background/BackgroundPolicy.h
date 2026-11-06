#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace MenuBackground
{
    constexpr int MaxDimension=2048,MaxTiles=196;
    struct V {float x=0,y=0;};
    struct Quad {std::array<V,4> position{},uv{};};
    enum class Fit {Cover,Contain,Stretch,Tile};
    inline float Finite(float value,float fallback,float low,float high)
    {return std::isfinite(value)?std::clamp(value,low,high):fallback;}
    struct Placement
    {
        Fit fit=Fit::Cover;
        float scale=1,x=0,y=0,rotation=0;
        bool flipX=false,flipY=false;
    };
    template<class Emit>
    int Layout(float width,float height,int imageWidth,int imageHeight,Placement p,Emit&& emit)
    {
        if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||imageWidth<=0||imageHeight<=0) return 0;
        p.scale=Finite(p.scale,1,.25f,3);p.x=Finite(p.x,0,-2,2);p.y=Finite(p.y,0,-2,2);
        const float angle=Finite(p.rotation,0,-180,180)*3.14159265358979323846f/180.f;
        const float c=std::cos(angle),s=std::sin(angle),ac=std::abs(c),as=std::abs(s);
        const float spanX=width*ac+height*as,spanY=width*as+height*ac;
        const V centre{width*(.5f+p.x*.5f),height*(.5f+p.y*.5f)};
        auto point=[&](float x,float y) {return V{centre.x+x*c-y*s,centre.y+x*s+y*c};};
        auto quad=[&](float x,float y,float w,float h) {
            Quad q;q.position={point(x,y),point(x+w,y),point(x+w,y+h),point(x,y+h)};
            const float u0=p.flipX?1.f:0.f,u1=1-u0,v0=p.flipY?1.f:0.f,v1=1-v0;
            q.uv={V{u0,v0},V{u1,v0},V{u1,v1},V{u0,v1}};emit(q);
        };
        const float iw=float(imageWidth),ih=float(imageHeight);
        if(p.fit==Fit::Tile)
        {
            // A fixed draw-call bound even for tiny images and extreme aspect ratios.
            const float scale=(std::max)({p.scale,spanX/(12.f*iw),spanY/(12.f*ih)});
            const float tw=iw*scale,th=ih*scale;
            const float dx=width*.5f-centre.x,dy=height*.5f-centre.y;
            const float cx=dx*c+dy*s,cy=-dx*s+dy*c;
            const int firstX=int(std::floor((cx-spanX*.5f)/tw)),lastX=int(std::ceil((cx+spanX*.5f)/tw));
            const int firstY=int(std::floor((cy-spanY*.5f)/th)),lastY=int(std::ceil((cy+spanY*.5f)/th));
            int count=0;
            for(int y=firstY;y<lastY&&count<MaxTiles;++y)
                for(int x=firstX;x<lastX&&count<MaxTiles;++x,++count) quad(x*tw,y*th,tw,th);
            return count;
        }
        float w=width,h=height;
        if(p.fit==Fit::Stretch) {w=spanX;h=spanY;}
        else
        {
            const float base=p.fit==Fit::Contain?(std::min)(width/(ac*iw+as*ih),height/(as*iw+ac*ih))
                :(std::max)(spanX/iw,spanY/ih);
            w=iw*base;h=ih*base;
        }
        w*=p.scale;h*=p.scale;quad(-w*.5f,-h*.5f,w,h);return 1;
    }
    struct Processing
    {
        float blur=0,brightness=1,contrast=1,saturation=1;
        bool operator==(const Processing&) const=default;
        static Processing Clean(Processing p)
        {return {Finite(p.blur,0,0,16),Finite(p.brightness,1,0,2),Finite(p.contrast,1,0,2),Finite(p.saturation,1,0,2)};}
    };
    struct Image {int width=0,height=0;std::vector<uint8_t> bgra;int naturalWidth=0,naturalHeight=0;};
    inline bool ValidImage(const Image& image)
    {return image.width>0&&image.height>0&&image.width<=MaxDimension&&image.height<=MaxDimension
        &&image.bgra.size()==size_t(image.width)*size_t(image.height)*4;}
    // Worker only. Separable normalized Gaussian on premultiplied color prevents
    // transparent PNG pixels from bleeding black/color into the visible edge.
    template<class Cancel>
    Image Process(const Image& source,Processing settings,Cancel&& cancel)
    {
        if(!ValidImage(source)||cancel()) return {};
        settings=Processing::Clean(settings);
        struct Pixel {float b=0,g=0,r=0,a=0;};
        const int w=source.width,h=source.height;
        std::vector<Pixel> pixels(size_t(w)*h),scratch;
        for(int y=0;y<h;++y)
        {
            if(cancel()) return {};
            for(int x=0;x<w;++x)
            {
                const size_t i=size_t(y)*w+x,k=i*4;
                float b=source.bgra[k]/255.f,g=source.bgra[k+1]/255.f,r=source.bgra[k+2]/255.f,a=source.bgra[k+3]/255.f;
                const float l=.2126f*r+.7152f*g+.0722f*b;
                auto color=[&](float v){return std::clamp(((l+(v-l)*settings.saturation)-.5f)*settings.contrast+.5f,0.f,1.f)*settings.brightness;};
                pixels[i]={std::clamp(color(b),0.f,1.f)*a,std::clamp(color(g),0.f,1.f)*a,std::clamp(color(r),0.f,1.f)*a,a};
            }
        }
        if(settings.blur>.01f)
        {
            const float sigma=settings.blur;
            const int radius=(std::min)(48,int(std::ceil(3.f*sigma)));
            std::array<float,97> weights{};float sum=0;
            for(int k=-radius;k<=radius;++k) {const float v=std::exp(-float(k*k)/(2*sigma*sigma));weights[k+radius]=v;sum+=v;}
            for(int i=0;i<=2*radius;++i) weights[i]/=sum;
            scratch.resize(pixels.size());
            for(int pass=0;pass<2;++pass)
            {
                for(int y=0;y<h;++y)
                {
                    if(cancel()) return {};
                    for(int x=0;x<w;++x)
                    {
                        Pixel p;
                        for(int k=-radius;k<=radius;++k)
                        {
                            const int sx=pass?x:std::clamp(x+k,0,w-1),sy=pass?std::clamp(y+k,0,h-1):y;
                            const auto& v=pixels[size_t(sy)*w+sx];const float a=weights[k+radius];
                            p.b+=v.b*a;p.g+=v.g*a;p.r+=v.r*a;p.a+=v.a*a;
                        }
                        scratch[size_t(y)*w+x]=p;
                    }
                }
                pixels.swap(scratch);
            }
        }
        Image out{w,h,{}};out.naturalWidth=source.naturalWidth;out.naturalHeight=source.naturalHeight;out.bgra.resize(source.bgra.size());
        auto byte=[](float v){return uint8_t(std::lround(std::clamp(v,0.f,1.f)*255.f));};
        for(size_t i=0;i<pixels.size();++i)
        {
            if((i%size_t(w))==0&&cancel()) return {};
            const auto& p=pixels[i];const float inv=p.a>1e-8f?1.f/p.a:0.f;
            out.bgra[i*4]=byte(p.b*inv);out.bgra[i*4+1]=byte(p.g*inv);out.bgra[i*4+2]=byte(p.r*inv);out.bgra[i*4+3]=byte(p.a);
        }
        return out;
    }
}
