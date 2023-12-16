module;

#include <DirectXMath.h>
#include <string>

export module Scene.Material;

import Scene.IRenderingContext;
import Scene.GltfUtils;
import Scene.Texture;
import Scene.Log;
import TinyGltf;

using DirectX::XMFLOAT4;

export class SceneMaterial
{
public:
    SceneMaterial() :
        mBaseColorTexture(L"BaseColorTexture", SceneTexture::eSrgb, XMFLOAT4(1.f, 1.f, 1.f, 1.f)),
        mBaseColorFactor(XMFLOAT4(1.f, 1.f, 1.f, 1.f)),
        mMetallicRoughnessTexture(L"MetallicRoughnessTexture", SceneTexture::eLinear, XMFLOAT4(1.f, 1.f, 1.f, 1.f)),
        mMetallicRoughnessFactor(XMFLOAT4(1.f, 1.f, 1.f, 1.f)),

        mNormalTexture(L"NormalTexture"),
        mOcclusionTexture(L"OcclusionTexture"),
        mEmissionTexture(L"EmissionTexture", SceneTexture::eSrgb, XMFLOAT4(0.f, 0.f, 0.f, 1.f)),
        mEmissionFactor(XMFLOAT4(0.f, 0.f, 0.f, 1.f))
    {}

    bool CreatePbrMetalness(IRenderingContext& ctx,
        const wchar_t* baseColorTexPath,
        DirectX::XMFLOAT4 baseColorFactor,
        const wchar_t* metallicRoughnessTexPath,
        float metallicFactor,
        float roughnessFactor)
    {
        if (!mBaseColorTexture.Create(ctx, baseColorTexPath))
            return false;
        mBaseColorFactor = baseColorFactor;

        if (!mMetallicRoughnessTexture.Create(ctx, metallicRoughnessTexPath))
            return false;
        mMetallicRoughnessFactor = XMFLOAT4(0.f, roughnessFactor, metallicFactor, 0.f);

        if (!mNormalTexture.CreateNeutral(ctx))
            return false;

        if (!mOcclusionTexture.CreateNeutral(ctx))
            return false;

        if (!mEmissionTexture.CreateNeutral(ctx))
            return false;
        mEmissionFactor = XMFLOAT4(0.f, 0.f, 0.f, 1.f);

        return true;
    }

    bool LoadFromGltf(IRenderingContext& ctx,
        const tinygltf::Model& model,
        const tinygltf::Material& material,
        const std::wstring& logPrefix)
    {
        auto& pbrMR = material.pbrMetallicRoughness;

        if (!mBaseColorTexture.LoadTextureFromGltf(pbrMR.baseColorTexture.index, ctx, model, logPrefix))
            return false;

        GltfUtils::FloatArrayToColor(mBaseColorFactor, pbrMR.baseColorFactor);

        SceneLog::Debug(L"%s%s: %s",
            logPrefix.c_str(),
            L"BaseColorFactor",
            GltfUtils::ColorToWstring(mBaseColorFactor).c_str());

        if (!mMetallicRoughnessTexture.LoadTextureFromGltf(pbrMR.metallicRoughnessTexture.index, ctx, model, logPrefix))
            return false;

        GltfUtils::FloatToColorComponent<2>(mMetallicRoughnessFactor, pbrMR.metallicFactor);
        GltfUtils::FloatToColorComponent<1>(mMetallicRoughnessFactor, pbrMR.roughnessFactor);

        SceneLog::Debug(L"%s%s: %s",
            logPrefix.c_str(),
            L"MetallicRoughnessFactor",
            GltfUtils::ColorToWstring(mBaseColorFactor).c_str());

        if (!mNormalTexture.LoadTextureFromGltf(material.normalTexture, model, ctx, logPrefix))
            return false;

        if (!mOcclusionTexture.LoadTextureFromGltf(material.occlusionTexture, model, ctx, logPrefix))
            return false;

        if (!mEmissionTexture.LoadTextureFromGltf(material.emissiveTexture.index, ctx, model, logPrefix))
            return false;

        GltfUtils::FloatArrayToColor(mEmissionFactor, material.emissiveFactor);

        SceneLog::Debug(L"%s%s: %s",
            logPrefix.c_str(),
            L"EmissionFactor",
            GltfUtils::ColorToWstring(mEmissionFactor).c_str());

        return true;
    }

    const SceneTexture& GetBaseColorTexture() const
    {
        return mBaseColorTexture;
    };

    XMFLOAT4 GetBaseColorFactor() const
    {
        return mBaseColorFactor;
    }

    const SceneTexture& GetMetallicRoughnessTexture() const
    {
        return mMetallicRoughnessTexture;
    };

    XMFLOAT4 GetMetallicRoughnessFactor() const
    {
        return mMetallicRoughnessFactor;
    }

    const SceneNormalTexture& GetNormalTexture() const
    {
        return mNormalTexture;
    };

    const SceneOcclusionTexture& GetOcclusionTexture() const
    {
        return mOcclusionTexture;
    };

    const SceneTexture& GetEmissionTexture() const
    {
        return mEmissionTexture;
    };

    XMFLOAT4 GetEmissionFactor() const
    {
        return mEmissionFactor;
    }

    void Animate(IRenderingContext& ctx)
    {
        const float totalAnimPos = 0.0f; /////ctx.GetFrameAnimationTime() / 3.f; //seconds
    }

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