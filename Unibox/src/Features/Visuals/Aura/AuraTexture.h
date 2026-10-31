#pragma once
#ifndef TEXTMODE
#include <d3d9.h>

namespace Aura::D3D
{
    // Called on Source's hardware render thread while the selected mask RT is
    // bound. Standalone/MSAA surfaces need a resolve instead of GetContainer.
    inline bool CaptureTarget(IDirect3DDevice9* device,IDirect3DTexture9*& owned)
    {
        IDirect3DSurface9* source=nullptr;
        if(FAILED(device->GetRenderTarget(0,&source)) || !source) return false;
        IDirect3DTexture9* texture=nullptr;
        source->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&texture));
        if(!texture)
        {
            D3DSURFACE_DESC description{},old{};
            if(FAILED(source->GetDesc(&description))) {source->Release();return false;}
            if(owned && SUCCEEDED(owned->GetLevelDesc(0,&old)) && old.Width==description.Width
                && old.Height==description.Height && old.Format==description.Format && (old.Usage&D3DUSAGE_RENDERTARGET))
            {texture=owned;texture->AddRef();}
            else if(FAILED(device->CreateTexture(description.Width,description.Height,1,D3DUSAGE_RENDERTARGET,
                description.Format,D3DPOOL_DEFAULT,&texture,nullptr))) {source->Release();return false;}
            IDirect3DSurface9* destination=nullptr;
            HRESULT result=texture->GetSurfaceLevel(0,&destination);
            if(SUCCEEDED(result)) result=device->StretchRect(source,nullptr,destination,nullptr,D3DTEXF_NONE);
            if(destination) destination->Release();
            if(FAILED(result)) {texture->Release();source->Release();return false;}
        }
        source->Release();
        if(owned) owned->Release();
        owned=texture;
        return true;
    }
}
#endif
