module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cassert>

#include "SceneConstants.hlsli"
#include <Engine.h>
#include <UnRender.h>

#define UNUSED_COLOR DirectX::XMFLOAT4(1.f, 0.f, 1.f, 1.f)

export module Scene;

import Scene.IRenderingContext;
import Scene.GltfUtils;
import Scene.Utils;
import Scene.Log;
import Scene.Texture;
import Scene.Material;
import Scene.Primitive;
import Scene.Node;
import TinyGltf;

using DirectX::XMFLOAT4;
using DirectX::XMMATRIX;
using DirectX::XMVECTOR;

const std::vector<D3D11_INPUT_ELEMENT_DESC> sVertexLayoutDesc =
{
    D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

struct CbFrame
{
    XMMATRIX ViewMtrx;
    XMFLOAT4 CameraPos;
    XMMATRIX ProjectionMtrx;
};

struct CbScene
{
    XMFLOAT4 AmbientLightLuminance;
    XMFLOAT4 LightsData[LIGHTS_DATA_MAX_SIZE];

    // !!! Переделать этот метод
    size_t AddLight(const SceneLight& light)
    {
        size_t dataIndex = light.index;
        LightsData[dataIndex] = XMFLOAT4{ light.intensity.x, light.intensity.y, light.intensity.z, (float)light.type };

        if (light.type == SceneLight::LightType::POINT)
        {
            LightsData[dataIndex + 1] = XMFLOAT4{ light.position.x, light.position.y, light.position.z, light.range };
            //return counter + 2;
            LightsData[dataIndex + 2].w = -1.0f;
        }
        else if (light.type == SceneLight::LightType::DIRECT)
        {
            LightsData[dataIndex + 1] = light.direction;
            //return counter + 2;
            LightsData[dataIndex + 2].w = -1.0f;
        }
        else if (light.type == SceneLight::LightType::SPOT)
        {
            LightsData[dataIndex + 1] = XMFLOAT4{ light.position.x, light.position.y, light.position.z, light.range };
            LightsData[dataIndex + 2] = XMFLOAT4{ light.direction.x, light.direction.y, light.direction.z, light.outerSpotAngle };
            //return counter + 3;
            LightsData[dataIndex + 3].w = -1.0f;
        }

        return 0;
    }
};

struct CbScenePrimitive
{
    // Metallness
    XMFLOAT4 BaseColorFactor;
    XMFLOAT4 MetallicRoughnessFactor;

    // Specularity
    XMFLOAT4 DiffuseColorFactor;
    XMFLOAT4 SpecularFactor;

    // Both workflows
    float    NormalTexScale;
    float    OcclusionTexStrength;
    float    padding[2];  // padding to 16 bytes
    XMFLOAT4 EmissionFactor;
};

export class Scene
{
public:

    Scene(const std::wstring sceneFilePath) :
        mSceneFilePath(sceneFilePath)
    {
        mViewData.eye = DirectX::XMVectorSet(0.0f, 4.0f, 10.0f, 1.0f);
        mViewData.at = DirectX::XMVectorSet(0.0f, -0.2f, 0.0f, 1.0f);
        mViewData.up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    }

    // TODO: Scene(const std::string &sceneFilePath);
    virtual ~Scene()
    {
        Destroy();
        SceneLog::Flush();
    }

    virtual bool Init(IRenderingContext& ctx)
    {
        uint32_t wndWidth, wndHeight;
        if (!ctx.GetWindowSize(wndWidth, wndHeight))
            return false;

        if (wndWidth < 1200u)
            wndWidth = 1200u;
        if (wndHeight < 900u)
            wndHeight = 900u;

        auto& device = ctx.GetDevice();
        auto& deviceContext = ctx.GetDeviceContext();

        HRESULT hr = S_OK;

        // Vertex shader
        ID3DBlob* pVsBlob = nullptr;
        if (!ctx.CreateVertexShader((WCHAR*)L"Decor/Scene.hlsl", "VS", "vs_4_0", pVsBlob, mVertexShader))
            return false;

        // Input layout
        hr = device.CreateInputLayout(sVertexLayoutDesc.data(),
            (UINT)sVertexLayoutDesc.size(),
            pVsBlob->GetBufferPointer(),
            pVsBlob->GetBufferSize(),
            &mVertexLayout);
        pVsBlob->Release();
        if (FAILED(hr))
            return false;

        // Culling vertex shader
        if (!ctx.CreateVertexShader((WCHAR*)L"Decor/Culling.hlsl", "VSMain", "vs_4_0", pVsBlob, mVsCulling))
            return false;

        // Pixel shaders
        if (!ctx.CreatePixelShader((WCHAR*)L"Decor/Scene.hlsl", "PsPbrMetalness", "ps_4_0", mPsPbrMetalness))
            return false;
        if (!ctx.CreatePixelShader((WCHAR*)L"Decor/Scene.hlsl", "PsConstEmissive", "ps_4_0", mPsConstEmmisive))
            return false;

        // Culling pixel shader
        if (!ctx.CreatePixelShader((WCHAR*)L"Decor/Culling.hlsl", "PSMain", "ps_4_0", mPsCulling))
            return false;


        // Create constant buffers
        D3D11_BUFFER_DESC bd;
        ZeroMemory(&bd, sizeof(bd));
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = 0;
        bd.ByteWidth = sizeof(CbScene);
        hr = device.CreateBuffer(&bd, nullptr, &mCbScene);
        if (FAILED(hr))
            return false;
        bd.ByteWidth = sizeof(CbFrame);
        hr = device.CreateBuffer(&bd, nullptr, &mCbFrame);
        if (FAILED(hr))
            return hr;
        bd.ByteWidth = sizeof(CbSceneNode);
        hr = device.CreateBuffer(&bd, nullptr, &mCbSceneNode);
        if (FAILED(hr))
            return hr;
        bd.ByteWidth = sizeof(CbScenePrimitive);
        hr = device.CreateBuffer(&bd, nullptr, &mCbScenePrimitive);
        if (FAILED(hr))
            return hr;

        // Create sampler state
        D3D11_SAMPLER_DESC sampDesc;
        ZeroMemory(&sampDesc, sizeof(sampDesc));
        sampDesc.Filter = D3D11_FILTER_ANISOTROPIC; // D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device.CreateSamplerState(&sampDesc, &mSamplerLinear);
        if (FAILED(hr))
            return hr;

        // Load scene
        if (!Load(ctx))
            return false;

        // Create default material
        if (!mDefaultMaterial.CreatePbrMetalness(ctx, nullptr, XMFLOAT4(0.5f, 0.5f, 0.5f, 1.f), nullptr, 0.0f, 0.4f))
            return false;

        // Matrices
        mViewMtrx = DirectX::XMMatrixLookAtLH(mViewData.eye, mViewData.at, mViewData.up);
        mProjectionMtrx = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4,
            (FLOAT)wndWidth / wndHeight,
            0.01f, 100.0f);

        // Scene constant buffer
        CbScene cbScene;
        cbScene.AmbientLightLuminance = mAmbientLight.luminance;
        for (auto& light : mLights)
            cbScene.AddLight(light);
        deviceContext.UpdateSubresource(mCbScene, 0, nullptr, &cbScene, 0, 0);

        return true;
    }

    virtual void Destroy()
    {
        SceneUtils::ReleaseAndMakeNull(mVsCulling);
        SceneUtils::ReleaseAndMakeNull(mPsCulling);
        
        SceneUtils::ReleaseAndMakeNull(mVertexShader);
        
        SceneUtils::ReleaseAndMakeNull(mPsPbrMetalness);
        SceneUtils::ReleaseAndMakeNull(mPsConstEmmisive);
        
        SceneUtils::ReleaseAndMakeNull(mVertexLayout);
        
        SceneUtils::ReleaseAndMakeNull(mCbScene);
        SceneUtils::ReleaseAndMakeNull(mCbFrame);
        SceneUtils::ReleaseAndMakeNull(mCbSceneNode);
        SceneUtils::ReleaseAndMakeNull(mCbScenePrimitive);
        
        SceneUtils::ReleaseAndMakeNull(mSamplerLinear);

        mRootNodes.clear();
    }

    virtual void AnimateFrame(IRenderingContext& ctx)
    {
        // debug: Materials
        for (auto& material : mMaterials)
            material.Animate(ctx);

        // Scene geometry
        for (auto& node : mRootNodes)
            node.Animate(ctx);
    }

    virtual void RenderFrame(IRenderingContext& ctx)
    {
        assert(&ctx);
        auto& deviceContext = ctx.GetDeviceContext();

        //// Frame constant buffer
        //CbFrame cbFrame;
        //cbFrame.ViewMtrx = XMMatrixTranspose(mViewMtrx);
        //XMStoreFloat4(&cbFrame.CameraPos, mViewData.eye);
        //cbFrame.ProjectionMtrx = XMMatrixTranspose(mProjectionMtrx);
        //deviceContext.UpdateSubresource(mCbFrame, 0, nullptr, &cbFrame, 0, 0);

        // Setup vertex shader
        deviceContext.VSSetShader(mVertexShader, nullptr, 0);
        deviceContext.VSSetConstantBuffers(0, 1, &mCbScene);
        deviceContext.VSSetConstantBuffers(1, 1, &mCbFrame);
        deviceContext.VSSetConstantBuffers(2, 1, &mCbSceneNode);

        // Setup geometry shader
        deviceContext.GSSetShader(nullptr, nullptr, 0);

        // Setup pixel shader data (shader itself is chosen later for each material)
        deviceContext.PSSetConstantBuffers(0, 1, &mCbScene);
        deviceContext.PSSetConstantBuffers(1, 1, &mCbFrame);
        deviceContext.PSSetConstantBuffers(2, 1, &mCbSceneNode);
        deviceContext.PSSetConstantBuffers(3, 1, &mCbScenePrimitive);
        deviceContext.PSSetSamplers(0, 1, &mSamplerLinear);

        // Scene geometry
        for (auto &node : mRootNodes)
            RenderRootNode(ctx, node);
    }

    virtual bool GetAmbientColor(float(&rgba)[4])
    {
        rgba[0] = mAmbientLight.luminance.x;
        rgba[1] = mAmbientLight.luminance.y;
        rgba[2] = mAmbientLight.luminance.z;
        rgba[3] = mAmbientLight.luminance.w;
        return true;
    }

    void SetCamera(IRenderingContext& ctx, const FSceneNode& SceneNode)
    {
        auto& deviceContext = ctx.GetDeviceContext();
        assert(&deviceContext);

        //Create projection matrix with swapped near/far for better accuracy
        static const float fZNear = 32760.0f;
        static const float fZFar = 1.0f;

        const float fAspect = SceneNode.FX / SceneNode.FY;
        const float fFovVert = SceneNode.Viewport->Actor->FovAngle / fAspect * static_cast<float>(PI) / 180.0f;

        const auto& cameraPosition = SceneNode.Coords.Origin;
        const auto& cameraToVector = SceneNode.Viewport->Actor->ViewRotation.Vector();
        const auto& cameraUpVector = SceneNode.Coords.YAxis;

        mViewData.eye = DirectX::XMVectorSet(cameraPosition.X, cameraPosition.Z, cameraPosition.Y, 1.0f);
        mViewData.at = DirectX::XMVectorSet(cameraToVector.X, cameraToVector.Z, cameraToVector.Y, 1.0f);
        mViewData.up = DirectX::XMVectorSet(cameraUpVector.X, cameraUpVector.Z, cameraUpVector.Y, 1.0f);

        // Matrices
        mViewMtrx = DirectX::XMMatrixLookToLH(mViewData.eye, mViewData.at, mViewData.up);
        mProjectionMtrx = DirectX::XMMatrixPerspectiveFovLH(fFovVert, fAspect, fZNear, fZFar);
        mProjectionMtrx.r[1].m128_f32[1] *= -1.0f; //Flip Y

        // !!! Может это лучше делать в RenderFrame?
        // Frame constant buffer can be updated now
        CbFrame cbFrame;
        cbFrame.ViewMtrx = XMMatrixTranspose(mViewMtrx);
        XMStoreFloat4(&cbFrame.CameraPos, mViewData.eye);
        cbFrame.ProjectionMtrx = XMMatrixTranspose(mProjectionMtrx);
        deviceContext.UpdateSubresource(mCbFrame, 0, nullptr, &cbFrame, 0, 0);
    }

private:

    // Loads the scene specified via constructor
    bool Load(IRenderingContext& ctx)
    {
        if (!LoadExternal(ctx, mSceneFilePath))
            return false;

        //AddScaleToRoots(100.0);
        //AddScaleToRoots({ 1.0f, -1.0f, 1.0f });
        //AddTranslationToRoots({ 0., -40., 0. }); // -1000, 800, 0
        //AddRotationQuaternionToRoots({ 0.000, -1.000, 0.000, 0.000 }); // 180°y    
        //AddRotationQuaternionToRoots({ 0.000, 0.707, 0.000, 0.707 }); // 90°y

        return PostLoadSanityTest();
    }

    bool LoadExternal(IRenderingContext& ctx, const std::wstring& filePath)
    {
        const auto fileExt = SceneUtils::GetFilePathExt(filePath);
        if ((fileExt.compare(L"glb") == 0) ||
            (fileExt.compare(L"gltf") == 0))
        {
            return LoadGLTF(ctx, filePath);
        }
        else
        {
            SceneLog::Error(L"The scene file has an unsupported file format extension (%s)!", fileExt.c_str());
            return false;
        }
    }

    bool PostLoadSanityTest()
    {
        if (mLights.size() > LIGHTS_MAX_COUNT)
        {
            SceneLog::Error(L"Lights count (%d) exceeded maximum limit (%d)!",
                mLights.size(), LIGHTS_MAX_COUNT);
            return false;
        }

        // Geometry using normal map must have tangent specified (for now)
        for (auto& node : mRootNodes)
            if (!NodeTangentSanityTest(node))
                return false;

        return true;
    }

    bool NodeTangentSanityTest(const SceneNode& node)
    {
        // Test node
        for (auto& primitive : node.mPrimitives)
        {
            auto& material = GetMaterial(primitive);

            if (material.GetNormalTexture().IsLoaded() && !primitive.IsTangentPresent())
            {
                SceneLog::Error(L"A scene primitive without tangent data uses a material with normal map!");
                return false;
            }
        }

        // Children
        for (auto& child : node.mChildren)
            if (!NodeTangentSanityTest(child))
                return false;

        return true;
    }

    // glTF loader
    bool LoadGLTF(IRenderingContext& ctx, const std::wstring& filePath)
    {
        using namespace std;

        SceneLog::Debug(L"");
        const std::wstring logPrefix = L"LoadGLTF: ";

        tinygltf::Model model;
        if (!GltfUtils::LoadModel(model, filePath))
            return false;

        SceneLog::Debug(L"");

        if (!LoadMaterialsFromGltf(ctx, model, logPrefix))
            return false;

        if (!LoadSceneFromGltf(ctx, model, logPrefix))
            return false;

        SetupDefaultLights();

        SceneLog::Debug(L"");

        return true;
    }

    bool LoadMaterialsFromGltf(IRenderingContext& ctx,
                               const tinygltf::Model& model,
                               const std::wstring& logPrefix)
    {
        const auto& materials = model.materials;

        SceneLog::Debug(L"%sMaterials: %d", logPrefix.c_str(), materials.size());

        const std::wstring materialLogPrefix = logPrefix + L"   ";
        const std::wstring valueLogPrefix = materialLogPrefix + L"   ";

        mMaterials.clear();
        mMaterials.reserve(materials.size());
        for (size_t matIdx = 0; matIdx < materials.size(); ++matIdx)
        {
            const auto& material = materials[matIdx];

            SceneLog::Debug(L"%s%d/%d \"%s\"",
                materialLogPrefix.c_str(),
                matIdx,
                materials.size(),
                SceneUtils::StringToWstring(material.name).c_str());

            SceneMaterial sceneMaterial;
            if (!sceneMaterial.LoadFromGltf(ctx, model, material, valueLogPrefix))
                return false;
            mMaterials.push_back(std::move(sceneMaterial));
        }

        return true;
    }

    bool LoadSceneFromGltf(IRenderingContext& ctx,
                           const tinygltf::Model& model,
                           const std::wstring& logPrefix)

    {
        // Choose one scene
        if (model.scenes.size() < 1)
        {
            SceneLog::Error(L"%sNo scenes present in the model!", logPrefix.c_str());
            return false;
        }
        if (model.scenes.size() > 1)
            SceneLog::Warning(L"%sMore scenes present in the model. Loading just the first one.", logPrefix.c_str());
        const auto& scene = model.scenes[0];

        SceneLog::Debug(L"%sScene 0 \"%s\": %d root node(s)",
            logPrefix.c_str(),
            SceneUtils::StringToWstring(scene.name).c_str(),
            scene.nodes.size());

        // Light sources
        // Нужна коллекция для всех статических источников света (и точечных, и прожекторов и солнечный свет)
        // Резервируем место для источников, заполняем их основные характеристики (цвет, интенсивность, радиус, направление и т.п.)
        // При проходе по иерархии узлов заполняем положение источника света в пространстве
        mLights.clear();
        mLights.reserve(model.lights.size());
        for (const auto& gltfLight : model.lights)
        {
            SceneLight sceneLight;
            if (!LoadLightFromGLTF(sceneLight, gltfLight, logPrefix + L"   "))
                return false;
            mLights.push_back(std::move(sceneLight));
        }

        // Проставим индексы источников света для общего массива данных об источниках света
        size_t indexInTotalLightDataArray = 0;
        for (auto& sceneLight : mLights)
        {
            sceneLight.index = indexInTotalLightDataArray;
            switch (sceneLight.type)
            {
            case SceneLight::LightType::DIRECT:
            case SceneLight::LightType::POINT:
                indexInTotalLightDataArray += 2; break;
            case SceneLight::LightType::SPOT:
                indexInTotalLightDataArray += 3; break;
            };
        }

        // Nodes hierarchy
        mRootNodes.clear();
        mRootNodes.reserve(scene.nodes.size());
        for (const auto nodeIdx : scene.nodes)
        {
            SceneNode sceneNode(true);
            if (!LoadSceneNodeFromGLTF(ctx, sceneNode, model, nodeIdx, logPrefix + L"   "))
                return false;
            mRootNodes.push_back(std::move(sceneNode));
        }

        // Проставляем каждому узлу списки источников света, которые освещают данный узел
        for (auto& rootNode : mRootNodes)
            rootNode.LinkLights(mLights);

        return true;
    }

    bool LoadSceneNodeFromGLTF(IRenderingContext& ctx,
                               SceneNode& sceneNode,
                               const tinygltf::Model& model,
                               int nodeIdx,
                               const std::wstring& logPrefix)
    {
        if (nodeIdx >= model.nodes.size())
        {
            SceneLog::Error(L"%sInvalid node index (%d/%d)!", logPrefix.c_str(), nodeIdx, model.nodes.size());
            return false;
        }

        const auto& node = model.nodes[nodeIdx];

        // Если это корневой узел, то проверяем имя узла (оно должно начинаться с "Node")    
        if (sceneNode.mIsRootNode)
        {
            auto nodeNameParts = SceneUtils::split(node.name, '_');
            if (nodeNameParts.size() > 1 && nodeNameParts[0] == "Node")
                // Сохрянем имя корневого узла (или лучше сохраним список ид источников света, которые освещаются данным узлом???)
                sceneNode.mName = nodeNameParts[1];
        }

        // Node itself
        if (!sceneNode.LoadFromGLTF(ctx, model, node, nodeIdx, logPrefix))
            return false;

        // Check if it has lights
        const auto& lightExtension = node.extensions.find("KHR_lights_punctual");
        if (lightExtension != std::end(node.extensions))
        {
            auto lightIdx = lightExtension->second.Get("light").GetNumberAsInt();
            // Set position of the light
            XMStoreFloat4(&mLights[lightIdx].position,
                XMVector3Transform(DirectX::XMVectorZero(), sceneNode.mLocalMtrx));
            // Set direction of the light
            XMStoreFloat4(&mLights[lightIdx].direction, DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));

            auto lightNameParts = SceneUtils::split(node.name, '_');
            if (lightNameParts.size() > 1)
                mLights[lightIdx].affectedNodes.assign(++lightNameParts.begin(), lightNameParts.end());
        }

        // Children
        sceneNode.mChildren.clear();
        sceneNode.mChildren.reserve(node.children.size());
        const std::wstring& childLogPrefix = logPrefix + L"   ";
        for (const auto childIdx : node.children)
        {
            if ((childIdx < 0) || (childIdx >= model.nodes.size()))
            {
                SceneLog::Error(L"%sInvalid child node index (%d/%d)!", childLogPrefix.c_str(), childIdx, model.nodes.size());
                return false;
            }

            //Log::Debug(L"%sLoading child %d \"%s\"",
            //           childLogPrefix.c_str(),
            //           childIdx,
            //           Utils::StringToWstring(model.nodes[childIdx].name).c_str());

            SceneNode childNode;
            if (!LoadSceneNodeFromGLTF(ctx, childNode, model, childIdx, childLogPrefix))
                return false;
            sceneNode.mChildren.push_back(std::move(childNode));
        }

        return true;
    }

    bool LoadLightFromGLTF(SceneLight& sceneLight,
                           const tinygltf::Light& light,
                           const std::wstring& logPrefix)
    {
        float intencityFactor = light.intensity * 0.005f; //*16.0f;

        sceneLight.range = light.range;
        sceneLight.intensity.x = light.color[0] * intencityFactor;
        sceneLight.intensity.y = light.color[1] * intencityFactor;
        sceneLight.intensity.z = light.color[2] * intencityFactor;
        sceneLight.intensity.w = 1.0f;

        if (light.type == "spot")
        {
            sceneLight.type = SceneLight::LightType::SPOT;
            sceneLight.innerSpotAngle = light.spot.innerConeAngle;
            sceneLight.outerSpotAngle = light.spot.outerConeAngle;
        }
        else if (light.type == "point")
            sceneLight.type = SceneLight::LightType::POINT;
        else if (light.type == "directional")
            sceneLight.type = SceneLight::LightType::DIRECT;

        return true;
    }

    // Materials
    const SceneMaterial& GetMaterial(const ScenePrimitive& primitive) const
    {
        const int idx = primitive.GetMaterialIdx();

        if (idx >= 0 && idx < mMaterials.size())
            return mMaterials[idx];
        else
            return mDefaultMaterial;
    }

#define SRGB_TO_LINEAR_PRECISE

    float SrgbValueToLinear(uint8_t v)
    {
        const float f = v / 255.f;
#ifdef SRGB_TO_LINEAR_PRECISE
        if (f <= 0.04045f)
            return f / 12.92f;
        else
            return pow((f + 0.055f) / 1.055f, 2.4f);
#else
        return pow(f, 2.2f);
#endif
    }

    XMFLOAT4 SrgbColorToFloat(uint8_t r, uint8_t g, uint8_t b, float intensity)
    {
#ifdef CONVERT_SRGB_INPUT_TO_LINEAR
        return XMFLOAT4(SrgbValueToLinear(r) * intensity,
            SrgbValueToLinear(g) * intensity,
            SrgbValueToLinear(b) * intensity,
            1.0f);
#else
        return DirectX::XMFLOAT4((r / 255.f) * intensity,
            (g / 255.f) * intensity,
            (b / 255.f) * intensity,
            1.0f);
#endif
    };

    // Lights
    void SetupDefaultLights()
    {
        const float amb = 0.035f;
        mAmbientLight.luminance = SrgbColorToFloat(amb, amb, amb, 0.5f);
    }

    // Transformations
    void AddScaleToRoots(double scale)
    {
        for (auto& rootNode : mRootNodes)
            rootNode.AddScale(scale);
    }

    void AddScaleToRoots(const std::vector<double>& vec)
    {
        for (auto& rootNode : mRootNodes)
            rootNode.AddScale(vec);
    }

    void AddRotationQuaternionToRoots(const std::vector<double>& vec)
    {
        for (auto& rootNode : mRootNodes)
            rootNode.AddRotationQuaternion(vec);
    }

    void AddTranslationToRoots(const std::vector<double>& vec)
    {
        for (auto& rootNode : mRootNodes)
            rootNode.AddTranslation(vec);
    }

    void AddMatrixToRoots(const std::vector<double>& vec)
    {
        for (auto& rootNode : mRootNodes)
            rootNode.AddMatrix(vec);
    }

    void RenderNode(IRenderingContext& ctx,
                    CbSceneNode& cbSceneNode,
                    const SceneNode& node,
                    const XMMATRIX& parentWorldMtrx)
    {
        assert(&ctx);

        auto& deviceContext = ctx.GetDeviceContext();
        assert(&deviceContext);

        const auto worldMtrx = node.GetWorldMtrx() * parentWorldMtrx;

        if (node.mPrimitives.size() > 0) {
            // Per-node constant buffer
            cbSceneNode.WorldMtrx = XMMatrixTranspose(worldMtrx);
            cbSceneNode.MeshColor = { 0.f, 1.f, 0.f, 1.f, };
            deviceContext.UpdateSubresource(mCbSceneNode, 0, nullptr, &cbSceneNode, 0, 0);

            // Draw current node
            for (auto& primitive : node.mPrimitives)
            {
                auto& material = GetMaterial(primitive);

                deviceContext.PSSetShader(mPsPbrMetalness, nullptr, 0);
                deviceContext.PSSetShaderResources(0, 1, &material.GetBaseColorTexture().srv);
                deviceContext.PSSetShaderResources(1, 1, &material.GetMetallicRoughnessTexture().srv);
                deviceContext.PSSetShaderResources(4, 1, &material.GetNormalTexture().srv);
                deviceContext.PSSetShaderResources(5, 1, &material.GetOcclusionTexture().srv);
                deviceContext.PSSetShaderResources(6, 1, &material.GetEmissionTexture().srv);

                CbScenePrimitive cbScenePrimitive;
                cbScenePrimitive.BaseColorFactor = material.GetBaseColorFactor();
                cbScenePrimitive.MetallicRoughnessFactor = material.GetMetallicRoughnessFactor();
                cbScenePrimitive.DiffuseColorFactor = UNUSED_COLOR;
                cbScenePrimitive.SpecularFactor = UNUSED_COLOR;
                cbScenePrimitive.NormalTexScale = material.GetNormalTexture().GetScale();
                cbScenePrimitive.OcclusionTexStrength = material.GetOcclusionTexture().GetStrength();
                cbScenePrimitive.EmissionFactor = material.GetEmissionFactor();
                deviceContext.UpdateSubresource(mCbScenePrimitive, 0, nullptr, &cbScenePrimitive, 0, 0);

                primitive.DrawGeometry(ctx, mVertexLayout);
            }
        }

        // Children
        for (auto& child : node.mChildren)
            RenderNode(ctx, cbSceneNode, child, worldMtrx);
    }

    void RenderRootNode(IRenderingContext& ctx, const SceneNode& node)
    {
        assert(&ctx);

        auto& deviceContext = ctx.GetDeviceContext();
        assert(&deviceContext);

        // !!! Может здесь нужно добавить явный признак того, что узел является корневым мэшем, а не источников света?
        if (node.mPrimitives.size() > 0 /*&& (node.mName == "A" || node.mName == "C" || node.mName == "B")*/)
        {
            const auto worldMtrx = node.GetWorldMtrx();

            // Per-node constant buffer
            CbSceneNode cbSceneNode;
            cbSceneNode.WorldMtrx = XMMatrixTranspose(worldMtrx);
            cbSceneNode.MeshColor = { 0.f, 1.f, 0.f, 1.f, };

            // !!! Может вынести заполенение LightIds в CbRootSceneNode?
            for (size_t i = 0; i < node.mLightIds.size(); ++i)
                cbSceneNode.LightIds[i] = node.mLightIds[i];
            cbSceneNode.Control.x = node.mLightIds.size();

            deviceContext.UpdateSubresource(mCbSceneNode, 0, nullptr, &cbSceneNode, 0, 0);

            // Children
            for (auto& child : node.mChildren)
                RenderNode(ctx, cbSceneNode, child, worldMtrx);
        }
    }

    void CullRootNode(IRenderingContext& ctx, size_t rootNodeIndex) //const SceneNode& node);
    {
        assert(&ctx);

        auto& deviceContext = ctx.GetDeviceContext();
        assert(&deviceContext);

        auto& node = mRootNodes[rootNodeIndex];

        // !!! Может здесь нужно добавить явный признак того, что узел является корневым мэшем, а не источников света?
        if (node.mPrimitives.size() > 0)
        {
            const auto worldMtrx = node.GetWorldMtrx();

            // Per-node constant buffer
            CbSceneNode cbSceneNode;
            cbSceneNode.WorldMtrx = XMMatrixTranspose(worldMtrx);
            ////cbSceneNode.MeshColor = { 0.f, 1.f, 0.f, 1.f, };
            cbSceneNode.Control.x = rootNodeIndex;

            deviceContext.UpdateSubresource(mCbSceneNode, 0, nullptr, &cbSceneNode, 0, 0);

            // ...
            for (auto& primitive : node.mPrimitives)
            {
                primitive.DrawGeometry(ctx, mVertexLayout);
            }
        }
    }

private:

    std::wstring                mSceneFilePath;

    // Geometry
    std::vector<SceneNode>      mRootNodes;

    // Materials
    std::vector<SceneMaterial>  mMaterials;
    SceneMaterial               mDefaultMaterial;

    // Lights
    struct {
        XMFLOAT4 luminance = XMFLOAT4{ 0.f, 0.f, 0.f, 0.f }; // omnidirectional luminance: lm * sr-1 * m-2
    } mAmbientLight;

    std::vector<SceneLight>     mLights;

    // Camera
    struct {
        XMVECTOR eye;
        XMVECTOR at;
        XMVECTOR up;
    } mViewData;
    XMMATRIX   mViewMtrx;
    XMMATRIX   mProjectionMtrx;

    // Shaders
    ID3D11VertexShader* mVertexShader = nullptr;
    ID3D11PixelShader* mPsPbrMetalness = nullptr;
    ID3D11PixelShader* mPsConstEmmisive = nullptr;
    ID3D11InputLayout* mVertexLayout = nullptr;

    ID3D11VertexShader* mVsCulling = nullptr;
    ID3D11PixelShader* mPsCulling = nullptr;

    ID3D11Buffer* mCbScene = nullptr;
    ID3D11Buffer* mCbFrame = nullptr;
    ID3D11Buffer* mCbSceneNode = nullptr;
    ID3D11Buffer* mCbScenePrimitive = nullptr;

    ID3D11SamplerState* mSamplerLinear = nullptr;
};