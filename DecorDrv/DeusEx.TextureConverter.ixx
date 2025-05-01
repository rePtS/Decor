module;

#include <D3D11.h>
#include <vector>
#include <array>
#include <cassert>
#include <wrl\client.h>

#include <Engine.h>

export module DeusEx.TextureConverter;

import Utils;

using Microsoft::WRL::ComPtr;

export class TextureConverter
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
        {
        }

        TextureData& operator=(const TextureData&) = delete;

        float fMultU;
        float fMultV;

        ComPtr<ID3D11Texture2D> pTexture;
        ComPtr<ID3D11ShaderResourceView> pShaderResourceView;
    };

    explicit TextureConverter(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        : m_Device(Device)
        , m_DeviceContext(DeviceContext)
    {

        // Create placeholder texture
        m_PlaceholderTexture.fMultU = 1.0f;
        m_PlaceholderTexture.fMultV = 1.0f;

        D3D11_TEXTURE2D_DESC TextureDesc;
        TextureDesc.Width = 1;
        TextureDesc.Height = 1;
        TextureDesc.MipLevels = 1;
        TextureDesc.ArraySize = 1;
        TextureDesc.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.SampleDesc.Quality = 0;
        TextureDesc.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
        TextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_SHADER_RESOURCE;
        TextureDesc.CPUAccessFlags = 0;
        TextureDesc.MiscFlags = 0;

        const uint32_t PlaceholderPixel = 0xff00ffff;
        D3D11_SUBRESOURCE_DATA PlaceHolderData;
        PlaceHolderData.pSysMem = &PlaceholderPixel;
        PlaceHolderData.SysMemPitch = sizeof(uint32_t);

        Utils::ThrowIfFailed(
            m_Device.CreateTexture2D(&TextureDesc, &PlaceHolderData, &m_PlaceholderTexture.pTexture),
            "Failed to create placeholder texture."
        );
        Utils::SetResourceName(m_PlaceholderTexture.pTexture, "Placeholder texture");

        D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc;
        ShaderResourceViewDesc.Format = TextureDesc.Format;
        ShaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
        ShaderResourceViewDesc.Texture2D.MipLevels = 1;
        ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;

        Utils::ThrowIfFailed(
            m_Device.CreateShaderResourceView(m_PlaceholderTexture.pTexture.Get(), &ShaderResourceViewDesc, &m_PlaceholderTexture.pShaderResourceView),
            "Failed to create placeholder texture SRV."
        );
        Utils::SetResourceName(m_PlaceholderTexture.pShaderResourceView, "Placeholder texture");
    };

    TextureConverter(const TextureConverter&) = delete;
    TextureConverter& operator=(const TextureConverter&) = delete;

    TextureData Convert(const FTextureInfo& Texture, const DWORD PolyFlags) const
    {
        IFormatConverter* const pConverter = m_FormatConverters[Texture.Format];
        if (pConverter == nullptr)
        {
            return m_PlaceholderTexture;
        }

        const bool bDynamic = Texture.bRealtimeChanged; // bRealtime isn't always set

        pConverter->Convert(Texture, PolyFlags);

        TextureData OutputTexture;

        D3D11_TEXTURE2D_DESC TextureDesc;
        TextureDesc.Width = Texture.UClamp;
        TextureDesc.Height = Texture.VClamp;
        TextureDesc.MipLevels = Texture.NumMips;
        TextureDesc.ArraySize = 1;
        TextureDesc.Format = pConverter->GetDXGIFormat();
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.SampleDesc.Quality = 0;
        TextureDesc.Usage = bDynamic ? D3D11_USAGE::D3D11_USAGE_DEFAULT : D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
        //TextureDesc.Usage = bDynamic ? D3D11_USAGE::D3D11_USAGE_DYNAMIC : D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
        TextureDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_SHADER_RESOURCE;
        TextureDesc.CPUAccessFlags = 0;
        //TextureDesc.CPUAccessFlags = bDynamic ? D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE : 0;
        TextureDesc.MiscFlags = 0;

        const wchar_t* const pszTexName = Texture.Texture ? Texture.Texture->GetName() : nullptr;

        Utils::ThrowIfFailed(
            m_Device.CreateTexture2D(&TextureDesc, m_ConvertedTextureData.GetSubResourceDataArray(), &OutputTexture.pTexture),
            "Failed to create texture '%s'.", pszTexName
        );
        Utils::SetResourceNameW(OutputTexture.pTexture, pszTexName);

        D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc;
        ShaderResourceViewDesc.Format = TextureDesc.Format;
        ShaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
        ShaderResourceViewDesc.Texture2D.MipLevels = Texture.NumMips;
        ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;

        Utils::ThrowIfFailed(
            m_Device.CreateShaderResourceView(OutputTexture.pTexture.Get(), &ShaderResourceViewDesc, &OutputTexture.pShaderResourceView),
            "Failed to create SRV for '%s'.", pszTexName
        );
        Utils::SetResourceNameW(OutputTexture.pShaderResourceView, pszTexName);

        OutputTexture.fMultU = 1.0f / (Texture.UClamp * Texture.UScale);
        OutputTexture.fMultV = 1.0f / (Texture.VClamp * Texture.VScale);

        return OutputTexture;
    }

    void Update(const FTextureInfo& Source, TextureData& Dest, const DWORD PolyFlags) const
    {
        assert(Source.bRealtimeChanged);

        IFormatConverter* const pConverter = m_FormatConverters[Source.Format];
        assert(pConverter); // Should have a converter as it was converted succesfully before

        pConverter->Convert(Source, PolyFlags);

        for (int i = 0; i < Source.NumMips; i++)
        {
            m_DeviceContext.UpdateSubresource(Dest.pTexture.Get(), i, nullptr, m_ConvertedTextureData.GetSubResourceDataSysMem(i), sizeof(DWORD) * Source.Mips[i]->USize, 0);
        }
    }

protected:
    ID3D11Device& m_Device;
    ID3D11DeviceContext& m_DeviceContext;

    class IFormatConverter;
    
    /// <summary>
    /// Scratch data buffer, used to initialize D3D textures.
    /// </summary>
    class ConvertedTextureData
    {
    public:
        typedef uint32_t PixelFormat;
        void Resize(const FTextureInfo& Texture, const IFormatConverter& FormatConverter)
        {
            m_SubResourceData.clear();
            m_Mips.clear();

            m_SubResourceData.resize(Texture.NumMips);

            const bool bWantsBuffer = FormatConverter.WantsBuffer();
            if (bWantsBuffer)
            {
                m_Mips.resize(Texture.NumMips);
            }

            for (size_t i = 0; i < m_SubResourceData.size(); i++)
            {
                assert(Texture.Mips[i]);
                const FMipmapBase& UnrealMip = *Texture.Mips[i];
                if (bWantsBuffer)
                {
                    m_Mips[i].resize(UnrealMip.USize * UnrealMip.VSize);
                    m_SubResourceData[i].pSysMem = m_Mips[i].data();
                }
                m_SubResourceData[i].SysMemPitch = FormatConverter.GetStride(UnrealMip);
            }
        }

        PixelFormat* GetMipBuffer(const unsigned int iMipLevel) { return m_Mips[iMipLevel].data(); }
        const D3D11_SUBRESOURCE_DATA* GetSubResourceDataArray() const { return m_SubResourceData.data(); }
        const void* GetSubResourceDataSysMem(const unsigned int iMipLevel) const { return m_SubResourceData[iMipLevel].pSysMem; }
        void SetSubResourceDataSysMem(const unsigned int iMipLevel, void* const p) { m_SubResourceData[iMipLevel].pSysMem = p; }

    private:
        std::vector<std::vector<PixelFormat>> m_Mips; // Scratch data, not required for all conversions
        std::vector<D3D11_SUBRESOURCE_DATA> m_SubResourceData; // References to Unreal data or converted scratch data
    };
    ConvertedTextureData m_ConvertedTextureData;
    
    /// <summary>
    /// Interface for format converters
    /// </summary>
    class IFormatConverter
    {
    public:
        IFormatConverter& operator=(const IFormatConverter&) = delete;
        virtual ~IFormatConverter() {};
        virtual void Convert(const FTextureInfo& Texture, const DWORD PolyFlags) = 0;
        virtual UINT GetStride(const FMipmapBase& Mip) const = 0;
        virtual DXGI_FORMAT GetDXGIFormat() const = 0;
        virtual bool WantsBuffer() const = 0; // Whether converter requires scratch space for conversion result
    };

    class FormatConverterIdentity : public IFormatConverter
    {
    public:
        explicit FormatConverterIdentity(ConvertedTextureData& Buffer) : m_Buffer(Buffer) {};
        virtual void Convert(const FTextureInfo& Texture, const DWORD PolyFlags) override
        {
            m_Buffer.Resize(Texture, *this);
            for (INT i = 0; i < Texture.NumMips; i++)
            {
                assert(Texture.Mips[i]);
                const FMipmapBase& UnrealMip = *Texture.Mips[i];
                m_Buffer.SetSubResourceDataSysMem(i, UnrealMip.DataPtr);
            }
        }

        virtual UINT GetStride(const FMipmapBase& Mip) const override { return Mip.USize * sizeof(ConvertedTextureData::PixelFormat); }
        virtual DXGI_FORMAT GetDXGIFormat() const override { return DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM; }
        virtual bool WantsBuffer() const override { return false; }
    private:
        ConvertedTextureData& m_Buffer;
    };

    class FormatConverterP8 : public IFormatConverter
    {
    public:
        explicit FormatConverterP8(ConvertedTextureData& Buffer) : m_Buffer(Buffer) {};
        virtual void Convert(const FTextureInfo& Texture, const DWORD PolyFlags) override
        {
            assert(Texture.Format == ETextureFormat::TEXF_P8);
            assert(Texture.Palette);

            FColor* const Palette = Texture.Palette;
            
            // Palette color 0 is the alpha mask color; make that index black w. alpha 0 (black looks best for the border that gets left after masking)
            if (PolyFlags & PF_Masked)
            {
                Palette[0].R = Palette[0].G = Palette[0].B = Palette[0].A = 0;
            }

            m_Buffer.Resize(Texture, *this);
            for (INT i = 0; i < Texture.NumMips; i++)
            {
                assert(Texture.Mips[i]);
                const FMipmapBase& UnrealMip = *Texture.Mips[i];

                const auto* const pSourceBegin = UnrealMip.DataPtr;
                const auto* const pSourceEnd = UnrealMip.DataPtr + UnrealMip.USize * UnrealMip.VSize;
                auto pDest = m_Buffer.GetMipBuffer(i);

                for (const auto* pSource = pSourceBegin; pSource != pSourceEnd; pSource++, pDest++)
                {
                    *pDest = reinterpret_cast<uint32_t&>(Palette[*pSource]);
                }
            }
        }

        virtual UINT GetStride(const FMipmapBase& Mip) const override { return Mip.USize * sizeof(ConvertedTextureData::PixelFormat); }
        virtual DXGI_FORMAT GetDXGIFormat() const override { return DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM; }
        virtual bool WantsBuffer() const override { return true; }
    private:
        ConvertedTextureData& m_Buffer;
    };

    class FormatConverterDXT : public FormatConverterIdentity
    {
    public:
        using FormatConverterIdentity::FormatConverterIdentity;
        virtual UINT GetStride(const FMipmapBase& Mip) const override { return (Mip.USize + sm_iBlockSizeInPixels - 1) / sm_iBlockSizeInPixels / sm_iBlockSizeInBytes; }
        virtual DXGI_FORMAT GetDXGIFormat() const override { return DXGI_FORMAT::DXGI_FORMAT_BC1_UNORM; }
    private:
        static const size_t sm_iBlockSizeInPixels = 4;
        static const size_t sm_iBlockSizeInBytes = 8;
    };

    FormatConverterIdentity m_FormatConverterIdentity = FormatConverterIdentity(m_ConvertedTextureData);
    FormatConverterP8 m_FormatConverterP8 = FormatConverterP8(m_ConvertedTextureData);
    FormatConverterDXT m_FormatConverterDXT = FormatConverterDXT(m_ConvertedTextureData);
    std::array<IFormatConverter*, 6> const m_FormatConverters = {
        &m_FormatConverterP8, // TEXF_P8
        &m_FormatConverterIdentity, // TEXF_RGBA7
        nullptr, // TEXF_RGB16
        &m_FormatConverterDXT, // TEXF_DXT1
        nullptr, //TEXF_RGB8
        nullptr //TEXF_RGBA8
    };

    TextureData m_PlaceholderTexture; // Placeholder texture for when unable to convert
};