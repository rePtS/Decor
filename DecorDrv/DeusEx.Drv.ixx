module;

#include <memory>
#include <cassert>
#include <fstream>
#include <sstream>

#include <Engine.h>
#include <UnRender.h>

export module DeusEx.Drv;

import <simple_json.hpp>;
import GPU.DeviceState;
import GPU.RenDevBackend;
import DeusEx.TextureCache;
import DeusEx.OcclusionMapCache;
import DeusEx.Renderer.Tile;
import DeusEx.Renderer.Gouraud;
import DeusEx.Renderer.ComplexSurface;
import DeusEx.Renderer.Composite;
import DeusEx.SceneManager;
import GlobalShaderConstants;
import Utils;

using json::JSON;

class UDecorRenderDevice : public URenderDevice
{
#pragma warning(push, 1)
    DECLARE_CLASS(UDecorRenderDevice, URenderDevice, CLASS_Config)
#pragma warning(pop)

public:
    explicit UDecorRenderDevice() :
        m_SceneManager(m_Backend)
    {
        URenderDevice::SpanBased = 0;
        URenderDevice::FullscreenOnly = 0;
        URenderDevice::SupportsFogMaps = 1;
        URenderDevice::SupportsTC = 1;
        URenderDevice::SupportsDistanceFog = 0;
        URenderDevice::SupportsLazyTextures = 0;
        URenderDevice::Coronas = 1;
        URenderDevice::ShinySurfaces = 1;
        //URenderDevice::HighDetailActors = 1;
        URenderDevice::VolumetricLighting = 1;
    }

    UDecorRenderDevice(const UDecorRenderDevice&) = delete;
    UDecorRenderDevice& operator=(const UDecorRenderDevice&) = delete;

    void StaticConstructor()
    {
    }

protected:
    void Render()
    {
        // Check if something to render
        if (m_pTileRenderer->IsMapped() || m_pGouraudRenderer->IsMapped() || m_pComplexSurfaceRenderer->IsMapped())
        {
            m_pGlobalShaderConstants->Bind();
            //if (m_Backend.UseHdr)
                m_pDeviceState->BindSamplerStates(); // Need to bind samplers states every frame as we use post process tonemapping
            m_pDeviceState->Bind();
            m_pTextureCache->BindTextures();
            m_pOcclusionMapCache->BindMaps();

            if (m_pTileRenderer->IsMapped())
            {
                m_pTileRenderer->Unmap();
                m_pTileRenderer->Bind();
                m_pTileRenderer->Draw();
            }

            if (m_pGouraudRenderer->IsMapped())
            {
                m_pGouraudRenderer->Unmap();
                m_pGouraudRenderer->Bind();
                m_pGouraudRenderer->Draw();
            }

            if (m_pComplexSurfaceRenderer->IsMapped())
            {
                m_pComplexSurfaceRenderer->Unmap();
                m_pComplexSurfaceRenderer->Bind();
                m_pComplexSurfaceRenderer->Draw();
            }
        }
    }

    // Convenience function so don't need to pass Viewport->...; template to pass varargs
    template<class... Args>
    void PrintFunc(Args... args)
    {
        assert(Viewport);
        assert(Viewport->Canvas);
        Viewport->Canvas->WrappedPrintf(Viewport->Canvas->SmallFont, 0, args...);
    }

    void InitJsonSettings()
    {
        std::ifstream f("DecorDrv\\settings.json");
        if (f.good())
        {
            std::stringstream buffer;
            buffer << f.rdbuf();
            m_Settings = JSON::Load(buffer.str());
        }
    }

    RenDevBackend m_Backend;
    SceneManager m_SceneManager;
    std::unique_ptr<GlobalShaderConstants> m_pGlobalShaderConstants;
    std::unique_ptr<DeviceState> m_pDeviceState;
    std::unique_ptr<TileRenderer> m_pTileRenderer;
    std::unique_ptr<GouraudRenderer> m_pGouraudRenderer;
    std::unique_ptr<ComplexSurfaceRenderer> m_pComplexSurfaceRenderer;
    std::unique_ptr<CompositeRenderer> m_pCompositeRenderer;
    std::unique_ptr<TextureCache> m_pTextureCache;
    std::unique_ptr<OcclusionMapCache> m_pOcclusionMapCache;
    JSON m_Settings;

    bool m_bNoTilesDrawnYet;

    // From URenderDevice
public:
    virtual UBOOL Init(UViewport* const pInViewport, const INT iNewX, const INT iNewY, const INT iNewColorBytes, const UBOOL bFullscreen) override
    {
        assert(pInViewport);

#ifdef _DEBUG
        pInViewport->Exec(L"ShowLog");
#endif

        Utils::LogMessagef(L"Initializing Direct3D 11 Renderer.");

        try
        {
            if (!m_Backend.Init(static_cast<HWND>(pInViewport->GetWindow())))
            {
                Utils::LogWarningf(L"Failed to initialize Direct3D 11 Renderer.");
                return false;
            }

            URenderDevice::Viewport = pInViewport;
            if (!SetRes(iNewX, iNewY, iNewColorBytes, bFullscreen))
            {
                Utils::LogWarningf(L"Failed to set resolution during Init().");
                return false;
            }

            InitJsonSettings();

            auto& Device = m_Backend.GetDevice();
            auto& DeviceContext = m_Backend.GetDeviceContext();

            m_pGlobalShaderConstants = std::make_unique<GlobalShaderConstants>(Device, DeviceContext, m_Settings);
            m_pDeviceState = std::make_unique<DeviceState>(Device, DeviceContext);
            m_pTextureCache = std::make_unique<TextureCache>(Device, DeviceContext);
            m_pOcclusionMapCache = std::make_unique<OcclusionMapCache>(Device, DeviceContext, 3);
            m_pTileRenderer = std::make_unique<TileRenderer>(Device, DeviceContext);
            m_pGouraudRenderer = std::make_unique<GouraudRenderer>(Device, DeviceContext);
            m_pComplexSurfaceRenderer = std::make_unique<ComplexSurfaceRenderer>(Device, DeviceContext);
            m_pCompositeRenderer = std::make_unique<CompositeRenderer>(Device, DeviceContext);
        }
        catch (const Utils::ComException& ex)
        {
            Utils::LogWarningf(L"Exception: %s", ex.what());
            return false;
        }

        m_pDeviceState->BindSamplerStates();

        return true;
    }

    virtual UBOOL SetRes(const INT iNewX, const INT iNewY, const INT iNewColorBytes, const UBOOL bFullscreen) override
    {
        assert(URenderDevice::Viewport);

        // Without BLIT_HardwarePaint, game doesn't trigger us when resizing
        // Without BLIT_Direct3D renderer only ever gets one draw call, and SetRes() isn't called on window resize
        if (!URenderDevice::Viewport->ResizeViewport(EViewportBlitFlags::BLIT_HardwarePaint | EViewportBlitFlags::BLIT_Direct3D, iNewX, iNewY, iNewColorBytes))
        {
            Utils::LogWarningf(L"Viewport resize failed (%d x %d).", iNewX, iNewY);
            return false;
        }

        try
        {
            m_Backend.SetRes(iNewX, iNewY);
        }
        catch (const Utils::ComException& ex)
        {
            Utils::LogWarningf(L"Exception: %s", ex.what());
            return false;
        }

        return true;
    }

    virtual void Exit() override
    {
    }

    virtual void Flush(const UBOOL bAllowPrecache) override
    {
    }

    virtual void Lock(const FPlane FlashScale, const FPlane FlashFog, const FPlane ScreenClear, const DWORD RenderLockFlags, BYTE* const pHitData, INT* const pHitSize) override
    {
        m_bNoTilesDrawnYet = true;

        m_Backend.NewFrame(); // В этом методе нужно установить текстуры рендер-бэкенда как цели рендеринга
        m_pTileRenderer->NewFrame();
        m_pGouraudRenderer->NewFrame();
        m_pComplexSurfaceRenderer->NewFrame();            

        DirectX::XMVECTOR flashColor = { 0.f, 0.f, 0.f, 0.f };
        if ((FVector)FlashScale != FVector(.5, .5, .5) || (FVector)FlashFog != FVector(0, 0, 0)) // From other renderers
            flashColor = { FlashFog.X, FlashFog.Y, FlashFog.Z, Min(FlashScale.X * 2.f,1.f) };
        m_pGlobalShaderConstants->NewFrame(flashColor);

        if (m_SceneManager.IsSceneRenderingEnabled())
        {
            m_pDeviceState->BindDefault();
            m_SceneManager.DrawScene();
        }
    }

    virtual void Unlock(const UBOOL bBlit) override
    {
        m_pGlobalShaderConstants->NewTick();

        Render();
            
        m_Backend.NewCompositeFrame(); // устанавливаем текстуры рендер-бэкенда как ресурсы для шейдера, а HDRTexture как цель рендеринга
        m_pCompositeRenderer->Bind(); // привязываем шейдер, вершинный буфер, индексный буфер
        m_pCompositeRenderer->Draw(); // рисуем однин прямоугольник на весь экран

        if (bBlit)
        {
            m_Backend.Present(); // здесь установим бэк-буфер как цель рендеринга и проведем цвето-коррекцию
        }
    }

    virtual void DrawComplexSurface(FSceneNode* const pFrame, FSurfaceInfo& Surface, FSurfaceFacet& Facet) override
    {
        // assert(m_bNoTilesDrawnYet); //Want to be sure that tiles are the last thing to be drawn

        const DWORD PolyFlags = Surface.PolyFlags;
        const auto& BlendState = m_pDeviceState->GetBlendStateForPolyFlags(PolyFlags);
        const auto& DepthStencilState = m_pDeviceState->GetDepthStencilStateForPolyFlags(PolyFlags);
        if (!m_pDeviceState->IsBlendStatePrepared(BlendState) || !m_pDeviceState->IsDepthStencilStatePrepared(DepthStencilState))
        {
            Render();
        }

        // 0x00000001 - needs texturing
        // 0x00000002 - needs lightmapping
        // 0x00000004 - use original UE1 rendering
        // 0x00000008 - poly is a water surface
        unsigned int TexFlags = 0;
            
        int maxINode = m_pGlobalShaderConstants->GetMaxINode();

        // Рисуем по-старому, если это второстепенное изображение (например, отражение),
        // либо если это MovableObject (у него значение iNode больше определенного)
        if (pFrame->Parent != nullptr || Facet.Polys->iNode > maxINode)
            TexFlags |= 0x00000004;

        const TextureConverter::TextureData* pTexDiffuse = nullptr;
        if (Surface.Texture)
        {
            if (!m_pTextureCache->IsPrepared(*Surface.Texture, 0))
            {
                Render();
            }
            pTexDiffuse = &m_pTextureCache->FindOrInsertAndPrepare(*Surface.Texture, 0, PolyFlags);
            TexFlags |= 0x00000001;
        }

        const TextureConverter::TextureData* pTexLight = nullptr;
        if (Surface.LightMap)
        {
            if (!m_pTextureCache->IsPrepared(*Surface.LightMap, 1))
            {
                Render();
            }
            pTexLight = &m_pTextureCache->FindOrInsertAndPrepare(*Surface.LightMap, 1, PolyFlags);
            TexFlags |= 0x00000002;
        }

        // TO-DO
        const auto& Poly = *Facet.Polys;
        const int surfId = pFrame->Level->Model->Nodes(Poly.iNode).iSurf;
        if (surfId >= 0)
        {
            const int mapId = pFrame->Level->Model->Surfs(surfId).iLightMap;
            if (mapId >= 0)
                m_pOcclusionMapCache->FindOrInsertAndPrepare(*pFrame->Level->Model, mapId);
        }

        auto waterFlag = Surface.Texture->bRealtime && (PolyFlags & PF_Portal /*&& PolyFlags & PF_Translucent*/);

        if (waterFlag)
            m_pComplexSurfaceRenderer->SetDrawMode(ComplexSurfaceRenderer::DM_Water);
        else if (PolyFlags & PF_Translucent)
            m_pComplexSurfaceRenderer->SetDrawMode(ComplexSurfaceRenderer::DM_Transparent);
        else
            m_pComplexSurfaceRenderer->SetDrawMode(ComplexSurfaceRenderer::DM_Solid);

        if (waterFlag)
            TexFlags |= 0x00000008;        

        if (pFrame->Parent == nullptr)
        {
            m_pGlobalShaderConstants->CheckViewChange(*pFrame, *Facet.Polys);
            m_pGlobalShaderConstants->SetComplexPoly(*pFrame, *Facet.Polys);
        }

        m_pDeviceState->PrepareDepthStencilState(DepthStencilState);
        m_pDeviceState->PrepareBlendState(BlendState);

        if (!m_pComplexSurfaceRenderer->IsMapped())
        {
            m_pComplexSurfaceRenderer->Map();
        }            

        // Code from OpenGL renderer to calculate texture coordinates
        const float UDot = Facet.MapCoords.XAxis | Facet.MapCoords.Origin;
        const float VDot = Facet.MapCoords.YAxis | Facet.MapCoords.Origin;

        // Draw each polygon
        for (const FSavedPoly* pPoly = Facet.Polys; pPoly; pPoly = pPoly->Next)
        {
            assert(pPoly);
            const FSavedPoly& Poly = *pPoly;
            if (Poly.NumPts < 3) // Skip invalid polygons
            {
                continue;
            }

            ComplexSurfaceRenderer::Vertex* const pVerts = m_pComplexSurfaceRenderer->GetTriangleFan(Poly.NumPts); // Reserve space and generate indices for fan		
            for (int i = 0; i < Poly.NumPts; i++)
            {
                ComplexSurfaceRenderer::Vertex& v = pVerts[i];

                // Code from OpenGL renderer to calculate texture coordinates
                const float U = Facet.MapCoords.XAxis | Poly.Pts[i]->Point;
                const float V = Facet.MapCoords.YAxis | Poly.Pts[i]->Point;
                const float UCoord = U - UDot;
                const float VCoord = V - VDot;

                // Diffuse texture coordinates
                v.TexCoords.x = (UCoord - Surface.Texture->Pan.X) * pTexDiffuse->fMultU;
                v.TexCoords.y = (VCoord - Surface.Texture->Pan.Y) * pTexDiffuse->fMultV;

                if (Surface.LightMap)
                {
                    // Lightmaps require pan correction of -.5
                    v.TexCoords1.x = (UCoord - (Surface.LightMap->Pan.X - 0.5f * Surface.LightMap->UScale)) * pTexLight->fMultU;
                    v.TexCoords1.y = (VCoord - (Surface.LightMap->Pan.Y - 0.5f * Surface.LightMap->VScale)) * pTexLight->fMultV;
                }
                //if (Surface.DetailTexture)
                //{
                //    v->TexCoord[2].x = (UCoord - Surface.DetailTexture->Pan.X)*detail->multU;
                //    v->TexCoord[2].y = (VCoord - Surface.DetailTexture->Pan.Y)*detail->multV;
                //}
                //if (Surface.FogMap)
                //{
                //    //Fogmaps require pan correction of -.5
                //    v->TexCoord[3].x = (UCoord - (Surface.FogMap->Pan.X - 0.5f*Surface.FogMap->UScale))*fogMap->multU;
                //    v->TexCoord[3].y = (VCoord - (Surface.FogMap->Pan.Y - 0.5f*Surface.FogMap->VScale))*fogMap->multV;
                //}
                //if (Surface.MacroTexture)
                //{
                //    v->TexCoord[4].x = (UCoord - Surface.MacroTexture->Pan.X)*macro->multU;
                //    v->TexCoord[4].y = (VCoord - Surface.MacroTexture->Pan.Y)*macro->multV;
                //}

                static_assert(sizeof(Poly.Pts[i]->Point) >= sizeof(v.Pos), "Point sizes differ, can't use reinterpret_cast");
                v.Pos = reinterpret_cast<decltype(v.Pos)&>(Poly.Pts[i]->Point);
                v.PolyFlags = PolyFlags;
                v.TexFlags = TexFlags;
            }
        }
    }

    virtual void DrawGouraudPolygon(FSceneNode* const pFrame, FTextureInfo& Info, FTransTexture** const ppPts, const int NumPts, const DWORD PolyFlags, FSpanBuffer* const pSpan) override
    {
        //assert(m_bNoTilesDrawnYet); //Want to be sure that tiles are the last thing to be drawn -> doesn't hold for gouraud

        if (NumPts < 3) // Degenerate triangle
        {
            return;
        }

        const auto& BlendState = m_pDeviceState->GetBlendStateForPolyFlags(PolyFlags);
        const auto& DepthStencilState = m_pDeviceState->GetDepthStencilStateForPolyFlags(PolyFlags);
        if (!m_pDeviceState->IsBlendStatePrepared(BlendState) || !m_pDeviceState->IsDepthStencilStatePrepared(DepthStencilState) || !m_pTextureCache->IsPrepared(Info, 0))
        {
            Render();
        }

        //auto waterFlag = Surface.Texture->bRealtime && (PolyFlags & PF_Portal /*&& PolyFlags & PF_Translucent*/);
        m_pGouraudRenderer->SetDrawMode(PolyFlags & PF_Translucent);

        m_pDeviceState->PrepareDepthStencilState(DepthStencilState);
        m_pDeviceState->PrepareBlendState(BlendState);
        const TextureConverter::TextureData& texDiffuse = m_pTextureCache->FindOrInsertAndPrepare(Info, 0, PolyFlags);

        if (!m_pGouraudRenderer->IsMapped())
        {
            m_pGouraudRenderer->Map();
        }

        GouraudRenderer::Vertex* const pVerts = m_pGouraudRenderer->GetTriangleFan(NumPts);
        for (int i = 0; i < NumPts; i++) // Set fan verts
        {
            GouraudRenderer::Vertex& v = pVerts[i];

            static_assert(sizeof(ppPts[i]->Point) >= sizeof(v.Pos), "Sizes differ, can't use reinterpret_cast");
            v.Pos = reinterpret_cast<decltype(v.Pos)&>(ppPts[i]->Point);

            static_assert(sizeof(ppPts[i]->Normal) >= sizeof(v.Normal), "Sizes differ, can't use reinterpret_cast");
            v.Normal = reinterpret_cast<decltype(v.Normal)&>(ppPts[i]->Normal);

            static_assert(sizeof(ppPts[i]->Light) >= sizeof(v.Color), "Sizes differ, can't use reinterpret_cast");
            v.Color = reinterpret_cast<decltype(v.Color)&>(ppPts[i]->Light);

            v.TexCoords.x = ppPts[i]->U * texDiffuse.fMultU;
            v.TexCoords.y = ppPts[i]->V * texDiffuse.fMultV;

            v.PolyFlags = PolyFlags;
        }
    }

    virtual void DrawTile(FSceneNode* const pFrame, FTextureInfo& Info, const FLOAT fX, const FLOAT fY, const FLOAT fXL, const FLOAT fYL, const FLOAT fU, const FLOAT fV, const FLOAT fUL, const FLOAT fVL, FSpanBuffer* const pSpan, const FLOAT fZ, const FPlane Color, const FPlane Fog, const DWORD PolyFlags) override
    {
        assert(m_pTileRenderer);
        assert(m_pTextureCache);
        assert(m_pDeviceState);
        m_bNoTilesDrawnYet = false;

        //SetSceneNode(pFrame); //Set scene node fix.

        const auto& BlendState = m_pDeviceState->GetBlendStateForPolyFlags(PolyFlags);

        // Flush state
        if (!m_pDeviceState->IsBlendStatePrepared(BlendState) || !m_pTextureCache->IsPrepared(Info, 0))
        {
            Render();
        }

        m_pDeviceState->PrepareBlendState(BlendState);
        const auto& Texture = m_pTextureCache->FindOrInsertAndPrepare(Info, 0, PolyFlags);

        if (!m_pTileRenderer->IsMapped())
        {
            m_pTileRenderer->Map();
        }

        TileRenderer::Tile& tile = m_pTileRenderer->GetTile();

        if (PolyFlags & PF_NoSmooth)
        {
            tile.XYPos.x = fX;
            tile.XYPos.y = fX + fXL;
            tile.XYPos.z = fY;
            tile.XYPos.w = fY + fYL;
        }
        else
        {
            float RFX2 = m_pGlobalShaderConstants->GetRFX2() * fZ;
            float RFY2 = m_pGlobalShaderConstants->GetRFY2() * fZ;

            tile.XYPos.x = (fX - pFrame->FX2) * RFX2;
            tile.XYPos.y = (fX + fXL - pFrame->FX2) * RFX2;
            tile.XYPos.z = (fY - pFrame->FY2) * RFY2;
            tile.XYPos.w = (fY + fYL - pFrame->FY2) * RFY2;
        }

        tile.TexCoord.x = fU * Texture.fMultU;
        tile.TexCoord.y = (fU + fUL) * Texture.fMultU;
        tile.TexCoord.z = fV * Texture.fMultV;
        tile.TexCoord.w = (fV + fVL) * Texture.fMultV;

        static_assert(sizeof(Color) >= sizeof(tile.Color), "Sizes differ, can't use reinterpret_cast");
        tile.Color = reinterpret_cast<const decltype(tile.Color)&>(Color);

        tile.PolyFlags = PolyFlags;

        tile.ZPos.x = fZ;
    }

    virtual void Draw2DLine(FSceneNode* const pFrame, const FPlane Color, const DWORD LineFlags, const FVector P1, const FVector P2) override
    {
    }

    virtual void Draw2DPoint(FSceneNode* const pFrame, const FPlane Color, const DWORD LineFlags, const FLOAT fX1, const FLOAT fY1, const FLOAT fX2, const FLOAT fY2, const FLOAT fZ) override
    {
    }

    virtual void ClearZ(FSceneNode* const pFrame) override
    {
        Render();
        m_Backend.ClearDepth();
    }

    virtual void PushHit(const BYTE* const pData, const INT iCount) override
    {
    }

    virtual void PopHit(const INT iCount, const UBOOL bForce) override
    {
    }

    /// <summary>
    /// Prints statistics
    /// </summary>
    /// <param name="pResult">is a 128 character string.</param>
    virtual void GetStats(TCHAR* const pResult) override
    {
        // Buffer is only 128 chars, so we do our own thing
        assert(Viewport);
        assert(Viewport->Canvas);

        PrintFunc(L"Tiles | Buffer fill: %Iu/%Iu. Draw calls: %Iu.", m_pTileRenderer->GetNumTiles(), m_pTileRenderer->GetMaxTiles(), m_pTileRenderer->GetNumDraws());
        PrintFunc(L"Gouraud | Buffer fill: %Iu/%Iu. Draw calls: %Iu.", m_pGouraudRenderer->GetNumIndices(), m_pGouraudRenderer->GetMaxIndices(), m_pGouraudRenderer->GetNumDraws());
        PrintFunc(L"Complex | Buffer fill: %Iu/%Iu. Draw calls: %Iu.", m_pComplexSurfaceRenderer->GetNumIndices(), m_pComplexSurfaceRenderer->GetMaxIndices(), m_pComplexSurfaceRenderer->GetNumDraws());
        PrintFunc(L"TexCache | Num: %Iu.", m_pTextureCache->GetNumTextures());

        m_pTextureCache->PrintSizeHistogram(*Viewport->Canvas);
    }

    virtual void ReadPixels(FColor* const pPixels) override
    {
    };
        
    /// <summary>
    /// This optional function can be used to set the frustum and viewport parameters per scene change instead of per drawXXXX() call.
    /// Standard Z parameters: near 1, far 32760.
    /// </summary>
    /// <param name="pFrame">Contains various information with which to build frustum and viewport.</param>
    virtual void SetSceneNode(FSceneNode* const pFrame) override
    {
        assert(pFrame);

        Render(); // need to draw everything that has not yet been drawn before changing the viewport, otherwise the objects will be drawn in the wrong place on the screen

        m_SceneManager.SetViewport(*pFrame);
        m_Backend.SetViewport(pFrame->FX, pFrame->FY, pFrame->XB, pFrame->YB);

        if (m_pGlobalShaderConstants->CheckLevelChange(*pFrame))
            m_pTextureCache->Flush(); // При смене уровня сбрасываем кэш текстур, иначе артефакты (на разных уровнях одинаковые текстуры используют разный Id?)
        m_pGlobalShaderConstants->CheckProjectionChange(*pFrame);

        auto levelIndex = pFrame->Level->GetOuter()->GetFName().GetIndex();
        auto levelPathName = pFrame->Level->GetOuter()->GetPathName();

        if (m_SceneManager.EnsureCurrentScene(levelIndex, levelPathName))
            m_pOcclusionMapCache->Flush();
    }

    virtual UBOOL Exec(const TCHAR* const Cmd, FOutputDevice& Ar = *GLog) override
    {
        //     OutputDebugString(Cmd);
        //     OutputDebugString(L"\n");
        if (wcscmp(Cmd, L"texsizehist") == 0)
        {
            assert(m_pTextureCache);
            //m_pTextureCache->PrintSizeHistogram();
        }

        return URenderDevice::Exec(Cmd, Ar);
    }
};

#pragma warning(push, 1)
IMPLEMENT_PACKAGE(DecorDrv);
IMPLEMENT_CLASS(UDecorRenderDevice);
#pragma warning(pop)