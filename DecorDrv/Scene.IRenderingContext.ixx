﻿module;

#include <d3d11.h>
#include <cstdint>
#include <bitset>

export module Scene.IRenderingContext;

/// <summary>
/// Interface to access necessary renderer internals
/// </summary>
export class IRenderingContext
{
public:

    virtual ~IRenderingContext() {};

    virtual ID3D11Device& GetDevice() = 0;
    virtual ID3D11DeviceContext& GetDeviceContext() = 0;

    virtual bool CreateVertexShader(
        WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3DBlob*& pVsBlob,
        ID3D11VertexShader*& pVertexShader) const = 0;

    virtual bool CreatePixelShader(
        WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3D11PixelShader*& pPixelShader) const = 0;

    virtual bool GetWindowSize(uint32_t& width, uint32_t& height) const = 0;

    //virtual bool UsesMSAA() const = 0;
    //virtual uint32_t GetMsaaCount() const = 0;
    //virtual uint32_t GetMsaaQuality() const = 0;
    //virtual float GetFrameAnimationTime() const = 0; // In seconds
};