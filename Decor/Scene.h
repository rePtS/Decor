#pragma once


#include "Constants.h"
//#include "IRenderingContext.h"
#include <fstream>

#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
//#include <tiny_gltf.h> // just the interfaces (no implementation)

#include <Engine.h>

import IRenderingContext;
import TinyGltf;

using namespace DirectX;

struct SceneVertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT4 Tangent; // w represents handedness of the tangent basis and is either 1 or -1
    XMFLOAT2 Tex;
};


class ScenePrimitive
{
public:

    ScenePrimitive();
    ScenePrimitive(const ScenePrimitive &);
    ScenePrimitive(ScenePrimitive &&);
    ~ScenePrimitive();

    ScenePrimitive& operator = (const ScenePrimitive&);
    ScenePrimitive& operator = (ScenePrimitive&&);

    bool LoadFromGLTF(IRenderingContext & ctx,
                      const tinygltf::Model &model,
                      const tinygltf::Mesh &mesh,
                      const int primitiveIdx,
                      const std::wstring &logPrefix);

    // Uses mikktspace tangent space calculator by Morten S. Mikkelsen.
    // Requires position, normal, and texture coordinates to be already loaded.
    bool CalculateTangentsIfNeeded(const std::wstring &logPrefix = std::wstring());

    size_t GetVerticesPerFace() const;
    size_t GetFacesCount() const;
    const size_t GetVertexIndex(const int face, const int vertex) const;
    const SceneVertex& GetVertex(const int face, const int vertex) const;
          SceneVertex& GetVertex(const int face, const int vertex);
    void GetPosition(float outpos[], const int face, const int vertex) const;
    void GetNormal(float outnormal[], const int face, const int vertex) const;
    void GetTextCoord(float outuv[], const int face, const int vertex) const;
    void SetTangent(const float tangent[], const float sign, const int face, const int vertex);

    bool IsTangentPresent() const { return mIsTangentPresent; }

    void DrawGeometry(IRenderingContext &ctx, ID3D11InputLayout *vertexLayout) const;

    void SetMaterialIdx(int idx) { mMaterialIdx = idx; };
    int GetMaterialIdx() const { return mMaterialIdx; };

    void Destroy();

private:

    bool LoadDataFromGLTF(const tinygltf::Model &model,
                              const tinygltf::Mesh &mesh,
                              const int primitiveIdx,
                              const std::wstring &logPrefix);

    void FillFaceStripsCacheIfNeeded() const;
    bool CreateDeviceBuffers(IRenderingContext &ctx);

    void DestroyGeomData();
    void DestroyDeviceBuffers();

private:

    // Geometry data
    std::vector<SceneVertex>    mVertices;
    std::vector<uint32_t>       mIndices;
    D3D11_PRIMITIVE_TOPOLOGY    mTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bool                        mIsTangentPresent = false;

    // Cached geometry data
    struct FaceStrip
    {
        size_t startIdx;
        size_t faceCount;
    };
    mutable bool                    mAreFaceStripsCached = false;
    mutable std::vector<FaceStrip>  mFaceStrips;
    mutable size_t                  mFaceStripsTotalCount = 0;

    // Device geometry data
    ID3D11Buffer*               mVertexBuffer = nullptr;
    ID3D11Buffer*               mIndexBuffer = nullptr;

    // Material
    int                         mMaterialIdx = -1;
};

struct AmbientLight
{
    XMFLOAT4 luminance = XMFLOAT4{ 0.f, 0.f, 0.f, 0.f }; // omnidirectional luminance: lm * sr-1 * m-2
};


struct SceneLight
{
    enum class LightType : uint32_t {
        DIRECT = 1,
        POINT = 2,
        SPOT = 3
    };

    LightType type;
    float range;
    float innerSpotAngle;
    float outerSpotAngle;
    XMFLOAT4 intensity;
    XMFLOAT4 position;
    XMFLOAT4 direction;

    std::vector<std::string> affectedNodes;
    size_t index;
};

class SceneNode
{
public:
    SceneNode(bool useDebugAnimation = false);

    ScenePrimitive* CreateEmptyPrimitive();

    void SetIdentity();
    void AddScale(double scale);
    void AddScale(const std::vector<double> &vec);
    void AddRotationQuaternion(const std::vector<double> &vec);
    void AddTranslation(const std::vector<double> &vec);
    void AddMatrix(const std::vector<double> &vec);

    bool LoadFromGLTF(IRenderingContext & ctx,
                      const tinygltf::Model &model,
                      const tinygltf::Node &node,
                      int nodeIdx,
                      const std::wstring &logPrefix);

    void Animate(IRenderingContext &ctx);

    XMMATRIX GetWorldMtrx() const { return mWorldMtrx; }

    void LinkLights(const std::vector<SceneLight> &lights);

private:
    friend class Scene;
    std::vector<ScenePrimitive> mPrimitives;
    std::vector<SceneNode>      mChildren;
    std::vector<size_t>         mLightIds;

private:
    bool        mIsRootNode;
    XMMATRIX    mLocalMtrx;
    XMMATRIX    mWorldMtrx;
    std::string mName;
};


class SceneTexture
{
public:
    enum ValueType
    {
        eLinear,
        eSrgb,
    };

    SceneTexture(const std::wstring &name, ValueType valueType, XMFLOAT4 neutralValue);

    SceneTexture(const SceneTexture &src);
    SceneTexture& operator =(const SceneTexture &src);
    SceneTexture(SceneTexture &&src);
    SceneTexture& operator =(SceneTexture &&src);
    ~SceneTexture();

    bool Create(IRenderingContext &ctx, const wchar_t *path);
    bool CreateNeutral(IRenderingContext &ctx);
    bool LoadTextureFromGltf(const int textureIndex,
                             IRenderingContext &ctx,
                             const tinygltf::Model &model,
                             const std::wstring &logPrefix);

    std::wstring    GetName()   const { return mName; }
    bool            IsLoaded()  const { return mIsLoaded; }

private:
    std::wstring    mName;
    ValueType       mValueType;
    XMFLOAT4        mNeutralValue;
    bool            mIsLoaded;
    // TODO: sampler, texCoord

public:
    ID3D11ShaderResourceView* srv;
};


class SceneNormalTexture : public SceneTexture
{
public:
    SceneNormalTexture(const std::wstring &name) :
        SceneTexture(name, SceneTexture::eLinear, XMFLOAT4(0.5f, 0.5f, 1.f, 1.f)) {}
    ~SceneNormalTexture() {}

    bool CreateNeutral(IRenderingContext &ctx);
    bool LoadTextureFromGltf(const tinygltf::NormalTextureInfo &normalTextureInfo,
                             const tinygltf::Model &model,
                             IRenderingContext &ctx,
                             const std::wstring &logPrefix);

    void    SetScale(float scale)   { mScale = scale; }
    float   GetScale() const        { return mScale; }

private:
    float mScale = 1.f;
};


class SceneOcclusionTexture : public SceneTexture
{
public:
    SceneOcclusionTexture(const std::wstring &name) :
        SceneTexture(name, SceneTexture::eLinear, XMFLOAT4(1.f, 0.f, 0.f, 1.f)) {}
    ~SceneOcclusionTexture() {}

    bool CreateNeutral(IRenderingContext &ctx);
    bool LoadTextureFromGltf(const tinygltf::OcclusionTextureInfo &occlusionTextureInfo,
                             const tinygltf::Model &model,
                             IRenderingContext &ctx,
                             const std::wstring &logPrefix);

    void    SetStrength(float strength) { mStrength = strength; }
    float   GetStrength() const         { return mStrength; }

private:
    float mStrength = 1.f;
};

class SceneMaterial
{
public:

    SceneMaterial();

public:

    bool CreatePbrMetalness(IRenderingContext &ctx,
                            const wchar_t * baseColorTexPath,
                            XMFLOAT4 baseColorFactor,
                            const wchar_t * metallicRoughnessTexPath,
                            float metallicFactor,
                            float roughnessFactor);

    bool LoadFromGltf(IRenderingContext &ctx,
                      const tinygltf::Model &model,
                      const tinygltf::Material &material,
                      const std::wstring &logPrefix);

    const SceneTexture &            GetBaseColorTexture()           const { return mBaseColorTexture; };
    XMFLOAT4                        GetBaseColorFactor()            const { return mBaseColorFactor; }
    const SceneTexture &            GetMetallicRoughnessTexture()   const { return mMetallicRoughnessTexture; };
    XMFLOAT4                        GetMetallicRoughnessFactor()    const { return mMetallicRoughnessFactor; }

    const SceneNormalTexture &      GetNormalTexture()              const { return mNormalTexture; };
    const SceneOcclusionTexture &   GetOcclusionTexture()           const { return mOcclusionTexture; };
    const SceneTexture &            GetEmissionTexture()            const { return mEmissionTexture; };
    XMFLOAT4                        GetEmissionFactor()             const { return mEmissionFactor; }

    void Animate(IRenderingContext &ctx);

private:

    // Metal/roughness workflow
    SceneTexture        mBaseColorTexture;
    XMFLOAT4            mBaseColorFactor;
    SceneTexture        mMetallicRoughnessTexture;
    XMFLOAT4            mMetallicRoughnessFactor;

    // Both workflows
    SceneNormalTexture      mNormalTexture;
    SceneOcclusionTexture   mOcclusionTexture;
    SceneTexture            mEmissionTexture;
    XMFLOAT4                mEmissionFactor;
};


struct CbSceneNode
{
    XMMATRIX WorldMtrx;
    XMFLOAT4 MeshColor; // May be eventually replaced by the emmisive component of the standard surface shader
    XMUINT4  Control;
    size_t   LightIds[NODE_LIGHTS_MAX_COUNT];
};


class Scene
{
public:    

    Scene(const std::wstring sceneFilePath);
    // TODO: Scene(const std::string &sceneFilePath);
    virtual ~Scene();

    virtual bool Init(IRenderingContext &ctx);
    virtual void Destroy();
    virtual void AnimateFrame(IRenderingContext &ctx);
    virtual void RenderFrame(IRenderingContext &ctx);
    virtual bool GetAmbientColor(float(&rgba)[4]);

    virtual void CullFrame(IRenderingContext& ctx);

    void SetCamera(IRenderingContext& ctx, const FSceneNode& SceneNode);

private:

    // Loads the scene specified via constructor
    bool Load(IRenderingContext &ctx);
    bool LoadExternal(IRenderingContext &ctx, const std::wstring &filePath);

    bool PostLoadSanityTest();
    bool NodeTangentSanityTest(const SceneNode &node);

    // glTF loader
    bool LoadGLTF(IRenderingContext &ctx,
                  const std::wstring &filePath);
    bool LoadMaterialsFromGltf(IRenderingContext &ctx,
                               const tinygltf::Model &model,
                               const std::wstring &logPrefix);
    bool LoadSceneFromGltf(IRenderingContext &ctx,
                           const tinygltf::Model &model,
                           const std::wstring &logPrefix);
    bool LoadSceneNodeFromGLTF(IRenderingContext &ctx,
                               SceneNode &sceneNode,
                               const tinygltf::Model &model,
                               int nodeIdx,
                               const std::wstring &logPrefix);
    
    bool LoadLightFromGLTF(SceneLight& sceneLight,
        const tinygltf::Light& light,
        const std::wstring& logPrefix);

    // Materials
    const SceneMaterial& GetMaterial(const ScenePrimitive &primitive) const;

    // Lights
    void SetupDefaultLights();

    // Transformations
    void AddScaleToRoots(double scale);
    void AddScaleToRoots(const std::vector<double> &vec);
    void AddRotationQuaternionToRoots(const std::vector<double> &vec);
    void AddTranslationToRoots(const std::vector<double> &vec);
    void AddMatrixToRoots(const std::vector<double> &vec);

    void RenderNode(IRenderingContext &ctx,
                    CbSceneNode& cbSceneNode,
                    const SceneNode &node,                    
                    const XMMATRIX &parentWorldMtrx);

    void RenderRootNode(IRenderingContext& ctx, const SceneNode& node);
    
    void CullRootNode(IRenderingContext& ctx, size_t rootNodeIndex); //const SceneNode& node);

private:

    std::wstring                mSceneFilePath;

    // Geometry
    std::vector<SceneNode>      mRootNodes;

    // Materials
    std::vector<SceneMaterial>  mMaterials;
    SceneMaterial               mDefaultMaterial;

    // Lights
    AmbientLight                mAmbientLight;
    std::vector<SceneLight>          mLights;

    // Camera
    struct {
        XMVECTOR eye;
        XMVECTOR at;
        XMVECTOR up;
    }                           mViewData;
    XMMATRIX                    mViewMtrx;
    XMMATRIX                    mProjectionMtrx;

    // Shaders
    ID3D11VertexShader*         mVertexShader = nullptr;
    ID3D11PixelShader*          mPsPbrMetalness = nullptr;
    ID3D11PixelShader*          mPsConstEmmisive = nullptr;
    ID3D11InputLayout*          mVertexLayout = nullptr;

    ID3D11VertexShader*         mVsCulling = nullptr;
    ID3D11PixelShader*          mPsCulling = nullptr;

    ID3D11Buffer*               mCbScene = nullptr;
    ID3D11Buffer*               mCbFrame = nullptr;
    ID3D11Buffer*               mCbSceneNode = nullptr;
    ID3D11Buffer*               mCbScenePrimitive = nullptr;

    ID3D11SamplerState*         mSamplerLinear = nullptr;
};