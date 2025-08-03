module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <wrl\client.h>
#include <cassert>

export module DeusEx.Renderer.ComplexSurface;

import GPU.ShaderCompiler;
import GPU.DynamicBuffer;

using Microsoft::WRL::ComPtr;

export class ComplexSurfaceRenderer
{
public:
    struct Vertex
    {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT2 TexCoords;
        DirectX::XMFLOAT2 TexCoords1;
        DirectX::XMFLOAT2 TexCoords2;
        unsigned int PolyFlags;
        unsigned int TexFlags;
    };

    enum DrawMode
    {
        DM_Solid = 0,
        DM_Water = 1,
        DM_Transparent = 2
    };

    explicit ComplexSurfaceRenderer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        : m_Device(Device)
        , m_DeviceContext(DeviceContext)
        , m_VertexBuffer(Device, DeviceContext, 4096)
        , m_IndexBuffer(Device, DeviceContext, DynamicGPUBufferHelpers::Fan2StripIndices(m_VertexBuffer.GetReserved()))
    {
        ShaderCompiler Compiler(m_Device, L"DecorDrv\\ComplexSurface.hlsl");
        m_pVertexShader = Compiler.CompileVertexShader();

        const D3D11_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            { "Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TexCoord", 1, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TexCoord", 2, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BlendIndices", 0, DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BlendIndices", 1, DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        m_pInputLayout = Compiler.CreateInputLayout(InputElementDescs, _countof(InputElementDescs));

        m_pGeometryShader = Compiler.CompileGeometryShader();
        m_pPixelShader = Compiler.CompilePixelShader();

        ShaderCompiler WaterPixelCompiler(m_Device, L"DecorDrv\\WaterSurface.hlsl");
        m_pWaterPixelShader = WaterPixelCompiler.CompilePixelShader();
    }

    ComplexSurfaceRenderer(const ComplexSurfaceRenderer&) = delete;
    ComplexSurfaceRenderer& operator=(const ComplexSurfaceRenderer&) = delete;

    void NewFrame()
    {
        m_VertexBuffer.Clear();
        m_IndexBuffer.Clear();
        m_iNumDraws = 0;
    }

    void Map() { m_VertexBuffer.Map(); m_IndexBuffer.Map(); }
    void Unmap() { m_VertexBuffer.Unmap(); m_IndexBuffer.Unmap(); }
    bool IsMapped() const { return m_VertexBuffer.IsMapped() || m_IndexBuffer.IsMapped(); }

    void SetDrawMode(DrawMode drawMode)
    {
        m_DrawMode = drawMode;
    }

    void Bind()
    {
        assert(m_pInputLayout);
        assert(m_pVertexShader);
        assert(m_pPixelShader);
        assert(m_pWaterPixelShader);

        m_DeviceContext.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        m_DeviceContext.IASetInputLayout(m_pInputLayout.Get());

        const UINT Strides[] = { sizeof(Vertex) };
        const UINT Offsets[] = { 0 };

        m_DeviceContext.IASetVertexBuffers(0, 1, m_VertexBuffer.GetAddressOf(), Strides, Offsets);
        m_DeviceContext.IASetIndexBuffer(m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_DeviceContext.VSSetShader(m_pVertexShader.Get(), nullptr, 0);
        m_DeviceContext.GSSetShader(m_pGeometryShader.Get(), nullptr, 0);

        switch (m_DrawMode)
        {        
            case ComplexSurfaceRenderer::DM_Water:
                m_DeviceContext.PSSetShader(m_pWaterPixelShader.Get(), nullptr, 0);
                break;
            default:
                m_DeviceContext.PSSetShader(m_pPixelShader.Get(), nullptr, 0);
                break;
        }
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
    };

    //Diagnostics
    size_t GetNumIndices() const { return m_IndexBuffer.GetSize(); }
    size_t GetNumDraws() const { return m_iNumDraws; }
    size_t GetMaxIndices() const { return m_IndexBuffer.GetReserved(); }

protected:
    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    ComPtr<ID3D11InputLayout> m_pInputLayout;
    ComPtr<ID3D11VertexShader> m_pVertexShader;
    ComPtr<ID3D11PixelShader> m_pPixelShader;
    ComPtr<ID3D11PixelShader> m_pWaterPixelShader;
    ComPtr<ID3D11GeometryShader> m_pGeometryShader;

    DynamicGPUBuffer<Vertex, D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER> m_VertexBuffer;
    DynamicGPUBuffer<unsigned short, D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER> m_IndexBuffer;

    size_t m_iNumDraws = 0; // Number of draw calls this frame, for stats
    DrawMode m_DrawMode = DrawMode::DM_Solid;
};