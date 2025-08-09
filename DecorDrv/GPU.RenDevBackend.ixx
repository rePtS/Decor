module;

#include <D3D11.h>
#include <D3DCompiler.inl>
#include <wrl\client.h>
#include <cassert>
#include <fstream>
#include "PostProcess.h"

export module GPU.RenDevBackend;

import Utils;
import GPU.RenderTexture;

using Microsoft::WRL::ComPtr;

export class RenDevBackend
{    

public:

    explicit RenDevBackend()
    {
    }

    RenDevBackend(const RenDevBackend&) = delete;
    RenDevBackend& operator=(const RenDevBackend&) = delete;
    
    ~RenDevBackend()
    {
        m_pToneMapPostProcess.reset();
    }

    bool Init(const HWND hWnd)
    {
        IDXGIAdapter1* const pSelectedAdapter = nullptr;
        const D3D_DRIVER_TYPE DriverType = D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_HARDWARE;

        UINT iFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#ifdef _DEBUG
        iFlags |= D3D11_CREATE_DEVICE_FLAG::D3D11_CREATE_DEVICE_DEBUG;
#endif

        const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL FeatureLevel;

        m_SwapChainDesc.BufferCount = 1;
        m_SwapChainDesc.BufferDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
        m_SwapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
        m_SwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
        m_SwapChainDesc.BufferDesc.Height = 0;
        m_SwapChainDesc.BufferDesc.Width = 0;
        m_SwapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER::DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        m_SwapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING::DXGI_MODE_SCALING_UNSPECIFIED;
        m_SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        m_SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        m_SwapChainDesc.OutputWindow = hWnd;
        m_SwapChainDesc.Windowed = TRUE;
        m_SwapChainDesc.SampleDesc.Count = 1;
        m_SwapChainDesc.SampleDesc.Quality = 0;
        m_SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_DISCARD; //Todo: Win8 DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
        //Todo: waitable swap chain IDXGISwapChain2::GetFrameLatencyWaitableObject        

        Utils::ThrowIfFailed(
            D3D11CreateDeviceAndSwapChain(
                pSelectedAdapter,
                DriverType,
                NULL,
                iFlags,
                FeatureLevels,
                _countof(FeatureLevels),
                D3D11_SDK_VERSION,
                &m_SwapChainDesc,
                &m_pSwapChain,
                &m_pDevice,
                &FeatureLevel,
                &m_pDeviceContext
            ),
            "Failed to create device and / or swap chain."
        );
        Utils::SetResourceName(m_pDeviceContext, "MainDeviceContext");
        Utils::SetResourceName(m_pSwapChain, "MainSwapChain");

        Utils::LogMessagef(L"Device created with Feature Level %x.", FeatureLevel);        

        Utils::ThrowIfFailed(
            m_pDevice.As(&m_pDXGIDevice),
            "Failed to get DXGI device."
        );
        Utils::SetResourceName(m_pDevice, "MainDevice");

        ComPtr<IDXGIAdapter> pAdapter;
        Utils::ThrowIfFailed(
            m_pDXGIDevice->GetAdapter(&pAdapter),
            "Failed to get DXGI adapter."
        );

        Utils::ThrowIfFailed(
            pAdapter.As(&m_pAdapter),
            "Failed to cast DXGI adapter."
        );

        DXGI_ADAPTER_DESC1 AdapterDesc;
        Utils::ThrowIfFailed(
            m_pAdapter->GetDesc1(&AdapterDesc),
            "Failed to get adapter descriptor."
        );

        Utils::LogMessagef(L"Adapter: %s.", AdapterDesc.Description);

        ComPtr<IDXGIFactory> pFactory;
        Utils::ThrowIfFailed(
            m_pAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&pFactory),
            "Failed to get DXGI factory."
        );
        pFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_WINDOW_CHANGES); // Stop DXGI from interfering with the game

        m_pHDRTexture = std::make_unique<RenderTexture>(DXGI_FORMAT_R16G16B16A16_FLOAT);
        m_pHDRTexture->SetDevice(m_pDevice.Get());

        m_pToneMapPostProcess = std::make_unique<DirectX::ToneMapPostProcess>(m_pDevice.Get());
        m_pToneMapPostProcess->SetOperator(DirectX::ToneMapPostProcess::ACESFilmic);
        m_pToneMapPostProcess->SetTransferFunction(DirectX::ToneMapPostProcess::Linear);

        return true;
    }

    void SetRes(const unsigned int iX, const unsigned int iY, bool fullScreen)
    {
        assert(m_pSwapChain);

        m_SwapChainDesc.BufferDesc.Width = iX;
        m_SwapChainDesc.BufferDesc.Height = iY;

        m_pBackBufferRTV = nullptr;
        m_pDepthStencilView = nullptr;

        // Set fullscreen resolution
        if (fullScreen)
        {
            DXGI_MODE_DESC fullscreenMode = m_SwapChainDesc.BufferDesc;
            
            Utils::ThrowIfFailed(
                m_pSwapChain->SetFullscreenState(TRUE, NULL),
                "Failed to switch to full-screen."
            );

            Utils::ThrowIfFailed(
                m_pSwapChain->ResizeTarget(&fullscreenMode),
                "Failed to set full-screen resolution."
            );

            m_SwapChainDesc.BufferDesc = fullscreenMode;
        }

        Utils::ThrowIfFailed(
            m_pSwapChain->ResizeBuffers(
                m_SwapChainDesc.BufferCount,
                m_SwapChainDesc.BufferDesc.Width,
                m_SwapChainDesc.BufferDesc.Height,
                m_SwapChainDesc.BufferDesc.Format,
                m_SwapChainDesc.Flags
            ),
            "Failed to resize swap chain (%u x %u)", iX, iY
        );        

        CreateRenderTargetViews();

        m_pHDRTexture->SizeResources(iX, iY);
        m_pToneMapPostProcess->SetHDRSourceTexture(m_pHDRTexture->GetShaderResourceView());
    }

    void NewFrame()
    {
        assert(m_pDeviceContext);        
        assert(m_pDepthStencilView);                

        const float ClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

        // Set the hdr-texture as RenderTargetView            
        auto hdrRenderTarget = m_pHDRTexture->GetRenderTargetView();

        m_pDeviceContext->ClearRenderTargetView(hdrRenderTarget, ClearColor);
        m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH | D3D11_CLEAR_FLAG::D3D11_CLEAR_STENCIL, 0.0f, 0);
        m_pDeviceContext->OMSetRenderTargets(1, &hdrRenderTarget, m_pDepthStencilView.Get());        
    }    

    void Present()
    {
        assert(m_pSwapChain);
        assert(m_pBackBufferRTV);
        assert(m_pDeviceContext);

        // Set back buffer as RenderTargetView
        m_pDeviceContext->OMSetRenderTargets(1, m_pBackBufferRTV.GetAddressOf(), nullptr);
        m_pToneMapPostProcess->Process(m_pDeviceContext.Get());

        m_pSwapChain->Present(1, 0);
    }

    /// <summary>
    /// Sets current viewport according given width, height and top left position
    /// </summary>
    /// <param name="FX">Width</param>
    /// <param name="FY">Height</param>
    /// <param name="XB">Top Left X</param>
    /// <param name="YB">Top Left Y</param>
    void SetViewport(float FX, float FY, int XB, int YB) //(const FSceneNode& SceneNode)
    {
        if (m_Viewport.TopLeftX == static_cast<float>(XB) && m_Viewport.TopLeftY == static_cast<float>(YB) && m_Viewport.Width == FX && m_Viewport.Height == FY)
        {
            return;
        }

        m_Viewport.TopLeftX = static_cast<float>(XB);
        m_Viewport.TopLeftY = static_cast<float>(YB);
        m_Viewport.Width = FX;
        m_Viewport.Height = FY;
        m_Viewport.MinDepth = 0.0;
        m_Viewport.MaxDepth = 1.0;

        m_pDeviceContext->RSSetViewports(1, &m_Viewport);
    }

    ID3D11Device& GetDevice() { return *m_pDevice.Get(); }
    ID3D11DeviceContext& GetDeviceContext() { return *m_pDeviceContext.Get(); }   

    bool GetWindowSize(uint32_t& width, uint32_t& height) const
    {
        assert(m_pSwapChain);

        width = m_SwapChainDesc.BufferDesc.Width;
        height = m_SwapChainDesc.BufferDesc.Height;
        return true;
    }

    void ClearDepth()
    {
        m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH | D3D11_CLEAR_FLAG::D3D11_CLEAR_STENCIL, 0.0f, 0);
    }

protected:
    void CreateRenderTargetViews()
    {
        assert(m_pSwapChain);
        assert(m_pDevice);        

        // Backbuffer
        ComPtr<ID3D11Texture2D> pBackBufferTex;
        Utils::ThrowIfFailed(
            m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(pBackBufferTex.GetAddressOf())),
            "Failed to get back buffer texture."
        );
        Utils::SetResourceName(pBackBufferTex, "BackBuffer");

        Utils::ThrowIfFailed(
            m_pDevice->CreateRenderTargetView(pBackBufferTex.Get(), nullptr, &m_pBackBufferRTV),
            "Failed to create RTV for back buffer texture."
        );
        Utils::SetResourceName(m_pBackBufferRTV, "BackBufferRTV");

        // Depth stencil
        D3D11_TEXTURE2D_DESC depthTextureDesc;
        depthTextureDesc.Width = m_SwapChainDesc.BufferDesc.Width;
        depthTextureDesc.Height = m_SwapChainDesc.BufferDesc.Height;
        depthTextureDesc.MipLevels = 1;
        depthTextureDesc.ArraySize = 1;
        depthTextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_D32_FLOAT;
        depthTextureDesc.SampleDesc.Count = 1;
        depthTextureDesc.SampleDesc.Quality = 0;
        depthTextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
        depthTextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_DEPTH_STENCIL;
        depthTextureDesc.CPUAccessFlags = 0;
        depthTextureDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> pDepthTexture;
        Utils::ThrowIfFailed(
            m_pDevice->CreateTexture2D(&depthTextureDesc, nullptr, pDepthTexture.GetAddressOf()),
            "Failed to create depth-stencil texture."
        );
        Utils::SetResourceName(pDepthTexture, "DepthStencil");

        Utils::ThrowIfFailed(
            m_pDevice->CreateDepthStencilView(pDepthTexture.Get(), nullptr, m_pDepthStencilView.GetAddressOf()),
            "Failed to create depth-stencil view."
        );
        Utils::SetResourceName(m_pDepthStencilView, "DepthStencilView");
        
        m_pDeviceContext->OMSetRenderTargets(1, m_pBackBufferRTV.GetAddressOf(), m_pDepthStencilView.Get());
    }

    D3D11_VIEWPORT m_Viewport;

    ComPtr<IDXGIDevice1> m_pDXGIDevice;
    ComPtr<IDXGIAdapter1> m_pAdapter;
    ComPtr<ID3D11Device> m_pDevice;
    ComPtr<ID3D11DeviceContext> m_pDeviceContext;

    ComPtr<IDXGISwapChain> m_pSwapChain;
    ComPtr<ID3D11RenderTargetView> m_pBackBufferRTV;
    ComPtr<ID3D11DepthStencilView> m_pDepthStencilView;

    DXGI_SWAP_CHAIN_DESC m_SwapChainDesc;

    std::unique_ptr<RenderTexture> m_pHDRTexture;
    std::unique_ptr<DirectX::ToneMapPostProcess> m_pToneMapPostProcess;
};