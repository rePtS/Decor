module;

#include <D3D11.h>
#include <limits>
#include <array>
#include <cassert>
#include <wrl\client.h>

#include <Engine.h>

export module GPU.DeviceState;

import Utils;

using Microsoft::WRL::ComPtr;

export class DeviceState
{
public:
    enum class RASTERIZER_STATE { DEFAULT, WIREFRAME, COUNT };
    enum class DEPTH_STENCIL_STATE { DEFAULT, NO_WRITE, COUNT };
    enum class BLEND_STATE { DEFAULT, MODULATE, TRANSLUCENT, TRANSLUCENT_FAKE_MULTIPASS, ALPHABLEND, INVIS, COUNT }; // TODO: for invis, just disable pixel shader
    enum class SAMPLER_STATE { LINEAR, POINT, COUNT };

    explicit DeviceState(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        : m_Device(Device)
        , m_DeviceContext(DeviceContext)
    {
        CreateRasterizerStates();
        CreateDepthStencilStates();
        CreateBlendStates();
        CreateSamplerStates();
    }

    DeviceState(const DeviceState&) = delete;
    DeviceState& operator= (const DeviceState&) = delete;

    bool Init()
    {
    }

    DEPTH_STENCIL_STATE GetDepthStencilStateForPolyFlags(const DWORD PolyFlags) const
    {
        DEPTH_STENCIL_STATE NewDepthStencilState = DEPTH_STENCIL_STATE::DEFAULT;
        if (PolyFlags & PF_NoOcclude && !(PolyFlags & PF_NoOcclude))
        {
            NewDepthStencilState = DEPTH_STENCIL_STATE::NO_WRITE;
        }
        return NewDepthStencilState;
    }

    bool IsDepthStencilStatePrepared(const DEPTH_STENCIL_STATE DepthStencilState) const
    {
        return m_PreparedDepthStencilState == DepthStencilState;
    }

    void PrepareDepthStencilState(const DEPTH_STENCIL_STATE DepthStencilState)
    {
        m_PreparedDepthStencilState = DepthStencilState;
    }

    BLEND_STATE GetBlendStateForPolyFlags(const DWORD PolyFlags) const
    {
        static const unsigned int BlendFlags = PF_Translucent | PF_Modulated | PF_Invisible /*| PF_AlphaBlend*/;

        BLEND_STATE NewBlendState = BLEND_STATE::DEFAULT;
        if (PolyFlags & BlendFlags)
        {
            // Order is important here, sometimes multiple flags are set
            if (PolyFlags & PF_Translucent)
            {
                NewBlendState = BLEND_STATE::TRANSLUCENT;
            }
            else if (PolyFlags & PF_Modulated)
            {
                NewBlendState = BLEND_STATE::MODULATE;
            }
            else if (PolyFlags & PF_Invisible)
            {
                NewBlendState = BLEND_STATE::INVIS;
            }
            //         else if (PolyFlags & PF_AlphaBlend)
            //         {
            //             NewBlendState = BLEND_STATE::ALPHABLEND;
            //         }
            else
            {
                assert(false);
            }
        }

        return NewBlendState;
    }

    bool IsBlendStatePrepared(const BLEND_STATE BlendState) const
    {
        return m_PreparedBlendState == BlendState;
    }

    void PrepareBlendState(const BLEND_STATE BlendState)
    {
        m_PreparedBlendState = BlendState;
    }
    
    /// <summary>
    /// Binds all sampler states to slots 0 - SAMPLER_STATE::COUNT -1;
    /// custom samplers should be bound to later slots.
    /// </summary>
    void BindSamplerStates() const
    {
        static_assert(sizeof(ComPtr<ID3D11SamplerState>) == sizeof(ID3D11SamplerState*), "Can't use ComPtr array as pointer array.");
        m_DeviceContext.PSSetSamplers(0, m_SamplerStates.size(), m_SamplerStates.data()->GetAddressOf());
    }

    void Bind() const
    {
        m_DeviceContext.OMSetBlendState(m_BlendStates[static_cast<size_t>(m_PreparedBlendState)].Get(), nullptr, 0xffffffff);
        m_DeviceContext.RSSetState(m_RasterizerStates[0].Get()); //todo
        m_DeviceContext.OMSetDepthStencilState(m_DepthStencilStates[static_cast<size_t>(m_PreparedDepthStencilState)].Get(), 0xffffffff);
    }

    void BindDefault() const
    {
        m_DeviceContext.OMSetBlendState(m_BlendStates[static_cast<size_t>(BLEND_STATE::DEFAULT)].Get(), nullptr, 0xffffffff);
        m_DeviceContext.RSSetState(m_RasterizerStates[static_cast<size_t>(RASTERIZER_STATE::DEFAULT)].Get()); //todo
        m_DeviceContext.OMSetDepthStencilState(m_DepthStencilStates[static_cast<size_t>(DEPTH_STENCIL_STATE::DEFAULT)].Get(), 0xffffffff);
    }

protected:
        
    /// <summary>
    /// Prototype for the various ID3D11Device::Create[Rasterizer/DepthStencil/Etc.]State() functions.
    /// </summary>
    template<class DescType, class StateType, const size_t Num, class Func>
    void CreateStates(const std::array<DescType, Num>& Descs, std::array<ComPtr<StateType>, Num>& States, const Func& CreationFunc)
    {
        for (size_t i = 0; i < Num; i++)
        {
            Utils::ThrowIfFailed(
                (m_Device.*CreationFunc)(&Descs[i], &States[i]),
                "Failed to create state (%Iu).", i
            );

            Utils::SetResourceName(States[i], "DeviceState");
        }
    }

    void CreateRasterizerStates()
    {
        std::array<D3D11_RASTERIZER_DESC, static_cast<size_t>(RASTERIZER_STATE::COUNT)> Descs;

        D3D11_RASTERIZER_DESC& RasDefault = Descs[static_cast<size_t>(RASTERIZER_STATE::DEFAULT)];
        RasDefault.FillMode = D3D11_FILL_MODE::D3D11_FILL_SOLID;
        RasDefault.CullMode = D3D11_CULL_MODE::D3D11_CULL_BACK;
        RasDefault.FrontCounterClockwise = FALSE;
        RasDefault.DepthBias = 0;
        RasDefault.DepthBiasClamp = 0.0f;
        RasDefault.SlopeScaledDepthBias = 0.0f;
        RasDefault.DepthClipEnable = TRUE;
        RasDefault.ScissorEnable = FALSE;
        RasDefault.MultisampleEnable = FALSE;
        RasDefault.AntialiasedLineEnable = FALSE;

        D3D11_RASTERIZER_DESC& RasWireframe = Descs[static_cast<size_t>(RASTERIZER_STATE::WIREFRAME)];
        RasWireframe = RasDefault;
        RasWireframe.FillMode = D3D11_FILL_MODE::D3D11_FILL_WIREFRAME;

        CreateStates(Descs, m_RasterizerStates, &ID3D11Device::CreateRasterizerState);
    }

    void CreateDepthStencilStates()
    {
        std::array<D3D11_DEPTH_STENCIL_DESC, static_cast<size_t>(DEPTH_STENCIL_STATE::COUNT)> Descs;

        D3D11_DEPTH_STENCIL_DESC& DepthDefault = Descs[static_cast<size_t>(DEPTH_STENCIL_STATE::DEFAULT)];
        DepthDefault.DepthEnable = TRUE;
        DepthDefault.DepthWriteMask = D3D11_DEPTH_WRITE_MASK::D3D11_DEPTH_WRITE_MASK_ALL;
        DepthDefault.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_GREATER_EQUAL; //As we use inverse z-buffer
        DepthDefault.StencilEnable = FALSE;
        DepthDefault.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        DepthDefault.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        DepthDefault.FrontFace.StencilFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_ALWAYS;
        DepthDefault.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP::D3D11_STENCIL_OP_KEEP;
        DepthDefault.FrontFace.StencilFailOp = D3D11_STENCIL_OP::D3D11_STENCIL_OP_KEEP;
        DepthDefault.FrontFace.StencilPassOp = D3D11_STENCIL_OP::D3D11_STENCIL_OP_KEEP;
        DepthDefault.BackFace.StencilFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_ALWAYS;
        DepthDefault.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP::D3D11_STENCIL_OP_KEEP;
        DepthDefault.BackFace.StencilFailOp = D3D11_STENCIL_OP::D3D11_STENCIL_OP_KEEP;
        DepthDefault.BackFace.StencilPassOp = D3D11_STENCIL_OP::D3D11_STENCIL_OP_KEEP;

        D3D11_DEPTH_STENCIL_DESC& DepthNoWrite = Descs[static_cast<size_t>(DEPTH_STENCIL_STATE::NO_WRITE)];
        DepthNoWrite = DepthDefault;
        DepthNoWrite.DepthWriteMask = D3D11_DEPTH_WRITE_MASK::D3D11_DEPTH_WRITE_MASK_ZERO;

        CreateStates(Descs, m_DepthStencilStates, &ID3D11Device::CreateDepthStencilState);
    }

    void CreateBlendStates()
    {
        std::array<D3D11_BLEND_DESC, static_cast<size_t>(BLEND_STATE::COUNT)> Descs;

        D3D11_BLEND_DESC& BlendDefault = Descs[static_cast<size_t>(BLEND_STATE::DEFAULT)];
        BlendDefault.AlphaToCoverageEnable = FALSE;
        BlendDefault.IndependentBlendEnable = FALSE;
        BlendDefault.RenderTarget[0].BlendEnable = FALSE;
        BlendDefault.RenderTarget[0].SrcBlend = D3D11_BLEND::D3D11_BLEND_ONE;
        BlendDefault.RenderTarget[0].DestBlend = D3D11_BLEND::D3D11_BLEND_ZERO;
        BlendDefault.RenderTarget[0].BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;
        BlendDefault.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND::D3D11_BLEND_ONE;
        BlendDefault.RenderTarget[0].DestBlendAlpha = D3D11_BLEND::D3D11_BLEND_ZERO;
        BlendDefault.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;
        BlendDefault.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE::D3D11_COLOR_WRITE_ENABLE_ALL;

        D3D11_BLEND_DESC& BlendModulate = Descs[static_cast<size_t>(BLEND_STATE::MODULATE)];
        BlendModulate = BlendDefault;
        BlendModulate.RenderTarget[0].BlendEnable = TRUE;
        BlendModulate.RenderTarget[0].SrcBlend = D3D11_BLEND::D3D11_BLEND_DEST_COLOR;
        BlendModulate.RenderTarget[0].DestBlend = D3D11_BLEND::D3D11_BLEND_SRC_COLOR;
        BlendModulate.RenderTarget[0].BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;

        D3D11_BLEND_DESC& BlendTranslucent = Descs[static_cast<size_t>(BLEND_STATE::TRANSLUCENT)];
        BlendTranslucent = BlendDefault;
        BlendTranslucent.RenderTarget[0].BlendEnable = TRUE;
        BlendTranslucent.RenderTarget[0].SrcBlend = D3D11_BLEND::D3D11_BLEND_ONE;
        BlendTranslucent.RenderTarget[0].DestBlend = D3D11_BLEND::D3D11_BLEND_INV_SRC_COLOR;
        BlendTranslucent.RenderTarget[0].BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;

        D3D11_BLEND_DESC& BlendTranslucentFakeMultipass = Descs[static_cast<size_t>(BLEND_STATE::TRANSLUCENT_FAKE_MULTIPASS)];
        BlendTranslucentFakeMultipass = BlendDefault;
        BlendTranslucentFakeMultipass.RenderTarget[0].BlendEnable = TRUE;
        BlendTranslucentFakeMultipass.RenderTarget[0].SrcBlend = D3D11_BLEND::D3D11_BLEND_ONE;
        BlendTranslucentFakeMultipass.RenderTarget[0].DestBlend = D3D11_BLEND::D3D11_BLEND_SRC1_COLOR;
        BlendTranslucentFakeMultipass.RenderTarget[0].BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;

        D3D11_BLEND_DESC& BlendAlphaBlend = Descs[static_cast<size_t>(BLEND_STATE::ALPHABLEND)];
        BlendAlphaBlend = BlendDefault;
        BlendAlphaBlend.RenderTarget[0].BlendEnable = TRUE;
        BlendAlphaBlend.RenderTarget[0].SrcBlend = D3D11_BLEND::D3D11_BLEND_SRC_ALPHA;
        BlendAlphaBlend.RenderTarget[0].DestBlend = D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA;
        BlendAlphaBlend.RenderTarget[0].BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;

        D3D11_BLEND_DESC& BlendInvis = Descs[static_cast<size_t>(BLEND_STATE::INVIS)];
        BlendInvis = BlendDefault;
        BlendInvis.RenderTarget[0].BlendEnable = FALSE;
        BlendInvis.RenderTarget[0].RenderTargetWriteMask = 0; //This is as fast as disabling the pixel shader

        CreateStates(Descs, m_BlendStates, &ID3D11Device::CreateBlendState);
    }

    void CreateSamplerStates()
    {
        std::array<D3D11_SAMPLER_DESC, static_cast<size_t>(SAMPLER_STATE::COUNT)> Descs;

        D3D11_SAMPLER_DESC& SamLinear = Descs[static_cast<size_t>(SAMPLER_STATE::LINEAR)];
        SamLinear.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        SamLinear.AddressU = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
        SamLinear.AddressV = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
        SamLinear.AddressW = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
        SamLinear.MipLODBias = 0.0f;
        SamLinear.MaxAnisotropy = 1;
        SamLinear.ComparisonFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_NEVER;
        SamLinear.BorderColor[0] = 0.0f;
        SamLinear.BorderColor[1] = 0.0f;
        SamLinear.BorderColor[2] = 0.0f;
        SamLinear.BorderColor[3] = 0.0f;
        SamLinear.MinLOD = std::numeric_limits<float>::lowest();
        SamLinear.MaxLOD = std::numeric_limits<float>::max();

        D3D11_SAMPLER_DESC& SamPoint = Descs[static_cast<size_t>(SAMPLER_STATE::POINT)];
        SamPoint = SamLinear;
        SamPoint.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_POINT;

        CreateStates(Descs, m_SamplerStates, &ID3D11Device::CreateSamplerState);
    }

    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    std::array<ComPtr<ID3D11RasterizerState>, static_cast<size_t>(RASTERIZER_STATE::COUNT)> m_RasterizerStates;
    RASTERIZER_STATE m_PreparedRasterizerState = RASTERIZER_STATE::DEFAULT;

    std::array<ComPtr<ID3D11DepthStencilState>, static_cast<size_t>(DEPTH_STENCIL_STATE::COUNT)> m_DepthStencilStates;
    DEPTH_STENCIL_STATE m_PreparedDepthStencilState = DEPTH_STENCIL_STATE::DEFAULT;

    std::array<ComPtr<ID3D11BlendState>, static_cast<size_t>(BLEND_STATE::COUNT)> m_BlendStates;
    BLEND_STATE m_PreparedBlendState = BLEND_STATE::DEFAULT;

    std::array<ComPtr<ID3D11SamplerState>, static_cast<size_t>(SAMPLER_STATE::COUNT)> m_SamplerStates;
};