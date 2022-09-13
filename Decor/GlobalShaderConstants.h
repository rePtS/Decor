#pragma once

#include <DirectXMath.h>
#include "ConstantBuffer.h"

////Operator new/delete for SSE aligned data
//class XMMAligned
//{
//public:
//    void* operator new(const size_t s)
//    {
//        return _aligned_malloc(s, std::alignment_of<__m128>::value);
//    }
//
//    void operator delete(void* const p)
//    {
//        _aligned_free(p);
//    }
//};

class GlobalShaderConstants //: public XMMAligned
{
public:
    explicit GlobalShaderConstants(ID3D11Device& Device, ID3D11DeviceContext& m_DeviceContext);
    GlobalShaderConstants(const GlobalShaderConstants&) = delete;
    GlobalShaderConstants& operator=(const GlobalShaderConstants&) = delete;

    //Operator new/delete for SSE aligned data
    void* operator new(const size_t s) { return _aligned_malloc(s, std::alignment_of<__m128>::value); }
    void operator delete(void* const p) { _aligned_free(p); }

    void Init();
    void SetSceneNode(const FSceneNode& SceneNode);
    void Bind();

protected:
    struct PerFrame
    {
        float fRes[2];
        float padding[2];
        DirectX::XMMATRIX ProjectionMatrix;
    };
    ConstantBuffer<PerFrame> m_CBufPerFrame;

    //Vars for projection change check
    float m_fFov = 0.0f;
    int m_iViewPortX = 0;
    int m_iViewPortY = 0;
};

