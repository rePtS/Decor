module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <cassert>
#include <random>
#include <fstream>

#include "Defines.hlsli"
#include <DeusEx.h>
#include <UnRender.h>

export module GlobalShaderConstants;

import GPU.ConstantBuffer;
import <simple_json.hpp>;

using DirectX::XMVECTOR;
using DirectX::XMVECTORU32;
using DirectX::XMMATRIX;
using json::JSON;

export class GlobalShaderConstants
{
    JSON& _settings;

    int _currentLevelIndex;
    std::string _currentLevelName;

public:
    explicit GlobalShaderConstants(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext, JSON& settings)
        : m_PerSceneBuffer(Device, DeviceContext, 2, settings)
        , m_PerFrameBuffer(Device, DeviceContext, 0, settings)
        , m_PerTickBuffer(Device, DeviceContext, 1)
        , m_PerComplexPolyBuffer(Device, DeviceContext, 3)
        , _settings(settings)
    {
    }

    GlobalShaderConstants(const GlobalShaderConstants&) = delete;
    GlobalShaderConstants& operator=(const GlobalShaderConstants&) = delete;

    // Operator new/delete for SSE aligned data
    void* operator new(const size_t s) { return _aligned_malloc(s, std::alignment_of<__m128>::value); }
    void operator delete(void* const p) { _aligned_free(p); }

    void Init()
    {
    }

    void Bind()
    {
        m_PerFrameBuffer.UpdateAndBind();
        m_PerTickBuffer.UpdateAndBind();
        m_PerSceneBuffer.UpdateAndBind();
        m_PerComplexPolyBuffer.UpdateAndBind();
    }

    void NewTick()
    {
        m_PerTickBuffer.NewTick();
    }

    void CheckProjectionChange(const FSceneNode& SceneNode)
    {
        assert(SceneNode.Viewport);
        assert(SceneNode.Viewport->Actor);
        //assert(reinterpret_cast<uintptr_t>(&m_PerFrameBuffer.m_Data.ProjectionMatrix) % 16 == 0);

        m_PerFrameBuffer.CheckProjectionChange(SceneNode);
    }

    void CheckViewChange(const FSceneNode& SceneNode, const FSavedPoly& Poly)
    {
        assert(Poly.NumPts >= 3);

        m_PerFrameBuffer.CheckViewChange(SceneNode, Poly, _currentLevelName);
    }
    
    bool CheckLevelChange(const FSceneNode& SceneNode)
    {
        auto levelChanged = false;
        auto levelIndex = SceneNode.Level->GetOuter()->GetFName().GetIndex();

        // if current level has changed
        if (_currentLevelIndex != levelIndex)
        {
            _currentLevelIndex = levelIndex;
            _currentLevelName = GetString(
                SceneNode.Level->GetOuter()->GetPathName());
            
            levelChanged = true;
        }

        m_PerSceneBuffer.SetSceneStaticLights(SceneNode, _currentLevelName);
        m_PerFrameBuffer.CheckLevelChange(SceneNode);

        return levelChanged;
    }

    static std::string GetString(const TCHAR* tStr)
    {
        char charBuf[64];
        wcstombs(charBuf, tStr, 64);
        return std::string(charBuf);
    }

    int GetMaxINode()
    {
        auto& jsonMaxINode = _settings[_currentLevelName]["MaxINode"];

        return jsonMaxINode.IsNull() ? INT_MAX : jsonMaxINode.ToInt();
    }

    void SetComplexPoly(const FSceneNode& SceneNode, const FSavedPoly& Poly)
    {        
        const auto& lightCache = m_PerSceneBuffer.GetLightCache();
        m_PerComplexPolyBuffer.SetComplexPoly(SceneNode, Poly, lightCache);
    }

    void NewFrame(const DirectX::XMVECTOR& color)
    {
        m_PerFrameBuffer.SetFlashColor(color);
        m_PerFrameBuffer.CheckWaterZone();
    }

    float GetRFX2() { return m_PerFrameBuffer.GetRFX2(); }
    float GetRFY2() { return m_PerFrameBuffer.GetRFY2(); }        

protected:
        
    static XMVECTOR HSVtoRGB(float H, float S, float V)
    {
        if (S == 0.0)
            return DirectX::XMVectorSet(V, V, V, 0.0f);

        float i = floor(H * 6.0);
        float f = H * 6.0f - i;
        float p = V * (1.0f - S);
        float q = V * (1.0f - S * f);
        float t = V * (1.0f - S * (1.0f - f));

        switch ((int)i % 6)
        {
        case 0: return DirectX::XMVectorSet(V, t, p, 0.0f);
        case 1: return DirectX::XMVectorSet(q, V, p, 0.0f);
        case 2: return DirectX::XMVectorSet(p, V, t, 0.0f);
        case 3: return DirectX::XMVectorSet(p, q, V, 0.0f);
        case 4: return DirectX::XMVectorSet(t, p, V, 0.0f);
        case 5: return DirectX::XMVectorSet(V, p, q, 0.0f);
        }

        return DirectX::XMVectorSet(0.5f, 0.5f, 0.5f, 0.0f); // Default color
    }

    static float NormalizeByte(BYTE byte)
    {
        return (float)byte / 255.0f;
    }

    /// <summary>
    /// Converts lights intensity to 4-component vector format,
    /// where w-part of the vector stores light source type
    /// </summary>
    /// <param name="light">DeuesEx light actor</param>
    static XMVECTOR GetLightColor(AActor* light, float correction = 1.0f)
    {
        auto color = HSVtoRGB(
            NormalizeByte(light->LightHue),
            1.0f - NormalizeByte(light->LightSaturation),
            1.0f);

        auto lightRadius = light->WorldLightRadius();
        auto lightBrightness = NormalizeByte(light->LightBrightness);

        color = DirectX::XMVectorScale(color,
            lightRadius * lightRadius * lightBrightness * correction);

        // Fix for the "cylindrical" light source
        if (light->LightEffect == LE_Cylinder)
            color = DirectX::XMVectorScale(color, 0.00003f * lightBrightness);

        uint32_t lightType = light->LightEffect;
        lightType |= static_cast<uint32_t>(light->LightType) << LIGHT_TYPE_OFFSET;
        lightType |= static_cast<uint32_t>(light->LightPeriod) << LIGHT_PERIOD_OFFSET;
        lightType |= static_cast<uint32_t>(light->LightPhase) << LIGHT_PHASE_OFFSET;
        
        if (light->bSpecialLit)
            lightType |= LIGHT_SPECIAL_MASK;

        assert(sizeof(float) == sizeof(uint32_t));

        return DirectX::XMVectorSetW(color, reinterpret_cast<float&>(lightType));
    }

    /// <summary>
    /// Converts lights position data to 4-component vector format,
    /// where w-part of the vector stores light source radius (for point and spot lights)
    /// </summary>
    /// <param name="light">DeuesEx light actor</param>
    static XMVECTOR GetLightLocation(AActor* light)
    {
        return DirectX::XMVectorSet(
            light->Location.X,
            light->Location.Y,
            light->Location.Z,
            light->WorldLightRadius());
    }

    /// <summary>
    /// Converts spot light direction data to 4-component vector format,
    /// where w-part of the vector stores angle of the light cone
    /// </summary>
    /// <param name="light"></param>
    /// <returns></returns>
    static XMVECTOR GetLightDirection(AActor* light)
    {
        auto lightDirection = light->Rotation.Vector();

        // the angle of the light cone
        float spotAngle = (float)light->LightCone / 510.0f * PI; // ... / 255.0f * (PI / 2.0f);

        return DirectX::XMVectorSet(
            lightDirection.X,
            lightDirection.Y,
            lightDirection.Z,
            spotAngle);
    }

    /// <summary>
    /// Converts light source data to constant buffer format
    /// </summary>
    /// <param name="light"></param>
    /// <returns></returns>
    static std::vector<XMVECTOR> GetLightData(AActor* light, float correction = 1.0f)
    {
        std::vector<XMVECTOR> lightData;

        lightData.push_back(GetLightColor(light, correction));
        lightData.push_back(GetLightLocation(light));

        if (light->LightEffect == LE_Spotlight)
            lightData.push_back(GetLightDirection(light));

        return lightData;
    }

    /// <summary>
    /// Prepares and stores constant buffer data
    /// related to the current level only (ex. set of static lights on the current level)
    /// </summary>
    class PerSceneBuffer
    {
        const static size_t MAX_BUF = 3072;

        struct PerScene
        {
            XMVECTOR StaticLights[MAX_BUF];
        };

        // Index of the current level (used to determine if current level has been changed)
        int m_CurrentLevelIndex;

        ConstantBuffer<PerScene> m_Buffer;
        std::unordered_map<AActor*, size_t> m_LightCache;
        JSON& _settings;

        unsigned int _slot;

    public:
        PerSceneBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext, unsigned int slot, JSON& settings)
            : m_Buffer(Device, DeviceContext), _slot(slot), _settings(settings)
        { }

        PerSceneBuffer(const PerSceneBuffer&) = delete;
        PerSceneBuffer& operator=(const PerSceneBuffer&) = delete;               

        /// <summary>
        /// Set scene's static lights data for GPU constant buffer
        /// </summary>
        /// <param name="SceneNode"></param>
        void SetSceneStaticLights(const FSceneNode& SceneNode, const std::string& levelName)
        {
            auto levelIndex = SceneNode.Level->GetOuter()->GetFName().GetIndex();

            auto& settings = _settings[levelName];

            // if current level has changed
            if (m_CurrentLevelIndex != levelIndex)
            {
                size_t bufferPos = 0;
                m_LightCache.clear();

                // process all static light sources on current level
                for (int lightNum = 0; lightNum < SceneNode.Level->Model->Lights.Num(); ++lightNum)
                {
                    auto lightActor = SceneNode.Level->Model->Lights(lightNum);
                    if (lightActor != nullptr)
                    {
                        assert(bufferPos < MAX_BUF);

                        // if light source is not already processed
                        if (!m_LightCache.contains(lightActor))
                        {
                            float correction = 1.0f;
                            auto lightName = GetString(lightActor->GetName());
                            if (settings.hasKey(lightName))
                                correction = settings.at(lightName).ToFloat();

                            // then process and add processed data for constant buffer
                            m_LightCache.insert({ lightActor, bufferPos });
                            auto lightData = GetLightData(lightActor, correction);
                            for (size_t i = 0; i < lightData.size(); ++i)
                                m_Buffer.m_Data.StaticLights[bufferPos + i] = lightData[i];

                            bufferPos += lightData.size(); // move the pointer to the free part of the buffer
                        }
                    }
                }

                m_Buffer.MarkAsDirty();
                m_CurrentLevelIndex = levelIndex;
            }
        }

        const std::unordered_map<AActor*, size_t>& GetLightCache()
        {
            return m_LightCache;
        }

        void UpdateAndBind()
        {
            m_Buffer.UpdateAndBind(_slot);
        }
    }
    m_PerSceneBuffer;

    /// <summary>
    /// Prepares and stores constant buffer data
    /// related to the current frame only (ex. projection and view matrix)
    /// </summary>
    class PerFrameBuffer
    {
        struct PerFrame
        {
            float fRes[4];            
            XMMATRIX ProjectionMatrix;
            XMMATRIX ViewMatrix;
            XMMATRIX ViewMatrixInv;
            XMVECTOR Origin;
            XMVECTOR FlashColor;
            XMVECTOR DynamicLights[MAX_LIGHTS_DATA_SIZE];
            uint32_t FrameControl; // bit 0 - флаг того, что текущий кадр возможно пересекает водная поверхность
            float ScreenWaterLevel; // Уровень, на который камера погружена в воду (0 - не погружена, 1 - погружена полностью)
        };

        ConstantBuffer<PerFrame> m_Buffer;        

        unsigned int _slot;

        // Index of the current level (used to determine if a level has been changed)
        int m_CurrentLevelIndex;

        // Light sources on the current level (actualy these are "dynamic" light sources as they can move or switch on/off)
        AAugmentation* m_AugLight;
        std::vector<AActor*> m_DynamicLights;
        size_t m_LightActorsNum = 0;

        ADeusExPlayer* m_Player = nullptr;

        // Vars for projection change check
        float m_fFov = 0.0f;
        int m_iViewPortX = 0;
        int m_iViewPortY = 0;

        float m_RFX2 = 0.0f;
        float m_RFY2 = 0.0f;

        // Cosine value of the view cone's angle
        float _viewConeAngle = 0.0f;

        // Actual view matrix
        FCoords m_Coords;

        //
        const XMVECTOR ScreenUpDir = { 0.0f, 1.0f, 0.0f, 0.0f };
        XMVECTOR _waterPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
        XMMATRIX _viewMatrix;
        XMMATRIX _viewMatrixInv;

        /// <summary>
        /// Screen height in view space
        /// </summary>
        float _screenHalfHeight;

        JSON& _settings;
        std::unordered_set<NAME_INDEX> _dynamicLightNameIds;

        /// <summary>
        /// Check if light augmentation is on
        /// </summary>
        bool IsAugLightActive()
        {
            return m_AugLight != nullptr && m_AugLight->bIsActive;
        }

        /// <summary>
        /// Checks if a given light actor is visible in the view cone
        /// </summary>
        bool IsLightActorVisible(const FCoords& coords, AActor* light)
        {
            // First, let's check if the light source is turned on:
            //  - it's type should not be LT_None
            //  - it's LightEffect may be LE_NonIncidence (otherwise we wont see some dynamic lights)
            //  - it's brightness should be greater 0
            if (light->LightType != LT_None &&
                //light->LightEffect != LE_NonIncidence &&
                light->LightBrightness > 0)
            {
                // position of the light source in View Space
                auto lightPos = light->Location.TransformPointBy(coords);
                // the range of the light source
                auto lightRadius = light->WorldLightRadius();                

                auto nearLightBoundary = lightPos.Z - lightRadius;
                auto farLightBoundary = lightPos.Z + lightRadius;

                // Check that the light source is located between the near and far plane of visibility
                if (farLightBoundary > NEAR_CLIPPING_DISTANCE && nearLightBoundary < FAR_CLIPPING_DISTANCE)
                {
                    auto lightDist = lightPos.Size();
                    if (lightDist < lightRadius)
                        return true;
                    else
                    {
                        // угловая ширина источника освещения
                        auto lightAngularWidth = atan(lightRadius / lightDist);
                        // угол между направлением камеры и лучом к центру источника освещения (угол расположения ИО)
                        auto lightPosAngle = atan(sqrt(lightPos.X * lightPos.X + lightPos.Y * lightPos.Y) / lightPos.Z);

                        // Если разница между углом расположения ИО и углом FOV камеры меньше угловой ширины ИО, то ИО считаем видимым
                        return (lightPosAngle - _viewConeAngle < lightAngularWidth);
                    }
                }
            }
            
            return false;
        }

        /// <summary>
        /// Set frame's dynamic lights data for GPU constant buffer
        /// </summary>        
        void SetDynamicLights(const FSceneNode& SceneNode)
        {
            // Проверяем кол-во акторов на сцене. Если оно изменилось,
            // то пересоздаем динамические источники света,
            // т.к. часть появившихся новых (либо удаленных старых) акторов могла быть источниками света
            auto num = SceneNode.Level->Actors.Num();
            if (num != m_LightActorsNum)
            {
                m_LightActorsNum = num;
                CollectDynamicLights(SceneNode);
            }

            size_t dynamicLightsBufferPos = 0;

            if (IsAugLightActive())
            {
                uint32_t augLightType = LE_Spotlight;
                augLightType |= static_cast<uint32_t>(LT_Steady) << LIGHT_TYPE_OFFSET;

                assert(sizeof(float) == sizeof(uint32_t));

                m_Buffer.m_Data.DynamicLights[0] = { 100000.0f, 100000.0f, 100000.0f, reinterpret_cast<float&>(augLightType) };
                m_Buffer.m_Data.DynamicLights[1] = { 0.0f, 0.0f, 0.0f, 4000.0f };
                m_Buffer.m_Data.DynamicLights[2] = { 0.0f, 0.0f, 1.0f, 0.5f };
                dynamicLightsBufferPos += 3;
            }
            
            // Остальные динамические источники освещения
            for (auto& light : m_DynamicLights)
            {
                if (IsLightActorVisible(SceneNode.Coords, light))
                {
                    auto lightData = GetLightData(light);
                    for (size_t i = 0; i < lightData.size(); ++i)
                        m_Buffer.m_Data.DynamicLights[dynamicLightsBufferPos + i] = lightData[i];

                    dynamicLightsBufferPos += lightData.size(); // move the pointer to the free part of the buffer
                }

                assert(dynamicLightsBufferPos < MAX_LIGHTS_DATA_SIZE);
            }

            uint32_t augLightType = LE_Unused;
            m_Buffer.m_Data.DynamicLights[dynamicLightsBufferPos] = { 0,0,0, reinterpret_cast<float&>(augLightType) };
        }

    public:
        PerFrameBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext, unsigned int slot, JSON& settings)
            : m_Buffer(Device, DeviceContext), _slot(slot), _settings(settings)
        {
            const auto& fnames = _settings.at("DynamicLightFNames");
            if (fnames.JSONType() == JSON::Class::Array)
            {
                for (size_t i = 0; i < fnames.length(); ++i)
                {
                    bool isOk;
                    const auto fnamestr = fnames.at(i).ToString(isOk);
                    if (isOk)
                    {
                        std::wstring wsTmp(fnamestr.begin(), fnamestr.end());
                        FName fname(wsTmp.c_str(), EFindName::FNAME_Find);
                        _dynamicLightNameIds.insert(fname.GetIndex());
                    }
                }
            }
        }

        PerFrameBuffer(const PerFrameBuffer&) = delete;
        PerFrameBuffer& operator=(const PerFrameBuffer&) = delete;

        float GetRFX2() { return m_RFX2; }
        float GetRFY2() { return m_RFY2; }        

        void CheckLevelChange(const FSceneNode& SceneNode)
        {
            auto levelIndex = SceneNode.Level->GetOuter()->GetFName().GetIndex();

            if (m_CurrentLevelIndex != levelIndex)
            {
                // The scene has changed, clear old scene data:
                m_AugLight = nullptr;
                m_Player = nullptr;
                m_DynamicLights.clear();
                m_LightActorsNum = 0;

                // Uploading data for a new scene:                
                static FName classNameAugLight(L"AugLight", EFindName::FNAME_Find);
                static FName classNameJCDentonMale(L"JCDentonMale", EFindName::FNAME_Find);

                for (size_t i = 0; i < SceneNode.Level->Actors.Num(); ++i)
                {
                    auto actor = SceneNode.Level->Actors(i);
                    if (actor != nullptr)
                    {
                        auto& actorFName = actor->GetClass()->GetFName();

                        // Checking that the current actor is a flashlight augmentation
                        if (actorFName == classNameAugLight)
                            m_AugLight = (AAugmentation*)actor;

                        else if (actorFName == classNameJCDentonMale)
                            m_Player = (ADeusExPlayer*)actor;
                    }
                }
                
                m_CurrentLevelIndex = levelIndex;
            }
        }

        void CollectDynamicLights(const FSceneNode& SceneNode)
        {
            // The scene has changed, clear old scene data:
            m_DynamicLights.clear();

            m_LightActorsNum = SceneNode.Level->Actors.Num();

            for (size_t i = 0; i < m_LightActorsNum; ++i)
            {
                auto actor = SceneNode.Level->Actors(i);
                if (actor != nullptr)
                {
                    auto actorFNameIdx = actor->GetClass()->GetFName().GetIndex();
                    if (_dynamicLightNameIds.contains(actorFNameIdx))
                        m_DynamicLights.push_back(actor);
                }
            }
        }

        void CheckProjectionChange(const FSceneNode& SceneNode)
        {
            assert(SceneNode.Viewport);
            assert(SceneNode.Viewport->Actor);
            assert(reinterpret_cast<uintptr_t>(&m_Buffer.m_Data.ProjectionMatrix) % 16 == 0);

            if (SceneNode.Viewport->Actor->FovAngle != m_fFov || SceneNode.X != m_iViewPortX || SceneNode.Y != m_iViewPortY)
            {
                // Create projection matrix with swapped near/far for better accuracy
                static const float fZNear = 32760.0f;
                static const float fZFar = 1.0f;

                const float halfFovInRadians = SceneNode.Viewport->Actor->FovAngle * static_cast<float>(PI) / 360.0f;

                const float aspect = SceneNode.FY / SceneNode.FX;
                const float halfFovTan = (float)appTan(halfFovInRadians);
                const float RProjZ = halfFovTan * fZNear;

                m_Buffer.m_Data.fRes[0] = SceneNode.FX;
                m_Buffer.m_Data.fRes[1] = SceneNode.FY;
                m_Buffer.m_Data.fRes[2] = 1.0f / SceneNode.FX;
                m_Buffer.m_Data.fRes[3] = 1.0f / SceneNode.FY;
                m_Buffer.m_Data.ProjectionMatrix = DirectX::XMMatrixPerspectiveOffCenterLH(-RProjZ, RProjZ, -aspect * RProjZ, aspect * RProjZ, fZNear, fZFar);
                m_Buffer.m_Data.ProjectionMatrix.r[1].m128_f32[1] *= -1.0f; //Flip Y

                m_Buffer.MarkAsDirty();
                m_fFov = SceneNode.Viewport->Actor->FovAngle;
                m_RFX2 = 2.0f * halfFovTan / SceneNode.FX;
                m_RFY2 = 2.0f * halfFovTan * aspect / SceneNode.FY;
                m_iViewPortX = SceneNode.X;
                m_iViewPortY = SceneNode.Y;

                auto halfFarWidth = fZNear * tan(halfFovInRadians);
                FVector farTopLeftClippingPoint(halfFarWidth, halfFarWidth * aspect, fZNear);

                auto frustumConeCosine = fZNear / farTopLeftClippingPoint.Size();
                _viewConeAngle = acos(frustumConeCosine);

                _screenHalfHeight = halfFovTan * aspect;                
            }
        }

        void CheckViewChange(const FSceneNode& SceneNode, const FSavedPoly& Poly, const std::string& levelName)
        {
            assert(Poly.NumPts >= 3);

            //    if (m_Coords.Origin != SceneNode.Coords.Origin || m_Coords.XAxis != SceneNode.Coords.XAxis || m_Coords.YAxis != SceneNode.Coords.YAxis || m_Coords.ZAxis != SceneNode.Coords.ZAxis)
            //    {

            const auto& c = SceneNode.Coords;
            _viewMatrix = DirectX::XMMatrixSet(
                c.XAxis.X, c.YAxis.X, c.ZAxis.X, c.Origin.X,
                c.XAxis.Y, c.YAxis.Y, c.ZAxis.Y, c.Origin.Y,
                c.XAxis.Z, c.YAxis.Z, c.ZAxis.Z, c.Origin.Z,
                0.0f, 0.0f, 0.0f, 1.0f
            );

            const auto& uc = SceneNode.Uncoords;
            _viewMatrixInv = DirectX::XMMatrixSet(
                uc.XAxis.X, uc.YAxis.X, uc.ZAxis.X, uc.Origin.X,
                uc.XAxis.Y, uc.YAxis.Y, uc.ZAxis.Y, uc.Origin.Y,
                uc.XAxis.Z, uc.YAxis.Z, uc.ZAxis.Z, uc.Origin.Z,
                0.0f, 0.0f, 0.0f, 1.0f
            );

            // Проверяем пересечение вертикальной линии камеры с плоскостью воды            
            CheckScreenWaterIntersection(c, levelName);

            SetDynamicLights(SceneNode);

            m_Buffer.m_Data.ViewMatrix = DirectX::XMMatrixTranspose(_viewMatrix);
            m_Buffer.m_Data.ViewMatrixInv = DirectX::XMMatrixTranspose(_viewMatrixInv);
            m_Buffer.m_Data.Origin = { c.Origin.X, c.Origin.Y, c.Origin.Z, 0 };
            m_Buffer.MarkAsDirty();

            //        m_Coords = SceneNode.Coords;
            //    }        
        }

        /// <summary>
        /// Проверяет, что экран пересекает водную поверхность на уровне.
        /// Если пересечение есть, то передает в константный буфер уровень пересечения.
        /// </summary>
        void CheckScreenWaterIntersection(const FCoords &c, const std::string& levelName)
        {
            if (m_Player == nullptr)
                return;
            
            if (m_Player->HeadRegion.Zone->bWaterZone)
            {
                m_Buffer.m_Data.ScreenWaterLevel = 0.0f; // камера полностью погружена в воду
            }
            else if (m_Player->Region.Zone->bWaterZone)
            {
                auto& settings = _settings[levelName];

                if (settings.hasKey("WaterLevels"))
                {
                    const auto& waterLevels = settings.at("WaterLevels");

                    auto waterLevelNum = waterLevels.length();
                    if (waterLevelNum > 0)
                    {
                        auto playerZ = m_Player->Location.Z;

                        // Перебираем все доступные водные поверхности и берем ближайщую из них                        
                        float nearestWaterLevel = waterLevels.at(0).ToFloat();
                        for (size_t i = 1; i < waterLevelNum; ++i)
                        {
                            auto waterLevel = waterLevels.at(i).ToFloat();
                            if (abs(playerZ - waterLevel) < abs(playerZ - nearestWaterLevel))
                                nearestWaterLevel = waterLevel;
                        }

                        DirectX::XMVECTOR waterPlane = { 0.0f, 0.0f, 1.0f, -nearestWaterLevel };

                        // Проверяем пересечение с найденной поверхностью:

                        XMVECTOR p1 = { 0.0f, -_screenHalfHeight, 1.0f, 0.0f }; // screen bottom center point
                        XMVECTOR p2 = { 0.0f, +_screenHalfHeight, 1.0f, 0.0f }; // screen up center point

                        // Переводим p1 и p2 в мировое пространство и работаем с ними                    
                        p1 = DirectX::XMVectorAdd(DirectX::XMVector3Transform(p1, _viewMatrixInv), { c.Origin.X, c.Origin.Y, c.Origin.Z, 0.0f });
                        p2 = DirectX::XMVectorAdd(DirectX::XMVector3Transform(p2, _viewMatrixInv), { c.Origin.X, c.Origin.Y, c.Origin.Z, 0.0f });

                        if (DirectX::XMVectorGetZ(p1) <= -DirectX::XMVectorGetW(waterPlane))
                            m_Buffer.m_Data.ScreenWaterLevel = -1.0f;
                        else
                        {
                            auto intersectionPoint = DirectX::XMPlaneIntersectLine(waterPlane, p1, p2);

                            // Находим уровень, на котором водная поверхность пересекает экран
                            // (расстояние от нижней средней точки до точки пересечения делим на высоту экрана)
                            auto intersectionDist = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(intersectionPoint, p1)));
                            // передаем полученное значение в шейдер
                            m_Buffer.m_Data.ScreenWaterLevel = intersectionDist / (_screenHalfHeight * 2.0f);
                        }
                    }
                }
            }
        }

        void SetFlashColor(const DirectX::XMVECTOR& color)
        {            
            m_Buffer.m_Data.FlashColor = color;
            m_Buffer.MarkAsDirty();
        }

        void CheckWaterZone()
        {
            __try // TO-DO: get rid of exception checking
            {
                if (m_Player != nullptr && m_Player->Region.Zone->bWaterZone)
                    m_Buffer.m_Data.FrameControl |= 1;
                else
                    m_Buffer.m_Data.FrameControl &= 0xFFFFFFFE;
                m_Buffer.MarkAsDirty();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        void UpdateAndBind()
        {
            m_Buffer.UpdateAndBind(_slot);
        }
    }
    m_PerFrameBuffer;
    
    /// <summary>
    /// Prepares and stores current time in constant buffer
    /// </summary>
    class PerTickBuffer
    {
        struct PerTick
        {
            float fTimeInSeconds;
            float fRandom;
            float fNormRandom;
            float padding;
        };

        long long m_InitialTime;

        float GetTimeSinceStart()
        {
            using namespace std::chrono;
            return float(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - m_InitialTime);
        }

        std::default_random_engine generator;
        std::uniform_real_distribution<float> distribution{ 0.0f, 1.0f };
        std::normal_distribution<float> normDistribution{ 0.7f, 0.3f };

        ConstantBuffer<PerTick> m_Buffer;

        unsigned int _slot;

    public:
        PerTickBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext, unsigned int slot)
            : m_Buffer(Device, DeviceContext), _slot(slot)
        {
            using namespace std::chrono;
            m_InitialTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        }

        PerTickBuffer(const PerTickBuffer&) = delete;
        PerTickBuffer& operator=(const PerTickBuffer&) = delete;

        void NewTick()
        {
            m_Buffer.m_Data.fTimeInSeconds = GetTimeSinceStart();
            m_Buffer.m_Data.fRandom = distribution(generator);
            m_Buffer.m_Data.fNormRandom = normDistribution(generator);
            m_Buffer.MarkAsDirty();
        }

        void UpdateAndBind()
        {
            m_Buffer.UpdateAndBind(_slot);
        }
    }
    m_PerTickBuffer;


    // !!! PerFrameBuffer лучше переименовать в PerViewBuffer, а в PerFrameBuffer хранить только данные
    // относящиеся к кадру (список статических и динамических ИС, видимых в текущем кадре)

    /// <summary>
    /// Prepares and stores constant buffer data
    /// related to the currently rendered polygon only
    /// </summary>
    class PerComplexPolyBuffer
    {
        struct PerComplexPoly
        {
            XMVECTORU32 PolyControl;            
            XMVECTORU32 StaticLightIds[MAX_LIGHTS_INDEX_SIZE]; // list of IDs of static light sources that are visible for the current ComplexPoly
        };

        ConstantBuffer<PerComplexPoly> m_Buffer;

        unsigned int _slot;
        
    public:
        PerComplexPolyBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext, unsigned int slot)
            : m_Buffer(Device, DeviceContext), _slot(slot)
        {}

        PerComplexPolyBuffer(const PerComplexPolyBuffer&) = delete;
        PerComplexPolyBuffer& operator=(const PerComplexPolyBuffer&) = delete;

        // prevDump - дамп статических ИС видимых на предыдущем кадре
        // currentDump - дамп статических ИС видимых на текущем кадре
        // В методе SetComplexPoly проверяем есть ли наблюдаемый ИС в currentDump, и если нет - то добавляем
        // В методе DumpCurrentFrameStaticLightIds меняем местами prevDump и currentDump
        // В методе GetLastFrameStaticLightIds возвращаем prevDump

        void SetComplexPoly(const FSceneNode& SceneNode, const FSavedPoly& Poly, const std::unordered_map<AActor*, size_t> &lightCache)
        {
            assert(Poly.NumPts >= 3);

            int currSurf = SceneNode.Level->Model->Nodes(Poly.iNode).iSurf;
            if (currSurf > -1)
            {                
                int lm = SceneNode.Level->Model->Surfs(currSurf).iLightMap;
                if (lm > -1)
                {
                    m_Buffer.m_Data.PolyControl = { 0, 0, 0, 0 };

                    int la = SceneNode.Level->Model->LightMap(lm).iLightActors;
                    if (la > -1)
                    {
                        size_t lightCounter = 0;

                        AActor* l = SceneNode.Level->Model->Lights(la);
                        while (l)
                        {
                            //auto lightId = lightCache.at(l);
                            //if (!_currentDump.contains(lightId))
                            //    _currentDump.insert(lightId);

                            m_Buffer.m_Data.StaticLightIds[lightCounter] = { lightCounter, lightCache.at(l), 0, 0 };
                            l = SceneNode.Level->Model->Lights(++la);
                            ++lightCounter;
                        }

                        m_Buffer.m_Data.PolyControl = { lightCounter, 0, 0, 0 };
                    }

                    m_Buffer.MarkAsDirty();
                }                
            }            
        }
        
        // TO-DO: the DumpCurrentFrameStaticLightIds method is needed to reset the cache of all static sources visible in the current frame.
        // In the same method, we save this dump in a separate field, so that we can then get it on demand via the GetLastFrameStaticLightIds method
        // ...

        void UpdateAndBind()
        {
            m_Buffer.UpdateAndBind(_slot);
        }
    }
    m_PerComplexPolyBuffer;
       
};