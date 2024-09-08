#include "Defines.hlsli"
#include "Common.hlsli"

Texture2D TexDiffuse : register(t0);
Texture2D TexLight : register(t1);
Texture2D TexNoise : register(t2);
Texture2DArray TexOcclusion : register(t3);

struct SPoly
{
    float4 Pos : Position0;
    float2 TexCoord : TexCoord0;
    float2 TexCoord1 : TexCoord1;
    uint PolyFlags : BlendIndices0;
    uint TexFlags : BlendIndices1;
};

struct VSOut
{
    float4 Pos : SV_Position;    
    float2 TexCoord : TexCoord0;
    float2 TexCoord1 : TexCoord1;    
    uint PolyFlags : BlendIndices0;
    uint TexFlags : BlendIndices1;
    float4 PosView : Position1;
    float4 PosWorld : Position2;
    float3 Normal : Normal;
};

SPoly VSMain(const SPoly Input)
{    
    return Input;
}

float4 PSMain_Old(const VSOut Input) : SV_Target
{
    if (Input.PolyFlags & PF_Masked)
    {
        clip(TexDiffuse.Sample(SamPoint, Input.TexCoord).a - 0.5f);
    }

    float4 Color = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (Input.TexFlags & 0x00000001)
    {
        const float3 Diffuse = TexDiffuse.Sample(SamLinear, Input.TexCoord).rgb;
        Color.rgb *= Diffuse;
    }
    if (Input.TexFlags & 0x00000002)
    {
        const float3 Light = TexLight.Sample(SamLinear, Input.TexCoord1).bgr * 2.0f;
        Color.rgb *= Light;
    }
    
    return Color;
}

PbrM_MatInfo PbrM_ComputeMatInfo(VSOut input)
{    
    //const float4 baseColor = BaseColorTexture.Sample(LinearSampler, input.Tex) * BaseColorFactor;
    //const float4 baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.5f, 0.5f, 1.f); // For now, we will use a fixed BaseColor, but then we will need to take it from TexDiffuse
    float4 baseColor;
    if (input.TexFlags & 0x00000001)
        baseColor = TexDiffuse.Sample(SamLinear, input.TexCoord) * float4(0.5f, 0.5f, 0.5f, 1.0f);
    else
        baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.5f, 0.5f, 1.f);

    //const float4 metalRoughness = MetalRoughnessTexture.Sample(LinearSampler, input.Tex) * MetallicRoughnessFactor;
    //const float4 metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.f, 0.4f, 0.f, 0.f); // For now, we will use a fixed metal Roughness
    float4 metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.f, 0.4f, 0.f, 0.f); // For now, we will use a fixed metal Roughness

    if (input.PolyFlags & PF_Translucent)    
    {
        metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.0f, 0.9f, 0.f);
    }

    const float4 metalness = float4(metalRoughness.bbb, 1);
    const float  roughness = metalRoughness.g;

    const float4 f0Diel = float4(0.04, 0.04, 0.04, 1);
#if defined USE_SMOOTH_REFRACTION_APPROX || defined USE_ROUGH_REFRACTION_APPROX
    const float4 diffuseDiel = baseColor;
#else
    const float4 diffuseDiel = (float4(1, 1, 1, 1) - f0Diel) * baseColor;
#endif

    const float4 f0Metal = baseColor;
    const float4 diffuseMetal = float4(0, 0, 0, 1);

    PbrM_MatInfo matInfo;
    matInfo.diffuse = lerp(diffuseDiel, diffuseMetal, metalness);
    matInfo.f0 = lerp(f0Diel, f0Metal, metalness);
    matInfo.alphaSq = max(roughness * roughness, 0.0001f);
    //matInfo.occlusion = lerp(1., OcclusionTexture.Sample(LinearSampler, input.Tex).r, OcclusionTexStrength);
    matInfo.occlusion = 1.0f; // For now, we will use a fixed mapInfo.occlusion

    return matInfo;
}

float4 PSMain(const VSOut input) : SV_Target0
{
    if (input.TexFlags & 0x00000004)
    {
        if (input.PolyFlags & PF_Masked)
        {
            clip(TexDiffuse.Sample(SamPoint, input.TexCoord).a - 0.5f);
        }

        float4 Color = float4(1.0f, 1.0f, 1.0f, 1.0f);

        if (input.TexFlags & 0x00000001)
        {
            const float3 Diffuse = TexDiffuse.Sample(SamLinear, input.TexCoord).rgb;
            Color.rgb *= Diffuse;
        }
        if (input.TexFlags & 0x00000002)
        {
            const float3 Light = TexLight.Sample(SamLinear, input.TexCoord1).bgr * 2.0f;
            Color.rgb *= Light;
        }
    
        Color.a = input.Pos.z;
        return Color;
    }

    if (input.PolyFlags & PF_Masked)
    {
        clip(TexDiffuse.Sample(SamPoint, input.TexCoord).a - 0.5f);
    }
    
    if (input.PolyFlags & PF_Unlit)
    {
        //return TexDiffuse.Sample(SamLinear, input.TexCoord).rgba;
        return float4(TexDiffuse.Sample(SamLinear, input.TexCoord).rgb, input.Pos.z);
    }

    PbrM_ShadingCtx shadingCtx;
    shadingCtx.normal = normalize(input.Normal);
    shadingCtx.viewDir = normalize((float3) input.PosView);

    const PbrM_MatInfo matInfo = PbrM_ComputeMatInfo(input);

    float4 output = float4(0, 0, 0, 0);
    float4 ambientLightLuminance = float4(0.05, 0.05, 0.05, 1);

    float4 luminance = float4(0.7, 0.7, 0.3, 1);

    output += PbrM_AmbLightContrib(ambientLightLuminance, shadingCtx, matInfo);

    //output += PbrM_DirLightContrib(directionalLightVector,
    //    luminance,
    //    shadingCtx,
    //    matInfo);
    
    for (uint i = 0; i < PolyControl.x; ++i)
    {
        uint occlusionMapId = StaticLightIds[i].x;
        
        //output += TexOcclusion.SampleLevel(SamLinear, float3(input.TexCoord1.x, input.TexCoord1.y, occlusionMapId), 0).rrra * 0.01f;
        
        float occlusionValue = TexOcclusion.SampleLevel(SamLinear, float3(input.TexCoord1.x, input.TexCoord1.y, occlusionMapId), 0).r;
        
        if (occlusionValue > 0)
        {
            uint lightBufPos = StaticLightIds[i].y;
            float4 intencity = StaticLights[lightBufPos];
         
            uint lightInfo = asuint(intencity.w);
            if (bool(lightInfo & LIGHT_SPECIAL_MASK) != bool(input.PolyFlags & PF_SpecialLit))
                continue;            
            
            float lightPeriod = ((lightInfo & LIGHT_PERIOD_MASK) >> LIGHT_PERIOD_OFFSET);
            
            float lightTypeRate = 1.0f;
            uint lightType = (lightInfo & LIGHT_TYPE_MASK) >> LIGHT_TYPE_OFFSET;
            switch (lightType)
            {
                case LT_Blink:
                    if (fTick.y < 0.2f)
                        lightTypeRate = 0.0f;
                    break;
                case LT_Flicker:
                    if (fTick.y > 0.2f)
                        lightTypeRate = 0.0f;
                    break;
                case LT_Pulse:                    
                    lightTypeRate = (sin(fTick.x / (lightPeriod * 4.0f)) + 1.0f) / 2.0f;
                    break;
                case LT_Strobe:
                    if (sin(fTick.x / (lightPeriod * 4.0f)) < 0.0f)
                        lightTypeRate = 0.0f;
                    break;
            }
            
            if (lightTypeRate == 0.0f)
                continue;
            
            occlusionValue *= lightTypeRate; // effect of the light type on occlusion
            
            uint lightEffect = lightInfo & LIGHT_EFFECT_MASK;
            switch (lightEffect)
            {
                case LE_Cylinder:
                    {
                        float4 lightPosData = StaticLights[lightBufPos + 1];
                
                        float lightRadius = lightPosData.w;
                        lightPosData.w = 0;
                        lightPosData = mul(lightPosData - Origin, ViewMatrix);
                        lightPosData.w = lightRadius;
                
                        //float4 lightPosData = mul(StaticLights[lightBufPos + 1] - Origin, ViewMatrix);
                        float3 posView = (float3)input.PosView;

                        // Skip point lights that are out of range of the point being shaded.
                        if (length((float3) lightPosData - posView) < lightPosData.w)
                        {
                            output += PbrM_AmbPointLightContrib(posView,
                                lightPosData,
                                intencity,
                                shadingCtx,
                                matInfo) * occlusionValue;
                        }
                    }
                    break;
                case LE_Spotlight:
                case LE_StaticSpot:
                    {
                        float4 lightPosData = StaticLights[lightBufPos + 1];
                
                        float lightRadius = lightPosData.w;
                        lightPosData.w = 0;
                        lightPosData = mul(lightPosData - Origin, ViewMatrix);
                        lightPosData.w = lightRadius;
                
                        float4 lightDirData = StaticLights[lightBufPos + 2];
                
                        float coneAngle = lightDirData.w;
                        lightDirData.w = 0;
                        lightDirData = mul(lightDirData, ViewMatrix);
                        lightDirData.w = coneAngle;
                
                        //float4 lightPosData = mul(StaticLights[lightBufPos + 1] - Origin, ViewMatrix);
                        //float4 lightDirData = mul(StaticLights[lightBufPos + 2], ViewMatrix);
                        float3 posView = (float3) input.PosView;

                        // Skip spot lights that are out of range of the point being shaded.
                        if (length((float3) lightPosData - posView) < lightPosData.w)
                            output += PbrM_SpotLightContrib(posView,
                                lightPosData,
                                lightDirData,
                                intencity,
                                shadingCtx,
                                matInfo) * occlusionValue;
                    }
                    break;
                default:
                    {
                        float4 lightPosData = StaticLights[lightBufPos + 1];
                
                        float lightRadius = lightPosData.w;
                        lightPosData.w = 0;
                        lightPosData = mul(lightPosData - Origin, ViewMatrix);
                        lightPosData.w = lightRadius;
                
                        //float4 lightPosData = mul(StaticLights[lightBufPos + 1] - Origin, ViewMatrix);
                        float3 posView = (float3) input.PosView;

                        // Skip point lights that are out of range of the point being shaded.
                        if (length((float3) lightPosData - posView) < lightPosData.w)
                            output += PbrM_PointLightContrib(posView,
                                lightPosData,
                                intencity,
                                shadingCtx,
                                matInfo) * occlusionValue;
                    }
                    break;
            }
        }                
    }
    
    // ќбрабатываем динамические источники света
    int lightBufPos = 0;
    while (lightBufPos < MAX_LIGHTS_DATA_SIZE)
    {
        float4 intencity = DynamicLights[lightBufPos];
        
        uint lightInfo = asuint(intencity.w);
        
        uint lightEffect = lightInfo & LIGHT_EFFECT_MASK;
        if (lightEffect == LE_None)
            break;
        
        switch (lightEffect)
        {
            case LE_Spotlight:
            case LE_StaticSpot:
                {
                    float4 lightPosData = DynamicLights[lightBufPos + 1];
                    float4 lightDirData = DynamicLights[lightBufPos + 2];
                    float3 posView = (float3)input.PosView;
                    lightBufPos += 3;
                
                    // Skip spot lights that are out of range of the point being shaded.
                    if (length((float3) lightPosData - posView) < lightPosData.w)
                        output += PbrM_SpotLightContrib(posView,
                                lightPosData,
                                lightDirData,
                                intencity,
                                shadingCtx,
                                matInfo);
                }
                break;
            default:
                {
                    float4 lightPosData = DynamicLights[lightBufPos + 1];
                    float3 posView = (float3)input.PosView;
                    lightBufPos += 2;

                    // Skip point lights that are out of range of the point being shaded.
                    if (length((float3) lightPosData - posView) < lightPosData.w)
                        output += PbrM_PointLightContrib(posView,
                                lightPosData,
                                intencity,
                                shadingCtx,
                                matInfo);
                }
                break;
        }
    }
    
    //output += EmissionTexture.Sample(LinearSampler, input.Tex) * EmissionFactor;

    //// Original lightmap    
    //if (input.TexFlags & 0x00000002)
    //{        
    //    const float3 Light = TexOcclusion.SampleLevel(SamLinear, float3(input.TexCoord1.x, input.TexCoord1.y, 0), 2).rrr;
    //    output.rgb *= Light;	    
    //}

    output.a = input.Pos.z;
    return output;
}

[maxvertexcount(3)]
void GSMain(triangle SPoly In[3], inout TriangleStream<VSOut> outputStream)
{
    float3 v0 = In[0].Pos.xyz;
    float3 v1 = In[1].Pos.xyz;
    float3 v2 = In[2].Pos.xyz;

    float3 vn = normalize(cross(v1 - v0, v2 - v0));

    for (uint i = 0; i < 3; i += 1)
    {
        VSOut output;
        output.Pos = mul(In[i].Pos, ProjectionMatrix);
        output.PosView = In[i].Pos;
        output.PosWorld = mul(In[i].Pos, ViewMatrixInv) + Origin;
        output.Normal = vn;
        output.TexCoord = In[i].TexCoord;
        output.TexCoord1 = In[i].TexCoord1;
        output.PolyFlags = In[i].PolyFlags;
        output.TexFlags = In[i].TexFlags;
        outputStream.Append(output);
    }

    outputStream.RestartStrip();
}