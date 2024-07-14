module;

#include <D3D11.h>
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cassert>
#include <random>

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

    void SetComplexPoly(const FSceneNode& SceneNode, const FSavedPoly& Poly, bool isWaterSurface)
    {        
        const auto& lightCache = m_PerSceneBuffer.GetLightCache();
        m_PerComplexPolyBuffer.SetComplexPoly(SceneNode, Poly, lightCache);

        if (isWaterSurface)
            m_PerFrameBuffer.CheckScreenWaterIntersection(SceneNode, Poly);
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
            XMMATRIX ViewMatrixInv;
            XMVECTOR Origin;
            XMVECTOR FlashColor;
            XMVECTOR DynamicLights[MAX_LIGHTS_DATA_SIZE];
            uint32_t FrameControl;
            float ScreenWaterLevel; // Уровень, на который камера погружена в воду (0 - не погружена, 1 - погружена полностью)
        };

        ConstantBuffer<PerFrame> m_Buffer;

        // Index of the current level (used to determine if a level has been changed)
        int m_CurrentLevelIndex;

        // Light sources on the current level (actualy these are "dynamic" light sources as they can move or switch on/off)
        AAugmentation* m_AugLight;
        std::vector<AActor*> m_DynamicLights;

        ADeusExPlayer* m_Player = nullptr;

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

        //
        const XMVECTOR ScreenUpDir = { 0.0f, 1.0f, 0.0f, 0.0f };
        XMVECTOR _waterPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
        XMMATRIX _viewMatrix;
        XMMATRIX _viewMatrixInv;

        /// <summary>
        /// Screen height in view space
        /// </summary>
        float _screenHalfHeight;

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

        bool XM_CALLCONV Intersects(
            const XMVECTOR orig, const XMVECTOR dir,
            const XMVECTOR v0, const XMVECTOR v1, const XMVECTOR v2,
            float& t)
        {
            static const float kEpsilon = 1.0e-10f; //0.000000001f;

            // Compute the plane's normal
            auto v0v1 = DirectX::XMVectorSubtract(v1, v0);
            auto v0v2 = DirectX::XMVectorSubtract(v2, v0);
            // No need to normalize
            auto N = DirectX::XMVector3Cross(v0v1, v0v2); // N
            float area2 = DirectX::XMVectorGetX(DirectX::XMVector3Length(N));

            // Step 1: Finding P

            // Check if the ray and plane are parallel
            float NdotRayDirection = DirectX::XMVectorGetX(DirectX::XMVector3Dot(N, dir));
            if (fabs(NdotRayDirection) < kEpsilon) // Almost 0
                return false; // They are parallel, so they don't intersect!

            // Compute d parameter using equation 2
            float d = -DirectX::XMVectorGetX(DirectX::XMVector3Dot(N, v0));

            // Compute t (equation 3)
            t = -(DirectX::XMVectorGetX(DirectX::XMVector3Dot(N, orig)) + d) / NdotRayDirection;

            // Check if the triangle is behind the ray
            if (t < 0) return false; // The triangle is behind

            // Compute the intersection point using equation 1
            auto P = DirectX::XMVectorAdd(orig, DirectX::XMVectorScale(dir, t)); //Vec3f P = orig + t * dir;

            // Step 2: Inside-Outside Test
            XMVECTOR C; // Vector perpendicular to triangle's plane

            // Edge 0
            auto edge0 = DirectX::XMVectorSubtract(v1, v0);
            auto vp0 = DirectX::XMVectorSubtract(P, v0);
            C = DirectX::XMVector3Cross(edge0, vp0);
            if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(N, C)) < 0) return false; // P is on the right side

            // Edge 1
            auto edge1 = DirectX::XMVectorSubtract(v2, v1);
            auto vp1 = DirectX::XMVectorSubtract(P, v1);
            C = DirectX::XMVector3Cross(edge1, vp1);
            if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(N, C)) < 0) return false; // P is on the right side

            // Edge 2
            auto edge2 = DirectX::XMVectorSubtract(v0, v2);
            auto vp2 = DirectX::XMVectorSubtract(P, v2);
            C = DirectX::XMVector3Cross(edge2, vp2);
            if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(N, C)) < 0) return false; // P is on the right side

            return true; // This ray hits the triangle
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
                FName classNameJCDentonMale(L"JCDentonMale", EFindName::FNAME_Find);

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

                        else if (actorFName == classNameJCDentonMale)
                        {
                            m_Player = (ADeusExPlayer*)actor;
                            int a = 0;
                        }
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

                _screenHalfHeight = halfFovTan * aspect;                
            }
        }

        void CheckViewChange(const FSceneNode& SceneNode, const FSavedPoly& Poly)
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

            // Если находимся в воде, то проверяем пересечение вертикальной линии камеры с плоскостью воды
            if (m_Player != nullptr && m_Player->Region.Zone->bWaterZone)
            {
                if (DirectX::XMVectorGetZ(_waterPlane) != 0.0f)
                {
                    ///////////////auto viewWaterPlane = DirectX::XMPlaneTransform(_waterPlane, DirectX::XMMatrixTranspose(_viewMatrix));
                    XMVECTOR p1 = { 0.0f, -_screenHalfHeight, 1.0f, 0.0f }; // screen bottom center point
                    XMVECTOR p2 = { 0.0f, +_screenHalfHeight, 1.0f, 0.0f }; // screen up center point

                    // Переводим p1 и p2 в мировое пространство и работаем с ними                    
                    p1 = DirectX::XMVectorAdd(DirectX::XMVector3Transform(p1, _viewMatrixInv), { c.Origin.X, c.Origin.Y, c.Origin.Z, 0.0f });
                    p2 = DirectX::XMVectorAdd(DirectX::XMVector3Transform(p2, _viewMatrixInv), { c.Origin.X, c.Origin.Y, c.Origin.Z, 0.0f });                    

                    if (DirectX::XMVectorGetZ(p2) <= -DirectX::XMVectorGetW(_waterPlane))
                        m_Buffer.m_Data.ScreenWaterLevel = -1.0f;
                    else
                    {
                        auto intersectionPoint = DirectX::XMPlaneIntersectLine(_waterPlane, p1, p2);

                        // Находим уровень, на котором водная поверхность пересекает экран
                        // (расстояние от нижней средней точки до точки пересечения делим на высоту экрана)
                        auto intersectionDist = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(intersectionPoint, p1)));
                        // передаем полученное значение в шейдер
                        m_Buffer.m_Data.ScreenWaterLevel = intersectionDist / (_screenHalfHeight * 2.0f);
                    }                                        
                }
            }

            // TODO: need to figure out how to track viewport changes in a fast way to use following method
            // SetDynamicLights(SceneNode);

            m_Buffer.m_Data.ViewMatrix = DirectX::XMMatrixTranspose(_viewMatrix);
            m_Buffer.m_Data.ViewMatrixInv = DirectX::XMMatrixTranspose(_viewMatrixInv);
            m_Buffer.m_Data.Origin = { c.Origin.X, c.Origin.Y, c.Origin.Z, 0 };
            m_Buffer.MarkAsDirty();

            //        m_Coords = SceneNode.Coords;
            //    }        
        }

        void CheckScreenWaterIntersection(const FSceneNode& SceneNode, const FSavedPoly& Poly)
        {
            if (m_Player != nullptr && m_Player->Region.Zone->bWaterZone)
            {
                for (int i = 2; i < Poly.NumPts; i++)
                {
                    XMVECTOR v0 = { Poly.Pts[0]->Point.X, Poly.Pts[0]->Point.Y, Poly.Pts[0]->Point.Z, 0.0f };
                    XMVECTOR v1 = { Poly.Pts[i - 1]->Point.X, Poly.Pts[i - 1]->Point.Y, Poly.Pts[i - 1]->Point.Z, 0.0f };
                    XMVECTOR v2 = { Poly.Pts[i]->Point.X, Poly.Pts[i]->Point.Y, Poly.Pts[i]->Point.Z, 0.0f };

                    XMVECTOR origin = { 0.0f, -_screenHalfHeight, 1.0f, 0.0f }; // screen bottom center point

                    float dist;
                    if (Intersects(origin, ScreenUpDir, v0, v1, v2, dist))
                    {
                        // вычисляем точку пересечения, приводим ее в мировые координаты и сохраняеем ее высоту (z-компонента)
                        const auto& co = SceneNode.Coords.Origin;
                        auto intersectionPoint = DirectX::XMVectorAdd(
                            DirectX::XMVector3Transform(
                                DirectX::XMVectorAdd(origin, DirectX::XMVectorScale(ScreenUpDir, dist)),
                                _viewMatrixInv
                            ), { co.X, co.Y, co.Z, 0.0f });
                        _waterPlane = { 0.0f, 0.0f, 1.0f, -DirectX::XMVectorGetZ(intersectionPoint) };

                        return;
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
            if (m_Player != nullptr && m_Player->Region.Zone->bWaterZone)
                m_Buffer.m_Data.FrameControl |= 1;
            else
                m_Buffer.m_Data.FrameControl &= 0xFFFFFFFE;
            m_Buffer.MarkAsDirty();
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
            m_Buffer.m_Data.fRandom = distribution(generator);
            m_Buffer.m_Data.fNormRandom = normDistribution(generator);
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
            XMVECTORU32 StaticLightIds[MAX_LIGHTS_INDEX_SIZE]; // list of IDs of static light sources that are visible for the current ComplexPoly
        };

        ConstantBuffer<PerComplexPoly> m_Buffer;
        
    public:
        PerComplexPolyBuffer(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
            : m_Buffer(Device, DeviceContext)
        {}

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