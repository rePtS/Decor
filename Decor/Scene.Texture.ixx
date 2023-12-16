module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <string>

export module Scene.Texture;

import Scene.IRenderingContext;
import Scene.GltfUtils;
import Scene.Utils;
import Scene.Log;
import TinyGltf;

using DirectX::XMFLOAT4;

export class SceneTexture
{
public:
    enum ValueType
    {
        eLinear,
        eSrgb,
    };    

    SceneTexture(const std::wstring& name, ValueType valueType, XMFLOAT4 neutralValue) :
        mName(name),
        mValueType(valueType),
        mNeutralValue(neutralValue),
        mIsLoaded(false),
        srv(nullptr)
    {}

    SceneTexture(const SceneTexture& src) :
        mName(src.mName),
        mValueType(src.mValueType),
        mNeutralValue(src.mNeutralValue),
        mIsLoaded(src.mIsLoaded),
        srv(src.srv)
    {
        // We are creating new reference of device resource
        SceneUtils::SafeAddRef(srv);
    }

    SceneTexture& operator =(const SceneTexture& src)
    {
        mName = src.mName;
        mValueType = src.mValueType;
        mNeutralValue = src.mNeutralValue;
        mIsLoaded = src.mIsLoaded;
        srv = src.srv;

        // We are creating new reference of device resource
        SceneUtils::SafeAddRef(srv);

        return *this;
    }

    SceneTexture(SceneTexture&& src) :
        mName(src.mName),
        mValueType(src.mValueType),
        mNeutralValue(SceneUtils::Exchange(src.mNeutralValue, XMFLOAT4(0.f, 0.f, 0.f, 0.f))),
        mIsLoaded(SceneUtils::Exchange(src.mIsLoaded, false)),
        srv(SceneUtils::Exchange(src.srv, nullptr))
    {}

    SceneTexture& operator =(SceneTexture&& src)
    {
        mName = src.mName;
        mValueType = src.mValueType;
        mNeutralValue = SceneUtils::Exchange(src.mNeutralValue, XMFLOAT4(0.f, 0.f, 0.f, 0.f));
        mIsLoaded = SceneUtils::Exchange(src.mIsLoaded, false);
        srv = SceneUtils::Exchange(src.srv, nullptr);

        return *this;
    }

    ~SceneTexture()
    {
        SceneUtils::ReleaseAndMakeNull(srv);
    }

    bool Create(IRenderingContext& ctx, const wchar_t* path)
    {
        //    auto device = ctx.GetDevice();
        //    if (!device)
        //        return false;
        //
        //    HRESULT hr = S_OK;
        //
        //    if (path)
        //    {
        //        D3DX11_IMAGE_LOAD_INFO ili;
        //        ili.Usage = D3D11_USAGE_IMMUTABLE;
        //#ifdef CONVERT_SRGB_INPUT_TO_LINEAR
        //        if (mValueType == SceneTexture::eSrgb)
        //        {
        //            ili.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        //            ili.Filter = D3DX11_FILTER_SRGB | D3DX11_FILTER_NONE;
        //        }
        //        else
        //#endif
        //        {
        //            ili.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        //        }
        //
        //        hr = D3DX11CreateShaderResourceViewFromFile(device, path, &ili, nullptr, &srv, nullptr);
        //        if (FAILED(hr))
        //            return false;
        //    }
        //    else
        //    {
        //        // Neutral constant texture
        //        if (!CreateNeutral(ctx))
        //            return false;
        //    }

            // Neutral constant texture
        if (!CreateNeutral(ctx))
            return false;

        return true;
    }

    bool CreateNeutral(IRenderingContext& ctx)
    {
        return CreateConstantTextureSRV(ctx, srv, mNeutralValue);
    }

    bool LoadTextureFromGltf(const int textureIndex,
        IRenderingContext& ctx,
        const tinygltf::Model& model,
        const std::wstring& logPrefix)

    {
        const auto& textures = model.textures;
        const auto& images = model.images;

        if (textureIndex >= (int)textures.size())
        {
            SceneLog::Error(L"%sInvalid texture index (%d/%d) in \"%s\""
                L"!",
                logPrefix.c_str(),
                textureIndex,
                textures.size(),
                GetName().c_str()
            );
            return false;
        }

        if (textureIndex < 0)
        {
            // No texture - load neutral constant one
            SceneLog::Debug(L"%s%s: Not specified - creating neutral constant texture",
                logPrefix.c_str(),
                GetName().c_str()
            );

            if (!CreateConstantTextureSRV(ctx, srv, mNeutralValue))
            {
                SceneLog::Error(L"%sFailed to create neutral constant texture for \"%s\"!",
                    logPrefix.c_str(),
                    GetName().c_str());
                return false;
            }

            return true;
        }

        const auto& texture = textures[textureIndex];

        const auto texSource = texture.source;
        if ((texSource < 0) || (texSource >= images.size()))
        {
            SceneLog::Error(L"%sInvalid source image index (%d/%d) in texture %d!",
                logPrefix.c_str(),
                texSource,
                images.size(),
                textureIndex);
            return false;
        }

        const auto& image = images[texSource];

        SceneLog::Debug(L"%s%s: \"%s\"/\"%s\", %dx%d, %dx%db %s, data size %dB",
            logPrefix.c_str(),
            GetName().c_str(),
            SceneUtils::StringToWstring(image.name).c_str(),
            SceneUtils::StringToWstring(image.uri).c_str(),
            image.width,
            image.height,
            image.component,
            image.bits,
            GltfUtils::ComponentTypeToWstring(image.pixel_type).c_str(),
            image.image.size());

        const auto srcPixelSize = image.component * image.bits / 8;
        const auto expectedSrcDataSize = image.width * image.height * srcPixelSize;
        if (image.width <= 0 ||
            image.height <= 0 ||
            image.component != 4 ||
            image.bits != 8 ||
            image.pixel_type != tinygltf::COMPONENT_TYPE_UNSIGNED_BYTE ||
            image.image.size() != expectedSrcDataSize)
        {
            SceneLog::Error(L"%sInvalid image \"%s\": \"%s\", %dx%d, %dx%db %s, data size %dB",
                logPrefix.c_str(),
                SceneUtils::StringToWstring(image.name).c_str(),
                SceneUtils::StringToWstring(image.uri).c_str(),
                image.width,
                image.height,
                image.component,
                image.bits,
                GltfUtils::ComponentTypeToWstring(image.pixel_type).c_str(),
                image.image.size());
            return false;
        }

        DXGI_FORMAT dataFormat;
#ifdef CONVERT_SRGB_INPUT_TO_LINEAR
        if (mValueType == SceneTexture::eSrgb)
            dataFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        else
#endif
            dataFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        if (!CreateTextureSrvFromData(ctx,
            srv,
            image.width,
            image.height,
            dataFormat,
            image.image.data(),
            image.width * 4 * sizeof(uint8_t)))
        {
            SceneLog::Error(L"%sFailed to create texture & SRV for image \"%s\": \"%s\", %dx%d",
                logPrefix.c_str(),
                SceneUtils::StringToWstring(image.name).c_str(),
                SceneUtils::StringToWstring(image.uri).c_str(),
                image.width,
                image.height);
            return false;
        }

        mIsLoaded = true;

        // TODO: Sampler

        return true;
    }

    std::wstring GetName() const { return mName; }
    bool IsLoaded() const { return mIsLoaded; }

private:
    std::wstring mName;
    ValueType    mValueType;
    XMFLOAT4     mNeutralValue;
    bool         mIsLoaded;
    // TODO: sampler, texCoord

    bool CreateTextureSrvFromData(IRenderingContext& ctx,
        ID3D11ShaderResourceView*& srv,
        const UINT width,
        const UINT height,
        const DXGI_FORMAT dataFormat,
        const void* data,
        const UINT lineMemPitch)
    {
        auto& device = ctx.GetDevice();
        assert(&device);

        HRESULT hr = S_OK;
        ID3D11Texture2D* tex = nullptr;

        // Texture
        D3D11_TEXTURE2D_DESC descTex;
        ZeroMemory(&descTex, sizeof(D3D11_TEXTURE2D_DESC));
        descTex.ArraySize = 1;
        descTex.Usage = D3D11_USAGE_IMMUTABLE;
        descTex.Format = dataFormat;
        descTex.Width = width;
        descTex.Height = height;
        descTex.MipLevels = 1;
        descTex.SampleDesc.Count = 1;
        descTex.SampleDesc.Quality = 0;
        descTex.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initData = { data, lineMemPitch, 0 };
        hr = device.CreateTexture2D(&descTex, &initData, &tex);
        if (FAILED(hr))
            return false;

        // Shader resource view
        D3D11_SHADER_RESOURCE_VIEW_DESC descSRV;
        descSRV.Format = descTex.Format;
        descSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        descSRV.Texture2D.MipLevels = 1;
        descSRV.Texture2D.MostDetailedMip = 0;
        hr = device.CreateShaderResourceView(tex, &descSRV, &srv);
        SceneUtils::ReleaseAndMakeNull(tex);
        if (FAILED(hr))
            return false;

        return true;
    }

    bool CreateConstantTextureSRV(IRenderingContext& ctx,
        ID3D11ShaderResourceView*& srv,
        XMFLOAT4 color)
    {
        return CreateTextureSrvFromData(ctx, srv,
            1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
            &color, sizeof(XMFLOAT4));
    }

public:
    ID3D11ShaderResourceView* srv;
};


export class SceneNormalTexture : public SceneTexture
{
public:
    SceneNormalTexture(const std::wstring& name) :
        SceneTexture(name, SceneTexture::eLinear, DirectX::XMFLOAT4(0.5f, 0.5f, 1.f, 1.f)) {}
    ~SceneNormalTexture() {}

    bool CreateNeutral(IRenderingContext& ctx)
    {
        mScale = 1.f;
        return SceneTexture::CreateNeutral(ctx);
    }

    bool LoadTextureFromGltf(const tinygltf::NormalTextureInfo& normalTextureInfo,
        const tinygltf::Model& model,
        IRenderingContext& ctx,
        const std::wstring& logPrefix)
    {
        if (!SceneTexture::LoadTextureFromGltf(normalTextureInfo.index, ctx, model, logPrefix))
            return false;

        mScale = (float)normalTextureInfo.scale;

        SceneLog::Debug(L"%s%s: scale %f",
            logPrefix.c_str(),
            GetName().c_str(),
            mScale);

        return true;
    }

    void SetScale(float scale) { mScale = scale; }
    float GetScale() const { return mScale; }

private:
    float mScale = 1.f;
};


export class SceneOcclusionTexture : public SceneTexture
{
public:
    SceneOcclusionTexture(const std::wstring& name) :
        SceneTexture(name, SceneTexture::eLinear, DirectX::XMFLOAT4(1.f, 0.f, 0.f, 1.f)) {}
    ~SceneOcclusionTexture() {}

    bool CreateNeutral(IRenderingContext& ctx)
    {
        mStrength = 1.f;
        return SceneTexture::CreateNeutral(ctx);
    }

    bool LoadTextureFromGltf(const tinygltf::OcclusionTextureInfo& occlusionTextureInfo,
        const tinygltf::Model& model,
        IRenderingContext& ctx,
        const std::wstring& logPrefix)
    {
        if (!SceneTexture::LoadTextureFromGltf(occlusionTextureInfo.index, ctx, model, logPrefix))
            return false;

        mStrength = (float)occlusionTextureInfo.strength;

        SceneLog::Debug(L"%s%s: strength %f",
            logPrefix.c_str(),
            GetName().c_str(),
            mStrength);

        return true;
    }

    void SetStrength(float strength) { mStrength = strength; }
    float GetStrength() const { return mStrength; }

private:
    float mStrength = 1.f;
};