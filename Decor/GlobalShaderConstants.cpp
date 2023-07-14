#include <chrono>
#include "GlobalShaderConstants.h"
#include <cassert>

GlobalShaderConstants::GlobalShaderConstants(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext):
    m_CBufPerFrame(Device, DeviceContext),
    m_CBufPerFrameReal(Device, DeviceContext)
{
    using namespace std::chrono;
    m_InitialTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void GlobalShaderConstants::CheckProjectionChange(const FSceneNode& SceneNode)
{
    assert(SceneNode.Viewport);
    assert(SceneNode.Viewport->Actor);
    assert(reinterpret_cast<uintptr_t>(&m_CBufPerFrame.m_Data.ProjectionMatrix) % 16 == 0);

    if (SceneNode.Viewport->Actor->FovAngle != m_fFov || SceneNode.X != m_iViewPortX || SceneNode.Y != m_iViewPortY)
    {
        //Create projection matrix with swapped near/far for better accuracy
        static const float fZNear = 32760.0f;
        static const float fZFar = 1.0f;

        const float fAspect = SceneNode.FX / SceneNode.FY;
        const float fFovVert = SceneNode.Viewport->Actor->FovAngle / fAspect * static_cast<float>(PI) / 180.0f;

        m_CBufPerFrame.m_Data.fRes[0] = SceneNode.FX;
        m_CBufPerFrame.m_Data.fRes[1] = SceneNode.FY;
        m_CBufPerFrame.m_Data.fRes[2] = 1.0f / SceneNode.FX;
        m_CBufPerFrame.m_Data.fRes[3] = 1.0f / SceneNode.FY;
        m_CBufPerFrame.m_Data.ProjectionMatrix = DirectX::XMMatrixPerspectiveFovLH(fFovVert, fAspect, fZNear, fZFar);
        m_CBufPerFrame.m_Data.ProjectionMatrix.r[1].m128_f32[1] *= -1.0f; //Flip Y

        m_CBufPerFrame.MarkAsDirty();
        m_fFov = SceneNode.Viewport->Actor->FovAngle;
        m_iViewPortX = SceneNode.X;
        m_iViewPortY = SceneNode.Y;

        
        auto halfFovAngleInRadians = SceneNode.Viewport->Actor->FovAngle * HALF_DEGREE_TO_RADIANS;
        auto halfFarWidth = fZNear * tan(halfFovAngleInRadians);
        FVector farTopLeftClippingPoint(halfFarWidth, halfFarWidth / fAspect, fZNear);

        auto frustumConeCosine = fZNear / farTopLeftClippingPoint.Size();
        m_SquaredViewConeCos = frustumConeCosine * frustumConeCosine;
    }    
}

void GlobalShaderConstants::CheckViewChange(const FSceneNode& SceneNode, const FSavedPoly& Poly)
{
    assert(Poly.NumPts >= 3);

    int currSurf = SceneNode.Level->Model->Nodes(Poly.iNode).iSurf;
    if (currSurf > -1)
    {
        int lm = SceneNode.Level->Model->Surfs(currSurf).iLightMap;
        if (lm > -1)
        {
            m_TempLights.clear();
            int la = SceneNode.Level->Model->LightMap(lm).iLightActors;
            if (la > -1)
            {                
                AActor* l = SceneNode.Level->Model->Lights(la);
                while (l)
                {
                    m_TempLights.push_back(l);
                    l = SceneNode.Level->Model->Lights(++la);
                }
            }
        }
    }

//    if (m_Coords.Origin != SceneNode.Coords.Origin || m_Coords.XAxis != SceneNode.Coords.XAxis || m_Coords.YAxis != SceneNode.Coords.YAxis || m_Coords.ZAxis != SceneNode.Coords.ZAxis)
//    {
        static const size_t SLICE_MAX_INDEX = SLICE_NUMBER - 1;
        static const float SLICE_THICKNESS = (FAR_CLIPPING_DISTANCE - NEAR_CLIPPING_DISTANCE) / (float)SLICE_NUMBER;

        const auto& c = SceneNode.Coords;
        auto viewMatrix = DirectX::XMMatrixSet(
            c.XAxis.X, c.YAxis.X, c.ZAxis.X, c.Origin.X,
            c.XAxis.Y, c.YAxis.Y, c.ZAxis.Y, c.Origin.Y,
            c.XAxis.Z, c.YAxis.Z, c.ZAxis.Z, c.Origin.Z,
            0.0f, 0.0f, 0.0f, 1.0f
        );

        // тестовый direct light source
        const auto& lightDir = DirectX::XMVectorSet(0.7f, 0.5f, -0.9f, 0.0f);

        // Очищаем информацию об источниках света
        m_LightsData.clear();
        for (size_t i = 0; i < SLICE_NUMBER; ++i)
            m_LightSlices[i].clear();

        // обрабатываем источники света:
        size_t lightIndex = 0;

        //ProcessLightSources(c, m_PointLights, lightIndex);
        //ProcessLightSources(c, m_SpotLights, lightIndex);
        //ProcessLightSources(c, m_TriggerLights, lightIndex);

        //// TODO Некоторые лампы являются точечными источниками света, а некоторые - прожекторами
        //// Нужно учитывать это. Пока все лампы считаем точечными источниками
        //ProcessLightSources(c, m_Lamps, lightIndex);

        ProcessLightSources(c, m_TempLights, lightIndex);

        // Добавляем фонарик, если нужно
        if (m_AugLight != nullptr && m_AugLight->bIsActive)
        {
            LightData lightData;

            lightData.Color = DirectX::XMVectorSet(100000.0f, 100000.0f, 100000.0f, LIGHT_SPOT);
            lightData.Location = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 4000.0f);
            lightData.Direction = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.5f);
            m_LightsData.push_back(lightData);

            m_LightSlices[0].push_back(lightIndex);
            lightIndex++;
        }

        size_t lightDataIndex = 0;
        for (size_t i = 0; i < m_LightsData.size(); ++i)
        {
            m_LightsData[i].RealIndex = lightDataIndex;
            m_CBufPerFrame.m_Data.Lights[lightDataIndex] = m_LightsData[i].Color;
            m_CBufPerFrame.m_Data.Lights[lightDataIndex+1] = m_LightsData[i].Location;

            auto lightType = (uint32_t)DirectX::XMVectorGetW(m_LightsData[i].Color) & LIGHT_TYPE_MASK;
            if (lightType == LIGHT_POINT || lightType == LIGHT_POINT_AMBIENT)
                lightDataIndex += 2;            
            else if (lightType == LIGHT_SPOT)
            {
                m_CBufPerFrame.m_Data.Lights[lightDataIndex + 2] = m_LightsData[i].Direction;
                lightDataIndex += 3;
            }
        }

        size_t indexCounter = 0;
        for (size_t i = 0; i < SLICE_NUMBER; ++i)
        {
            m_CBufPerFrame.m_Data.IndexesOfFirstLightsInSlices[i] = indexCounter;

            for (const auto& index : m_LightSlices[i])
            {
                m_CBufPerFrame.m_Data.LightIndexesFromAllSlices[indexCounter] = m_LightsData[index].RealIndex;
                indexCounter++;
            }
        }
        m_CBufPerFrame.m_Data.IndexesOfFirstLightsInSlices[SLICE_NUMBER] = indexCounter;

        m_CBufPerFrame.m_Data.ViewMatrix = DirectX::XMMatrixTranspose(viewMatrix);
        m_CBufPerFrame.m_Data.LightDir = DirectX::XMVector4Transform(lightDir, viewMatrix);
        m_CBufPerFrame.MarkAsDirty();

//        m_Coords = SceneNode.Coords;
//    }        
}

void GlobalShaderConstants::ProcessLightSources(const FCoords& c, const std::vector<AActor*>& lights, size_t& lightIndex)
{
    static const size_t SLICE_MAX_INDEX = SLICE_NUMBER - 1;
    static const float SLICE_THICKNESS = (FAR_CLIPPING_DISTANCE - NEAR_CLIPPING_DISTANCE) / (float)SLICE_NUMBER;

    for (auto& light : lights)
    {
        // TODO Не используем источник,
        if (light->LightType != LT_None // если его тип LT_None (так бывает, если он выключен),
            && light->LightEffect != LE_NonIncidence // если тип эффекта LE_NonIncidence (вообще-то это точечный источник света, но заполняющий большой объем - нам пока такие не нужны),        
            && light->LightBrightness > 0 // если яркость источника света равна 0,        
            //&& !light->bSpecialLit) // если не установлен признак специального освещения (по-хорошему, его тоже нужно обрабатывать - освещать таким светом только те пиксели, которые тоже имеют признак bSpecialLit)
            )
        {
            // вычисляем координаты источников во View Space
            auto lightPos = light->Location.TransformPointBy(c);
            // получаем радиус действия источника
            auto lightRadius = light->WorldLightRadius();

            auto lightRadiusSquared = lightRadius * lightRadius;

            auto nearLightBoundary = lightPos.Z - lightRadius;
            auto farLightBoundary = lightPos.Z + lightRadius;

            // Проверяем, что источник света находится между ближней и дальней плоскостью видимости
            if (farLightBoundary > NEAR_CLIPPING_DISTANCE && nearLightBoundary < FAR_CLIPPING_DISTANCE)
            {
                // Проверяем, что источник света попадает в конус видимости (либо, что камера находится в пределах действия источника света).
                // Используем именно конус, а не усеченную пирамиду (View Frustum) для ускорения проверки
                // Проверку делаем по методу Charles Bloom'а:
                //      V = sphere.center - cone.apex_location
                //      a = V * cone.direction_normal
                //      Square(a) > dotProduct(V,V) * Square(cone.cos) -> sphere intersects the cone
                // 
                // Так как мы работаем с View Space, то вершина конуса является началом координат и направление конуса совпадает с осью Z.
                // Соответственно, вектор V становится равен lightPos и переменная a = lightPos.Z
                if (lightPos.Z * lightPos.Z > (lightPos | lightPos) * m_SquaredViewConeCos || lightPos.SizeSquared() < lightRadiusSquared)
                {
                    size_t firstSlice = 0;
                    if (nearLightBoundary > NEAR_CLIPPING_DISTANCE)
                        firstSlice = (size_t)floorf((nearLightBoundary - NEAR_CLIPPING_DISTANCE) / SLICE_THICKNESS);

                    size_t lastSlice = SLICE_MAX_INDEX;
                    if (farLightBoundary < FAR_CLIPPING_DISTANCE)
                        lastSlice = (size_t)floorf((farLightBoundary - NEAR_CLIPPING_DISTANCE) / SLICE_THICKNESS);

                    // добавляем источник в список источников
                    LightData lightData;
                    lightData.Location = DirectX::XMVectorSet(lightPos.X, lightPos.Y, lightPos.Z, lightRadius);

                    float lightBrightness = (float)light->LightBrightness / 255.0f;
                    auto& color = HSVtoRGB((float)light->LightHue / 255.0f, (1.0f - (float)light->LightSaturation / 255.0f), 1.0f);
                    color = DirectX::XMVectorScale(color, lightRadiusSquared * (float)light->LightBrightness / 255.0f);

                    if (light->LightEffect == LE_Spotlight)
                    {
                        uint32_t lightType = LIGHT_SPOT;
                        if (light->bSpecialLit)
                            lightType |= LIGHT_SPECIAL_MASK;

                        color = DirectX::XMVectorSetW(color, lightType);

                        auto lightVector = light->Rotation.Vector().TransformVectorBy(c);

                        // угол светового конуса
                        //float spotAngle = (float)light->LightCone / 255.0f * (PI / 2.0f);
                        float spotAngle = (float)light->LightCone / 510.0f * PI;
                        lightData.Direction = DirectX::XMVectorSet(lightVector.X, lightVector.Y, lightVector.Z, spotAngle);
                    }
                    else if (light->LightEffect == LE_Cylinder)
                    {
                        uint32_t lightType = LIGHT_POINT_AMBIENT;
                        if (light->bSpecialLit)
                            lightType |= LIGHT_SPECIAL_MASK;

                        color = DirectX::XMVectorScale(color, 0.00005f * lightBrightness);
                        color = DirectX::XMVectorSetW(color, lightType);
                    }
                    else
                    {
                        uint32_t lightType = LIGHT_POINT;
                        if (light->bSpecialLit)
                            lightType |= LIGHT_SPECIAL_MASK;

                        color = DirectX::XMVectorSetW(color, lightType);
                    }

                    lightData.Color = color;

                    m_LightsData.push_back(lightData);

                    // назначаем источник для слоев
                    for (size_t i = firstSlice; i <= lastSlice; ++i)
                        m_LightSlices[i].push_back(lightIndex);

                    lightIndex++;
                }
            };
        }
    }
}

DirectX::XMVECTOR GlobalShaderConstants::HSVtoRGB(float H, float S, float V)
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
}

void GlobalShaderConstants::CheckLevelChange(const FSceneNode& SceneNode)
{
    auto levelIndex = SceneNode.Level->GetOuter()->GetFName().GetIndex();

    if (m_CurrentLevelIndex != levelIndex)
    {
        // Сцена поменялась, выгружаем данные по старой сцене:
        m_AugLight = nullptr;
        m_Lamps.clear();
        m_TriggerLights.clear();
        m_PointLights.clear();
        m_SpotLights.clear();

        // Загружаем данные по новой сцене:
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

                // Проверка, что текущий актор является лампой
                if (actorFName == classNameLamp1 || actorFName == classNameLamp2 || actorFName == classNameLamp3)
                    m_Lamps.push_back(actor);

                // Проверка, что текущий актор является триггерным источником света
                else if (actorFName == classNameTriggerLight)
                    m_TriggerLights.push_back(actor);

                // Проверка, что текущий актор является аугментацией-фонариком
                else if (actorFName == classNameAugLight)
                    m_AugLight = (AAugmentation*)actor;

                // Проверка, что текущий актор является точечным источником света
                else if (actorFName == classNameLight || actorFName == classNameBarrelFire)
                    m_PointLights.push_back(actor);

                // Проверка, что текущий актор является направленным источником света
                else if (actorFName == classNameSpotlight)
                    m_SpotLights.push_back(actor);
            }
        }

        m_CurrentLevelIndex = levelIndex;
    }
}

float GlobalShaderConstants::GetTimeSinceStart() {
    using namespace std::chrono;
    return float(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - m_InitialTime);
}

void GlobalShaderConstants::Bind()
{
    if (m_CBufPerFrame.IsDirty())
    {
        m_CBufPerFrame.Update();
    }

    m_CBufPerFrame.Bind(0);

    m_CBufPerFrameReal.m_Data.fTimeInSeconds = GetTimeSinceStart();
    m_CBufPerFrameReal.MarkAsDirty();
    m_CBufPerFrameReal.Update();
    m_CBufPerFrameReal.Bind(1);
}


