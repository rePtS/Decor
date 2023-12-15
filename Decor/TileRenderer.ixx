module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <cassert>
#include <wrl\client.h>

export module TileRenderer;

import ShaderCompiler;
import DynamicGPUBuffer;

using Microsoft::WRL::ComPtr;

export class TileRenderer
{
public:
    struct Tile
    {
        DirectX::XMFLOAT4 XYPos;
        DirectX::XMFLOAT4 ZPos;
        DirectX::XMFLOAT4 TexCoord;
        DirectX::XMFLOAT3 Color;
        unsigned int PolyFlags;
    };

    explicit TileRenderer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        :m_Device(Device)
        , m_DeviceContext(DeviceContext)
        , m_InstanceBuffer(Device, DeviceContext, 4096)
    {
        ShaderCompiler Compiler(m_Device, L"Decor\\Tile.hlsl");
        m_pVertexShader = Compiler.CompileVertexShader();

        const D3D11_INPUT_ELEMENT_DESC InputElementDescs[] =
        {
            {"Position", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"Position", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TexCoord", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TexCoord", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"BlendIndices", 0, DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1}
        };

        m_pInputLayout = Compiler.CreateInputLayout(InputElementDescs, _countof(InputElementDescs));

        m_pPixelShader = Compiler.CompilePixelShader();
    }

    TileRenderer(const TileRenderer&) = delete;
    TileRenderer& operator=(const TileRenderer&) = delete;

    void NewFrame()
    {
        m_InstanceBuffer.Clear();
        m_iNumDraws = 0;
    }

    void Map() { m_InstanceBuffer.Map(); }
    void Unmap() { m_InstanceBuffer.Unmap(); }
    bool IsMapped() const { return m_InstanceBuffer.IsMapped(); }

    void Bind()
    {
        assert(m_pInputLayout);
        assert(m_pVertexShader);
        assert(m_pPixelShader);

        m_DeviceContext.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        m_DeviceContext.IASetInputLayout(m_pInputLayout.Get());

        const UINT Strides[] = { sizeof(Tile) };
        const UINT Offsets[] = { 0 };

        m_DeviceContext.IASetVertexBuffers(0, 1, m_InstanceBuffer.GetAddressOf(), Strides, Offsets);
        m_DeviceContext.VSSetShader(m_pVertexShader.Get(), nullptr, 0);
        m_DeviceContext.GSSetShader(nullptr, nullptr, 0);
        m_DeviceContext.PSSetShader(m_pPixelShader.Get(), nullptr, 0);
    }

    void Draw()
    {
        assert(!IsMapped());
        m_DeviceContext.DrawInstanced(4, m_InstanceBuffer.GetNumNewElements(), 0, m_InstanceBuffer.GetFirstNewElementIndex()); //Just draw 4 non-existent vertices per quad, we're only interested in SV_VertexID.
        m_iNumDraws++;
    }

    Tile& GetTile()
    {
        return m_InstanceBuffer.PushBack();
    }

    //Diagnostics
    size_t GetNumTiles() const { return m_InstanceBuffer.GetSize(); }
    size_t GetNumDraws() const { return m_iNumDraws; }
    size_t GetMaxTiles() const { return m_InstanceBuffer.GetReserved(); }

protected:
    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    ComPtr<ID3D11InputLayout> m_pInputLayout;
    ComPtr<ID3D11VertexShader> m_pVertexShader;
    ComPtr<ID3D11PixelShader> m_pPixelShader;

    DynamicGPUBuffer<Tile, D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER> m_InstanceBuffer;  //We only create a per-instance-data buffer, we don't use a vertex buffer as vertex positions are irrelevant

    size_t m_iNumDraws = 0; //Number of draw calls this frame, for stats
};