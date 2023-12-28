module;

#include <D3D11.h>
#include <D3DCompiler.inl>
#include <wrl\client.h>
#include <cassert>
#include <fstream>

export module GPU.RenDevBackend;

import Scene.IRenderingContext;
import Utils;

using Microsoft::WRL::ComPtr;

export class RenDevBackend : public IRenderingContext
{
public:

    explicit RenDevBackend()
    {
    }

    RenDevBackend(const RenDevBackend&) = delete;
    RenDevBackend& operator=(const RenDevBackend&) = delete;
    
    ~RenDevBackend()
    {
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
        m_SwapChainDesc.Flags = 0;
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

        return true;
    }

    void SetRes(const unsigned int iX, const unsigned int iY)
    {
        assert(m_pSwapChain);

        m_SwapChainDesc.BufferDesc.Width = iX;
        m_SwapChainDesc.BufferDesc.Height = iY;

        m_pBackBufferRTV = nullptr;
        m_pDepthStencilView = nullptr;
        m_pCullingRTV = nullptr;
        m_pCullingTexture = nullptr;
        m_pStageCullingTexture = nullptr;
        m_pCullingDepthStencilView = nullptr;

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
    }

    void NewFrame()
    {
        assert(m_pDeviceContext);
        assert(m_pBackBufferRTV);
        assert(m_pCullingRTV);
        assert(m_pDepthStencilView);
        assert(m_pCullingDepthStencilView);

        // "Цвет", которым очищается culling-буфер 
        // По идее имеет значение только компонента R. И в нее нужно записать такое значение,
        // которое будет интерпретироваться, как отсутствие объекта. Значение 0 не совсем подходит,
        // так как под этим индексом в буфер может быть записан какой-нибудь объект.
        // !!! Похоже, что придется индексы узлов передавать в шейдер со смещением, чтобы не спутать нулевой объект с отсутсвием объекта
        const float CullingClearColor[] = { 0, 0, 0, 0 };
        m_pDeviceContext->ClearRenderTargetView(m_pCullingRTV.Get(), CullingClearColor);
        m_pDeviceContext->ClearDepthStencilView(m_pCullingDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH | D3D11_CLEAR_FLAG::D3D11_CLEAR_STENCIL, 0.0f, 0);

        const float ClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_pDeviceContext->ClearRenderTargetView(m_pBackBufferRTV.Get(), ClearColor);
        m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH | D3D11_CLEAR_FLAG::D3D11_CLEAR_STENCIL, 0.0f, 0);
    }

    void Present()
    {
        assert(m_pSwapChain);
        m_pSwapChain->Present(0, 0);
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

    bool CreateVertexShader(WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3DBlob*& pVsBlob,
        ID3D11VertexShader*& pVertexShader) const
    {
        HRESULT hr = S_OK;

        if (!CompileShader(szFileName, szEntryPoint, szShaderModel, &pVsBlob))
        {
            //Log::Error(L"The FX file failed to compile.");
            return false;
        }

        hr = m_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(),
            pVsBlob->GetBufferSize(),
            nullptr,
            &pVertexShader);
        if (FAILED(hr))
        {
            //Log::Error(L"mDevice->CreateVertexShader failed.");
            pVsBlob->Release();
            return false;
        }

        return true;
    }

    bool CreatePixelShader(WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3D11PixelShader*& pPixelShader) const
    {
        HRESULT hr = S_OK;
        ID3DBlob* pPSBlob;

        if (!CompileShader(szFileName, szEntryPoint, szShaderModel, &pPSBlob))
        {
            //Log::Error(L"The FX file failed to compile.");
            return false;
        }

        hr = m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(),
            pPSBlob->GetBufferSize(),
            nullptr,
            &pPixelShader);
        pPSBlob->Release();
        if (FAILED(hr))
        {
            //Log::Error(L"mDevice->CreatePixelShader failed.");
            return false;
        }

        return true;
    }

    bool GetWindowSize(uint32_t& width, uint32_t& height) const
    {
        if (!IsValid())
            return false;

        assert(m_pSwapChain);

        width = m_SwapChainDesc.BufferDesc.Width;
        height = m_SwapChainDesc.BufferDesc.Height;
        return true;
    }

    void ClearDepth()
    {
        m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH | D3D11_CLEAR_FLAG::D3D11_CLEAR_STENCIL, 0.0f, 0);
    }

    void SetDefaultRenderTarget()
    {
        // Меняем RenderTarget на дефолтный (для ренедра графики, собственно)
        m_pDeviceContext->OMSetRenderTargets(1, m_pBackBufferRTV.GetAddressOf(), m_pDepthStencilView.Get());
    }

    void SetCullingRenderTarget()
    {
        m_pDeviceContext->OMSetRenderTargets(1, m_pCullingRTV.GetAddressOf(), m_pCullingDepthStencilView.Get());
        m_pDeviceContext->ClearDepthStencilView(m_pCullingDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH, 0.0f, 0);
    }

    std::bitset<256> GetCulledRoots()
    {
        // Выделяем чтение ресурса в отдельный метод
        // Метод будет возвращать bitset
        std::bitset<256> usedNodes;

        // Считаем, что до вызова этого метода конекст был уже переключен на отсечение графики,
        // и теперь нужно получить результаты этого отсечения (по идее это лучше вынести в отдельный метод!!!)
        D3D11_MAPPED_SUBRESOURCE mappedImgData;
        ZeroMemory(&mappedImgData, sizeof(D3D11_MAPPED_SUBRESOURCE));

        m_pDeviceContext->CopyResource(m_pStageCullingTexture.Get(), m_pCullingTexture.Get());
        m_pDeviceContext->Map(m_pStageCullingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedImgData);

        uint32_t* textureData = (uint32_t*)mappedImgData.pData;
        uint32_t nodeIndex = 0;

        for (size_t i = 0; i < m_CullingBufferSize; i += 1000)
        {
            nodeIndex = textureData[i];
            usedNodes[nodeIndex] = true;
        }

        //m_pDeviceContext->Unmap(m_pStageCullingTexture.Get(), 0);

        return usedNodes;
    }

protected:
    void CreateRenderTargetViews()
    {
        assert(m_pSwapChain);
        assert(m_pDevice);

        // Culling texture
        D3D11_TEXTURE2D_DESC cullingTextureDesc;
        ZeroMemory(&cullingTextureDesc, sizeof(cullingTextureDesc));
        cullingTextureDesc.Width = m_SwapChainDesc.BufferDesc.Width; //64; //m_SwapChainDesc.BufferDesc.Width / 16;
        cullingTextureDesc.Height = m_SwapChainDesc.BufferDesc.Height; //36; //m_SwapChainDesc.BufferDesc.Height / 16;
        cullingTextureDesc.MipLevels = 1;
        cullingTextureDesc.ArraySize = 1;
        cullingTextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R32_UINT;
        cullingTextureDesc.SampleDesc.Count = 1;
        cullingTextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
        cullingTextureDesc.CPUAccessFlags = 0;
        cullingTextureDesc.MiscFlags = 0;

        m_CullingBufferSize = cullingTextureDesc.Width * cullingTextureDesc.Height;

        // Culling staging texture
        cullingTextureDesc.BindFlags = 0;
        cullingTextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_STAGING;
        cullingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        Utils::ThrowIfFailed(
            m_pDevice->CreateTexture2D(&cullingTextureDesc, nullptr, m_pStageCullingTexture.GetAddressOf()),
            "Failed to create stage culling texture."
        );
        Utils::SetResourceName(m_pStageCullingTexture, "StageCullingTexture");

        // Culling buffer texture
        cullingTextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_RENDER_TARGET; //| D3D11_BIND_SHADER_RESOURCE;
        cullingTextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
        cullingTextureDesc.CPUAccessFlags = 0;

        Utils::ThrowIfFailed(
            m_pDevice->CreateTexture2D(&cullingTextureDesc, nullptr, m_pCullingTexture.GetAddressOf()),
            "Failed to create culling buffer texture."
        );
        Utils::SetResourceName(m_pCullingTexture, "CullingTexture");

        // Culling Render Target    
        D3D11_RENDER_TARGET_VIEW_DESC cullingRenderTargetViewDesc;
        cullingRenderTargetViewDesc.Format = cullingTextureDesc.Format;
        cullingRenderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        cullingRenderTargetViewDesc.Texture2D.MipSlice = 0;

        Utils::ThrowIfFailed(
            m_pDevice->CreateRenderTargetView(m_pCullingTexture.Get(), &cullingRenderTargetViewDesc, &m_pCullingRTV),
            "Failed to create RTV for back buffer texture."
        );
        Utils::SetResourceName(m_pCullingRTV, "CullingRTV");

        //Backbuffer
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

        // Culling Depth stencil
        D3D11_TEXTURE2D_DESC cullingDepthTextureDesc;
        cullingDepthTextureDesc.Width = cullingTextureDesc.Width;
        cullingDepthTextureDesc.Height = cullingTextureDesc.Height;
        cullingDepthTextureDesc.MipLevels = 1;
        cullingDepthTextureDesc.ArraySize = 1;
        cullingDepthTextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_D32_FLOAT;
        cullingDepthTextureDesc.SampleDesc.Count = 1;
        cullingDepthTextureDesc.SampleDesc.Quality = 0;
        cullingDepthTextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
        cullingDepthTextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_DEPTH_STENCIL;
        cullingDepthTextureDesc.CPUAccessFlags = 0;
        cullingDepthTextureDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> pCullingDepthTexture;
        Utils::ThrowIfFailed(
            m_pDevice->CreateTexture2D(&cullingDepthTextureDesc, nullptr, pCullingDepthTexture.GetAddressOf()),
            "Failed to create culling depth-stencil texture."
        );
        Utils::SetResourceName(pCullingDepthTexture, "CullingDepthStencil");

        Utils::ThrowIfFailed(
            m_pDevice->CreateDepthStencilView(pCullingDepthTexture.Get(), nullptr, m_pCullingDepthStencilView.GetAddressOf()),
            "Failed to create culling depth-stencil view."
        );
        Utils::SetResourceName(m_pCullingDepthStencilView, "CullingDepthStencilView");

        //Depth stencil
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

        SetDefaultRenderTarget();
    }

    bool CompileShader(WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3DBlob** ppBlobOut) const

    {
        HRESULT hr = S_OK;

        DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
        // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
        // Setting this flag improves the shader debugging experience, but still allows 
        // the shaders to be optimized and to run exactly the way they will run in 
        // the release configuration of this program.
        dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

        ID3DBlob* pErrorBlob;
        hr = D3DCompileFromFile(szFileName, nullptr, nullptr, szEntryPoint, szShaderModel,
            dwShaderFlags, 0, ppBlobOut, &pErrorBlob);
        if (FAILED(hr))
        {
            //if (pErrorBlob)
            //    Log::Error(L"CompileShader: D3DX11CompileFromFile failed: \n%S",
            //        (char*)pErrorBlob->GetBufferPointer());
            if (pErrorBlob)
                pErrorBlob->Release();
            return false;
        }

        //if (pErrorBlob)
        //    Log::Debug(L"CompileShader: D3DX11CompileFromFile: \n%S",
        //        (char*)pErrorBlob->GetBufferPointer());

        if (pErrorBlob)
            pErrorBlob->Release();

        return true;
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

    ComPtr<ID3D11Texture2D> m_pCullingTexture;
    ComPtr<ID3D11Texture2D> m_pStageCullingTexture;
    ComPtr<ID3D11RenderTargetView> m_pCullingRTV;
    ComPtr<ID3D11DepthStencilView> m_pCullingDepthStencilView;
    size_t m_CullingBufferSize;
};