module;

#include <D3D11.h>
#include <string>
#include <fstream>

#include <Engine.h>
#include <UnRender.h>

export module DeusEx.SceneManager;

import Scene;
import Scene.IRenderingContext;

/// <summary>
/// A class for managing the rendering of gltf models
/// in conjunction with the usual graphics of the game
/// </summary>
export class SceneManager
{
	int m_CurrentSceneIndex;
	Scene* m_Scene = nullptr;
    IRenderingContext& m_RenderingContext;

public:	
    SceneManager(IRenderingContext& ctx)
        : m_RenderingContext(ctx)
    { }

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    ~SceneManager()
    {
        if (m_Scene)
        {
            delete m_Scene;
            m_Scene = nullptr;
        }
    }

    void DrawScene()
    {
        if (m_Scene)
        {
            m_Scene->AnimateFrame(m_RenderingContext);
            m_Scene->RenderFrame(m_RenderingContext);
        }
    }

    /// <summary>
    /// Loads a scene with given name as gltf model
    /// </summary>
    void LoadLevel(const TCHAR* szLevelName)
    {
        // Unloading the current level
        if (m_Scene)
        {
            delete m_Scene;
            m_Scene = nullptr;
        }

        // Getting the relative name of the gltf file with the geometry of the level
        wchar_t levelFileName[256];
        wsprintf(levelFileName, L"DecorDrv/Scenes/%s.gltf", szLevelName);

        // Checking if there is a file on the disk
        std::ifstream levelFile(levelFileName);
        if (levelFile.good())
        {
            // Load new scene
            auto scene = new Scene(levelFileName);
            scene->Init(m_RenderingContext);

            // All is done, can continue
            m_Scene = scene;
        }
        else
        {
            // The level could not be loaded, use the usual rendering
        }
    }

    bool IsSceneRenderingEnabled()
    {
        return m_Scene != nullptr;
    }

    /// <summary>
    /// Checks if we need to load a scene
    /// </summary>
    bool EnsureCurrentScene(int sceneIndex, const TCHAR* sceneName)
    {
        if (m_CurrentSceneIndex != sceneIndex)
        {
            LoadLevel(sceneName);
            m_CurrentSceneIndex = sceneIndex;
            return true;
        }

        return false;
    }

    /// <summary>
    /// Sets viewport for gltf scene rendering according viewport information from the game
    /// </summary>
    void SetViewport(const FSceneNode& SceneNode)
    {
        if (m_Scene)
        {
            m_Scene->SetCamera(m_RenderingContext, SceneNode);
        }
    }
};