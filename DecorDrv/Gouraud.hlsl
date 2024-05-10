#include "Defines.hlsli"
#include "Common.hlsli"

Texture2D TexDiffuse : register(t0);

struct SPoly
{
    float4 Pos : Position0;
    float3 Normal : Normal0;
    float3 Color : Color0;
    float2 TexCoord : TexCoord0;
    uint PolyFlags : BlendIndices0;
};

struct VSOut
{
    float4 Pos : SV_Position;
    float3 Normal : Normal0;
    float3 Color : Color0;
    float2 TexCoord : TexCoord0;
    uint PolyFlags : BlendIndices0;
    float4 PosWorld : Position1;
};

VSOut VSMain(const SPoly Input)
{
    VSOut Output;
    Output.Pos = mul(Input.Pos, ProjectionMatrix);
    Output.PosWorld = Input.Pos;	
    Output.Normal = -normalize(Input.Normal);
    Output.Color = Input.Color;
    Output.TexCoord = Input.TexCoord;
    Output.PolyFlags = Input.PolyFlags;
    return Output;
}

float4 PSMain_Old(const VSOut Input) : SV_Target
{
    float4 Color = float4(Input.Color, 1.0f);

    if (Input.PolyFlags & (PF_Masked | PF_Modulated))
    {
        clip(TexDiffuse.Sample(SamPoint, Input.TexCoord).a - 0.5f);	
    }

    if (Input.PolyFlags & PF_Modulated)
    {
        return TexDiffuse.Sample(SamPoint, Input.TexCoord);
    }

    const float3 Diffuse = TexDiffuse.Sample(SamLinear, Input.TexCoord).rgb;
    Color.rgb *= Diffuse;

    return Color;
}

PbrM_MatInfo PbrM_ComputeMatInfo(VSOut input)
{    
    //const float4 baseColor = BaseColorTexture.Sample(LinearSampler, input.Tex) * BaseColorFactor;
    //const float4 baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.5f, 0.5f, 1.f); // For now, we will use a fixed BaseColor, but then we will need to take it from TexDiffuse
    float4 baseColor;
    //if (input.TexFlags & 0x00000001)
        baseColor = TexDiffuse.Sample(SamLinear, input.TexCoord) * float4(0.5f, 0.5f, 0.5f, 1.0f);
    //else
    //    baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.5f, 0.5f, 1.f);

    //const float4 metalRoughness = MetalRoughnessTexture.Sample(LinearSampler, input.Tex) * MetallicRoughnessFactor;
    //const float4 metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.f, 0.4f, 0.f, 0.f); // For now, we will use a fixed metal Roughness
    float4 metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.f, 0.4f, 0.f, 0.f); // For now, we will use a fixed metal Roughness

    //if ((input.PolyFlags & PF_Masked) || (input.PolyFlags & PF_Translucent))
    //{
    //    metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.0f, 0.9f, 0.f);
    //}

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
    float4 Color = float4(input.Color, 1.0f);

    if (input.PolyFlags & (PF_Masked | PF_Modulated))
    {
        clip(TexDiffuse.Sample(SamPoint, input.TexCoord).a - 0.5f);	
    }

    if (input.PolyFlags & PF_Modulated)
    {
        return TexDiffuse.Sample(SamPoint, input.TexCoord);
    }

    const float3 Diffuse = TexDiffuse.Sample(SamLinear, input.TexCoord).rgb;
    
    if (input.PolyFlags & PF_Translucent && !any(Diffuse))
    {
        clip(-1.0f);
    }
    
    Color.rgb *= Diffuse;
    Color.a = input.Pos.z;
    return Color;

/*
    if (input.PolyFlags & (PF_Masked | PF_Modulated))
    {
        clip(TexDiffuse.Sample(SamPoint, input.TexCoord).a - 0.5f);	
    }

    if (input.PolyFlags & PF_Modulated)
    {
        return TexDiffuse.Sample(SamPoint, input.TexCoord);
    }

    PbrM_ShadingCtx shadingCtx;
    //shadingCtx.normal = normalize(input.Normal + 0.5 * normalize(TexDiffuse.Sample(SamLinear, input.TexCoord).rgb));
    shadingCtx.normal = normalize(input.Normal); //ComputeNormal(input); - now used input.Normal for testing    
    shadingCtx.viewDir = normalize((float3)input.PosWorld);

    const PbrM_MatInfo matInfo = PbrM_ComputeMatInfo(input);

    float4 output = float4(0, 0, 0, 0);
    float4 ambientLightLuminance = float4(0.02, 0.02, 0.02, 1);

    float4 luminance = float4(0.7, 0.7, 0.3, 1);

    output += PbrM_AmbLightContrib(ambientLightLuminance, shadingCtx, matInfo);

    //output += PbrM_DirLightContrib(directionalLightVector,
    //    luminance,
    //    shadingCtx,
    //    matInfo);

    uint firstLightsInSlices[MAX_SLICE_DATA_SIZE] = (uint[MAX_SLICE_DATA_SIZE])IndexesOfFirstLightsInSlices;
    
    uint slice = (uint)floor((input.PosWorld.z - NEAR_CLIPPING_DISTANCE) / SLICE_THICKNESS);

    slice = max(0, slice);
    slice = min(SLICE_MAX_INDEX, slice);

    uint lightId = 0;
    uint lightType = 0;
    uint lightInfo = 0;
    for (uint i = firstLightsInSlices[slice]; i < firstLightsInSlices[slice+1]; ++i)
    {
        lightId = LightIndexesFromAllSlices[i >> 2][i & 3];
        float4 intencity = Lights[lightId];

        lightInfo = (uint)intencity.w;
        if (bool(lightInfo & LIGHT_SPECIAL_MASK) != bool(input.PolyFlags & PF_SpecialLit))
            continue;

        lightType = lightInfo & LIGHT_TYPE_MASK;

        // Point
        if (lightType == LIGHT_POINT) {
            float4 lightPosData = Lights[lightId + 1];
            float3 posWorld = (float3)input.PosWorld;

            // Skip point lights that are out of range of the point being shaded.
            if (length((float3)lightPosData - posWorld) < lightPosData.w)
            output += PbrM_PointLightContrib(posWorld,
                lightPosData,
                intencity,
                shadingCtx,
                matInfo);
        }

        // "Ambient" Point
        if (lightType == LIGHT_POINT_AMBIENT) {
            float4 lightPosData = Lights[lightId + 1];
            float3 posWorld = (float3)input.PosWorld;

            // Skip point lights that are out of range of the point being shaded.
            if (length((float3)lightPosData - posWorld) < lightPosData.w)
                output += PbrM_AmbPointLightContrib(posWorld,
                    lightPosData,
                    intencity,
                    shadingCtx,
                    matInfo);
        }

        // Spot
        if (lightType == LIGHT_SPOT) {
            float4 lightPosData = Lights[lightId + 1];
            float4 lightDirData = Lights[lightId + 2];
            float3 posWorld = (float3)input.PosWorld;

            // Skip spot lights that are out of range of the point being shaded.
            if (length((float3)lightPosData - posWorld) < lightPosData.w)
            output += PbrM_SpotLightContrib(posWorld,
                lightPosData,
                lightDirData,
                intencity,
                shadingCtx,
                matInfo);
        }
    }


    //output += EmissionTexture.Sample(LinearSampler, input.Tex) * EmissionFactor;
    
    //output.rgb = input.Normal;
    //output.rgb = TexNoise.Sample(SamLinear, input.TexCoord).rrr;

    output.a = 1;
    return output;
*/
}