module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <wrl\client.h>
#include <cassert>

export module DeusEx.Renderer.Composite;

import GPU.ShaderCompiler;
import GPU.DynamicBuffer;

using Microsoft::WRL::ComPtr;

export class CompositeRenderer
{
    const size_t NumVerts = 4;
    const size_t NumIndices = 6;

public:
    struct Vertex
    {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT2 TexCoords;
    };

    explicit CompositeRenderer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        : m_Device(Device)
        , m_DeviceContext(DeviceContext)
        , m_VertexBuffer(Device, DeviceContext, NumVerts)
        , m_IndexBuffer(Device, DeviceContext, NumIndices)
    {
        ShaderCompiler Compiler(m_Device, L"DecorDrv\\Composite.hlsl");
        m_pVertexShader = Compiler.CompileVertexShader();

        const D3D11_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        m_pInputLayout = Compiler.CreateInputLayout(InputElementDescs, _countof(InputElementDescs));

        m_pPixelShader = Compiler.CompilePixelShader();

        // Сразу создаем буферы
        Map();
        
        auto pVerts = m_VertexBuffer.PushBack(NumVerts);

        float z = 0.0f;
        float screenSize = 1.0f;

        pVerts[0].Pos = DirectX::XMFLOAT3(-screenSize, -screenSize, z);
        pVerts[0].TexCoords = DirectX::XMFLOAT2(0.0f, 1.0f);

        pVerts[1].Pos = DirectX::XMFLOAT3(screenSize, screenSize, z);
        pVerts[1].TexCoords = DirectX::XMFLOAT2(1.0f, 0.0f);

        pVerts[2].Pos = DirectX::XMFLOAT3(-screenSize, screenSize, z);
        pVerts[2].TexCoords = DirectX::XMFLOAT2(0.0f, 0.0f);

        pVerts[3].Pos = DirectX::XMFLOAT3(screenSize, -screenSize, z);
        pVerts[3].TexCoords = DirectX::XMFLOAT2(1.0f, 1.0f);

        auto pIndices = m_IndexBuffer.PushBack(NumIndices);
        pIndices[0] = 0;
        pIndices[1] = 1;
        pIndices[2] = 2;
        pIndices[3] = 0;
        pIndices[4] = 3;
        pIndices[5] = 1;

        Unmap();
    }

    CompositeRenderer(const CompositeRenderer&) = delete;
    CompositeRenderer& operator=(const CompositeRenderer&) = delete;

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
    }

protected:
    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    ComPtr<ID3D11InputLayout> m_pInputLayout;
    ComPtr<ID3D11VertexShader> m_pVertexShader;
    ComPtr<ID3D11PixelShader> m_pPixelShader;
    ComPtr<ID3D11GeometryShader> m_pGeometryShader;

    DynamicGPUBuffer<Vertex, D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER> m_VertexBuffer;
    DynamicGPUBuffer<unsigned short, D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER> m_IndexBuffer;

    void Map() { m_VertexBuffer.Map(); m_IndexBuffer.Map(); }
    void Unmap() { m_VertexBuffer.Unmap(); m_IndexBuffer.Unmap(); }
    bool IsMapped() const { return m_VertexBuffer.IsMapped() || m_IndexBuffer.IsMapped(); }
};