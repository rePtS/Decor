module;

//#include "TextureCache.h"
//#include "Helpers.h"
#include "FastNoiseLite.h"
#include <map>

#include <D3D11.h>
#include <unordered_map>
#include <array>

#include <wrl\client.h>

#include <Engine.h>

export module TextureCache;

import TextureConverter;
import Helpers;

using Microsoft::WRL::ComPtr;

export class TextureCache
{
public:
    static const unsigned int sm_iMaxSlots = 2; //Maximum texture slot managed by the cache

    explicit TextureCache(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        :m_DeviceContext(DeviceContext)
        , m_TextureConverter(Device, DeviceContext)
    {
        ResetDirtySlots();
        CreateNoiseTexture(Device);
    }

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    const TextureConverter::TextureData& FindOrInsert(FTextureInfo& Texture)
    {
        auto it = m_Textures.find(Texture.CacheID);
        if (it != m_Textures.end())
        {
            if (Texture.bRealtimeChanged)
            {
                m_TextureConverter.Update(Texture, it->second);
                Texture.bRealtimeChanged = 0; //Clear this flag (from other renderes)
            }

            return it->second;
        }

        TextureConverter::TextureData NewData = m_TextureConverter.Convert(Texture);
        const TextureConverter::TextureData& Data = m_Textures.emplace(Texture.CacheID, std::move(NewData)).first->second;

        return Data;
    }

    const TextureConverter::TextureData& FindOrInsertAndPrepare(FTextureInfo& Texture, const unsigned int iSlot)
    {
        const TextureConverter::TextureData& Data = FindOrInsert(Texture);

        m_iDirtyBeginSlot = std::min(m_iDirtyBeginSlot, iSlot);
        m_iDirtyEndSlot = std::max(m_iDirtyEndSlot, iSlot);
        m_PreparedSRVs[iSlot] = Data.pShaderResourceView.Get();
        m_PreparedIds[iSlot] = Texture.CacheID;

        return Data;
    }

    /**
    Instead of checking what's actually bound, for our purposes it's enough to just check if someone else WANTED to bind something else.
    However this means that preparing a new texture and then not using it to render will result in a false positive for having to flush geometry.
    */
    bool IsPrepared(const FTextureInfo& Texture, const unsigned int iSlot) const
    {
        return m_iDirtyBeginSlot <= iSlot && m_iDirtyEndSlot >= iSlot && m_PreparedIds[iSlot] == Texture.CacheID;
    }

    void BindTextures()
    {
        if (m_iDirtyBeginSlot > m_iDirtyEndSlot) //Anything prepared?
        {
            return;
        }

        m_DeviceContext.PSSetShaderResources(m_iDirtyBeginSlot, m_iDirtyEndSlot - m_iDirtyBeginSlot + 1, &m_PreparedSRVs[m_iDirtyBeginSlot]);

        // TODO «агружаем в шейдер текстуру шума (возможно это надо вынести у другое место. ѕока так)
        m_DeviceContext.PSSetShaderResources(sm_iMaxSlots, 1, m_NoiseTextureData.pShaderResourceView.GetAddressOf());

        ResetDirtySlots();
    }

    void Flush()
    {
        ID3D11ShaderResourceView* const nullSRV[1] = { nullptr };
        m_DeviceContext.PSSetShaderResources(0, sm_iMaxSlots, nullSRV); //To be able to release textures
        m_Textures.clear();

        ResetDirtySlots();
    }

    size_t GetNumTextures() const
    {
        return m_Textures.size();
    }

    void PrintSizeHistogram(UCanvas& c) const
    {
        typedef decltype(D3D11_TEXTURE2D_DESC::Width) st;
        std::map<st, std::map<st, size_t>> Histogram;
        for (const auto& t : m_Textures)
        {
            D3D11_TEXTURE2D_DESC Desc;
            t.second.pTexture->GetDesc(&Desc);

            auto it = Histogram.emplace(Desc.Width, std::map<st, size_t>()).first;
            auto it2 = it->second.emplace(Desc.Height, 0).first;
            it2->second++;
        }
        for (const auto& Width : Histogram)
        {
            for (const auto& Height : Width.second)
            {
                c.WrappedPrintf(c.SmallFont, 0, L"%u x %u : %Iu", Width.first, Height.first, Height.second);
            }
        }
    }

protected:
    void ResetDirtySlots()
    {
        m_iDirtyBeginSlot = m_PreparedIds.size() - 1;
        m_iDirtyEndSlot = 0;
    }

    ID3D11DeviceContext& m_DeviceContext;

    TextureConverter m_TextureConverter;
    std::unordered_map<long long, TextureConverter::TextureData> m_Textures;

    std::array<decltype(FTextureInfo::CacheID), sm_iMaxSlots> m_PreparedIds;
    std::array<ID3D11ShaderResourceView*, sm_iMaxSlots> m_PreparedSRVs;

    //Tracking of which slots need to be bound on BindTextures()
    unsigned int m_iDirtyBeginSlot;
    unsigned int m_iDirtyEndSlot;

    TextureConverter::TextureData m_NoiseTextureData;
    void CreateNoiseTexture(ID3D11Device& Device)
    {
        FastNoiseLite noise;
        noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);

        size_t width = 64, height = 64;
        std::vector<uint32_t> image(width * height);
        int index = 0;

        for (size_t i = 0; i < width; ++i)
        {
            for (size_t j = 0; j < height; ++j)
            {
                image[index++] = uint32_t(1003741823 * (noise.GetNoise((float)i, (float)j) + 1.0f));
            }
        }

        D3D11_TEXTURE2D_DESC TextureDesc;
        TextureDesc.Width = width;
        TextureDesc.Height = height;
        TextureDesc.ArraySize = 1;
        TextureDesc.MipLevels = 1;
        TextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.SampleDesc.Quality = 0;
        TextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
        TextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_SHADER_RESOURCE;
        TextureDesc.CPUAccessFlags = 0;
        TextureDesc.MiscFlags = 0;

        size_t lineMemPitch = width * sizeof(uint32_t);
        D3D11_SUBRESOURCE_DATA initData = { image.data(), lineMemPitch, 0 };

        const wchar_t* const pszTexName = L"Noise";
        Decor2::ThrowIfFailed(
            Device.CreateTexture2D(&TextureDesc, &initData, &m_NoiseTextureData.pTexture),
            "Failed to create texture '%s'.", pszTexName
        );
        Decor2::SetResourceNameW(m_NoiseTextureData.pTexture, pszTexName);

        D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc;
        ShaderResourceViewDesc.Format = TextureDesc.Format;
        ShaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
        ShaderResourceViewDesc.Texture2D.MipLevels = 1;
        ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;

        Decor2::ThrowIfFailed(
            Device.CreateShaderResourceView(m_NoiseTextureData.pTexture.Get(), &ShaderResourceViewDesc, &m_NoiseTextureData.pShaderResourceView),
            "Failed to create SRV for '%s'.", pszTexName
        );
        Decor2::SetResourceNameW(m_NoiseTextureData.pShaderResourceView, pszTexName);

        m_NoiseTextureData.fMultU = 1.0f; // / (Texture.UClamp * Texture.UScale);
        m_NoiseTextureData.fMultV = 1.0f; // / (Texture.VClamp * Texture.VScale);
    }
};

//TextureCache::TextureCache(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
//:m_DeviceContext(DeviceContext)
//,m_TextureConverter(Device, DeviceContext)
//{
//    ResetDirtySlots();
//    CreateNoiseTexture(Device);
//}
//
//const TextureConverter::TextureData& TextureCache::FindOrInsert(FTextureInfo& Texture)
//{
//    auto it = m_Textures.find(Texture.CacheID);
//    if (it != m_Textures.end())
//    {
//        if (Texture.bRealtimeChanged)
//        {
//            m_TextureConverter.Update(Texture, it->second);
//            Texture.bRealtimeChanged = 0; //Clear this flag (from other renderes)
//        }
//
//        return it->second;
//    }
//
//    TextureConverter::TextureData NewData = m_TextureConverter.Convert(Texture);
//    const TextureConverter::TextureData& Data = m_Textures.emplace(Texture.CacheID, std::move(NewData)).first->second;
//
//    return Data;
//}
//
//
//const TextureConverter::TextureData& TextureCache::FindOrInsertAndPrepare(FTextureInfo& Texture, const size_t iSlot)
//{
//    const TextureConverter::TextureData& Data = FindOrInsert(Texture);
//
//    m_iDirtyBeginSlot = std::min(m_iDirtyBeginSlot, iSlot);
//    m_iDirtyEndSlot = std::max(m_iDirtyEndSlot, iSlot);
//    m_PreparedSRVs[iSlot] = Data.pShaderResourceView.Get();
//    m_PreparedIds[iSlot] = Texture.CacheID;
//
//    return Data;
//}
//
//void TextureCache::BindTextures()
//{
//    if (m_iDirtyBeginSlot > m_iDirtyEndSlot) //Anything prepared?
//    {
//        return;
//    }
//    
//    m_DeviceContext.PSSetShaderResources(m_iDirtyBeginSlot, m_iDirtyEndSlot - m_iDirtyBeginSlot + 1, &m_PreparedSRVs[m_iDirtyBeginSlot]);
//
//    // TODO «агружаем в шейдер текстуру шума (возможно это надо вынести у другое место. ѕока так)
//    m_DeviceContext.PSSetShaderResources(sm_iMaxSlots, 1, m_NoiseTextureData.pShaderResourceView.GetAddressOf());
//
//    ResetDirtySlots();
//}
//
//void TextureCache::Flush()
//{
//    ID3D11ShaderResourceView* const nullSRV[1] = { nullptr };
//    m_DeviceContext.PSSetShaderResources(0, sm_iMaxSlots, nullSRV); //To be able to release textures
//    m_Textures.clear();
//
//    ResetDirtySlots();
//}
//
//void TextureCache::ResetDirtySlots()
//{
//    m_iDirtyBeginSlot = m_PreparedIds.size() - 1;
//    m_iDirtyEndSlot = 0;
//}
//
//void TextureCache::PrintSizeHistogram(UCanvas& c) const
//{
//    typedef decltype(D3D11_TEXTURE2D_DESC::Width) st;
//    std::map<st, std::map<st, size_t>> Histogram;
//    for (const auto& t : m_Textures)
//    {
//        D3D11_TEXTURE2D_DESC Desc;
//        t.second.pTexture->GetDesc(&Desc);
//
//        auto it = Histogram.emplace(Desc.Width, std::map<st, size_t>()).first;
//        auto it2 = it->second.emplace(Desc.Height, 0).first;
//        it2->second++;
//    }
//    for (const auto& Width : Histogram)
//    {
//        for (const auto& Height : Width.second)
//        {
//            c.WrappedPrintf(c.SmallFont, 0, L"%u x %u : %Iu", Width.first, Height.first, Height.second);
//        }
//    }
//}
//
//void TextureCache::CreateNoiseTexture(ID3D11Device& Device)
//{
//    FastNoiseLite noise;
//    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
//
//    size_t width = 64, height = 64;
//    std::vector<uint32_t> image(width * height);
//    int index = 0;
//
//    for (size_t i = 0; i < width; ++i)
//    {
//        for (size_t j = 0; j < height; ++j)
//        {
//            image[index++] = uint32_t(1003741823 * (noise.GetNoise((float)i, (float)j) + 1.0f));
//        }
//    }
//
//    D3D11_TEXTURE2D_DESC TextureDesc;
//    TextureDesc.Width = width;
//    TextureDesc.Height = height;
//    TextureDesc.ArraySize = 1;
//    TextureDesc.MipLevels = 1;
//    TextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
//    TextureDesc.SampleDesc.Count = 1;
//    TextureDesc.SampleDesc.Quality = 0;
//    TextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
//    TextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_SHADER_RESOURCE;
//    TextureDesc.CPUAccessFlags = 0;
//    TextureDesc.MiscFlags = 0;
//        
//    size_t lineMemPitch = width * sizeof(uint32_t);
//    D3D11_SUBRESOURCE_DATA initData = { image.data(), lineMemPitch, 0 };
//
//    const wchar_t* const pszTexName = L"Noise";
//    Decor::ThrowIfFailed(
//        Device.CreateTexture2D(&TextureDesc, &initData, &m_NoiseTextureData.pTexture),
//        "Failed to create texture '%s'.", pszTexName
//    );
//    Decor::SetResourceNameW(m_NoiseTextureData.pTexture, pszTexName);
//
//    D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc;
//    ShaderResourceViewDesc.Format = TextureDesc.Format;
//    ShaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
//    ShaderResourceViewDesc.Texture2D.MipLevels = 1;
//    ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
//
//    Decor::ThrowIfFailed(
//        Device.CreateShaderResourceView(m_NoiseTextureData.pTexture.Get(), &ShaderResourceViewDesc, &m_NoiseTextureData.pShaderResourceView),
//        "Failed to create SRV for '%s'.", pszTexName
//    );
//    Decor::SetResourceNameW(m_NoiseTextureData.pShaderResourceView, pszTexName);
//
//    m_NoiseTextureData.fMultU = 1.0f; // / (Texture.UClamp * Texture.UScale);
//    m_NoiseTextureData.fMultV = 1.0f; // / (Texture.VClamp * Texture.VScale);
//}
