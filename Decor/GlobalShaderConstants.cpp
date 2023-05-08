#include "GlobalShaderConstants.h"
#include <cassert>

GlobalShaderConstants::GlobalShaderConstants(ID3D11Device& Device, ID3D11DeviceContext& DeviceContext)
:m_CBufPerFrame(Device, DeviceContext)
{

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

void GlobalShaderConstants::CheckViewChange(const FSceneNode& SceneNode)
{
    if (m_Coords.Origin != SceneNode.Coords.Origin || m_Coords.XAxis != SceneNode.Coords.XAxis || m_Coords.YAxis != SceneNode.Coords.YAxis || m_Coords.ZAxis != SceneNode.Coords.ZAxis)
    {
        const auto& c = SceneNode.Coords;
        auto viewMatrix = DirectX::XMMatrixSet(
            c.XAxis.X, c.YAxis.X, c.ZAxis.X, c.Origin.X,
            c.XAxis.Y, c.YAxis.Y, c.ZAxis.Y, c.Origin.Y,
            c.XAxis.Z, c.YAxis.Z, c.ZAxis.Z, c.Origin.Z,
            0.0f, 0.0f, 0.0f, 1.0f
        );

        // тестовый direct light source
        const auto& lightDir = DirectX::XMVectorSet(0.7f, 0.5f, -0.9f, 0.0f);

        // ќчищаем информацию об источниках света
        m_LightsData.clear();
        for (size_t i = 0; i < SLICE_NUMBER; ++i)
            m_LightSlices[i].clear();

        // обрабатываем источники света:
        int count = 0;
        size_t lightIndex = 0;
        for (auto& light : m_PointLights)
        {
            // вычисл€ем координаты источников во View Space
            auto lightPos = light->Location.TransformPointBy(c);
            // получаем радиус действи€ источника
            auto lightRadius = light->WorldLightRadius();

            // ѕровер€ем, что источник света находитс€ между ближней и дальней плоскостью видимости
            if ((lightPos.Z + lightRadius) > 1.0f && (lightPos.Z - lightRadius) < 32760.0f)
            {
                // ѕровер€ем, что источник света попадает в конус видимости.
                // »спользуем именно конус, а не усеченную пирамиду (View Frustum) дл€ ускорени€ проверки
                // ѕроверку делаем по методу Charles Bloom'а:
                //      V = sphere.center - cone.apex_location
                //      a = V * cone.direction_normal
                //      Square(a) <= dotProduct(V,V) * Square(cone.cos) -> sphere intersects the cone
                // 
                // “ак как мы работаем с View Space, то вершина конуса €вл€етс€ началом координат и направление конуса совпадает с осью Z.
                // —оответственно, вектор V становитс€ равен lightPos и переменна€ a = lightPos.Z
                if (lightPos.Z * lightPos.Z <= (lightPos | lightPos) * m_SquaredViewConeCos)
                {
                    count++;

                    int firstSlice = 0;
                    if ((lightPos.Z - lightRadius) > 1.0f)
                        firstSlice = floorf((lightPos.Z - lightRadius - 1.0f) / 3275.9f);

                    int lastSlice = SLICE_MAX_INDEX;
                    if ((lightPos.Z + lightRadius) < 32760.0f)
                        lastSlice = floorf((lightPos.Z + lightRadius - 1.0f) / 3275.9f);

                    // добавл€ем источник в список источников
                    LightData lightData;
                    lightData.Color = DirectX::XMVectorSet(1000.0f, 500.5f, 500.5f, (float)LightData::LightType::POINT);
                    lightData.Location = DirectX::XMVectorSet(lightPos.X, lightPos.Y, lightPos.Z, lightRadius);
                    m_LightsData.push_back(lightData);

                    // назначаем источник дл€ слоев
                    for (size_t i = firstSlice; i <= lastSlice; ++i)
                        m_LightSlices[i].push_back(lightIndex);

                    lightIndex++;
                }
            };
        }

        size_t indexCounter = 0;
        for (size_t i = 0; i < SLICE_NUMBER; ++i)
        {
            m_CBufPerFrame.m_Data.IndexesOfFirstLightsInSlices[i] = indexCounter;

            for (const auto& index : m_LightSlices[i])
            {
                m_CBufPerFrame.m_Data.LightIndexesFromAllSlices[indexCounter] = index;
                indexCounter++;
            }
        }
        m_CBufPerFrame.m_Data.IndexesOfFirstLightsInSlices[SLICE_NUMBER] = indexCounter;

        size_t lightDataIndex = 0;
        for (size_t i = 0; i < m_LightsData.size(); ++i)
        {
            m_CBufPerFrame.m_Data.Lights[lightDataIndex] = m_LightsData[i].Color;
            m_CBufPerFrame.m_Data.Lights[lightDataIndex+1] = m_LightsData[i].Location;
            lightDataIndex += 2;
        }

        m_CBufPerFrame.m_Data.ViewMatrix = DirectX::XMMatrixTranspose(viewMatrix);
        m_CBufPerFrame.m_Data.LightDir = DirectX::XMVector4Transform(lightDir, viewMatrix);
        m_CBufPerFrame.MarkAsDirty();

        m_Coords = SceneNode.Coords;
    }    
}

void GlobalShaderConstants::CheckLevelChange(const FSceneNode& SceneNode)
{
    auto levelIndex = SceneNode.Level->GetOuter()->GetFName().GetIndex();

    if (m_CurrentLevelIndex != levelIndex)
    {
        // —цена помен€лась, выгружаем данные по старой сцене:
        m_AugLight = nullptr;
        m_Lamps.clear();
        m_TriggerLights.clear();
        m_PointLights.clear();
        m_SpotLights.clear();

        // «агружаем данные по новой сцене:
        FName classNameLamp1(L"Lamp1", EFindName::FNAME_Find);
        FName classNameLamp2(L"Lamp2", EFindName::FNAME_Find);
        FName classNameLamp3(L"Lamp3", EFindName::FNAME_Find);
        FName classNameTriggerLight(L"TriggerLight", EFindName::FNAME_Find);
        FName classNameAugLight(L"AugLight", EFindName::FNAME_Find);
        FName classNameLight(L"Light", EFindName::FNAME_Find);
        FName classNameSpotlight(L"Spotlight", EFindName::FNAME_Find);

        for (size_t i = 0; i < SceneNode.Level->Actors.Num(); ++i)
        {
            auto actor = SceneNode.Level->Actors(i);
            if (actor != nullptr)
            {
                auto& actorFName = actor->GetClass()->GetFName();

                // ѕроверка, что текущий актор €вл€етс€ лампой
                if (actorFName == classNameLamp1 || actorFName == classNameLamp2 || actorFName == classNameLamp3)
                    m_Lamps.push_back(actor);

                // ѕроверка, что текущий актор €вл€етс€ триггерным источником света
                else if (actorFName == classNameTriggerLight)
                    m_TriggerLights.push_back(actor);

                // ѕроверка, что текущий актор €вл€етс€ аугментацией-фонариком
                else if (actorFName == classNameAugLight)
                    m_AugLight = (AAugmentation*)actor;

                // ѕроверка, что текущий актор €вл€етс€ точечным источником света
                else if (actorFName == classNameLight)
                    m_PointLights.push_back(actor);

                // ѕроверка, что текущий актор €вл€етс€ направленным источником света
                else if (actorFName == classNameSpotlight)
                    m_SpotLights.push_back(actor);
            }
        }

        m_CurrentLevelIndex = levelIndex;
    }
}

void GlobalShaderConstants::Bind()
{
    if (m_CBufPerFrame.IsDirty())
    {
        m_CBufPerFrame.Update();
    }

    m_CBufPerFrame.Bind(0);
}


