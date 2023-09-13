#pragma once
#include <D3D11.h>
#include <vector>
#include <unordered_map>

#include <wrl\client.h>
using Microsoft::WRL::ComPtr;

#include <Engine.h>

class OcclusionMapCache
{
public:
    struct TextureData
    {
        TextureData() {}

        TextureData(const TextureData&) = default;
        TextureData(TextureData&& Other) //VS2015 doesn't support default move constructors
            :fMultU(Other.fMultU)
            , fMultV(Other.fMultV)
            , pTexture(std::move(Other.pTexture))
            , pShaderResourceView(std::move(Other.pShaderResourceView))
            , DataBuffer(std::move(Other.DataBuffer))
            , pSubResourceData(std::move(Other.pSubResourceData))
        {
        }
        TextureData& operator=(const TextureData&) = delete;

        float fMultU;
        float fMultV;

        ComPtr<ID3D11Texture2D> pTexture;
        ComPtr<ID3D11ShaderResourceView> pShaderResourceView;
        
        std::vector<uint8_t> DataBuffer;
        std::vector<D3D11_SUBRESOURCE_DATA> pSubResourceData;
    };

    explicit OcclusionMapCache(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext, unsigned int Slot);
    OcclusionMapCache(const OcclusionMapCache&) = delete;
    OcclusionMapCache& operator=(const OcclusionMapCache&) = delete;

    // Может здесь тоже использовать CacheID (от лайтмапа)?
    const TextureData& FindOrInsert(const UModel& Model, const int mapId);
    const TextureData& FindOrInsertAndPrepare(const UModel& Model, const int mapId);

    bool IsPrepared(const int mapId) const { return m_PreparedId == mapId; }

    void BindMaps();
    void Flush();

protected:
    unsigned int m_Slot;

    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    std::unordered_map<int, TextureData> m_OcclusionMaps;

    int m_PreparedId;
    ID3D11ShaderResourceView* m_PreparedSRV;

    TextureData Convert(const UModel& Model, const int mapId) const;
    uint8_t Antialize(const std::vector<uint8_t>& buffer, size_t vclamp, size_t uclamp, size_t index) const;

    TextureData m_PlaceholderMap; //Placeholder occlusion map
};
