module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cassert>

#include "Defines.hlsli"
#include <DeusEx.h>
#include <UnRender.h>

export module GlobalShaderConstants;

import GPU.ConstantBuffer;

using DirectX::XMVECTOR;
using DirectX::XMVECTORU32;
using DirectX::XMMATRIX;

export class GlobalShaderConstants
{
public:
    explicit GlobalShaderConstants(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
        : m_PerSceneBuffer(Device, DeviceContext)
        , m_PerFrameBuffer(Device, DeviceContext)
        , m_PerTickBuffer(Device, DeviceContext)
        , m_PerComplexPolyBuffer(Device, DeviceContext)
    { }

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
        m_PerFrameBuffer.UpdateAndBind(0);
        m_PerTickBuffer.UpdateAndBind(1);
        m_PerSceneBuffer.UpdateAndBind(2);
        m_PerComplexPolyBuffer.UpdateAndBind(3);
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

        m_PerFrameBuffer.CheckViewChange(SceneNode, Poly);
    }
    
    void CheckLevelChange(const FSceneNode& SceneNode)
    {
        m_PerSceneBuffer.SetSceneStaticLights(SceneNode);
        m_PerFrameBuffer.CheckLevelChange(SceneNode);
    }

    void SetComplexPoly(const FSceneNode& SceneNode, const FSavedPoly& Poly)
    {
        const auto& lightCache = m_PerSceneBuffer.GetLightCache();
        m_PerComplexPolyBuffer.SetComplexPoly(SceneNode, Poly, lightCache);
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
    static XMVECTOR GetLightColor(AActor* light)
    {
        auto color = HSVtoRGB(
            NormalizeByte(light->LightHue),
            1.0f - NormalizeByte(light->LightSaturation),
            1.0f);

        auto lightRadius = light->WorldLightRadius();
        auto lightBrightness = NormalizeByte(light->LightBrightness);

        color = DirectX::XMVectorScale(color,
            lightRadius * lightRadius * lightBrightness);

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
    static std::vector<XMVECTOR> GetLightData(AActor* light)
    {
        std::vector<XMVECTOR> lightData;

        lightData.push_back(GetLightColor(light));
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

    public:
        PerSceneBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
            : m_Buffer(Device, DeviceContext)
        { }

        PerSceneBuffer(const PerSceneBuffer&) = delete;
        PerSceneBuffer& operator=(const PerSceneBuffer&) = delete;               

        /// <summary>
        /// Set scene's static lights data for GPU constant buffer
        /// </summary>
        /// <param name="SceneNode"></param>
        void SetSceneStaticLights(const FSceneNode& SceneNode)
        {
            auto levelIndex = SceneNode.Level->GetOuter()->GetFName().GetIndex();

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
                            // then process and add processed data for constant buffer
                            m_LightCache.insert({ lightActor, bufferPos });
                            auto lightData = GetLightData(lightActor);
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

        void UpdateAndBind(unsigned int iSlot)
        {
            m_Buffer.UpdateAndBind(iSlot);
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
            XMVECTOR Origin;
            XMVECTOR DynamicLights[MAX_LIGHTS_DATA_SIZE];
        };

        ConstantBuffer<PerFrame> m_Buffer;

        // Index of the current level (used to determine if a level has been changed)
        int m_CurrentLevelIndex;

        // Light sources on the current level (actualy these are "dynamic" light sources as they can move or switch on/off)
        AAugmentation* m_AugLight;
        std::vector<AActor*> m_DynamicLights;

        // Vars for projection change check
        float m_fFov = 0.0f;
        int m_iViewPortX = 0;
        int m_iViewPortY = 0;

        float m_RFX2 = 0.0f;
        float m_RFY2 = 0.0f;

        // Cosine value (powered by 2) of the view cone's angle
        float m_SquaredViewConeCos = 0.0f;

        // Actual view matrix
        FCoords m_Coords;

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
            //  - it's LightEffect should not be LE_NonIncidence (actually, this is a point light source, but filling a large volume - we don't need such yet)
            //  - it's brightness should be greater 0
            if (light->LightType != LT_None &&
                light->LightEffect != LE_NonIncidence &&
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
                    // Check that the light source falls into the cone of visibility (or that the camera is within the range of the light source).
                    // We use a View Cone, not a View Frustum to speed up the verification
                    // We do the check using the Charles Bloom method:
                    //      V = sphere.center - cone.apex_location
                    //      a = V * cone.direction_normal
                    //      Square(a) > dotProduct(V,V) * Square(cone.cos) -> sphere intersects the cone
                    // 
                    // Since we are working with View Space, the vertex of the Cone is the origin and the direction of the Cone coincides with the Z axis.
                    // Accordingly, the vector V becomes equal to lightPos and the variable a = lightPos.Z
                    return lightPos.Z * lightPos.Z > (lightPos | lightPos) * m_SquaredViewConeCos
                        || lightPos.SizeSquared() < lightRadius * lightRadius;
                }
            }

            return false;
        }

        /// <summary>
        /// Set frame's dynamic lights data for GPU constant buffer
        /// </summary>        
        void SetDynamicLights(const FSceneNode& SceneNode)
        {
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
        }

    public:
        PerFrameBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
            : m_Buffer(Device, DeviceContext)
        { }

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
                m_DynamicLights.clear();

                // Uploading data for a new scene:
                FName classNameLamp1(L"Lamp1", EFindName::FNAME_Find);
                FName classNameLamp2(L"Lamp2", EFindName::FNAME_Find);
                FName classNameLamp3(L"Lamp3", EFindName::FNAME_Find);
                FName classNameTriggerLight(L"TriggerLight", EFindName::FNAME_Find);
                FName classNameAugLight(L"AugLight", EFindName::FNAME_Find);
                FName classNameLight(L"Light", EFindName::FNAME_Find);
                FName classNameSpotlight(L"Spotlight", EFindName::FNAME_Find);
                FName classNameBarrelFire(L"BarrelFire", EFindName::FNAME_Find);

                for (size_t i = 0; i < SceneNode.Level->Actors.Num(); ++i)
                {
                    auto actor = SceneNode.Level->Actors(i);
                    if (actor != nullptr)
                    {
                        auto& actorFName = actor->GetClass()->GetFName();

                        // Checking that the current actor is a lamp
                        if (actorFName == classNameLamp1 || actorFName == classNameLamp2 || actorFName == classNameLamp3)
                            m_DynamicLights.push_back(actor);

                        // Checking that the current actor is a trigger light source
                        else if (actorFName == classNameTriggerLight)
                            m_DynamicLights.push_back(actor);

                        // Checking that the current actor is a flashlight augmentation
                        else if (actorFName == classNameAugLight)
                            m_AugLight = (AAugmentation*)actor;
                    }
                }

                m_CurrentLevelIndex = levelIndex;
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
                m_SquaredViewConeCos = frustumConeCosine * frustumConeCosine;
            }
        }

        void CheckViewChange(const FSceneNode& SceneNode, const FSavedPoly& Poly)
        {
            assert(Poly.NumPts >= 3);

            //    if (m_Coords.Origin != SceneNode.Coords.Origin || m_Coords.XAxis != SceneNode.Coords.XAxis || m_Coords.YAxis != SceneNode.Coords.YAxis || m_Coords.ZAxis != SceneNode.Coords.ZAxis)
            //    {

            const auto& c = SceneNode.Coords;
            auto viewMatrix = DirectX::XMMatrixSet(
                c.XAxis.X, c.YAxis.X, c.ZAxis.X, c.Origin.X,
                c.XAxis.Y, c.YAxis.Y, c.ZAxis.Y, c.Origin.Y,
                c.XAxis.Z, c.YAxis.Z, c.ZAxis.Z, c.Origin.Z,
                0.0f, 0.0f, 0.0f, 1.0f
            );
            
            // TODO: need to figure out how to track viewport changes in a fast way to use following method
            // SetDynamicLights(SceneNode);

            m_Buffer.m_Data.ViewMatrix = DirectX::XMMatrixTranspose(viewMatrix);
            m_Buffer.m_Data.Origin = { c.Origin.X, c.Origin.Y, c.Origin.Z, 0 };
            m_Buffer.MarkAsDirty();

            //        m_Coords = SceneNode.Coords;
            //    }        
        }

        void UpdateAndBind(unsigned int iSlot)
        {
            m_Buffer.UpdateAndBind(iSlot);
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
            float padding[3];
        };

        long long m_InitialTime;

        float GetTimeSinceStart()
        {
            using namespace std::chrono;
            return float(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - m_InitialTime);
        }

        ConstantBuffer<PerTick> m_Buffer;

    public:
        PerTickBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
            : m_Buffer(Device, DeviceContext)
        {
            using namespace std::chrono;
            m_InitialTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        }

        PerTickBuffer(const PerTickBuffer&) = delete;
        PerTickBuffer& operator=(const PerTickBuffer&) = delete;

        void NewTick()
        {
            m_Buffer.m_Data.fTimeInSeconds = GetTimeSinceStart();
            m_Buffer.MarkAsDirty();
        }

        void UpdateAndBind(unsigned int iSlot)
        {
            m_Buffer.UpdateAndBind(iSlot);
        }
    }
    m_PerTickBuffer;

    /// <summary>
    /// Prepares and stores constant buffer data
    /// related to the currently rendered polygon only
    /// </summary>
    class PerComplexPolyBuffer
    {
        struct PerComplexPoly
        {
            XMVECTORU32 PolyControl;
            // list of IDs of static light sources that are visible for the current ComplexPoly
            XMVECTORU32 StaticLightIds[MAX_LIGHTS_INDEX_SIZE];
            XMMATRIX PolyVM;
        };

        ConstantBuffer<PerComplexPoly> m_Buffer;

    public:
        PerComplexPolyBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
            : m_Buffer(Device, DeviceContext)
        { }

        PerComplexPolyBuffer(const PerComplexPolyBuffer&) = delete;
        PerComplexPolyBuffer& operator=(const PerComplexPolyBuffer&) = delete;

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

        void UpdateAndBind(unsigned int iSlot)
        {
            m_Buffer.UpdateAndBind(iSlot);
        }
    }
    m_PerComplexPolyBuffer;
       
};