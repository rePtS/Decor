module;

#include <D3D11.h>
#include <string>
#include <fstream>

#include <Engine.h>
#include <UnRender.h>

export module DeusEx.SceneManager;

import Scene;
import Scene.IRenderingContext;

export class SceneManager
{
	int m_CurrentSceneIndex;
	Scene* m_Scene = nullptr;
    IRenderingContext& m_RenderingContext;

public:	
    SceneManager(IRenderingContext& ctx) :
        m_RenderingContext(ctx)
    {
    }

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
            m_Scene->CullFrame(m_RenderingContext);
            m_Scene->RenderFrame(m_RenderingContext);
        }
    }

    void LoadLevel(const TCHAR* szLevelName)
    {
        // Выгрузка текущего уровня
        if (m_Scene)
        {
            delete m_Scene;
            m_Scene = nullptr;
        }

        // Получаем относительное имя gltf-файла с геометрией уровня
        wchar_t levelFileName[256];
        wsprintf(levelFileName, L"Decor/Scenes/%s.gltf", szLevelName);

        // Проверяем, есть ли файл на диске
        std::ifstream levelFile(levelFileName);
        if (levelFile.good())
        {
            // Load new scene
            auto scene = new Scene(levelFileName);
            scene->Init(m_RenderingContext);

            // Все загружено, можно продолжать
            m_Scene = scene;
        }
        else
        {
            // Уровень загрузить не удалось, используем обычный рендеринг            
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

    void SetViewport(const FSceneNode& SceneNode)
    {
        if (m_Scene)
        {
            m_Scene->SetCamera(m_RenderingContext, SceneNode);
        }
    }
};