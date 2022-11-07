#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <codecvt>
#include <locale>

#include "Scene.h"
#include "Helpers.h"
#include "mikktspace.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include <tiny_gltf.h>

#define UNUSED_COLOR XMFLOAT4(1.f, 0.f, 1.f, 1.f)
#define STRIP_BREAK static_cast<uint32_t>(-1)

using namespace DirectX;

namespace Log
{
    enum ELoggingLevel
    {
        eError,
        eWarning,
        eDebug,
        eInfo
    };

    static std::wstringstream wstream;

    extern ELoggingLevel sLoggingLevel = Log::eInfo;

    const wchar_t* LogLevelToString(ELoggingLevel level)
    {
        switch (level)
        {
        case eDebug:
            return L"Debug";
        case eInfo:
            return L"Info";
        case eWarning:
            return L"Warning";
        case eError:
            return L"Error";
        default:
            return L"Uknown";
        }
    }

    void Flush()
    {
        std::wofstream outFile("Decor.log");
        outFile << wstream.str();
        outFile.close();
        wstream.clear();
    }

    template <typename... Args>
    void Write(ELoggingLevel msgLevel, const wchar_t* msg, Args... args)
    {
        if (sLoggingLevel < msgLevel)
            return;

        wchar_t tmpBuff1[1024] = {};
        swprintf_s(tmpBuff1, msg, args...);

        wchar_t tmpBuff2[1024] = {};
        swprintf_s(tmpBuff2, L"[% 7s] %s\n", LogLevelToString(msgLevel), tmpBuff1);
        
        wstream << tmpBuff2;
    }


    template <typename... Args>
    void Debug(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eDebug, msg, args...);
    }


    template <typename... Args>
    void Info(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eInfo, msg, args...);
    }


    template <typename... Args>
    void Warning(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eWarning, msg, args...);
    }


    template <typename... Args>
    void Error(const wchar_t* msg, Args... args)
    {
        Write(ELoggingLevel::eError, msg, args...);
    }
};

namespace Utils
{
    template<class T, class U = T>
    T Exchange(T& obj, U&& new_value)
    {
        T old_value = std::move(obj);
        obj = std::forward<U>(new_value);
        return old_value;
    }

    template <typename T>
    void ReleaseAndMakeNull(T& value)
    {
        if (value)
        {
            value->Release();
            value = nullptr;
        }
    }

    template <typename T>
    void SafeAddRef(T& value)
    {
        if (value)
            value->AddRef();
    }

    template <typename T>
    inline T ToggleBits(const T& aVal, const T& aMask)
    {
        return static_cast<T>(aVal ^ aMask);
    }

    template <typename T>
    inline T Lerp(const T& min, const T& max, const float c)
    {
        return (1.f - c) * min + c * max;
    }

    std::wstring GetFilePathExt(const std::wstring &path)
    {
        const auto lastDot = path.find_last_of(L".");
        if (lastDot != std::wstring::npos)
            return path.substr(lastDot + 1);
        else
            return L"";
    };

    std::string GetFilePathExt(const std::string& path)
    {
        const auto lastDot = path.find_last_of(".");
        if (lastDot != std::string::npos)
            return path.substr(lastDot + 1);
        else
            return "";
    };

    std::string WstringToString(const std::wstring& wideString)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

        return converter.to_bytes(wideString);
    }


    std::wstring StringToWstring(const std::string& string)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

        return converter.from_bytes(string);
    }

    inline const wchar_t* ConfigName()
    {
#ifdef DEBUG
        return L"debug";
#else
        return L"release";
#endif // DEBUG
    }

    float ModX(float x, float y)
    {
        float result = fmod(x, y);
        if (result < 0.0f)
            result += y;

        return result;
    };
};

namespace SceneUtils
{
    bool CreateTextureSrvFromData(IRenderingContext& ctx,
        ID3D11ShaderResourceView*& srv,
        const UINT width,
        const UINT height,
        const DXGI_FORMAT dataFormat,
        const void* data,
        const UINT lineMemPitch)
    {
        auto &device = ctx.GetDevice();
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
        Utils::ReleaseAndMakeNull(tex);
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
        return XMFLOAT4((r / 255.f) * intensity,
            (g / 255.f) * intensity,
            (b / 255.f) * intensity,
            1.0f);
#endif
    };
}

namespace GltfUtils
{
    bool LoadModel(tinygltf::Model& model, const std::wstring& filePath)
    {
        using namespace std;

        // Convert to plain string for tinygltf
        string filePathA = Utils::WstringToString(filePath);

        tinygltf::TinyGLTF tinyGltf;
        string errA, warnA;
        wstring ext = Utils::GetFilePathExt(filePath);

        bool ret = false;
        if (ext.compare(L"glb") == 0)
        {
            Log::Debug(L"Gltf::LoadModel: Reading binary glTF from \"%s\"", filePath.c_str());
            ret = tinyGltf.LoadBinaryFromFile(&model, &errA, &warnA, filePathA);
        }
        else
        {
            Log::Debug(L"Gltf::LoadModel: Reading ASCII glTF from \"%s\"", filePath.c_str());
            ret = tinyGltf.LoadASCIIFromFile(&model, &errA, &warnA, filePathA);
        }

        if (!errA.empty())
            Log::Debug(L"Gltf::LoadModel: Error: %s", Utils::StringToWstring(errA).c_str());

        if (!warnA.empty())
            Log::Debug(L"Gltf::LoadModel: Warning: %s", Utils::StringToWstring(warnA).c_str());

        if (ret)
            Log::Debug(L"Gltf::LoadModel: Succesfully loaded model");
        else
            Log::Error(L"Gltf::LoadModel: Failed to parse glTF file \"%s\"", filePath.c_str());

        return ret;
    }

    D3D11_PRIMITIVE_TOPOLOGY ModeToTopology(int mode)
    {
        switch (mode)
        {
        case TINYGLTF_MODE_POINTS:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case TINYGLTF_MODE_LINE:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case TINYGLTF_MODE_LINE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case TINYGLTF_MODE_TRIANGLES:
            return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case TINYGLTF_MODE_TRIANGLE_STRIP:
            return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            //case TINYGLTF_MODE_LINE_LOOP:
            //case TINYGLTF_MODE_TRIANGLE_FAN:
        default:
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    std::wstring ModeToWstring(int mode)
    {
        if (mode == TINYGLTF_MODE_POINTS)
            return L"POINTS";
        else if (mode == TINYGLTF_MODE_LINE)
            return L"LINE";
        else if (mode == TINYGLTF_MODE_LINE_LOOP)
            return L"LINE_LOOP";
        else if (mode == TINYGLTF_MODE_TRIANGLES)
            return L"TRIANGLES";
        else if (mode == TINYGLTF_MODE_TRIANGLE_FAN)
            return L"TRIANGLE_FAN";
        else if (mode == TINYGLTF_MODE_TRIANGLE_STRIP)
            return L"TRIANGLE_STRIP";
        else
            return L"**UNKNOWN**";
    }

    std::wstring StringIntMapToWstring(const std::map<std::string, int>& m)
    {
        std::stringstream ss;
        bool first = true;
        for (auto item : m)
        {
            if (!first)
                ss << ", ";
            else
                first = false;
            ss << item.first << ": " << item.second;
        }
        return Utils::StringToWstring(ss.str());
    }

    std::wstring TypeToWstring(int ty)
    {
        if (ty == TINYGLTF_TYPE_SCALAR)
            return L"SCALAR";
        else if (ty == TINYGLTF_TYPE_VECTOR)
            return L"VECTOR";
        else if (ty == TINYGLTF_TYPE_VEC2)
            return L"VEC2";
        else if (ty == TINYGLTF_TYPE_VEC3)
            return L"VEC3";
        else if (ty == TINYGLTF_TYPE_VEC4)
            return L"VEC4";
        else if (ty == TINYGLTF_TYPE_MATRIX)
            return L"MATRIX";
        else if (ty == TINYGLTF_TYPE_MAT2)
            return L"MAT2";
        else if (ty == TINYGLTF_TYPE_MAT3)
            return L"MAT3";
        else if (ty == TINYGLTF_TYPE_MAT4)
            return L"MAT4";
        return L"**UNKNOWN**";
    }


    std::wstring ComponentTypeToWstring(int ty)
    {
        if (ty == TINYGLTF_COMPONENT_TYPE_BYTE)
            return L"BYTE";
        else if (ty == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
            return L"UNSIGNED_BYTE";
        else if (ty == TINYGLTF_COMPONENT_TYPE_SHORT)
            return L"SHORT";
        else if (ty == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            return L"UNSIGNED_SHORT";
        else if (ty == TINYGLTF_COMPONENT_TYPE_INT)
            return L"INT";
        else if (ty == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            return L"UNSIGNED_INT";
        else if (ty == TINYGLTF_COMPONENT_TYPE_FLOAT)
            return L"FLOAT";
        else if (ty == TINYGLTF_COMPONENT_TYPE_DOUBLE)
            return L"DOUBLE";

        return L"**UNKNOWN**";
    }
   
    std::wstring ColorToWstring(const XMFLOAT4& color)
    {
        std::wstringstream ss;
        ss << L"[ ";
        ss << color.x << L", ";
        ss << color.y << L", ";
        ss << color.z << L", ";
        ss << color.w;
        ss << L" ]";

        return ss.str();
    }

    std::wstring FloatArrayToWstring(const std::vector<double>& arr)
    {
        if (arr.size() == 0)
            return L"";

        std::stringstream ss;
        ss << "[ ";
        for (size_t i = 0; i < arr.size(); i++)
            ss << arr[i] << ((i != arr.size() - 1) ? ", " : "");
        ss << " ]";

        return Utils::StringToWstring(ss.str());
    }

    std::wstring StringDoubleMapToWstring(const std::map<std::string, double>& mp)
    {
        if (mp.size() == 0)
            return L"";

        std::stringstream ss;
        ss << "[";
        bool first = true;
        for (auto& item : mp)
        {
            ss << (first ? " " : ", ");
            ss << item.first << ": " << item.second;
            first = false;
        }
        ss << " ]";

        return Utils::StringToWstring(ss.str());
    }

    std::wstring ParameterValueToWstring(const tinygltf::Parameter& param)
    {
        if (!param.number_array.empty())
            return FloatArrayToWstring(param.number_array);
        else if (!param.json_double_value.empty())
            return StringDoubleMapToWstring(param.json_double_value);
        else if (param.has_number_value)
        {
            std::wstringstream ss;
            ss << param.number_value;
            return ss.str();
        }
        else
            return Utils::StringToWstring("\"" + param.string_value + "\"");
    }

    bool FloatArrayToColor(XMFLOAT4& color, const std::vector<double>& vector)
    {
        switch (vector.size())
        {
        case 4:
            color = XMFLOAT4((float)vector[0],
                (float)vector[1],
                (float)vector[2],
                (float)vector[3]);
            return true;
        case 3:
            color = XMFLOAT4((float)vector[0],
                (float)vector[1],
                (float)vector[2],
                1.0);
            return true;
        default:
            return false;
        }
    }

    template <int component>
    void FloatToColorComponent(XMFLOAT4& color, double value)
    {
        static_assert(component < 4, "Incorrect color component index!");

        switch (component)
        {
        case 0: color.x = (float)value; break;
        case 1: color.y = (float)value; break;
        case 2: color.z = (float)value; break;
        case 3: color.w = (float)value; break;
        }
    }
}

namespace TangentCalculator
{    
    ScenePrimitive& GetPrimitive(const SMikkTSpaceContext* context)
    {
        return *static_cast<ScenePrimitive*>(context->m_pUserData);
    }

    int getNumFaces(const SMikkTSpaceContext* context)
    {
        return (int)GetPrimitive(context).GetFacesCount();
    }

    int getNumVerticesOfFace(const SMikkTSpaceContext* context,
        const int face)
    {
        face; // unused param

        return (int)GetPrimitive(context).GetVerticesPerFace();
    }

    void getPosition(const SMikkTSpaceContext* context,
        float outpos[],
        const int face,
        const int vertex)
    {
        GetPrimitive(context).GetPosition(outpos, face, vertex);
    }

    void getNormal(const SMikkTSpaceContext* context,
        float outnormal[],
        const int face,
        const int vertex)
    {
        GetPrimitive(context).GetNormal(outnormal, face, vertex);
    }

    void getTexCoord(const SMikkTSpaceContext* context,
        float outuv[],
        const int face,
        const int vertex)
    {
        GetPrimitive(context).GetTextCoord(outuv, face, vertex);
    }

    void setTSpaceBasic(const SMikkTSpaceContext* context,
        const float tangent[],
        const float sign,
        const int face,
        const int vertex)
    {
        GetPrimitive(context).SetTangent(tangent, sign, face, vertex);
    }

    bool Calculate(ScenePrimitive& primitive)
    {
        SMikkTSpaceInterface iface;
        iface.m_getNumFaces = getNumFaces;
        iface.m_getNumVerticesOfFace = getNumVerticesOfFace;
        iface.m_getPosition = getPosition;
        iface.m_getNormal = getNormal;
        iface.m_getTexCoord = getTexCoord;
        iface.m_setTSpaceBasic = setTSpaceBasic;
        iface.m_setTSpace = nullptr;

        SMikkTSpaceContext context;
        context.m_pInterface = &iface;
        context.m_pUserData = &primitive;

        return genTangSpaceDefault(&context) == 1;
    }
}

const std::vector<D3D11_INPUT_ELEMENT_DESC> sVertexLayoutDesc =
{
    D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    D3D11_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

struct CbScene
{
    XMMATRIX ViewMtrx;
    XMFLOAT4 CameraPos;
    XMMATRIX ProjectionMtrx;
};

struct CbFrame
{
    XMFLOAT4 AmbientLightLuminance;

    XMFLOAT4 DirectLightDirs[DIRECT_LIGHTS_MAX_COUNT];
    XMFLOAT4 DirectLightLuminances[DIRECT_LIGHTS_MAX_COUNT];

    XMFLOAT4 PointLightPositions[POINT_LIGHTS_MAX_COUNT];
    XMFLOAT4 PointLightIntensities[POINT_LIGHTS_MAX_COUNT];

    int32_t  DirectLightsCount; // at the end to avoid 16-byte packing issues
    int32_t  PointLightsCount;  // at the end to avoid 16-byte packing issues
    int32_t  dummy_padding[2];  // padding to 16 bytes multiple
};

struct CbSceneNode
{
    XMMATRIX WorldMtrx;
    XMFLOAT4 MeshColor; // May be eventually replaced by the emmisive component of the standard surface shader
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

Scene::Scene(const std::wstring sceneFilePath) :
    mSceneFilePath(sceneFilePath)
{
    mViewData.eye = XMVectorSet(0.0f,  4.0f, 10.0f, 1.0f);
    mViewData.at  = XMVectorSet(0.0f, -0.2f,  0.0f, 1.0f);
    mViewData.up  = XMVectorSet(0.0f,  1.0f,  0.0f, 1.0f);
}

Scene::~Scene()
{
    Destroy();
    Log::Flush();
}


bool Scene::Init(IRenderingContext &ctx)
{
    if (!ctx.IsValid())
        return false;

    uint32_t wndWidth, wndHeight;
    if (!ctx.GetWindowSize(wndWidth, wndHeight))
        return false;

    if (wndWidth < 1200u)
        wndWidth = 1200u;
    if (wndHeight < 900u)
        wndHeight = 900u;

    auto &device = ctx.GetDevice();
    auto &deviceContext = ctx.GetDeviceContext();

    HRESULT hr = S_OK;

    // Vertex shader
    ID3DBlob* pVsBlob = nullptr;
    if (!ctx.CreateVertexShader(L"Decor/Scene.hlsl", "VS", "vs_4_0", pVsBlob, mVertexShader))
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

    // Pixel shaders
    if (!ctx.CreatePixelShader(L"Decor/Scene.hlsl", "PsPbrMetalness", "ps_4_0", mPsPbrMetalness))
        return false;
    if (!ctx.CreatePixelShader(L"Decor/Scene.hlsl", "PsPbrSpecularity", "ps_4_0", mPsPbrSpecularity))
        return false;
    if (!ctx.CreatePixelShader(L"Decor/Scene.hlsl", "PsConstEmissive", "ps_4_0", mPsConstEmmisive))
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

    if (!mDefaultMaterial.CreatePbrSpecularity(ctx,
                                               nullptr,
                                               XMFLOAT4(0.5f, 0.5f, 0.5f, 1.f),
                                               nullptr,
                                               XMFLOAT4(0.f, 0.f, 0.f, 1.f)))
        return false;

    // Matrices
    mViewMtrx = XMMatrixLookAtLH(mViewData.eye, mViewData.at, mViewData.up);
    mProjectionMtrx = XMMatrixPerspectiveFovLH(XM_PIDIV4,
                                               (FLOAT)wndWidth / wndHeight,
                                               0.01f, 100.0f);

    // Scene constant buffer can be updated now
    CbScene cbScene;
    cbScene.ViewMtrx = XMMatrixTranspose(mViewMtrx);
    XMStoreFloat4(&cbScene.CameraPos, mViewData.eye);
    cbScene.ProjectionMtrx = XMMatrixTranspose(mProjectionMtrx);
    deviceContext.UpdateSubresource(mCbScene, 0, NULL, &cbScene, 0, 0);

    return true;
}

bool Scene::LoadExternal(IRenderingContext &ctx, const std::wstring &filePath)
{
    const auto fileExt = Utils::GetFilePathExt(filePath);
    if ((fileExt.compare(L"glb") == 0) ||
        (fileExt.compare(L"gltf") == 0))
    {
        return LoadGLTF(ctx, filePath);
    }
    else
    {
        Log::Error(L"The scene file has an unsupported file format extension (%s)!", fileExt.c_str());
        return false;
    }
}



const tinygltf::Accessor& GetPrimitiveAttrAccessor(bool &accessorLoaded,
                                                   const tinygltf::Model &model,
                                                   const std::map<std::string, int> &attributes,
                                                   const int primitiveIdx,
                                                   bool requiredData,
                                                   const std::string &attrName,
                                                   const std::wstring &logPrefix)
{
    static tinygltf::Accessor dummyAccessor;

    const auto attrIt = attributes.find(attrName);
    if (attrIt == attributes.end())
    {
        Log::Write(requiredData ? Log::eError : Log::eDebug,
                   L"%sNo %s attribute present in primitive %d!",
                   logPrefix.c_str(),
                   Utils::StringToWstring(attrName).c_str(),
                   primitiveIdx);
        accessorLoaded = false;
        return dummyAccessor;
    }

    const auto accessorIdx = attrIt->second;
    if ((accessorIdx < 0) || (accessorIdx >= model.accessors.size()))
    {
        Log::Error(L"%sInvalid %s accessor index (%d/%d)!",
                   logPrefix.c_str(),
                   Utils::StringToWstring(attrName).c_str(),
                   accessorIdx,
                   model.accessors.size());
        accessorLoaded = false;
        return dummyAccessor;
    }

    accessorLoaded = true;
    return model.accessors[accessorIdx];
}

template <typename ComponentType,
          size_t ComponentCount,
          typename TDataConsumer>
bool IterateGltfAccesorData(const tinygltf::Model &model,
                            const tinygltf::Accessor &accessor,
                            TDataConsumer DataConsumer,
                            const wchar_t *logPrefix,
                            const wchar_t *logDataName)
{
    Log::Debug(L"%s%s accesor \"%s\": view %d, offset %d, type %s<%s>, count %d",
               logPrefix,
               logDataName,
               Utils::StringToWstring(accessor.name).c_str(),
               accessor.bufferView,
               accessor.byteOffset,
               GltfUtils::TypeToWstring(accessor.type).c_str(),
               GltfUtils::ComponentTypeToWstring(accessor.componentType).c_str(),
               accessor.count);

    // Buffer view

    const auto bufferViewIdx = accessor.bufferView;

    if ((bufferViewIdx < 0) || (bufferViewIdx >= model.bufferViews.size()))
    {
        Log::Error(L"%sInvalid %s view buffer index (%d/%d)!",
                   logPrefix, logDataName, bufferViewIdx, model.bufferViews.size());
        return false;
    }

    const auto &bufferView = model.bufferViews[bufferViewIdx];

    //Log::Debug(L"%s%s buffer view %d \"%s\": buffer %d, offset %d, length %d, stride %d, target %s",
    //           logPrefix,
    //           logDataName,
    //           bufferViewIdx,
    //           Utils::StringToWstring(bufferView.name).c_str(),
    //           bufferView.buffer,
    //           bufferView.byteOffset,
    //           bufferView.byteLength,
    //           bufferView.byteStride,
    //           GltfUtils::TargetToWstring(bufferView.target).c_str());

    // Buffer

    const auto bufferIdx = bufferView.buffer;

    if ((bufferIdx < 0) || (bufferIdx >= model.buffers.size()))
    {
        Log::Error(L"%sInvalid %s buffer index (%d/%d)!",
                   logPrefix, logDataName, bufferIdx, model.buffers.size());
        return false;
    }

    const auto &buffer = model.buffers[bufferIdx];

    const auto byteEnd = bufferView.byteOffset + bufferView.byteLength;
    if (byteEnd > buffer.data.size())
    {
        Log::Error(L"%sAccessing data chunk outside %s buffer %d!",
                   logPrefix, logDataName, bufferIdx);
        return false;
    }

    //Log::Debug(L"%s%s buffer %d \"%s\": data %x, size %d, uri \"%s\"",
    //           logPrefix,
    //           logDataName,
    //           bufferIdx,
    //           Utils::StringToWstring(buffer.name).c_str(),
    //           buffer.data.data(),
    //           buffer.data.size(),
    //           Utils::StringToWstring(buffer.uri).c_str());

    // TODO: Check that buffer view is large enough to contain all data from accessor?

    // Data

    const auto componentSize = sizeof(ComponentType);
    const auto typeSize = ComponentCount * componentSize;
    const auto stride = bufferView.byteStride;
    const auto typeOffset = (stride == 0) ? typeSize : stride;

    auto ptr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    int idx = 0;
    for (; idx < accessor.count; ++idx, ptr += typeOffset)
        DataConsumer(idx, ptr);

    return true;
}


bool Scene::LoadGLTF(IRenderingContext &ctx,
                     const std::wstring &filePath)
{
    using namespace std;

    Log::Debug(L"");
    const std::wstring logPrefix = L"LoadGLTF: ";

    tinygltf::Model model;
    if (!GltfUtils::LoadModel(model, filePath))
        return false;

    Log::Debug(L"");

    if (!LoadMaterialsFromGltf(ctx, model, logPrefix))
        return false;

    if (!LoadSceneFromGltf(ctx, model, logPrefix))
        return false;

    SetupDefaultLights();

    Log::Debug(L"");

    return true;
}


bool Scene::LoadMaterialsFromGltf(IRenderingContext &ctx,
                                  const tinygltf::Model &model,
                                  const std::wstring &logPrefix)
{
    const auto &materials = model.materials;

    Log::Debug(L"%sMaterials: %d", logPrefix.c_str(), materials.size());

    const std::wstring materialLogPrefix = logPrefix + L"   ";
    const std::wstring valueLogPrefix = materialLogPrefix + L"   ";

    mMaterials.clear();
    mMaterials.reserve(materials.size());
    for (size_t matIdx = 0; matIdx < materials.size(); ++matIdx)
    {
        const auto &material = materials[matIdx];

        Log::Debug(L"%s%d/%d \"%s\"",
                   materialLogPrefix.c_str(),
                   matIdx,
                   materials.size(),
                   Utils::StringToWstring(material.name).c_str());

        SceneMaterial sceneMaterial;
        if (!sceneMaterial.LoadFromGltf(ctx, model, material, valueLogPrefix))
            return false;
        mMaterials.push_back(std::move(sceneMaterial));
    }

    return true;
}


bool Scene::LoadSceneFromGltf(IRenderingContext &ctx,
                              const tinygltf::Model &model,
                              const std::wstring &logPrefix)
{
    // Choose one scene
    if (model.scenes.size() < 1)
    {
        Log::Error(L"%sNo scenes present in the model!", logPrefix.c_str());
        return false;
    }
    if (model.scenes.size() > 1)
        Log::Warning(L"%sMore scenes present in the model. Loading just the first one.", logPrefix.c_str());
    const auto &scene = model.scenes[0];

    Log::Debug(L"%sScene 0 \"%s\": %d root node(s)",
               logPrefix.c_str(),
               Utils::StringToWstring(scene.name).c_str(),
               scene.nodes.size());

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

    return true;
}


bool Scene::LoadSceneNodeFromGLTF(IRenderingContext &ctx,
                                  SceneNode &sceneNode,
                                  const tinygltf::Model &model,
                                  int nodeIdx,
                                  const std::wstring &logPrefix)
{
    if (nodeIdx >= model.nodes.size())
    {
        Log::Error(L"%sInvalid node index (%d/%d)!", logPrefix.c_str(), nodeIdx, model.nodes.size());
        return false;
    }

    const auto &node = model.nodes[nodeIdx];

    // Node itself
    if (!sceneNode.LoadFromGLTF(ctx, model, node, nodeIdx, logPrefix))
        return false;

    // Children
    sceneNode.mChildren.clear();
    sceneNode.mChildren.reserve(node.children.size());
    const std::wstring &childLogPrefix = logPrefix + L"   ";
    for (const auto childIdx : node.children)
    {
        if ((childIdx < 0) || (childIdx >= model.nodes.size()))
        {
            Log::Error(L"%sInvalid child node index (%d/%d)!", childLogPrefix.c_str(), childIdx, model.nodes.size());
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


const SceneMaterial& Scene::GetMaterial(const ScenePrimitive &primitive) const
{
    const int idx = primitive.GetMaterialIdx();

    if (idx >= 0 && idx < mMaterials.size())
        return mMaterials[idx];
    else
        return mDefaultMaterial;
}


void Scene::Destroy()
{
    Utils::ReleaseAndMakeNull(mVertexShader);

    Utils::ReleaseAndMakeNull(mPsPbrMetalness);
    Utils::ReleaseAndMakeNull(mPsPbrSpecularity);
    Utils::ReleaseAndMakeNull(mPsConstEmmisive);

    Utils::ReleaseAndMakeNull(mVertexLayout);

    Utils::ReleaseAndMakeNull(mCbScene);
    Utils::ReleaseAndMakeNull(mCbFrame);
    Utils::ReleaseAndMakeNull(mCbSceneNode);
    Utils::ReleaseAndMakeNull(mCbScenePrimitive);

    Utils::ReleaseAndMakeNull(mSamplerLinear);

    mRootNodes.clear();
}


void Scene::AnimateFrame(IRenderingContext &ctx)
{
    if (!ctx.IsValid())
        return;

    // debug: Materials
    for (auto &material : mMaterials)
        material.Animate(ctx);

    // Scene geometry
    for (auto &node : mRootNodes)
        node.Animate(ctx);

    // Directional lights (are steady for now)
    for (auto &dirLight : mDirectLights)
        dirLight.dirTransf = dirLight.dir;

    // Animate point lights (harwired animation for now)
    
    const float time = 0.0f; ///////ctx.GetFrameAnimationTime();
    const float period = 15.f; //seconds
    const float totalAnimPos = time / period;
    const float angle = totalAnimPos * XM_2PI;

    const auto pointCount = mPointLights.size();
    for (int i = 0; i < pointCount; i++)
    {
        const float lightRelOffsetCircular = (float)i / pointCount;
        const float lightRelOffsetInterval = (float)i / (pointCount - 1);

        const float rotationAngle = -2.f * angle - lightRelOffsetCircular * XM_2PI;
        const float orbitInclination = Utils::Lerp(mPointLights[i].orbitInclinationMin,
                                                   mPointLights[i].orbitInclinationMax,
                                                   lightRelOffsetInterval);

        const XMMATRIX translationMtrx  = XMMatrixTranslation(mPointLights[i].orbitRadius, 0.f, 0.f);
        const XMMATRIX rotationMtrx     = XMMatrixRotationY(rotationAngle);
        const XMMATRIX inclinationMtrx  = XMMatrixRotationZ(orbitInclination);
        const XMMATRIX transfMtrx = translationMtrx * rotationMtrx * inclinationMtrx;

        const XMFLOAT4 basePos{ 0.f, 0.f, 0.f, 0.f };
        const XMVECTOR lightVec = XMLoadFloat4(&basePos);
        const XMVECTOR lightVecTransf = XMVector3Transform(lightVec, transfMtrx);
        XMStoreFloat4(&mPointLights[i].posTransf, lightVecTransf);
    }
    
}


void Scene::RenderFrame(IRenderingContext &ctx)
{
    assert(&ctx);

    //if (mPointLights.size() > POINT_LIGHTS_MAX_COUNT)
    //    return;
    //if (mDirectLights.size() > DIRECT_LIGHTS_MAX_COUNT)
    //    return;

    auto &deviceContext = ctx.GetDeviceContext();

    // Frame constant buffer
    CbFrame cbFrame;
    cbFrame.AmbientLightLuminance = mAmbientLight.luminance;
    cbFrame.DirectLightsCount = (int32_t)mDirectLights.size();
    for (int i = 0; i < cbFrame.DirectLightsCount; i++)
    {
        cbFrame.DirectLightDirs[i]       = mDirectLights[i].dirTransf;
        cbFrame.DirectLightLuminances[i] = mDirectLights[i].luminance;
    }
    cbFrame.PointLightsCount = (int32_t)mPointLights.size();
    for (int i = 0; i < cbFrame.PointLightsCount; i++)
    {
        cbFrame.PointLightPositions[i]   = mPointLights[i].posTransf;
        cbFrame.PointLightIntensities[i] = mPointLights[i].intensity;
    }
    deviceContext.UpdateSubresource(mCbFrame, 0, nullptr, &cbFrame, 0, 0);

    // Setup vertex shader
    deviceContext.VSSetShader(mVertexShader, nullptr, 0);
    deviceContext.VSSetConstantBuffers(0, 1, &mCbScene);
    deviceContext.VSSetConstantBuffers(1, 1, &mCbFrame);
    deviceContext.VSSetConstantBuffers(2, 1, &mCbSceneNode);

    // Setup pixel shader data (shader itself is chosen later for each material)
    deviceContext.PSSetConstantBuffers(0, 1, &mCbScene);
    deviceContext.PSSetConstantBuffers(1, 1, &mCbFrame);
    deviceContext.PSSetConstantBuffers(2, 1, &mCbSceneNode);
    deviceContext.PSSetConstantBuffers(3, 1, &mCbScenePrimitive);
    deviceContext.PSSetSamplers(0, 1, &mSamplerLinear);

    // Scene geometry
    for (auto &node : mRootNodes)
        RenderNode(ctx, node, XMMatrixIdentity());

    //// Proxy geometry for point lights
    //for (int i = 0; i < mPointLights.size(); i++)
    //{
    //    CbSceneNode cbSceneNode;

    //    const float radius = 0.07f;
    //    XMMATRIX lightScaleMtrx = XMMatrixScaling(radius, radius, radius);
    //    XMMATRIX lightTrnslMtrx = XMMatrixTranslationFromVector(XMLoadFloat4(&mPointLights[i].posTransf));
    //    XMMATRIX lightMtrx = lightScaleMtrx * lightTrnslMtrx;
    //    cbSceneNode.WorldMtrx = XMMatrixTranspose(lightMtrx);

    //    const float radius2 = radius * radius;
    //    cbSceneNode.MeshColor = {
    //        mPointLights[i].intensity.x / radius2,
    //        mPointLights[i].intensity.y / radius2,
    //        mPointLights[i].intensity.z / radius2,
    //        mPointLights[i].intensity.w / radius2,
    //    };

    //    immCtx.UpdateSubresource(mCbSceneNode, 0, nullptr, &cbSceneNode, 0, 0);

    //    immCtx.PSSetShader(mPsConstEmmisive, nullptr, 0);
    //}
}


void Scene::SetupDefaultLights()
{
    const uint8_t amb = 120;
    mAmbientLight.luminance = SceneUtils::SrgbColorToFloat(amb, amb, amb, 1.0f);

    const float lum = 3.0f;
    mDirectLights.resize(1);
    mDirectLights[0].dir = XMFLOAT4(1.f, 1.f, 1.f, 1.0f);
    mDirectLights[0].luminance = XMFLOAT4(lum, lum, lum, 1.0f);

    SetupPointLights(3);
}


bool Scene::SetupPointLights(size_t count,
                             float intensity,
                             float orbitRadius,
                             float orbitInclMin,
                             float orbitInclMax)
{
    if (count > POINT_LIGHTS_MAX_COUNT)
    {
        Log::Error(L"SetupPointLights: requested number of point lights (%d) exceeds the limit (%d)", count, POINT_LIGHTS_MAX_COUNT);
        return false;
    }

    mPointLights.resize(count);

    for (auto &light : mPointLights)
    {
        light.intensity = XMFLOAT4(intensity, intensity, intensity, 1.0f);
        light.orbitRadius = orbitRadius;
        light.orbitInclinationMin = orbitInclMin;
        light.orbitInclinationMax = orbitInclMax;
    }

    return true;
}


bool Scene::SetupPointLights(const std::initializer_list<XMFLOAT4> &intensities,
                             float orbitRadius,
                             float orbitInclMin,
                             float orbitInclMax)
{
    if (intensities.size() > POINT_LIGHTS_MAX_COUNT)
    {
        Log::Error(L"SetupPointLights: requested number of point lights (%d) exceeds the limit (%d)",
                   intensities.size(), POINT_LIGHTS_MAX_COUNT);
        return false;
    }

    if (intensities.size() != intensities.size())
    {
        Log::Error(L"SetupPointLights: provided intensities count (%d) doesn't match the light count (%d)",
                   intensities.size(), POINT_LIGHTS_MAX_COUNT);
        return false;
    }

    mPointLights.resize(intensities.size());

    auto itLight = mPointLights.begin();
    auto itIntensity = intensities.begin();
    for (; itLight != mPointLights.end() && itIntensity != intensities.end(); ++itLight, ++itIntensity)
    {
        auto &light = *itLight;

        light.intensity = *itIntensity;
        light.orbitRadius = orbitRadius;
        light.orbitInclinationMin = orbitInclMin;
        light.orbitInclinationMax = orbitInclMax;
    }

    return true;
}


void Scene::AddScaleToRoots(double scale)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddScale(scale);
}


void Scene::AddScaleToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddScale(vec);
}


void Scene::AddRotationQuaternionToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddRotationQuaternion(vec);
}


void Scene::AddTranslationToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddTranslation(vec);
}


void Scene::AddMatrixToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddMatrix(vec);
}


void Scene::RenderNode(IRenderingContext &ctx,
                       const SceneNode &node,
                       const XMMATRIX &parentWorldMtrx)
{
    assert(&ctx);

    auto &deviceContext = ctx.GetDeviceContext();
    assert(&deviceContext);

    const auto worldMtrx = node.GetWorldMtrx() * parentWorldMtrx;

    // Per-node constant buffer
    CbSceneNode cbSceneNode;
    cbSceneNode.WorldMtrx = XMMatrixTranspose(worldMtrx);
    cbSceneNode.MeshColor = { 0.f, 1.f, 0.f, 1.f, };
    deviceContext.UpdateSubresource(mCbSceneNode, 0, nullptr, &cbSceneNode, 0, 0);

    // Draw current node
    for (auto &primitive : node.mPrimitives)
    {
        auto &material = GetMaterial(primitive);

        switch (material.GetWorkflow())
        {
        case MaterialWorkflow::kPbrMetalness:
        {
            deviceContext.PSSetShader(mPsPbrMetalness, nullptr, 0);
            deviceContext.PSSetShaderResources(0, 1, &material.GetBaseColorTexture().srv);
            deviceContext.PSSetShaderResources(1, 1, &material.GetMetallicRoughnessTexture().srv);
            deviceContext.PSSetShaderResources(4, 1, &material.GetNormalTexture().srv);
            deviceContext.PSSetShaderResources(5, 1, &material.GetOcclusionTexture().srv);
            deviceContext.PSSetShaderResources(6, 1, &material.GetEmissionTexture().srv);

            CbScenePrimitive cbScenePrimitive;
            cbScenePrimitive.BaseColorFactor            = material.GetBaseColorFactor();
            cbScenePrimitive.MetallicRoughnessFactor    = material.GetMetallicRoughnessFactor();
            cbScenePrimitive.DiffuseColorFactor         = UNUSED_COLOR;
            cbScenePrimitive.SpecularFactor             = UNUSED_COLOR;
            cbScenePrimitive.NormalTexScale             = material.GetNormalTexture().GetScale();
            cbScenePrimitive.OcclusionTexStrength       = material.GetOcclusionTexture().GetStrength();
            cbScenePrimitive.EmissionFactor             = material.GetEmissionFactor();
            deviceContext.UpdateSubresource(mCbScenePrimitive, 0, nullptr, &cbScenePrimitive, 0, 0);
            break;
        }
        case MaterialWorkflow::kPbrSpecularity:
        {
            deviceContext.PSSetShader(mPsPbrSpecularity, nullptr, 0);
            deviceContext.PSSetShaderResources(2, 1, &material.GetBaseColorTexture().srv);
            deviceContext.PSSetShaderResources(3, 1, &material.GetSpecularTexture().srv);
            deviceContext.PSSetShaderResources(4, 1, &material.GetNormalTexture().srv);
            deviceContext.PSSetShaderResources(5, 1, &material.GetOcclusionTexture().srv);
            deviceContext.PSSetShaderResources(6, 1, &material.GetEmissionTexture().srv);

            CbScenePrimitive cbScenePrimitive;
            cbScenePrimitive.DiffuseColorFactor         = material.GetBaseColorFactor();
            cbScenePrimitive.SpecularFactor             = material.GetSpecularFactor();
            cbScenePrimitive.BaseColorFactor            = UNUSED_COLOR;
            cbScenePrimitive.MetallicRoughnessFactor    = UNUSED_COLOR;
            cbScenePrimitive.NormalTexScale             = material.GetNormalTexture().GetScale();
            cbScenePrimitive.OcclusionTexStrength       = material.GetOcclusionTexture().GetStrength();
            cbScenePrimitive.EmissionFactor             = material.GetEmissionFactor();
            deviceContext.UpdateSubresource(mCbScenePrimitive, 0, nullptr, &cbScenePrimitive, 0, 0);
            break;
        }
        default:
            continue;
        }

        primitive.DrawGeometry(ctx, mVertexLayout);
    }

    // Children
    for (auto &child : node.mChildren)
        RenderNode(ctx, child, worldMtrx);
}

bool Scene::GetAmbientColor(float(&rgba)[4])
{
    rgba[0] = mAmbientLight.luminance.x;
    rgba[1] = mAmbientLight.luminance.y;
    rgba[2] = mAmbientLight.luminance.z;
    rgba[3] = mAmbientLight.luminance.w;
    return true;
}


ScenePrimitive::ScenePrimitive()
{}

ScenePrimitive::ScenePrimitive(const ScenePrimitive &src) :
    mVertices(src.mVertices),
    mIndices(src.mIndices),
    mTopology(src.mTopology),
    mIsTangentPresent(src.mIsTangentPresent),
    mVertexBuffer(src.mVertexBuffer),
    mIndexBuffer(src.mIndexBuffer),
    mMaterialIdx(src.mMaterialIdx)
{
    // We are creating new references of device resources
    Utils::SafeAddRef(mVertexBuffer);
    Utils::SafeAddRef(mIndexBuffer);
}

ScenePrimitive::ScenePrimitive(ScenePrimitive &&src) :
    mVertices(std::move(src.mVertices)),
    mIndices(std::move(src.mIndices)),
    mIsTangentPresent(Utils::Exchange(src.mIsTangentPresent, false)),
    mTopology(Utils::Exchange(src.mTopology, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)),
    mVertexBuffer(Utils::Exchange(src.mVertexBuffer, nullptr)),
    mIndexBuffer(Utils::Exchange(src.mIndexBuffer, nullptr)),
    mMaterialIdx(Utils::Exchange(src.mMaterialIdx, -1))
{}

ScenePrimitive& ScenePrimitive::operator =(const ScenePrimitive &src)
{
    mVertices = src.mVertices;
    mIndices = src.mIndices;
    mIsTangentPresent = src.mIsTangentPresent;
    mTopology = src.mTopology;
    mVertexBuffer = src.mVertexBuffer;
    mIndexBuffer = src.mIndexBuffer;

    // We are creating new references of device resources
    Utils::SafeAddRef(mVertexBuffer);
    Utils::SafeAddRef(mIndexBuffer);

    mMaterialIdx = src.mMaterialIdx;

    return *this;
}

ScenePrimitive& ScenePrimitive::operator =(ScenePrimitive &&src)
{
    mVertices = std::move(src.mVertices);
    mIndices = std::move(src.mIndices);
    mIsTangentPresent = Utils::Exchange(src.mIsTangentPresent, false);
    mTopology = Utils::Exchange(src.mTopology, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    mVertexBuffer = Utils::Exchange(src.mVertexBuffer, nullptr);
    mIndexBuffer = Utils::Exchange(src.mIndexBuffer, nullptr);

    mMaterialIdx = Utils::Exchange(src.mMaterialIdx, -1);

    return *this;
}

ScenePrimitive::~ScenePrimitive()
{
    Destroy();
}

bool ScenePrimitive::LoadFromGLTF(IRenderingContext & ctx,
                                  const tinygltf::Model &model,
                                  const tinygltf::Mesh &mesh,
                                  const int primitiveIdx,
                                  const std::wstring &logPrefix)
{
    if (!LoadDataFromGLTF(model, mesh, primitiveIdx, logPrefix))
        return false;
        //throw new std::exception("Failed to load data from GLTF!");
    if (!CreateDeviceBuffers(ctx))
        return false;
        //throw new std::exception("Failed to create device buffers!");

    return true;
}


bool ScenePrimitive::LoadDataFromGLTF(const tinygltf::Model &model,
                                      const tinygltf::Mesh &mesh,
                                      const int primitiveIdx,
                                      const std::wstring &logPrefix)
{
    bool success = false;
    const auto &primitive = mesh.primitives[primitiveIdx];
    const auto &attrs = primitive.attributes;
    const auto subItemsLogPrefix = logPrefix + L"   ";
    const auto dataConsumerLogPrefix = subItemsLogPrefix + L"   ";

    Log::Debug(L"%sPrimitive %d/%d: mode %s, attributes [%s], indices %d, material %d",
               logPrefix.c_str(),
               primitiveIdx,
               mesh.primitives.size(),
               GltfUtils::ModeToWstring(primitive.mode).c_str(),
               GltfUtils::StringIntMapToWstring(primitive.attributes).c_str(),
               primitive.indices,
               primitive.material);

    // Positions

    auto &posAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                 true, "POSITION", subItemsLogPrefix.c_str());
    if (!success)
        return false;

    if ((posAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
        (posAccessor.type != TINYGLTF_TYPE_VEC3))
    {
        Log::Error(L"%sUnsupported POSITION data type!", subItemsLogPrefix.c_str());
        return false;
    }

    mVertices.clear();
    mVertices.reserve(posAccessor.count);
    if (mVertices.capacity() < posAccessor.count)
    {
        Log::Error(L"%sUnable to allocate %d vertices!", subItemsLogPrefix.c_str(), posAccessor.count);
        mVertices.clear();
        return false;
    }

    auto PositionDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
    {
        auto pos = *reinterpret_cast<const XMFLOAT3*>(ptr);

        itemIdx; // unused param
        //Log::Debug(L"%s%d: pos [%.4f, %.4f, %.4f]",
        //           dataConsumerLogPrefix.c_str(),
        //           itemIdx,
        //           pos.x, pos.y, pos.z);

        mVertices.push_back(SceneVertex{ XMFLOAT3(pos.x, pos.y, pos.z),
                                         XMFLOAT3(0.0f, 0.0f, 1.0f), // TODO: Leave invalid?
                                         XMFLOAT4(1.0f, 0.5f, 0.0f, 1.0f),  // debug; TODO: Leave invalid?
                                         XMFLOAT2(0.0f, 0.0f) });
    };

    if (!IterateGltfAccesorData<float, 3>(model,
                                          posAccessor,
                                          PositionDataConsumer,
                                          subItemsLogPrefix.c_str(),
                                          L"Position"))
        return false;

    // Normals
    auto &normalAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                    false, "NORMAL", subItemsLogPrefix.c_str());
    if (success)
    {
        if ((normalAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
            (normalAccessor.type != TINYGLTF_TYPE_VEC3))
        {
            Log::Error(L"%sUnsupported NORMAL data type!", subItemsLogPrefix.c_str());
            return false;
        }

        if (normalAccessor.count != posAccessor.count)
        {
            Log::Error(L"%sNormals count (%d) is different from position count (%d)!",
                       subItemsLogPrefix.c_str(), normalAccessor.count, posAccessor.count);
            return false;
        }

        auto NormalDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
        {
            auto normal = *reinterpret_cast<const XMFLOAT3*>(ptr);

            //Log::Debug(L"%s%d: normal [%.4f, %.4f, %.4f]",
            //           dataConsumerLogPrefix.c_str(),
            //           itemIdx, normal.x, normal.y, normal.z);

            mVertices[itemIdx].Normal = normal;
        };

        if (!IterateGltfAccesorData<float, 3>(model,
                                              normalAccessor,
                                              NormalDataConsumer,
                                              subItemsLogPrefix.c_str(),
                                              L"Normal"))
            return false;
    }
    //else
    //{
    //    // No normals provided
    //    // TODO: Generate?
    //}

    // Tangents
    auto &tangentAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                     false, "TANGENT", subItemsLogPrefix.c_str());
    if (success)
    {
        if ((tangentAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
            (tangentAccessor.type != TINYGLTF_TYPE_VEC4))
        {
            Log::Error(L"%sUnsupported TANGENT data type!", subItemsLogPrefix.c_str());
            return false;
        }

        if (tangentAccessor.count != posAccessor.count)
        {
            Log::Error(L"%sTangents count (%d) is different from position count (%d)!",
                       subItemsLogPrefix.c_str(), tangentAccessor.count, posAccessor.count);
            return false;
        }

        auto TangentDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
        {
            auto tangent = *reinterpret_cast<const XMFLOAT4*>(ptr);

            //Log::Debug(L"%s%d: tangent [%7.4f, %7.4f, %7.4f] * %.1f",
            //           dataConsumerLogPrefix.c_str(), itemIdx,
            //           tangent.x, tangent.y, tangent.z, tangent.w);

            if ((tangent.w != 1.f) && (tangent.w != -1.f))
                Log::Warning(L"%s%d: tangent w component (handedness) is not equal to 1 or -1 but to %7.4f",
                           dataConsumerLogPrefix.c_str(), itemIdx, tangent.w);

            mVertices[itemIdx].Tangent = tangent;
        };

        if (!IterateGltfAccesorData<float, 4>(model,
                                              tangentAccessor,
                                              TangentDataConsumer,
                                              subItemsLogPrefix.c_str(),
                                              L"Tangent"))
            return false;

        mIsTangentPresent = true;
    }
    else
    {
        Log::Debug(L"%sTangents are not present", subItemsLogPrefix.c_str());
    }

    // Texture coordinates
    auto &texCoord0Accessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                       false, "TEXCOORD_0", subItemsLogPrefix.c_str());
    if (success)
    {
        if ((texCoord0Accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
            (texCoord0Accessor.type != TINYGLTF_TYPE_VEC2))
        {
            Log::Error(L"%sUnsupported TEXCOORD_0 data type!", subItemsLogPrefix.c_str());
            return false;
        }

        if (texCoord0Accessor.count != posAccessor.count)
        {
            Log::Error(L"%sTexture coords count (%d) is different from position count (%d)!",
                       subItemsLogPrefix.c_str(), texCoord0Accessor.count, posAccessor.count);
            return false;
        }

        auto TexCoord0DataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
        {
            auto texCoord0 = *reinterpret_cast<const XMFLOAT2*>(ptr);

            //Log::Debug(L"%s%d: texCoord0 [%.1f, %.1f]",
            //           dataConsumerLogPrefix.c_str(), itemIdx, texCoord0.x, texCoord0.y);

            mVertices[itemIdx].Tex = texCoord0;
        };        

        if (!IterateGltfAccesorData<float, 2>(model,
                                              texCoord0Accessor,
                                              TexCoord0DataConsumer,
                                              subItemsLogPrefix.c_str(),
                                              L"Texture coordinates"))
            return false;
    }

    // Indices

    const auto indicesAccessorIdx = primitive.indices;
    if (indicesAccessorIdx >= model.accessors.size())
    {
        Log::Error(L"%sInvalid indices accessor index (%d/%d)!",
                   subItemsLogPrefix.c_str(), indicesAccessorIdx, model.accessors.size());
        return false;
    }
    if (indicesAccessorIdx < 0)
    {
        Log::Error(L"%sNon-indexed geometry is not supported!", subItemsLogPrefix.c_str());
        return false;
    }

    const auto &indicesAccessor = model.accessors[indicesAccessorIdx];

    if (indicesAccessor.type != TINYGLTF_TYPE_SCALAR)
    {
        Log::Error(L"%sUnsupported indices data type (must be scalar)!", subItemsLogPrefix.c_str());
        return false;
    }
    if ((indicesAccessor.componentType < TINYGLTF_COMPONENT_TYPE_BYTE) ||
        (indicesAccessor.componentType > TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
    {
        Log::Error(L"%sUnsupported indices data component type (%d)!",
                   subItemsLogPrefix.c_str(), indicesAccessor.componentType);
        return false;
    }

    mIndices.clear();
    mIndices.reserve(indicesAccessor.count);
    if (mIndices.capacity() < indicesAccessor.count)
    {
        Log::Error(L"%sUnable to allocate %d indices!", subItemsLogPrefix.c_str(), indicesAccessor.count);
        return false;
    }

    const auto indicesComponentType = indicesAccessor.componentType;
    auto IndexDataConsumer =
        [this, &dataConsumerLogPrefix, indicesComponentType]
        (int itemIdx, const unsigned char *ptr)
    {
        switch (indicesComponentType)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:              mIndices.push_back(*reinterpret_cast<const int8_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:     mIndices.push_back(*reinterpret_cast<const uint8_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_SHORT:             mIndices.push_back(*reinterpret_cast<const int16_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:    mIndices.push_back(*reinterpret_cast<const uint16_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_INT:               mIndices.push_back(*reinterpret_cast<const int32_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:      mIndices.push_back(*reinterpret_cast<const uint32_t*>(ptr)); break;
        }

        // debug
        itemIdx; // unused param
        //Log::Debug(L"%s%d: %d",
        //           dataConsumerLogPrefix.c_str(),
        //           itemIdx,
        //           mIndices.back());
    };

    // TODO: Wrap into IterateGltfAccesorData(componentType, ...)? std::forward()?
    switch (indicesComponentType)
    {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
        IterateGltfAccesorData<const int8_t, 1>(model,
                                                indicesAccessor,
                                                IndexDataConsumer,
                                                subItemsLogPrefix.c_str(),
                                                L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        IterateGltfAccesorData<uint8_t, 1>(model,
                                           indicesAccessor,
                                           IndexDataConsumer,
                                           subItemsLogPrefix.c_str(),
                                           L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
        IterateGltfAccesorData<int16_t, 1>(model,
                                           indicesAccessor,
                                           IndexDataConsumer,
                                           subItemsLogPrefix.c_str(),
                                           L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        IterateGltfAccesorData<uint16_t, 1>(model,
                                            indicesAccessor,
                                            IndexDataConsumer,
                                            subItemsLogPrefix.c_str(),
                                            L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_INT:
        IterateGltfAccesorData<int32_t, 1>(model,
                                           indicesAccessor,
                                           IndexDataConsumer,
                                           subItemsLogPrefix.c_str(),
                                           L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        IterateGltfAccesorData<uint32_t, 1>(model,
                                            indicesAccessor,
                                            IndexDataConsumer,
                                            subItemsLogPrefix.c_str(),
                                            L"Indices");
        break;
    }
    if (mIndices.size() != indicesAccessor.count)
    {
        Log::Error(L"%sFailed to load indices (loaded %d instead of %d))!",
                   subItemsLogPrefix.c_str(), mIndices.size(), indicesAccessor.count);
        return false;
    }

    // DX primitive topology
    mTopology = GltfUtils::ModeToTopology(primitive.mode);
    if (mTopology == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)
    {
        Log::Error(L"%sUnsupported primitive topology!", subItemsLogPrefix.c_str());
        return false;
    }

    // Material
    const auto matIdx = primitive.material;
    if (matIdx >= 0)
    {
        if (matIdx >= model.materials.size())
        {
            Log::Error(L"%sInvalid material index (%d/%d)!",
                       subItemsLogPrefix.c_str(), matIdx, model.materials.size());
            return false;
        }

        mMaterialIdx = matIdx;
    }

    // reverse indices
    std::reverse(mIndices.begin(), mIndices.end());

#ifndef SIMPLE_RENDER_MODE
    CalculateTangentsIfNeeded(subItemsLogPrefix);
#endif // !1    

    return true;
}


bool ScenePrimitive::CalculateTangentsIfNeeded(const std::wstring &logPrefix)
{
    // TODO: if (material needs tangents && are not present) ... GetMaterial()
    // TODO: Requires position, normal, and texcoords
    // TODO: Only for triangles?
    if (!IsTangentPresent())
    {
        Log::Debug(L"%sComputing tangents...", logPrefix.c_str());

        if (!TangentCalculator::Calculate(*this))
        {
            Log::Error(L"%sTangents computation failed!", logPrefix.c_str());
            return false;
        }
        mIsTangentPresent = true;
    }

    return true;
}

size_t ScenePrimitive::GetVerticesPerFace() const
{
    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
        return 1;
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
        return 2;
    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return 2;
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        return 3;
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        return 3;
    default:
        return 0;
    }
}


size_t ScenePrimitive::GetFacesCount() const
{
    FillFaceStripsCacheIfNeeded();

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        return mIndices.size() / GetVerticesPerFace();

    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        return mFaceStripsTotalCount;

    default:
        return 0; // Unsupported
    }
}


void ScenePrimitive::FillFaceStripsCacheIfNeeded() const
{
    if (mAreFaceStripsCached)
        return;

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    {
        mFaceStrips.clear();

        const auto count = mIndices.size();
        for (size_t i = 0; i < count; )
        {
            // Start
            while ((i < count) && (mIndices[i] == STRIP_BREAK))
            {
                ++i;
            }
            const size_t start = i;

            // Length
            size_t length = 0;
            while ((i < count) && (mIndices[i] != STRIP_BREAK))
            {
                ++length;
                ++i;
            }

            // Strip
            if (length >= GetVerticesPerFace())
            {
                const auto faceCount = length - (GetVerticesPerFace() - 1);
                mFaceStrips.push_back({ start, faceCount });
            }
        }

        mFaceStripsTotalCount = 0;
        for (const auto &strip : mFaceStrips)
            mFaceStripsTotalCount += strip.faceCount;

        mAreFaceStripsCached = true;
        return;
    }

    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    default:
        return;
    }
}


const size_t ScenePrimitive::GetVertexIndex(const int face, const int vertex) const
{
    if (vertex >= GetVerticesPerFace())
        return 0;

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return vertex;

    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    {
        const bool isOdd = (face % 2 == 1);

        if (isOdd && (vertex == 1))
            return 2;
        else if (isOdd && (vertex == 2))
            return 1;
        else
            return vertex;
    }

    default:
        return 0; // Unsupported
    }
}


const SceneVertex& ScenePrimitive::GetVertex(const int face, const int vertex) const
{
    static const SceneVertex invalidVert{};

    FillFaceStripsCacheIfNeeded();

    if (vertex >= GetVerticesPerFace())
        return invalidVert;

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    {
        const auto idx = face * GetVerticesPerFace() + vertex;
        if ((idx < 0) || (idx >= mIndices.size()))
            return invalidVert;
        return mVertices[mIndices[idx]];
    }

    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    {
        if (!mAreFaceStripsCached)
            return invalidVert;
        if (face >= mFaceStripsTotalCount)
            return invalidVert;

        // Strip
        // (naive impl for now, could be done in log time using cumulative counts and binary search)
        size_t strip = 0;
        size_t skippedFaces = 0;
        for (; strip < mFaceStrips.size(); strip++)
        {
            const auto currentFaceCount = mFaceStrips[strip].faceCount;
            if (face < (skippedFaces + currentFaceCount))
                break; // found
            skippedFaces += currentFaceCount;
        }
        if (strip >= mFaceStrips.size())
            return invalidVert;

        // Face & vertex
        const auto faceIdx   = face - skippedFaces;
        const auto vertexIdx = GetVertexIndex((int)faceIdx, vertex);
        const auto idx = mFaceStrips[strip].startIdx + faceIdx + vertexIdx;
        if ((idx < 0) || (idx >= mIndices.size()))
            return invalidVert;
        return mVertices[mIndices[idx]];
    }

    default:
        return invalidVert; // Unsupported
    }
}


SceneVertex& ScenePrimitive::GetVertex(const int face, const int vertex)
{
    return
        const_cast<SceneVertex &>(
            static_cast<const ScenePrimitive&>(*this).
                GetVertex(face, vertex));
}


void ScenePrimitive::GetPosition(float outpos[],
                                 const int face,
                                 const int vertex) const
{
    const auto &pos = GetVertex(face, vertex).Pos;
    outpos[0] = pos.x;
    outpos[1] = pos.y;
    outpos[2] = pos.z;
}


void ScenePrimitive::GetNormal(float outnormal[],
                               const int face,
                               const int vertex) const
{
    const auto &normal = GetVertex(face, vertex).Normal;
    outnormal[0] = normal.x;
    outnormal[1] = normal.y;
    outnormal[2] = normal.z;
}


void ScenePrimitive::GetTextCoord(float outuv[],
                                  const int face,
                                  const int vertex) const
{
    const auto &tex = GetVertex(face, vertex).Tex;
    outuv[0] = tex.x;
    outuv[1] = tex.y;
}


void ScenePrimitive::SetTangent(const float intangent[],
                                const float sign,
                                const int face,
                                const int vertex)
{
    auto &tangent = GetVertex(face, vertex).Tangent;
    tangent.x = intangent[0];
    tangent.y = intangent[1];
    tangent.z = intangent[2];
    tangent.w = sign;
}


bool ScenePrimitive::CreateDeviceBuffers(IRenderingContext & ctx)
{
    DestroyDeviceBuffers();

    auto &device = ctx.GetDevice();
    assert(&device);

    HRESULT hr = S_OK;

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));

    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory(&initData, sizeof(initData));

    // Vertex buffer
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = (UINT)(sizeof(SceneVertex) * mVertices.size());
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    initData.pSysMem = mVertices.data();
    hr = device.CreateBuffer(&bd, &initData, &mVertexBuffer);
    if (FAILED(hr))
    {
        DestroyDeviceBuffers();
        return false;
    }

    // Index buffer
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(uint32_t) * (UINT)mIndices.size();
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    initData.pSysMem = mIndices.data();
    hr = device.CreateBuffer(&bd, &initData, &mIndexBuffer);
    if (FAILED(hr))
    {
        DestroyDeviceBuffers();
        return false;
    }

    return true;
}


void ScenePrimitive::Destroy()
{
    DestroyGeomData();
    DestroyDeviceBuffers();
}


void ScenePrimitive::DestroyGeomData()
{
    mVertices.clear();
    mIndices.clear();
    mTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
}


void ScenePrimitive::DestroyDeviceBuffers()
{
    Utils::ReleaseAndMakeNull(mVertexBuffer);
    Utils::ReleaseAndMakeNull(mIndexBuffer);
}


void ScenePrimitive::DrawGeometry(IRenderingContext &ctx, ID3D11InputLayout* vertexLayout) const
{
    auto &deviceContext = ctx.GetDeviceContext();

    deviceContext.IASetInputLayout(vertexLayout);
    UINT stride = sizeof(SceneVertex);
    UINT offset = 0;
    deviceContext.IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);
    deviceContext.IASetIndexBuffer(mIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    deviceContext.IASetPrimitiveTopology(mTopology);

    deviceContext.DrawIndexed((UINT)mIndices.size(), 0, 0);
}


SceneNode::SceneNode(bool isRootNode) :
    mIsRootNode(isRootNode),
    mLocalMtrx(XMMatrixIdentity()),
    mWorldMtrx(XMMatrixIdentity())
{}

ScenePrimitive* SceneNode::CreateEmptyPrimitive()
{
    mPrimitives.clear();
    mPrimitives.resize(1);
    if (mPrimitives.size() != 1)
        return nullptr;

    return &mPrimitives[0];
}

void SceneNode::SetIdentity()
{
    mLocalMtrx = XMMatrixIdentity();
}

void SceneNode::AddScale(double scale)
{
    AddScale({ scale, scale, scale });
}

void SceneNode::AddScale(const std::vector<double> &vec)
{
    if (vec.size() != 3)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddScale: vector of incorrect size (%d instead of 3)",
                         vec.size());
        return;
    }

    const auto mtrx = XMMatrixScaling((float)vec[0], (float)vec[1], (float)vec[2]);

    mLocalMtrx = mLocalMtrx * mtrx;
}

void SceneNode::AddRotationQuaternion(const std::vector<double> &vec)
{
    if (vec.size() != 4)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddRotationQuaternion: vector of incorrect size (%d instead of 4)",
                         vec.size());
        return;
    }

    const XMFLOAT4 quaternion((float)vec[0], (float)vec[1], (float)vec[2], (float)vec[3]);
    const auto xmQuaternion = XMLoadFloat4(&quaternion);
    const auto mtrx = XMMatrixRotationQuaternion(xmQuaternion);

    mLocalMtrx = mLocalMtrx * mtrx;
}

void SceneNode::AddTranslation(const std::vector<double> &vec)
{
    if (vec.size() != 3)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddTranslation: vector of incorrect size (%d instead of 3)",
                         vec.size());
        return;
    }

    const auto mtrx = XMMatrixTranslation((float)vec[0], (float)vec[1], (float)vec[2]);

    mLocalMtrx = mLocalMtrx * mtrx;
}

void SceneNode::AddMatrix(const std::vector<double> &vec)
{
    if (vec.size() != 16)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddMatrix: vector of incorrect size (%d instead of 16)",
                         vec.size());
        return;
    }

    const auto mtrx = XMMatrixSet(
        (float)vec[0],  (float)vec[1],  (float)vec[2],  (float)vec[3],
        (float)vec[4],  (float)vec[5],  (float)vec[6],  (float)vec[7],
        (float)vec[8],  (float)vec[9],  (float)vec[10], (float)vec[11],
        (float)vec[12], (float)vec[13], (float)vec[14], (float)vec[15]);

    mLocalMtrx = mLocalMtrx * mtrx;
}


bool SceneNode::LoadFromGLTF(IRenderingContext & ctx,
                             const tinygltf::Model &model,
                             const tinygltf::Node &node,
                             int nodeIdx,
                             const std::wstring &logPrefix)
{
    // debug
    if (Log::sLoggingLevel >= Log::eDebug)
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
        Log::Debug(L"%sNode %d/%d \"%s\": mesh %d, transform %s, children %d",
                   logPrefix.c_str(), 
                   nodeIdx,
                   model.nodes.size(),
                   Utils::StringToWstring(node.name).c_str(),
                   node.mesh,
                   transforms.c_str(),
                   node.children.size());
    }

    const std::wstring &subItemsLogPrefix = logPrefix + L"   ";

    // Local transformation
    SetIdentity();
    if (node.matrix.size() == 16)
    {
        AddMatrix(node.matrix);

        // Sanity checking
        if (!node.scale.empty())
            Log::Warning(L"%sNode %d/%d \"%s\": node.scale is not empty when tranformation matrix is provided. Ignoring.",
                         logPrefix.c_str(),
                         nodeIdx,
                         model.nodes.size(),
                         Utils::StringToWstring(node.name).c_str());
        if (!node.rotation.empty())
            Log::Warning(L"%sNode %d/%d \"%s\": node.rotation is not empty when tranformation matrix is provided. Ignoring.",
                         logPrefix.c_str(),
                         nodeIdx,
                         model.nodes.size(),
                         Utils::StringToWstring(node.name).c_str());
        if (!node.translation.empty())
            Log::Warning(L"%sNode %d/%d \"%s\": node.translation is not empty when tranformation matrix is provided. Ignoring.",
                         logPrefix.c_str(),
                         nodeIdx,
                         model.nodes.size(),
                         Utils::StringToWstring(node.name).c_str());
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
        Log::Error(L"%sInvalid mesh index (%d/%d)!", subItemsLogPrefix.c_str(), meshIdx, model.meshes.size());
        return false;
    }
    if (meshIdx >= 0)
    {
        const auto &mesh = model.meshes[meshIdx];

        Log::Debug(L"%sMesh %d/%d \"%s\": %d primitive(s)",
                   subItemsLogPrefix.c_str(),
                   meshIdx,
                   model.meshes.size(),
                   Utils::StringToWstring(mesh.name).c_str(),
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


void SceneNode::Animate(IRenderingContext &ctx)
{

    if (mIsRootNode)
    {
        const float time = 0.0f;////ctx.GetFrameAnimationTime();
        const float period = 15.f; //seconds
        const float totalAnimPos = time / period;
        const float angle = totalAnimPos * XM_2PI;

        const XMMATRIX rotMtrx = XMMatrixRotationY(angle);

        mWorldMtrx = mLocalMtrx * rotMtrx;
    }
    else
        mWorldMtrx = mLocalMtrx;

    for (auto &child : mChildren)
        child.Animate(ctx);

}


SceneTexture::SceneTexture(const std::wstring &name,
                           ValueType valueType,
                           XMFLOAT4 neutralValue) :
    mName(name),
    mValueType(valueType),
    mNeutralValue(neutralValue),
    mIsLoaded(false),
    srv(nullptr)
{}

SceneTexture::SceneTexture(const SceneTexture &src) :
    mName(src.mName),
    mValueType(src.mValueType),
    mNeutralValue(src.mNeutralValue),
    mIsLoaded(src.mIsLoaded),
    srv(src.srv)
{
    // We are creating new reference of device resource
    Utils::SafeAddRef(srv);
}

SceneTexture& SceneTexture::operator =(const SceneTexture &src)
{
    mName           = src.mName;
    mValueType      = src.mValueType;
    mNeutralValue   = src.mNeutralValue;
    mIsLoaded       = src.mIsLoaded;
    srv             = src.srv;

    // We are creating new reference of device resource
    Utils::SafeAddRef(srv);

    return *this;
}

SceneTexture::SceneTexture(SceneTexture &&src) :
    mName(src.mName),
    mValueType(src.mValueType),
    mNeutralValue(Utils::Exchange(src.mNeutralValue, XMFLOAT4(0.f, 0.f, 0.f, 0.f))),
    mIsLoaded(Utils::Exchange(src.mIsLoaded, false)),
    srv(Utils::Exchange(src.srv, nullptr))
{}

SceneTexture& SceneTexture::operator =(SceneTexture &&src)
{
    mName           = src.mName;
    mValueType      = src.mValueType;
    mNeutralValue   = Utils::Exchange(src.mNeutralValue, XMFLOAT4(0.f, 0.f, 0.f, 0.f));
    mIsLoaded       = Utils::Exchange(src.mIsLoaded, false);
    srv             = Utils::Exchange(src.srv, nullptr);

    return *this;
}

SceneTexture::~SceneTexture()
{
    Utils::ReleaseAndMakeNull(srv);
}


bool SceneTexture::Create(IRenderingContext &ctx, const wchar_t *path)
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


bool SceneTexture::CreateNeutral(IRenderingContext &ctx)
{
    return SceneUtils::CreateConstantTextureSRV(ctx, srv, mNeutralValue);
}


bool SceneTexture::LoadTextureFromGltf(const int textureIndex,
                                       IRenderingContext &ctx,
                                       const tinygltf::Model &model,
                                       const std::wstring &logPrefix)
{
    const auto &textures = model.textures;
    const auto &images = model.images;

    if (textureIndex >= (int)textures.size())
    {
        Log::Error(L"%sInvalid texture index (%d/%d) in \"%s\""
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

        Log::Debug(L"%s%s: Not specified - creating neutral constant texture",
                   logPrefix.c_str(),
                   GetName().c_str()
        );

        if (!SceneUtils::CreateConstantTextureSRV(ctx, srv, mNeutralValue))
        {
            Log::Error(L"%sFailed to create neutral constant texture for \"%s\"!",
                       logPrefix.c_str(),
                       GetName().c_str());
            return false;
        }

        return true;
    }

    const auto &texture = textures[textureIndex];

    const auto texSource = texture.source;
    if ((texSource < 0) || (texSource >= images.size()))
    {
        Log::Error(L"%sInvalid source image index (%d/%d) in texture %d!",
                   logPrefix.c_str(),
                   texSource,
                   images.size(),
                   textureIndex);
        return false;
    }

    const auto &image = images[texSource];

    Log::Debug(L"%s%s: \"%s\"/\"%s\", %dx%d, %dx%db %s, data size %dB",
               logPrefix.c_str(),
               GetName().c_str(),
               Utils::StringToWstring(image.name).c_str(),
               Utils::StringToWstring(image.uri).c_str(),
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
        image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
        image.image.size() != expectedSrcDataSize)
    {
        Log::Error(L"%sInvalid image \"%s\": \"%s\", %dx%d, %dx%db %s, data size %dB",
                   logPrefix.c_str(),
                   Utils::StringToWstring(image.name).c_str(),
                   Utils::StringToWstring(image.uri).c_str(),
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

    if (!SceneUtils::CreateTextureSrvFromData(ctx,
                                              srv,
                                              image.width,
                                              image.height,
                                              dataFormat,
                                              image.image.data(),
                                              image.width * 4 * sizeof(uint8_t)))
    {
        Log::Error(L"%sFailed to create texture & SRV for image \"%s\": \"%s\", %dx%d",
                   logPrefix.c_str(),
                   Utils::StringToWstring(image.name).c_str(),
                   Utils::StringToWstring(image.uri).c_str(),
                   image.width,
                   image.height);
        return false;
    }

    mIsLoaded = true;

    // TODO: Sampler

    return true;
}


bool SceneNormalTexture::CreateNeutral(IRenderingContext &ctx)
{
    mScale = 1.f;
    return SceneTexture::CreateNeutral(ctx);
}


bool SceneNormalTexture::LoadTextureFromGltf(const tinygltf::NormalTextureInfo &normalTextureInfo,
                                             const tinygltf::Model &model,
                                             IRenderingContext &ctx,
                                             const std::wstring &logPrefix)
{
    if (!SceneTexture::LoadTextureFromGltf(normalTextureInfo.index, ctx, model, logPrefix))
        return false;

    mScale = (float)normalTextureInfo.scale;

    Log::Debug(L"%s%s: scale %f",
               logPrefix.c_str(),
               GetName().c_str(),
               mScale);

    return true;

}


bool SceneOcclusionTexture::CreateNeutral(IRenderingContext &ctx)
{
    mStrength = 1.f;
    return SceneTexture::CreateNeutral(ctx);
}


bool SceneOcclusionTexture::LoadTextureFromGltf(const tinygltf::OcclusionTextureInfo &occlusionTextureInfo,
                                                const tinygltf::Model &model,
                                                IRenderingContext &ctx,
                                                const std::wstring &logPrefix)
{
    if (!SceneTexture::LoadTextureFromGltf(occlusionTextureInfo.index, ctx, model, logPrefix))
        return false;

    mStrength = (float)occlusionTextureInfo.strength;

    Log::Debug(L"%s%s: strength %f",
               logPrefix.c_str(),
               GetName().c_str(),
               mStrength);

    return true;

}


SceneMaterial::SceneMaterial() :
    mWorkflow(MaterialWorkflow::kNone),
    mBaseColorTexture(L"BaseColorTexture", SceneTexture::eSrgb, XMFLOAT4(1.f, 1.f, 1.f, 1.f)),
    mBaseColorFactor(XMFLOAT4(1.f, 1.f, 1.f, 1.f)),
    mMetallicRoughnessTexture(L"MetallicRoughnessTexture", SceneTexture::eLinear, XMFLOAT4(1.f, 1.f, 1.f, 1.f)),
    mMetallicRoughnessFactor(XMFLOAT4(1.f, 1.f, 1.f, 1.f)),

    mSpecularTexture(L"SpecularTexture", SceneTexture::eLinear, XMFLOAT4(1.f, 1.f, 1.f, 1.f)),
    mSpecularFactor(XMFLOAT4(1.f, 1.f, 1.f, 1.f)),

    mNormalTexture(L"NormalTexture"),
    mOcclusionTexture(L"OcclusionTexture"),
    mEmissionTexture(L"EmissionTexture", SceneTexture::eSrgb, XMFLOAT4(0.f, 0.f, 0.f, 1.f)),
    mEmissionFactor(XMFLOAT4(0.f, 0.f, 0.f, 1.f))
{}


bool SceneMaterial::CreatePbrSpecularity(IRenderingContext &ctx,
                                         const wchar_t * diffuseTexPath,
                                         XMFLOAT4 diffuseFactor,
                                         const wchar_t * specularTexPath,
                                         XMFLOAT4 specularFactor)
{
    if (!mBaseColorTexture.Create(ctx, diffuseTexPath))
        return false;
    mBaseColorFactor = diffuseFactor;

    if (!mSpecularTexture.Create(ctx, specularTexPath))
        return false;
    mSpecularFactor = specularFactor;

    if (!mNormalTexture.CreateNeutral(ctx))
        return false;

    if (!mOcclusionTexture.CreateNeutral(ctx))
        return false;

    if (!mEmissionTexture.CreateNeutral(ctx))
        return false;
    mEmissionFactor = XMFLOAT4(0.f, 0.f, 0.f, 1.f);

    mWorkflow = MaterialWorkflow::kPbrSpecularity;

    return true;
}


bool SceneMaterial::CreatePbrMetalness(IRenderingContext &ctx,
                                       const wchar_t * baseColorTexPath,
                                       XMFLOAT4 baseColorFactor,
                                       const wchar_t * metallicRoughnessTexPath,
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

    mWorkflow = MaterialWorkflow::kPbrMetalness;

    return true;
}


bool SceneMaterial::LoadFromGltf(IRenderingContext &ctx,
                                 const tinygltf::Model &model,
                                 const tinygltf::Material &material,
                                 const std::wstring &logPrefix)
{
    auto &pbrMR = material.pbrMetallicRoughness;

    if (!mBaseColorTexture.LoadTextureFromGltf(pbrMR.baseColorTexture.index, ctx, model, logPrefix))
        return false;

    GltfUtils::FloatArrayToColor(mBaseColorFactor, pbrMR.baseColorFactor);

    Log::Debug(L"%s%s: %s",
               logPrefix.c_str(),
               L"BaseColorFactor",
               GltfUtils::ColorToWstring(mBaseColorFactor).c_str());

    if (!mMetallicRoughnessTexture.LoadTextureFromGltf(pbrMR.metallicRoughnessTexture.index, ctx, model, logPrefix))
        return false;

    GltfUtils::FloatToColorComponent<2>(mMetallicRoughnessFactor, pbrMR.metallicFactor);
    GltfUtils::FloatToColorComponent<1>(mMetallicRoughnessFactor, pbrMR.roughnessFactor);

    Log::Debug(L"%s%s: %s",
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

    Log::Debug(L"%s%s: %s",
               logPrefix.c_str(),
               L"EmissionFactor",
               GltfUtils::ColorToWstring(mEmissionFactor).c_str());

    mWorkflow = MaterialWorkflow::kPbrMetalness;

    return true;
}


void SceneMaterial::Animate(IRenderingContext &ctx)
{
    const float totalAnimPos = 0.0f; /////ctx.GetFrameAnimationTime() / 3.f; //seconds
}

bool Scene::Load(IRenderingContext& ctx)
{
    if (!LoadExternal(ctx, mSceneFilePath))
        return false;

    //AddScaleToRoots(100.0);
    //AddScaleToRoots({ 1.0f, -1.0f, 1.0f });
    //AddTranslationToRoots({ 0., -40., 0. }); // -1000, 800, 0
    //AddRotationQuaternionToRoots({ 0.000, -1.000, 0.000, 0.000 }); // 180°y
    //AddRotationQuaternionToRoots({ 0.000, 0.000, -1.000, 0.000 }); // 180°y
    //AddRotationQuaternionToRoots({ 0.000, 0.707, 0.000, 0.707 }); // 90°y
    //AddRotationQuaternionToRoots({ 0.707, 0.000, 0.000, 0.707 }); // 90°y

    const float amb = 0.35f;
    mAmbientLight.luminance = XMFLOAT4(amb, amb, amb, 0.5f);

    const float lum = 3.0f;
    mDirectLights.resize(1);
    mDirectLights[0].dir = XMFLOAT4(0.7f, 1.f, 0.9f, 1.0f);
    mDirectLights[0].luminance = XMFLOAT4(lum, lum, lum, 1.0f);    

    return PostLoadSanityTest();
}


bool Scene::PostLoadSanityTest()
{
    if (mPointLights.size() > POINT_LIGHTS_MAX_COUNT)
    {
        Log::Error(L"Point lights count (%d) exceeded maximum limit (%d)!",
            mPointLights.size(), POINT_LIGHTS_MAX_COUNT);
        return false;
    }

    if (mDirectLights.size() > DIRECT_LIGHTS_MAX_COUNT)
    {
        Log::Error(L"Directional lights count (%d) exceeded maximum limit (%d)!",
            mDirectLights.size(), DIRECT_LIGHTS_MAX_COUNT);
        return false;
    }

    // Geometry using normal map must have tangent specified (for now)
    for (auto& node : mRootNodes)
        if (!NodeTangentSanityTest(node))
            return false;

    return true;
}


bool Scene::NodeTangentSanityTest(const SceneNode& node)
{
    // Test node
    for (auto& primitive : node.mPrimitives)
    {
        auto& material = GetMaterial(primitive);

        if (material.GetNormalTexture().IsLoaded() && !primitive.IsTangentPresent())
        {
            Log::Error(L"A scene primitive without tangent data uses a material with normal map!");
            return false;
        }
    }

    // Children
    for (auto& child : node.mChildren)
        if (!NodeTangentSanityTest(child))
            return false;

    return true;
}

void Scene::SetCamera(IRenderingContext& ctx, const FSceneNode& SceneNode)
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
    
    mViewData.eye = XMVectorSet(cameraPosition.X, cameraPosition.Z, cameraPosition.Y, 1.0f);
    mViewData.at = XMVectorSet(cameraToVector.X, cameraToVector.Z, cameraToVector.Y, 1.0f);
    mViewData.up = XMVectorSet(cameraUpVector.X, cameraUpVector.Z, cameraUpVector.Y, 1.0f);
    
    // Matrices
    mViewMtrx = XMMatrixLookToLH(mViewData.eye, mViewData.at, mViewData.up);
    mProjectionMtrx = DirectX::XMMatrixPerspectiveFovLH(fFovVert, fAspect, fZNear, fZFar);
    mProjectionMtrx.r[1].m128_f32[1] *= -1.0f; //Flip Y

    // Scene constant buffer can be updated now
    CbScene cbScene;
    cbScene.ViewMtrx = XMMatrixTranspose(mViewMtrx);
    XMStoreFloat4(&cbScene.CameraPos, mViewData.eye);
    cbScene.ProjectionMtrx = XMMatrixTranspose(mProjectionMtrx);
    deviceContext.UpdateSubresource(mCbScene, 0, NULL, &cbScene, 0, 0);
}
