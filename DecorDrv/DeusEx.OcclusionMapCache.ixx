module;

#include <D3D11.h>
#include <vector>
#include <unordered_map>
#include <string>

#include <wrl\client.h>

#include <Engine.h>

export module DeusEx.OcclusionMapCache;

import Utils;

using Microsoft::WRL::ComPtr;

export class OcclusionMapCache
{
public:
    struct TextureData
    {
        TextureData() {}

        TextureData(const TextureData&) = default;
        TextureData(TextureData&& Other)
            : fMultU(Other.fMultU)
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

    explicit OcclusionMapCache(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext, unsigned int Slot)
        :m_Device(Device), m_DeviceContext(DeviceContext), m_Slot(Slot)
    {
        m_PreparedId = 0;
        m_PreparedSRV = nullptr;

        // Create placeholder texture
        m_PlaceholderMap.fMultU = 1.0f;
        m_PlaceholderMap.fMultV = 1.0f;

        D3D11_TEXTURE2D_DESC TextureDesc;
        TextureDesc.Width = 1;
        TextureDesc.Height = 1;
        TextureDesc.MipLevels = 1;
        TextureDesc.ArraySize = 1;
        TextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8_UNORM;
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.SampleDesc.Quality = 0;
        TextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
        TextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_SHADER_RESOURCE;
        TextureDesc.CPUAccessFlags = 0;
        TextureDesc.MiscFlags = 0;

        const uint8_t PlaceholderPixel = 0xff;
        D3D11_SUBRESOURCE_DATA PlaceHolderData;
        PlaceHolderData.pSysMem = &PlaceholderPixel;
        PlaceHolderData.SysMemPitch = sizeof(uint8_t);

        Utils::ThrowIfFailed(
            m_Device.CreateTexture2D(&TextureDesc, &PlaceHolderData, &m_PlaceholderMap.pTexture),
            "Failed to create placeholder occlusion map."
        );
        Utils::SetResourceName(m_PlaceholderMap.pTexture, "Placeholder occlusion map");

        D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc;
        ShaderResourceViewDesc.Format = TextureDesc.Format;
        ShaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
        ShaderResourceViewDesc.Texture2D.MipLevels = 1;
        ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;

        Utils::ThrowIfFailed(
            m_Device.CreateShaderResourceView(m_PlaceholderMap.pTexture.Get(), &ShaderResourceViewDesc, &m_PlaceholderMap.pShaderResourceView),
            "Failed to create placeholder occlusion map SRV."
        );
        Utils::SetResourceName(m_PlaceholderMap.pShaderResourceView, "Placeholder occlusion map");
    }

    OcclusionMapCache(const OcclusionMapCache&) = delete;
    OcclusionMapCache& operator=(const OcclusionMapCache&) = delete;

    const TextureData& FindOrInsert(const UModel& Model, const int mapId)
    {
        auto it = m_OcclusionMaps.find(mapId);
        if (it != m_OcclusionMaps.end())
        {
            return it->second;
        }

        OcclusionMapCache::TextureData NewData = Convert(Model, mapId);
        const OcclusionMapCache::TextureData& Data = m_OcclusionMaps.emplace(mapId, std::move(NewData)).first->second;

        return Data;
    }

    const TextureData& FindOrInsertAndPrepare(const UModel& Model, const int mapId)
    {
        const OcclusionMapCache::TextureData& Data = FindOrInsert(Model, mapId);

        m_PreparedSRV = Data.pShaderResourceView.Get();
        m_PreparedId = mapId;

        return Data;
    }

    bool IsPrepared(const int mapId) const
    {
        return m_PreparedId == mapId;
    }

    void BindMaps()
    {
        m_DeviceContext.PSSetShaderResources(m_Slot, 1, &m_PreparedSRV);
    }

    void Flush()
    {
        m_PreparedSRV = nullptr;
        m_DeviceContext.PSSetShaderResources(m_Slot, 1, &m_PreparedSRV); // To be able to release maps
        m_OcclusionMaps.clear();
    }

protected:
    const uint8_t MaxLight = 255;
    const uint8_t LowLight = 50;
    const uint8_t MinLight = 2;

    unsigned int m_Slot;

    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    std::unordered_map<int, TextureData> m_OcclusionMaps;

    int m_PreparedId;
    ID3D11ShaderResourceView* m_PreparedSRV;

    TextureData Convert(const UModel& Model, const int mapId) const
    {
        auto& map = Model.LightMap(mapId);

        if (map.iLightActors < 0)
            return m_PlaceholderMap;

        // Count the number of light sources for this shading map
        size_t numLights = 0;
        auto lightIndex = map.iLightActors;
        while (Model.Lights(lightIndex++) != nullptr)
            numLights++;

        if (numLights == 0)
            return m_PlaceholderMap;

        OcclusionMapCache::TextureData OutputTexture;

        size_t mapSize = map.UClamp * map.VClamp;
        OutputTexture.DataBuffer.reserve(numLights * mapSize);

        // Reference to the bitmask of the shading maps
        auto mapData = reinterpret_cast<const BYTE*>(Model.LightBits.GetData());

        // The number of bytes that occupies one row in the bitmask of the shading map
        size_t bytesPerUClamp = (map.UClamp + 7) / 8;
        // The number of bytes occupied by the shading map for a single light source in the bitmask
        size_t bytesPerLight = map.VClamp * bytesPerUClamp;

        std::vector<uint8_t> bufferCopy(mapSize);

        for (size_t lightIndex = 0; lightIndex < numLights; ++lightIndex)
        {
            for (size_t v = 0; v < map.VClamp; ++v)
            {
                size_t uclampCounter = map.UClamp;
                for (size_t byteIndex = 0; byteIndex < bytesPerUClamp; ++byteIndex)
                {
                    auto lightByte = mapData[map.DataOffset + lightIndex * bytesPerLight + v * bytesPerUClamp + byteIndex];

                    OutputTexture.DataBuffer.push_back(lightByte & 0x01 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                    OutputTexture.DataBuffer.push_back(lightByte & 0x02 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                    OutputTexture.DataBuffer.push_back(lightByte & 0x04 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                    OutputTexture.DataBuffer.push_back(lightByte & 0x08 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                    OutputTexture.DataBuffer.push_back(lightByte & 0x10 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                    OutputTexture.DataBuffer.push_back(lightByte & 0x20 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                    OutputTexture.DataBuffer.push_back(lightByte & 0x40 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                    OutputTexture.DataBuffer.push_back(lightByte & 0x80 ? MaxLight : MinLight); if (--uclampCounter == 0) break;
                }
            }

            auto lightOffset = lightIndex * mapSize;
            for (size_t i = 0; i < mapSize; ++i)
                OutputTexture.DataBuffer[lightOffset + i] = LitEdges(OutputTexture.DataBuffer, lightOffset, map.VClamp, map.UClamp, i);

            // We are not using antializing maps yet
            /*
                    std::copy(OutputTexture.DataBuffer.begin() + lightIndex * mapSize,
                        OutputTexture.DataBuffer.end(),
                        bufferCopy.begin());

                    for (size_t i = 0; i < mapSize; ++i)
                        OutputTexture.DataBuffer[lightIndex * mapSize + i] = Antialize(bufferCopy, map.VClamp, map.UClamp, i);
            */
        }

        D3D11_TEXTURE2D_DESC TextureDesc;
        TextureDesc.Width = map.UClamp; //Texture.UClamp;
        TextureDesc.Height = map.VClamp; //Texture.VClamp;
        TextureDesc.MipLevels = 1; //Texture.NumMips;
        TextureDesc.ArraySize = numLights;
        TextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8_UNORM;
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.SampleDesc.Quality = 0;
        TextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
        TextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_SHADER_RESOURCE;
        TextureDesc.CPUAccessFlags = 0;
        TextureDesc.MiscFlags = 0;

        OutputTexture.pSubResourceData.resize(numLights);
        for (size_t i = 0; i < numLights; ++i)
        {
            OutputTexture.pSubResourceData[i].pSysMem = &OutputTexture.DataBuffer.data()[i * mapSize];
            OutputTexture.pSubResourceData[i].SysMemPitch = map.UClamp * sizeof(uint8_t);
            OutputTexture.pSubResourceData[i].SysMemSlicePitch = mapSize * sizeof(uint8_t);
        }

        auto texName = std::wstring(L"OcclusionMap") + std::to_wstring(mapId);
        const wchar_t* const pszTexName = texName.c_str();

        Utils::ThrowIfFailed(
            m_Device.CreateTexture2D(&TextureDesc, OutputTexture.pSubResourceData.data(), &OutputTexture.pTexture),
            "Failed to create texture '%s'.", pszTexName
        );
        Utils::SetResourceNameW(OutputTexture.pTexture, pszTexName);

        D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc;
        ShaderResourceViewDesc.Format = TextureDesc.Format;
        ShaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        ShaderResourceViewDesc.Texture2DArray.MostDetailedMip = 0;
        ShaderResourceViewDesc.Texture2DArray.MipLevels = TextureDesc.MipLevels;
        ShaderResourceViewDesc.Texture2DArray.FirstArraySlice = 0;
        ShaderResourceViewDesc.Texture2DArray.ArraySize = numLights;

        Utils::ThrowIfFailed(
            m_Device.CreateShaderResourceView(OutputTexture.pTexture.Get(), &ShaderResourceViewDesc, &OutputTexture.pShaderResourceView),
            "Failed to create SRV for '%s'.", pszTexName
        );
        Utils::SetResourceNameW(OutputTexture.pShaderResourceView, pszTexName);

        OutputTexture.fMultU = 1.0f / (map.UClamp * map.UScale);
        OutputTexture.fMultV = 1.0f / (map.VClamp * map.VScale);

        return OutputTexture;
    }

    uint8_t Antialize(const std::vector<uint8_t>& buffer, size_t vclamp, size_t uclamp, size_t index) const
    {
        float cnt = 1, sum = buffer[index]; // central texel

        auto i = index / uclamp;
        auto j = index % uclamp;
        auto jMax = uclamp - 1;
        auto iMax = vclamp - 1;

        if (i > 0)
        {
            auto topIndex = (i - 1) * uclamp + j;
            sum += buffer[topIndex], ++cnt; // texel above the central textile

            if (j > 0)
                sum += buffer[topIndex - 1], ++cnt; // texel on the top left of the central textile
            if (j < jMax)
                sum += buffer[topIndex + 1], ++cnt; // texel on the top right of the central textile
        }

        if (i < iMax)
        {
            auto bottomIndex = (i + 1) * uclamp + j;
            sum += buffer[bottomIndex], ++cnt; // texel under the central textile

            if (j > 0)
                sum += buffer[bottomIndex - 1], ++cnt; // texel to the left from the bottom of the central textile
            if (j < jMax)
                sum += buffer[bottomIndex + 1], ++cnt; // texel to the right from the bottom of the central textile
        }

        if (j > 0)
            sum += buffer[index - 1], ++cnt; // texel to the left of the central textile
        if (j < jMax)
            sum += buffer[index + 1], ++cnt; // texel to the right of the central textile

        return static_cast<uint8_t>(sum / cnt);
    }
    
    /// <summary>
    /// Lights edges of an occlusion map.
    /// Adds some light for min-lighted pixels
    /// </summary>
    /// <param name="buffer">Occlusion map buffer</param>
    /// <param name="lightOffset">Offset of current map in the buffer</param>
    /// <param name="vclamp">width of the occlusion map</param>
    /// <param name="uclamp">height of the occlusion map</param>
    /// <param name="index">index of pixel in the occlusion map</param>
    /// <returns>Corrected occlusion map pixel value</returns>    
    uint8_t LitEdges(const std::vector<uint8_t>& buffer, size_t lightOffset, size_t vclamp, size_t uclamp, size_t index) const
    {
        auto i = index / uclamp;
        auto j = index % uclamp;
        auto jMax = uclamp - 1;
        auto iMax = vclamp - 1;

        auto value = buffer[lightOffset + index];
        if (value == MinLight)
        {
            size_t targetIndex = lightOffset + index; // index of pixel in occlussion map (OM) affecting current OM pixel

            if (i == 0) // top row of pixels
            {
                if (j == 0) // top left pixel
                    targetIndex += uclamp + 1;
                else if (j == jMax) // top right pixel
                    targetIndex += uclamp - 1;
                else // top row without corners
                    targetIndex += uclamp;
            }
            else if (i == iMax) // bottom row of pixels
            {
                if (j == 0) // bottom left pixel
                    targetIndex -= uclamp - 1;
                else if (j == jMax) // bottom right pixel
                    targetIndex -= uclamp + 1;
                else // bottom row without corners
                    targetIndex -= uclamp;
            }
            else
            {
                if (j == 0) // left column of pixels
                    targetIndex += 1;
                else if (j == jMax) // right column of pixels
                    targetIndex -= 1;
                else // usual pixel
                    return value;
            }

            return buffer[targetIndex] == MinLight ? MinLight : LowLight;
        }
        else
            return value;
    }

    TextureData m_PlaceholderMap; // Placeholder occlusion map
};