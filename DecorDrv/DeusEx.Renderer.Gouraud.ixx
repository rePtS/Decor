module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <wrl\client.h>
#include <cassert>

export module DeusEx.Renderer.Gouraud;

import GPU.ShaderCompiler;
import GPU.DynamicBuffer;

using Microsoft::WRL::ComPtr;
using DirectX::XMFLOAT3;
using DirectX::XMFLOAT2;

export class GouraudRenderer
{
public:
    struct Vertex
    {
        XMFLOAT3 Pos;
        XMFLOAT3 Normal;
        XMFLOAT3 Color;
        XMFLOAT2 TexCoords;
        unsigned int PolyFlags;
    };

    explicit GouraudRenderer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        : m_Device(Device)
        , m_DeviceContext(DeviceContext)
        , m_VertexBuffer(Device, DeviceContext, 4096)
        , m_IndexBuffer(Device, DeviceContext, DynamicGPUBufferHelpers::Fan2StripIndices(m_VertexBuffer.GetReserved()))
    {
        ShaderCompiler Compiler(m_Device, L"DecorDrv\\Gouraud.hlsl");
        m_pVertexShader = Compiler.CompileVertexShader();

        const D3D11_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"Normal", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"Color", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BlendIndices", 0, DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0} // TODO make 8 bits, if necessary at all -> can't, hlsl doesn't support 8 bit data type
        };

        m_pInputLayout = Compiler.CreateInputLayout(InputElementDescs, _countof(InputElementDescs));

        m_pPixelShader = Compiler.CompilePixelShader();
    }

    GouraudRenderer(const GouraudRenderer&) = delete;
    GouraudRenderer& operator=(const GouraudRenderer&) = delete;

    void NewFrame()
    {
        m_VertexBuffer.Clear();
        m_IndexBuffer.Clear();
        m_iNumDraws = 0;
    }

    void Map() { m_VertexBuffer.Map(); m_IndexBuffer.Map(); }
    void Unmap() { m_VertexBuffer.Unmap(); m_IndexBuffer.Unmap(); }
    bool IsMapped() const { return m_VertexBuffer.IsMapped() || m_IndexBuffer.IsMapped(); }

    void Bind()
    {
        assert(m_pInputLayout);
        assert(m_pVertexShader);
        assert(m_pPixelShader);

        m_DeviceContext.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        m_DeviceContext.IASetInputLayout(m_pInputLayout.Get());

        const UINT Strides[] = { sizeof(Vertex) };
        const UINT Offsets[] = { 0 };

        m_DeviceContext.IASetVertexBuffers(0, 1, m_VertexBuffer.GetAddressOf(), Strides, Offsets);
        m_DeviceContext.IASetIndexBuffer(m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_DeviceContext.VSSetShader(m_pVertexShader.Get(), nullptr, 0);
        m_DeviceContext.GSSetShader(nullptr, nullptr, 0);
        m_DeviceContext.PSSetShader(m_pPixelShader.Get(), nullptr, 0);
    }

    void Draw()
    {
        assert(!IsMapped());
        m_DeviceContext.DrawIndexed(m_IndexBuffer.GetNumNewElements(), m_IndexBuffer.GetFirstNewElementIndex(), 0);
        m_iNumDraws++;
    }

    Vertex* GetTriangleFan(const size_t iSize)
    {
        return DynamicGPUBufferHelpers::GetTriangleFan(m_VertexBuffer, m_IndexBuffer, iSize);
    }

    // Diagnostics
    size_t GetNumIndices() const { return m_IndexBuffer.GetSize(); }
    size_t GetNumDraws() const { return m_iNumDraws; }
    size_t GetMaxIndices() const { return m_IndexBuffer.GetReserved(); }

protected:
    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    ComPtr<ID3D11InputLayout> m_pInputLayout;
    ComPtr<ID3D11VertexShader> m_pVertexShader;
    ComPtr<ID3D11PixelShader> m_pPixelShader;

    DynamicGPUBuffer<Vertex, D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER> m_VertexBuffer;
    DynamicGPUBuffer<unsigned short, D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER> m_IndexBuffer;

    size_t m_iNumDraws = 0; // Number of draw calls this frame, for stats
};