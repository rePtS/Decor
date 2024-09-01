module;

#include <DirectXMath.h>
#include <vector>
#include <string>
#include "SceneConstants.hlsli"

export module Scene.Node;

import Scene.IRenderingContext;
import Scene.Primitive;
import Scene.Utils;
import Scene.Log;
import TinyGltf;

using DirectX::XMFLOAT4;
using DirectX::XMMATRIX;
using DirectX::XMUINT4;

export struct SceneLight
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

export class SceneNode
{
public:
    SceneNode(bool isRootNode = false) :
        mIsRootNode(isRootNode),
        mLocalMtrx(DirectX::XMMatrixIdentity()),
        mWorldMtrx(DirectX::XMMatrixIdentity())
    {}

    ScenePrimitive* CreateEmptyPrimitive()
    {
        mPrimitives.clear();
        mPrimitives.resize(1);
        if (mPrimitives.size() != 1)
            return nullptr;

        return &mPrimitives[0];
    }

    void SetIdentity()
    {
        mLocalMtrx = DirectX::XMMatrixIdentity();
    }

    void AddScale(double scale)
    {
        AddScale({ scale, scale, scale });
    }

    void AddScale(const std::vector<double>& vec)
    {
        if (vec.size() != 3)
        {
            if (vec.size() != 0)
                SceneLog::Warning(L"SceneNode::AddScale: vector of incorrect size (%d instead of 3)",
                    vec.size());
            return;
        }

        const auto mtrx = DirectX::XMMatrixScaling((float)vec[0], (float)vec[1], (float)vec[2]);

        mLocalMtrx = mLocalMtrx * mtrx;
    }

    void AddRotationQuaternion(const std::vector<double>& vec)
    {
        if (vec.size() != 4)
        {
            if (vec.size() != 0)
                SceneLog::Warning(L"SceneNode::AddRotationQuaternion: vector of incorrect size (%d instead of 4)",
                    vec.size());
            return;
        }

        const XMFLOAT4 quaternion((float)vec[0], (float)vec[1], (float)vec[2], (float)vec[3]);
        const auto xmQuaternion = XMLoadFloat4(&quaternion);
        const auto mtrx = DirectX::XMMatrixRotationQuaternion(xmQuaternion);

        mLocalMtrx = mLocalMtrx * mtrx;
    }

    void AddTranslation(const std::vector<double>& vec)
    {
        if (vec.size() != 3)
        {
            if (vec.size() != 0)
                SceneLog::Warning(L"SceneNode::AddTranslation: vector of incorrect size (%d instead of 3)",
                    vec.size());
            return;
        }

        const auto mtrx = DirectX::XMMatrixTranslation((float)vec[0], (float)vec[1], (float)vec[2]);

        mLocalMtrx = mLocalMtrx * mtrx;
    }

    void AddMatrix(const std::vector<double>& vec)
    {
        if (vec.size() != 16)
        {
            if (vec.size() != 0)
                SceneLog::Warning(L"SceneNode::AddMatrix: vector of incorrect size (%d instead of 16)",
                    vec.size());
            return;
        }

        const auto mtrx = DirectX::XMMatrixSet(
            (float)vec[0], (float)vec[1], (float)vec[2], (float)vec[3],
            (float)vec[4], (float)vec[5], (float)vec[6], (float)vec[7],
            (float)vec[8], (float)vec[9], (float)vec[10], (float)vec[11],
            (float)vec[12], (float)vec[13], (float)vec[14], (float)vec[15]);

        mLocalMtrx = mLocalMtrx * mtrx;
    }

    bool LoadFromGLTF(IRenderingContext& ctx,
        const tinygltf::Model& model,
        const tinygltf::Node& node,
        int nodeIdx,
        const std::wstring& logPrefix)
    {
        // debug
        if (SceneLog::sLoggingLevel >= SceneLog::ELoggingLevel::eDebug)
        {
            std::wstring transforms;
            if (!node.rotation.empty())
                transforms += L"rotation ";
            if (!node.scale.empty())
                transforms += L"scale ";
            if (!node.translation.empty())
                transforms += L"translation ";
            if (!node.matrix.empty())
                transforms += L"matrix ";
            if (transforms.empty())
                transforms = L"none";
            SceneLog::Debug(L"%sNode %d/%d \"%s\": mesh %d, transform %s, children %d",
                logPrefix.c_str(),
                nodeIdx,
                model.nodes.size(),
                SceneUtils::StringToWstring(node.name).c_str(),
                node.mesh,
                transforms.c_str(),
                node.children.size());
        }

        const std::wstring& subItemsLogPrefix = logPrefix + L"   ";

        // Local transformation
        SetIdentity();
        if (node.matrix.size() == 16)
        {
            AddMatrix(node.matrix);

            // Sanity checking
            if (!node.scale.empty())
                SceneLog::Warning(L"%sNode %d/%d \"%s\": node.scale is not empty when tranformation matrix is provided. Ignoring.",
                    logPrefix.c_str(),
                    nodeIdx,
                    model.nodes.size(),
                    SceneUtils::StringToWstring(node.name).c_str());
            if (!node.rotation.empty())
                SceneLog::Warning(L"%sNode %d/%d \"%s\": node.rotation is not empty when tranformation matrix is provided. Ignoring.",
                    logPrefix.c_str(),
                    nodeIdx,
                    model.nodes.size(),
                    SceneUtils::StringToWstring(node.name).c_str());
            if (!node.translation.empty())
                SceneLog::Warning(L"%sNode %d/%d \"%s\": node.translation is not empty when tranformation matrix is provided. Ignoring.",
                    logPrefix.c_str(),
                    nodeIdx,
                    model.nodes.size(),
                    SceneUtils::StringToWstring(node.name).c_str());
        }
        else
        {
            AddScale(node.scale);
            AddRotationQuaternion(node.rotation);
            AddTranslation(node.translation);
        }

        // Mesh
        const auto meshIdx = node.mesh;
        if (meshIdx >= (int)model.meshes.size())
        {
            SceneLog::Error(L"%sInvalid mesh index (%d/%d)!", subItemsLogPrefix.c_str(), meshIdx, model.meshes.size());
            return false;
        }
        if (meshIdx >= 0)
        {
            const auto& mesh = model.meshes[meshIdx];

            SceneLog::Debug(L"%sMesh %d/%d \"%s\": %d primitive(s)",
                subItemsLogPrefix.c_str(),
                meshIdx,
                model.meshes.size(),
                SceneUtils::StringToWstring(mesh.name).c_str(),
                mesh.primitives.size());

            // Primitives
            const auto primitivesCount = mesh.primitives.size();
            mPrimitives.reserve(primitivesCount);
            for (size_t i = 0; i < primitivesCount; ++i)
            {
                ScenePrimitive primitive;
                if (!primitive.LoadFromGLTF(ctx, model, mesh, (int)i, subItemsLogPrefix + L"   "))
                    return false;
                mPrimitives.push_back(std::move(primitive));
            }
        }

        return true;
    }

    void Animate(IRenderingContext& ctx)
    {
        if (mIsRootNode)
        {
            const float time = 0.0f;////ctx.GetFrameAnimationTime();
            const float period = 15.f; //seconds
            const float totalAnimPos = time / period;
            const float angle = totalAnimPos * DirectX::XM_2PI;

            const XMMATRIX rotMtrx = DirectX::XMMatrixRotationY(angle);

            mWorldMtrx = mLocalMtrx * rotMtrx;
        }
        else
            mWorldMtrx = mLocalMtrx;

        for (auto& child : mChildren)
            child.Animate(ctx);
    }

    DirectX::XMMATRIX GetWorldMtrx() const { return mWorldMtrx; }

    void LinkLights(const std::vector<SceneLight>& lights)
    {
        mLightIds.clear();
        for (size_t i = 0; i < lights.size(); ++i) {
            auto& nodeNames = lights.at(i).affectedNodes;
            if (std::find(nodeNames.begin(), nodeNames.end(), mName) != nodeNames.end())
                mLightIds.push_back(lights[i].index);
        }
    }

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

export struct CbSceneNode
{
    XMMATRIX WorldMtrx;
    XMFLOAT4 MeshColor; // May be eventually replaced by the emmisive component of the standard surface shader
    XMUINT4  Control;
    size_t   LightIds[NODE_LIGHTS_MAX_COUNT];
};