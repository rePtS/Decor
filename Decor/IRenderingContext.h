#pragma once
#include <d3d11.h>
#include <cstdint>

// Used by a scene to access necessary renderer internals
class IRenderingContext
{
public:

    virtual ~IRenderingContext() {};

    //virtual ID3D11Device*           GetDevice() const = 0;

    //virtual ID3D11DeviceContext*    GetImmediateContext() const = 0;

    virtual ID3D11Device& GetDevice() = 0;
    virtual ID3D11DeviceContext& GetDeviceContext() = 0;

    virtual bool CreateVertexShader(
        WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3DBlob *&pVsBlob,
        ID3D11VertexShader *&pVertexShader) const = 0;

    virtual bool CreatePixelShader(
        WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3D11PixelShader *&pPixelShader) const = 0;

    virtual bool GetWindowSize(uint32_t &width, uint32_t &height) const = 0;

    //virtual bool UsesMSAA() const = 0;
    //virtual uint32_t GetMsaaCount() const = 0;
    //virtual uint32_t GetMsaaQuality() const = 0;

    //virtual float GetFrameAnimationTime() const = 0; // In seconds

    virtual bool IsValid() const
    {
        //return GetDevice() && GetImmediateContext();
        return true;
    };

    /// <summary>
    /// Render states used by render backend
    /// </summary>
    enum RenderState
    {
        RENDER_DEFAULT = 0,        
        PRE_LOAD_LEVEL = 10,
        LOAD_LEVEL = 20,
        RENDER_LEVEL = 30
    };

    virtual void PreLoadLevel() = 0;
    virtual void LoadLevel(const TCHAR* szLevelName) = 0;
};
