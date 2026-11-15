#include "../../SixBox/src/Features/ImGui/Overlay/Linux/mask_codec.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char** argv)
{
    using namespace LocalExtension;
    ModelPixels input;input.width=80;input.height=120;input.sequence=7;
    FILETIME time{};GetSystemTimeAsFileTime(&time);input.capturedAt=(uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;
    input.pixels.resize(size_t(input.width)*input.height*3);
    const auto pixel=[&](unsigned x,unsigned y,unsigned r,unsigned g,unsigned b)
    { auto* p=input.pixels.data()+(y*input.width+x)*3;p[0]=r;p[1]=g;p[2]=b; };
    // A concave model-like contour with a head, shoulder and two separate legs.
    // There is deliberately no rectangular bounding-box border.
    for(unsigned x=32;x<48;++x){pixel(x,10,255,80,0);pixel(x,29,255,80,0);}
    for(unsigned y=10;y<30;++y){pixel(32,y,255,80,0);pixel(47,y,255,80,0);}
    for(unsigned x=20;x<60;++x){pixel(x,32,128,40,0);pixel(x,72,255,80,0);}
    for(unsigned y=32;y<73;++y){pixel(20,y,255,80,0);pixel(59,y,255,80,0);}
    for(unsigned y=73;y<105;++y)for(unsigned x:{24u,34u,45u,55u})pixel(x,y,255,80,0);
    ModelEncoder codec;
    const auto bytes=codec.Encode(input,12345);
    std::ofstream output(argc > 1 ? argv[1] : "model-native.bin",std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());output.close();
    auto empty=input;std::fill(empty.pixels.begin(),empty.pixels.end(),0);empty.sequence=8;
    const auto clear=codec.Encode(empty,12345);
    if(clear.size()!=sizeof(ModelHeader)) return 2;
    ModelHeader header{};memcpy(&header,bytes.data(),sizeof(header));
    if(header.x!=20||header.y!=10||header.cropWidth!=40||header.cropHeight!=95||!header.bytes)return 3;
    std::cout<<"Native WIC codec: "<<header.bytes<<" PNG bytes; cropped "<<header.cropWidth<<"x"<<header.cropHeight<<"; empty frame verified\n";
}
