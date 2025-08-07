#include "CommonSurface.hlsli"

PbrM_MatInfo PbrM_ComputeWaterInfo(VSOut input)
{
    float4 baseColor;
    if (input.TexFlags & 0x00000001)
        baseColor = TexDiffuse.Sample(SamLinear, input.TexCoord) * float4(0.5f, 0.5f, 0.5f, 1.0f);
    else
        baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.5f, 0.5f, 1.f);

    const float4 metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.0f, 0.9f, 0.f); // For now, we will use a fixed metal Roughness
    const float4 metalness = float4(metalRoughness.bbb, 1);
    const float roughness = metalRoughness.g;

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
    matInfo.occlusion = 1.0f; // For now, we will use a fixed mapInfo.occlusion

    return matInfo;
}

float4 PSMain(const VSOut input) : SV_Target
{
    if (input.PolyFlags & PF_Masked)
    {
        clip(TexDiffuse.Sample(SamPoint, input.TexCoord).a - 0.5f);
    }
    
    PbrM_ShadingCtx shadingCtx;
    shadingCtx.normal = normalize(input.Normal + 0.05f * TexNoise.Sample(SamLinear, input.PosWorld.xy / 900.0f + float2(0.0001f * fTick.x, 0.0f)).rgb);
    shadingCtx.viewDir = normalize((float3) input.PosView);

    const PbrM_MatInfo matInfo = PbrM_ComputeWaterInfo(input);

    float4 output = GetAdvancedPixel(input, shadingCtx, matInfo);
    output += GetDynamicPixel(input, shadingCtx, matInfo);

    if (input.TexFlags & 0x00000010)
        output += TexFog.Sample(SamLinear, input.TexCoord2).bgra * 2.0f;

    output = AddUnderWaterFog(output, input.Pos.z, input.Pos.y) + FlashColor;
    output.a = input.Pos.z * DepthFactor;
    return output;
}