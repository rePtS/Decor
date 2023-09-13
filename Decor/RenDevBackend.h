#pragma once

#include "IRenderingContext.h"
#include "Scene.h"

#include <Engine.h>

#include <wrl\client.h>
using Microsoft::WRL::ComPtr;

class RenDevBackend : public IRenderingContext
{
public:
    
    explicit RenDevBackend();
    RenDevBackend(const RenDevBackend&) = delete;
    RenDevBackend& operator=(const RenDevBackend&) = delete;
    ~RenDevBackend();

    bool Init(const HWND hWnd);
    void SetRes(const unsigned int iX, const unsigned int iY);

    void NewFrame();
    void Present();
    void SetViewport(const FSceneNode& SceneNode);

    ID3D11Device& GetDevice(){ return *m_pDevice.Get(); }
    ID3D11DeviceContext& GetDeviceContext() { return *m_pDeviceContext.Get(); }

    bool CreateVertexShader(WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3DBlob*& pVsBlob,
        ID3D11VertexShader*& pVertexShader) const;

    bool CreatePixelShader(WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3D11PixelShader*& pPixelShader) const;

    bool GetWindowSize(uint32_t& width,
        uint32_t& height) const;

    void DrawScene();

    void LoadLevel(const TCHAR* szLevelName);
    bool IsSceneRenderingEnabled() { return m_Scene != nullptr; };

    void ClearDepth();

    void SetDefaultRenderTarget();
    void SetCullingRenderTarget();
    std::bitset<256> GetCulledRoots();

    /// <summary>
    /// Checks if we need to load a scene
    /// </summary>
    bool EnsureCurrentScene(int sceneIndex, const TCHAR* sceneName);

protected:
    void CreateRenderTargetViews();

    bool CompileShader(WCHAR* szFileName,
        LPCSTR szEntryPoint,
        LPCSTR szShaderModel,
        ID3DBlob** ppBlobOut) const;

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

    int m_CurrentSceneIndex;
    Scene *m_Scene = nullptr;    
};
