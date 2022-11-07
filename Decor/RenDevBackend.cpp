#include "RendevBackend.h"
#include "Helpers.h"
#include "vmthook.h"
#include <D3DCompiler.inl>
#include <cassert>
#include <winuser.h>

namespace DecorHook
{
    static const std::size_t PE_INDEX = 34;

    typedef void(__stdcall* ProcessEvent) (UFunction*, void*, void*); //typedef for the processevent function pointer

    VMTHook* hook = nullptr; //Pointer to the VMTHook class
    ProcessEvent pProcessEvent = nullptr; //pointer to original processevent
    UFunction* pUFunc = nullptr; //pointers to processevent arguements
    void* pParms = nullptr;
    void* pResult = nullptr;
    UObject* pCallObject = nullptr;

    void __declspec(naked) ProcessEventHooked()
    {
        __asm mov pCallObject, ecx; //get caller from ecx register and save it in pCallObject

        __asm
        {
            push eax
            mov eax, dword ptr[esp + 0x8]
            mov pUFunc, eax
            mov eax, dword ptr[esp + 0xC]
            mov pParms, eax
            mov eax, dword ptr[esp + 0x10]
            mov pResult, eax
            pop eax
        } // Manually get the proper parameters for the function

        __asm pushad //Save registers on stack                  
        
        //Do stuff here!
        //MessageBox(NULL, L"!", L"Test", MB_OK | MB_ICONQUESTION); 

        __asm popad //restore registers from stack
        __asm
        {
            push pResult
            push pParms
            push pUFunc
            call pProcessEvent

            retn 0xC
        } //put parameters on stack and call the orginal function
    }

    void Attach(UViewport* const pViewport)
    {
        assert(hook == nullptr);
        hook = new VMTHook(pViewport); //hook object
        pProcessEvent = hook->GetOriginalFunction<ProcessEvent>(PE_INDEX); //save the orginal funtion in global variable
        hook->HookFunction(&ProcessEventHooked, PE_INDEX); //replace the orginal function with the hooked function
    }

    void Deattach()
    {
        if (hook)
        {
            delete hook;
            hook = nullptr;
        }
    }
}

RenDevBackend::RenDevBackend()
{

}

RenDevBackend::~RenDevBackend()
{
    if (m_Scene)
    {
        delete m_Scene;
        m_Scene = nullptr;
    }

    DecorHook::Deattach();
}

bool RenDevBackend::Init(const HWND hWnd)
{
    IDXGIAdapter1* const pSelectedAdapter = nullptr;
    const D3D_DRIVER_TYPE DriverType = D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_HARDWARE;

    UINT iFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#ifdef _DEBUG
    iFlags |= D3D11_CREATE_DEVICE_FLAG::D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL FeatureLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
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

    Decor::ThrowIfFailed(
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
    Decor::SetResourceName(m_pDeviceContext, "MainDeviceContext");
    Decor::SetResourceName(m_pSwapChain, "MainSwapChain");

    LOGMESSAGEF(L"Device created with Feature Level %x.", FeatureLevel);

    Decor::ThrowIfFailed(
        m_pDevice.As(&m_pDXGIDevice),
        "Failed to get DXGI device."
    );
    Decor::SetResourceName(m_pDevice, "MainDevice");

    ComPtr<IDXGIAdapter> pAdapter;
    Decor::ThrowIfFailed(
        m_pDXGIDevice->GetAdapter(&pAdapter), 
        "Failed to get DXGI adapter."
    );

    Decor::ThrowIfFailed(
        pAdapter.As(&m_pAdapter), 
        "Failed to cast DXGI adapter."
    );

    DXGI_ADAPTER_DESC1 AdapterDesc;
    Decor::ThrowIfFailed(
        m_pAdapter->GetDesc1(&AdapterDesc),
        "Failed to get adapter descriptor."
    );

    LOGMESSAGEF(L"Adapter: %s.", AdapterDesc.Description);

    m_State = RenderState::RENDER_DEFAULT;

    return true;
}

void RenDevBackend::SetRes(const unsigned int iX, const unsigned int iY)
{
    assert(m_pSwapChain);

    m_SwapChainDesc.BufferDesc.Width = iX;
    m_SwapChainDesc.BufferDesc.Height = iY;

    m_pBackBufferRTV = nullptr;
    m_pDepthStencilView = nullptr;
    
    Decor::ThrowIfFailed(
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

void RenDevBackend::CreateRenderTargetViews()
{
    assert(m_pSwapChain);
    assert(m_pDevice);

    //Backbuffer
    ComPtr<ID3D11Texture2D> pBackBufferTex;
    Decor::ThrowIfFailed(
        m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(pBackBufferTex.GetAddressOf())), 
        "Failed to get back buffer texture."
    );
    Decor::SetResourceName(pBackBufferTex, "BackBuffer");

    Decor::ThrowIfFailed(
        m_pDevice->CreateRenderTargetView(pBackBufferTex.Get(), nullptr, &m_pBackBufferRTV),
        "Failed to create RTV for back buffer texture."
    );
    Decor::SetResourceName(m_pBackBufferRTV, "BackBufferRTV");

    //Depth stencil
    D3D11_TEXTURE2D_DESC DepthTextureDesc;
    DepthTextureDesc.Width = m_SwapChainDesc.BufferDesc.Width;
    DepthTextureDesc.Height = m_SwapChainDesc.BufferDesc.Height;
    DepthTextureDesc.MipLevels = 1;
    DepthTextureDesc.ArraySize = 1;
    DepthTextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_D32_FLOAT;
    DepthTextureDesc.SampleDesc.Count = 1;
    DepthTextureDesc.SampleDesc.Quality = 0;
    DepthTextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
    DepthTextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_DEPTH_STENCIL;
    DepthTextureDesc.CPUAccessFlags = 0;
    DepthTextureDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> pDepthTexture;
    Decor::ThrowIfFailed(
        m_pDevice->CreateTexture2D(&DepthTextureDesc, nullptr, pDepthTexture.GetAddressOf()),
        "Failed to create depth-stencil texture."
    );
    Decor::SetResourceName(pDepthTexture, "DepthStencil");

    Decor::ThrowIfFailed(
        m_pDevice->CreateDepthStencilView(pDepthTexture.Get(), nullptr, m_pDepthStencilView.GetAddressOf()),
        "Failed to create depth-stencil view."
    );
    Decor::SetResourceName(m_pDepthStencilView, "DepthStencilView");

    //Set
    m_pDeviceContext->OMSetRenderTargets(1, m_pBackBufferRTV.GetAddressOf(), m_pDepthStencilView.Get());
}

void RenDevBackend::NewFrame()
{
    assert(m_pDeviceContext);
    assert(m_pBackBufferRTV);
    assert(m_pDepthStencilView);

    const float ClearColor[] = {0.0f, 0.5f, 0.0f, 0.0f};

    m_pDeviceContext->ClearRenderTargetView(m_pBackBufferRTV.Get(), ClearColor);
    m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH | D3D11_CLEAR_FLAG::D3D11_CLEAR_STENCIL, 0.0f, 0);
}

void RenDevBackend::DrawScene()
{
    if (m_Scene)
    {
        m_Scene->AnimateFrame(*this);
        m_Scene->RenderFrame(*this);
    }
}

void RenDevBackend::Present()
{
    assert(m_pSwapChain);
    m_pSwapChain->Present(0, 0);
}

void RenDevBackend::SetViewport(const FSceneNode& SceneNode)
{
    if (m_Scene)
    {
        m_Scene->SetCamera(*this, SceneNode);
    }

    if (m_Viewport.TopLeftX == static_cast<float>(SceneNode.XB) && m_Viewport.TopLeftY == static_cast<float>(SceneNode.YB) && m_Viewport.Width == SceneNode.FX && m_Viewport.Height == SceneNode.FY)
    {
        return;
    }

    m_Viewport.TopLeftX = static_cast<float>(SceneNode.XB);
    m_Viewport.TopLeftY = static_cast<float>(SceneNode.YB);    
    m_Viewport.Width = SceneNode.FX;
    m_Viewport.Height = SceneNode.FY;
    m_Viewport.MinDepth = 0.0;
    m_Viewport.MaxDepth = 1.0;

    m_pDeviceContext->RSSetViewports(1, &m_Viewport);
}


bool RenDevBackend::CompileShader(WCHAR* szFileName,
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

bool RenDevBackend::CreateVertexShader(WCHAR* szFileName,
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


bool RenDevBackend::CreatePixelShader(WCHAR* szFileName,
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

bool RenDevBackend::GetWindowSize(uint32_t& width,
    uint32_t& height) const
{
    if (!IsValid())
        return false;

    assert(m_pSwapChain);

    width = m_SwapChainDesc.BufferDesc.Width;
    height = m_SwapChainDesc.BufferDesc.Height;
    return true;
}

void RenDevBackend::AttachHook(UViewport* const pViewport)
{
    DecorHook::Attach(pViewport);
}

void RenDevBackend::PreLoadLevel()
{
    m_State = RenderState::PRE_LOAD_LEVEL;
}

void RenDevBackend::LoadLevel(const TCHAR* szLevelName)
{
    if (m_State == RenderState::PRE_LOAD_LEVEL)
    {
        m_State = RenderState::LOAD_LEVEL;

        //MessageBox(NULL, szLevelName, L"Test", MB_OK | MB_ICONQUESTION);

        // Выгрузка текущего уровня
        if (m_Scene)
        {
            delete m_Scene;
            m_Scene = nullptr;
        };
        
        // Получаем относительное имя gltf-файла с геометрией уровня
        wchar_t levelFileName[256];
        wsprintf(levelFileName, L"Decor/Scenes/%s.gltf", szLevelName);
        
        // Проверяем, есть ли файл на диске
        std::ifstream levelFile(levelFileName);
        if (levelFile.good())
        {
            // Load new scene
            m_Scene = new Scene(levelFileName);
            m_Scene->Init(*this);

            // Все загружено, можно продолжать
            m_State = RenderState::RENDER_LEVEL;
        }
        else
        {
            // Уровень загрузить не удалось, используем обычный рендеринг
            m_State = RenderState::RENDER_DEFAULT;
        }
    }
}

void RenDevBackend::ClearDepth()
{
    m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_FLAG::D3D11_CLEAR_DEPTH | D3D11_CLEAR_FLAG::D3D11_CLEAR_STENCIL, 0.0f, 0);
}