#include "Common.hlsli"

#define CONVERT_SRGB_INPUT_TO_LINEAR
#define CONVERT_LINEAR_OUTPUT_TO_SRGB

//#define USE_SMOOTH_REFRACTION_APPROX
//#define USE_ROUGH_REFRACTION_APPROX

static const float PI = 3.14159265f;

Texture2D TexDiffuse : register(t0);
Texture2D TexLight : register(t1);

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
    float4 PosWorld : Position1;
    float3 Normal : Normal;
};

SPoly VSMain(const SPoly Input)
{    
    return Input;
}

//float4 PSMain(const VSOut Input) : SV_Target
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

struct PbrM_MatInfo
{
    float4 diffuse;
    float4 f0;
    float  alphaSq;
    float  occlusion;

    float specPower; // temporary blinn-phong approximation
};

struct PbrM_ShadingCtx
{
    float3 normal;
    float3 viewDir;
};

PbrM_MatInfo PbrM_ComputeMatInfo(VSOut input)
{
// Пока заомментируем, но в дальнейшем возможно будет использоваться
//    const float4 baseColor = BaseColorTexture.Sample(LinearSampler, input.Tex) * BaseColorFactor;
//
//    const float4 metalRoughness = MetalRoughnessTexture.Sample(LinearSampler, input.Tex) * MetallicRoughnessFactor;
//    const float4 metalness = float4(metalRoughness.bbb, 1);
//    const float  roughness = metalRoughness.g;
//
//    const float4 f0Diel = float4(0.04, 0.04, 0.04, 1);
//#if defined USE_SMOOTH_REFRACTION_APPROX || defined USE_ROUGH_REFRACTION_APPROX
//    const float4 diffuseDiel = baseColor;
//#else
//    const float4 diffuseDiel = (float4(1, 1, 1, 1) - f0Diel) * baseColor;
//#endif
//
//    const float4 f0Metal = baseColor;
//    const float4 diffuseMetal = float4(0, 0, 0, 1);

//    PbrM_MatInfo matInfo;
//    matInfo.diffuse = lerp(diffuseDiel, diffuseMetal, metalness);
//    matInfo.f0 = lerp(f0Diel, f0Metal, metalness);
//    matInfo.alphaSq = max(roughness * roughness, 0.0001f);
//    matInfo.occlusion = lerp(1., OcclusionTexture.Sample(LinearSampler, input.Tex).r, OcclusionTexStrength);

    PbrM_MatInfo matInfo;
    matInfo.diffuse = float4(0.5, 0.5, 0.5, 0.5);
    matInfo.f0 = float4(0.5, 0.5, 0.5, 0.5);
    matInfo.alphaSq = 0.0001f;
    matInfo.occlusion = 0.5;

    return matInfo;
}

float4 FresnelSchlick(PbrM_MatInfo matInfo, float cosTheta)
{
    const float4 f0 = matInfo.f0;
    return f0 + (1 - f0) * pow(clamp(1 - cosTheta, 0, 1), 5);
}

float4 FresnelIntegralApprox(float4 f0)
{
    return lerp(f0, 1.f, 0.05f); // Very ad-hoc approximation :-)
}

float4 PbrM_AmbLightContrib(float4 luminance,
    PbrM_ShadingCtx shadingCtx,
    PbrM_MatInfo matInfo)
{
#if defined USE_SMOOTH_REFRACTION_APPROX
    const float NdotV = max(dot(shadingCtx.normal, shadingCtx.viewDir), 0.);
    const float4 fresnelNV = FresnelSchlick(matInfo, NdotV);

    const float4 fresnelIntegral = FresnelIntegralApprox(matInfo.f0);

    const float4 diffuse = matInfo.diffuse * (1.0 - fresnelNV) * (1.0 - fresnelIntegral);
    const float4 specular = fresnelNV; // assuming that full specular lobe integrates to 1
#elif defined USE_ROUGH_REFRACTION_APPROX
    const float NdotV = max(dot(shadingCtx.normal, shadingCtx.viewDir), 0.);
    const float4 fresnelNV = FresnelSchlick(matInfo, NdotV);

    const float4 fresnelIntegral = FresnelIntegralApprox(matInfo.f0);

    const float4 roughFresnelNV = lerp(fresnelNV, fresnelIntegral, matInfo.alphaSq);

    const float4 diffuse = matInfo.diffuse * (1.0 - roughFresnelNV) /** (1.0 - fresnelIntegral)*/;
    const float4 specular = roughFresnelNV;
#else
    const float4 diffuse = matInfo.diffuse;
    const float4 specular = matInfo.f0; // assuming that full specular lobe integrates to 1
#endif

    return (diffuse + specular) * luminance * matInfo.occlusion;
}

float ThetaCos(float3 normal, float3 lightDir)
{
    return max(dot(normal, lightDir), 0.);    
}

float GgxMicrofacetDistribution(PbrM_MatInfo matInfo, float NdotH)
{
    const float alphaSq = matInfo.alphaSq;

    const float f = (NdotH * alphaSq - NdotH) * NdotH + 1.0;
    return alphaSq / (PI * f * f);
}

float GgxVisibilityOcclusion(PbrM_MatInfo matInfo, float NdotL, float NdotV)
{
    float alphaSq = matInfo.alphaSq;

    float ggxv = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaSq) + alphaSq);
    float ggxl = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaSq) + alphaSq);

    return 0.5 / (ggxv + ggxl);
}

float4 DiffuseBRDF()
{
    return 1 / PI;
};

float4 PbrM_BRDF(float3 lightDir, PbrM_ShadingCtx shadingCtx, PbrM_MatInfo matInfo)
{
    float4 brdf = float4(0, 0, 0, 1);

    float NdotL = dot(lightDir, shadingCtx.normal);
    float NdotV = dot(shadingCtx.viewDir, shadingCtx.normal);

    //if (NdotL < 0)
    //    return float4(0, 0, 0, 1);
    if (NdotL >= 0)
    {
        NdotL = max(NdotL, 0.01f);
        NdotV = max(NdotV, 0.01f);

        // Halfway vector
        const float3 halfwayRaw = lightDir + shadingCtx.viewDir;
        const float  halfwayLen = length(halfwayRaw);
        const float3 halfway = (halfwayLen > 0.001f) ? (halfwayRaw / halfwayLen) : shadingCtx.normal;        
        const float  halfwayCos = max(dot(halfway, shadingCtx.normal), 0.);
        const float  HdotV = max(dot(halfway, shadingCtx.viewDir), 0.);

        // Microfacet-based specular component
        const float4 fresnelHV = FresnelSchlick(matInfo, HdotV);
        const float distr = GgxMicrofacetDistribution(matInfo, halfwayCos);
        const float vis = GgxVisibilityOcclusion(matInfo, NdotL, NdotV);

        const float4 specular = fresnelHV * vis * distr;

        // Diffuse
#if defined USE_SMOOTH_REFRACTION_APPROX
        const float4 fresnelNV = FresnelSchlick(matInfo, NdotV);
        const float4 fresnelNL = FresnelSchlick(matInfo, NdotL);
        const float4 diffuse = DiffuseBRDF() * matInfo.diffuse * (1.0 - fresnelNV) * (1.0 - fresnelNL);
#elif defined USE_ROUGH_REFRACTION_APPROX
        const float4 fresnelNV = FresnelSchlick(matInfo, NdotV);
        const float4 fresnelNL = FresnelSchlick(matInfo, NdotL);

        const float4 fresnelIntegral = FresnelIntegralApprox(matInfo.f0);

        const float4 roughFresnelNV = lerp(fresnelNV, fresnelIntegral, matInfo.alphaSq);
        const float4 roughFresnelNL = lerp(fresnelNL, fresnelIntegral, matInfo.alphaSq);

        const float4 diffuse = DiffuseBRDF() * matInfo.diffuse * (1.0 - roughFresnelNV) * (1.0 - roughFresnelNL);
#else
        const float4 diffuse = DiffuseBRDF() * matInfo.diffuse;
#endif

        //return specular + diffuse;
        brdf = specular + diffuse;
    }

    return brdf;
}

float4 PbrM_DirLightContrib(float3 lightDir,
    float4 luminance,
    PbrM_ShadingCtx shadingCtx,
    PbrM_MatInfo matInfo)
{
    const float thetaCos = ThetaCos(shadingCtx.normal, lightDir);    

    const float4 brdf = PbrM_BRDF(lightDir, shadingCtx, matInfo);

    return brdf * thetaCos * luminance;    
}

float4 PbrM_PointLightContrib(float3 surfPos,
    float4 lightPos,
    float4 intensity,
    PbrM_ShadingCtx shadingCtx,
    PbrM_MatInfo matInfo)
{
    const float3 dirRaw = (float3)lightPos - surfPos;
    const float  len = length(dirRaw);
    const float3 lightDir = dirRaw / len;
    const float  distSqr = len * len;

    const float thetaCos = ThetaCos(shadingCtx.normal, lightDir);

    const float4 brdf = PbrM_BRDF(lightDir, shadingCtx, matInfo);

    return brdf * thetaCos * intensity / distSqr;
}

float4 PSMain(const VSOut input) : SV_Target
//float4 PsPbrMetalness(const VSOut input) : SV_Target
{
    PbrM_ShadingCtx shadingCtx;
    shadingCtx.normal = normalize(input.Normal); //ComputeNormal(input); - now used input.Normal for testing    
    shadingCtx.viewDir = normalize((float3)input.PosWorld);

    const PbrM_MatInfo matInfo = PbrM_ComputeMatInfo(input);

    float4 output = float4(0, 0, 0, 0);
    float4 ambientLightLuminance = float4(0.5, 0.5, 0.5, 1);
    float3 directionalLightVector = normalize((float3)LightDir);

    float4 luminance = float4(1, 2, 0, 1);

    //output += PbrM_AmbLightContrib(ambientLightLuminance, shadingCtx, matInfo);

    output += PbrM_DirLightContrib(directionalLightVector,
        luminance,
        shadingCtx,
        matInfo);

    uint firstLightsInSlices[16] = (uint[16])IndexesOfFirstLightsInSlices;
    //uint lightIndexes[1024] = (uint[1024])LightIndexesFromAllSlices;
    
    uint slice = (uint)floor((input.PosWorld.z - 1.0f) / 3275.9f);

    slice = max(0, slice);
    slice = min(9, slice);
    
    //if (slice == 0)
    //    output = float4(0.1, 0.0, 0.3, 1);
    //if (slice == 1)
    //    output = float4(0.7, 0.0, 0.3, 1);    

    uint lightId = 0;
    for (uint i = firstLightsInSlices[slice]; i < firstLightsInSlices[slice+1]; i++)
    {
        //lightId = lightIndexes[i];
        lightId = LightIndexesFromAllSlices[i >> 2][i & 3];
        float4 intencity = Lights[lightId];

        // Point
        if (intencity.w == 2) {
            float4 lightPosData = Lights[lightId + 1];
            float3 posWorld = (float3)input.PosWorld;

            // Skip point lights that are out of range of the point being shaded.
            //if (length((float3)lightPosData - posWorld) < lightPosData.w)
            output += PbrM_PointLightContrib(posWorld,
                lightPosData,
                intencity,
                shadingCtx,
                matInfo);
        }
    }


    //output += EmissionTexture.Sample(LinearSampler, input.Tex) * EmissionFactor;

    output.a = 1;
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
        output.PosWorld = In[i].Pos;
        output.Normal = vn;
        output.TexCoord = In[i].TexCoord;
        output.TexCoord1 = In[i].TexCoord1;
        output.PolyFlags = In[i].PolyFlags;
        output.TexFlags = In[i].TexFlags;
        outputStream.Append(output);
    }

    outputStream.RestartStrip();
}