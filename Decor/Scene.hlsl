//#include "Constants.h"

#define LIGHTS_DATA_MAX_SIZE 1024

#define CONVERT_SRGB_INPUT_TO_LINEAR
#define CONVERT_LINEAR_OUTPUT_TO_SRGB

#define NODE_LIGHTS_MAX_COUNT 64

//#define USE_SMOOTH_REFRACTION_APPROX
//#define USE_ROUGH_REFRACTION_APPROX

static const float PI = 3.14159265f;

// Metalness workflow
Texture2D BaseColorTexture      : register(t0);
Texture2D MetalRoughnessTexture : register(t1);

// Specularity workflow
Texture2D DiffuseTexture        : register(t2);
Texture2D SpecularTexture       : register(t3);

// Both workflows
Texture2D NormalTexture         : register(t4);
Texture2D OcclusionTexture      : register(t5);
Texture2D EmissionTexture       : register(t6);

SamplerState LinearSampler : register(s0);

cbuffer cbScene : register(b0)
{
    float4 AmbientLightLuminance;
    float4 LightsData[LIGHTS_DATA_MAX_SIZE];
};

cbuffer cbFrame : register(b1)
{
    matrix ViewMtrx;
    float4 CameraPos;
    matrix ProjectionMtrx;
};

cbuffer cbSceneNode : register(b2)
{
    matrix WorldMtrx;
    float4 MeshColor;
    int4 LightIds[16];
};

cbuffer cbScenePrimitive : register(b3)
{
    // Metallness
    float4 BaseColorFactor;
    float4 MetallicRoughnessFactor;

    // Specularity
    float4 DiffuseColorFactor;
    float4 SpecularFactor;

    // Both workflows
    float  NormalTexScale;
    float  OcclusionTexStrength;
    float4 EmissionFactor;
};

struct VS_INPUT
{
    float4 Pos      : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 Tex      : TEXCOORD0;
};

struct PS_INPUT
{
    float4 PosProj  : SV_POSITION;
    float4 PosWorld : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float4 Tangent  : TEXCOORD2; // TODO: Semantics?
    float2 Tex      : TEXCOORD3;
};


PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output = (PS_INPUT)0;
    
    output.PosWorld = mul(input.Pos, WorldMtrx);

    output.PosProj = mul(output.PosWorld, ViewMtrx);
    output.PosProj = mul(output.PosProj, ProjectionMtrx);

    output.Normal  = mul(input.Normal, (float3x3)WorldMtrx);
    output.Tangent = float4(mul(input.Tangent.xyz, (float3x3)WorldMtrx),
                            input.Tangent.w);

    output.Tex = input.Tex;

    return output;
}


float3 ComputeNormal(PS_INPUT input)
{
    // TODO: Optimize?
    //if normalTex is (0, 0, 1)
    //   return normalize(input.Normal);

    const float3 frameNormal    = normalize(input.Normal); // transformed and interpolated - renormalize
    const float3 frameTangent   = normalize(input.Tangent.xyz);
    const float3 frameBitangent = normalize(cross(frameNormal, frameTangent) * input.Tangent.w);

    const float3 normalTex      = NormalTexture.Sample(LinearSampler, input.Tex).xyz;
    const float3 localNormal    = normalize((normalTex * 2 - 1) * float3(NormalTexScale, NormalTexScale, 1.0));

    return
        localNormal.x * frameTangent +
        localNormal.y * frameBitangent +
        localNormal.z * frameNormal;
}


float4 PsDebugVisualizer(PS_INPUT input)
{
    //const float3 dir = ComputeNormal(input);
    ////const float3 dir = normalize(input.Normal);
    ////const float3 dir = normalize(input.Tangent.xyz);
    ////const float3 dir = normalize(cross(normalize(input.Normal), normalize(input.Tangent.xyz)) * input.Tangent.w); // bitangent
    //const float3 color = (dir + 1) / 2;
    //return float4(color, 1.);

    //const float3 normalTex = NormalTexture.Sample(LinearSampler, input.Tex).xyz;
    ////const float3 localNormal = normalize((normalTex * 2 - 1) * float3(NormalTexScale, NormalTexScale, 1.0));
    //return float4(normalTex, 1.);

    //const float3 tex = DiffuseTexture.Sample(LinearSampler, input.Tex).xyz;
    //return float4(tex, 1.);

    //const float occlusionTex = OcclusionTexture.Sample(LinearSampler, input.Tex).r;
    ////return float4(occlusionTex, occlusionTex, occlusionTex, 1.);
    ////return float4(OcclusionTexStrength, OcclusionTexStrength, OcclusionTexStrength, 1.);
    //const float occlusion = lerp(1., occlusionTex, OcclusionTexStrength);
    //return float4(occlusion, occlusion, occlusion, 1.);

    const float4 emissionTex = EmissionTexture.Sample(LinearSampler, input.Tex);
    //return float4(emissionTex.rgb, 1.);
    //return float4(EmissionFactor.rgb, 1.);
    const float4 emission = emissionTex * EmissionFactor;
    return float4(emission.rgb, 1.);
}


float ThetaCos(float3 normal, float3 lightDir)
{
    return max(dot(normal, lightDir), 0.);
}


float4 DiffuseBRDF()
{
    return 1 / PI;
};


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


float4 FresnelSchlick(PbrM_MatInfo matInfo, float cosTheta)
{
    const float4 f0 = matInfo.f0;
    return f0 + (1 - f0) * pow(clamp(1 - cosTheta, 0, 1), 5);
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


float4 FresnelIntegralApprox(float4 f0)
{
    return lerp(f0, 1.f, 0.05f); // Very ad-hoc approximation :-)
}


float4 PbrM_BRDF(float3 lightDir, PbrM_ShadingCtx shadingCtx, PbrM_MatInfo matInfo)
{
    float NdotL = dot(lightDir, shadingCtx.normal);
    float NdotV = dot(shadingCtx.viewDir, shadingCtx.normal);

    if (NdotL < 0)
        return float4(0, 0, 0, 1);

    NdotL = max(NdotL, 0.01f);
    NdotV = max(NdotV, 0.01f);

    // Halfway vector
    const float3 halfwayRaw = lightDir + shadingCtx.viewDir;
    const float  halfwayLen = length(halfwayRaw);
    const float3 halfway = (halfwayLen > 0.001f) ? (halfwayRaw / halfwayLen) : shadingCtx.normal;
    const float  halfwayCos = max(dot(halfway, shadingCtx.normal), 0);
    const float  HdotV = max(dot(halfway, shadingCtx.viewDir), 0.);

    // Microfacet-based specular component
    const float4 fresnelHV  = FresnelSchlick(matInfo, HdotV);
    const float distr       = GgxMicrofacetDistribution(matInfo, halfwayCos);
    const float vis         = GgxVisibilityOcclusion(matInfo, NdotL, NdotV);

    const float4 specular = fresnelHV * vis * distr;

    // Diffuse
#if defined USE_SMOOTH_REFRACTION_APPROX
    const float4 fresnelNV  = FresnelSchlick(matInfo, NdotV);
    const float4 fresnelNL  = FresnelSchlick(matInfo, NdotL);
    const float4 diffuse    = DiffuseBRDF() * matInfo.diffuse * (1.0 - fresnelNV) * (1.0 - fresnelNL);
#elif defined USE_ROUGH_REFRACTION_APPROX
    const float4 fresnelNV  = FresnelSchlick(matInfo, NdotV);
    const float4 fresnelNL  = FresnelSchlick(matInfo, NdotL);

    const float4 fresnelIntegral = FresnelIntegralApprox(matInfo.f0);

    const float4 roughFresnelNV = lerp(fresnelNV, fresnelIntegral, matInfo.alphaSq);
    const float4 roughFresnelNL = lerp(fresnelNL, fresnelIntegral, matInfo.alphaSq);

    const float4 diffuse = DiffuseBRDF() * matInfo.diffuse * (1.0 - roughFresnelNV) * (1.0 - roughFresnelNL);
#else
    const float4 diffuse = DiffuseBRDF() * matInfo.diffuse;
#endif

    return specular + diffuse;
}


float4 PbrM_AmbLightContrib(float4 luminance,
                            PbrM_ShadingCtx shadingCtx,
                            PbrM_MatInfo matInfo)
{
#if defined USE_SMOOTH_REFRACTION_APPROX
    const float NdotV = max(dot(shadingCtx.normal, shadingCtx.viewDir), 0.);
    const float4 fresnelNV = FresnelSchlick(matInfo, NdotV);

    const float4 fresnelIntegral = FresnelIntegralApprox(matInfo.f0);

    const float4 diffuse  = matInfo.diffuse * (1.0 - fresnelNV) * (1.0 - fresnelIntegral);
    const float4 specular = fresnelNV; // assuming that full specular lobe integrates to 1
#elif defined USE_ROUGH_REFRACTION_APPROX
    const float NdotV = max(dot(shadingCtx.normal, shadingCtx.viewDir), 0.);
    const float4 fresnelNV = FresnelSchlick(matInfo, NdotV);

    const float4 fresnelIntegral = FresnelIntegralApprox(matInfo.f0);

    const float4 roughFresnelNV = lerp(fresnelNV, fresnelIntegral, matInfo.alphaSq);
    
    const float4 diffuse  = matInfo.diffuse * (1.0 - roughFresnelNV) /** (1.0 - fresnelIntegral)*/;
    const float4 specular = roughFresnelNV;
#else
    const float4 diffuse  = matInfo.diffuse;
    const float4 specular = matInfo.f0; // assuming that full specular lobe integrates to 1
#endif

    return (diffuse + specular) * luminance * matInfo.occlusion;
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


float DoSpotCone(float4 lightDir, float3 L)
{
    // If the cosine angle of the light's direction 
    // vector and the vector from the light source to the point being 
    // shaded is less than minCos, then the spotlight contribution will be 0.
    float minCos = cos(lightDir.w);
    // If the cosine angle of the light's direction vector
    // and the vector from the light source to the point being shaded
    // is greater than maxCos, then the spotlight contribution will be 1.
    float maxCos = lerp(minCos, 1, 0.5f);
    float cosAngle = dot(normalize((float3)lightDir), normalize(-L));
    // Blend between the maxixmum and minimum cosine angles.
    return smoothstep(minCos, maxCos, cosAngle);
}


float4 PbrM_SpotLightContrib(float3 surfPos,
    float4 lightPosData,
    float4 lightDirData,
    float4 intensity,
    PbrM_ShadingCtx shadingCtx,
    PbrM_MatInfo matInfo)
{
    const float3 dirRaw = (float3)lightPosData - surfPos;
    const float  len = length(dirRaw);
    const float3 lightDir = dirRaw / len;
    const float  distSqr = len * len;

    const float thetaCos = ThetaCos(shadingCtx.normal, lightDir);
    const float spotIntensity = DoSpotCone(lightDirData, lightDir);

    const float4 brdf = PbrM_BRDF(lightDir, shadingCtx, matInfo);

    return brdf * thetaCos * intensity * spotIntensity / distSqr;
}


PbrM_MatInfo PbrM_ComputeMatInfo(PS_INPUT input)
{
    const float4 baseColor      = BaseColorTexture.Sample(LinearSampler, input.Tex) * BaseColorFactor;

    const float4 metalRoughness = MetalRoughnessTexture.Sample(LinearSampler, input.Tex) * MetallicRoughnessFactor;
    const float4 metalness      = float4(metalRoughness.bbb, 1);
    const float  roughness      = metalRoughness.g;

    const float4 f0Diel         = float4(0.04, 0.04, 0.04, 1);
#if defined USE_SMOOTH_REFRACTION_APPROX || defined USE_ROUGH_REFRACTION_APPROX
    const float4 diffuseDiel    = baseColor;
#else
    const float4 diffuseDiel    = (float4(1, 1, 1, 1) - f0Diel) * baseColor;
#endif

    const float4 f0Metal        = baseColor;
    const float4 diffuseMetal   = float4(0, 0, 0, 1);

    PbrM_MatInfo matInfo;
    matInfo.diffuse     = lerp(diffuseDiel,  diffuseMetal,  metalness);
    matInfo.f0          = lerp(f0Diel, f0Metal, metalness);
    matInfo.alphaSq     = max(roughness * roughness, 0.0001f);
    matInfo.occlusion   = lerp(1., OcclusionTexture.Sample(LinearSampler, input.Tex).r, OcclusionTexStrength);

    return matInfo;
}


float4 PsPbrMetalness(PS_INPUT input) : SV_Target
{
    // debug
    //return PsDebugVisualizer(input);


    PbrM_ShadingCtx shadingCtx;
    shadingCtx.normal  = normalize(input.Normal); //ComputeNormal(input); - now used input.Normal for testing
    shadingCtx.viewDir = normalize((float3)CameraPos - (float3)input.PosWorld);

    const PbrM_MatInfo matInfo = PbrM_ComputeMatInfo(input);

    float4 output = float4(0, 0, 0, 0);

    output += PbrM_AmbLightContrib(AmbientLightLuminance, shadingCtx, matInfo);

    int i = 0;
    int lightId = 0;
    int lightIdsArr[64] = (int[64])LightIds;
    while(i < NODE_LIGHTS_MAX_COUNT)
    {
        lightId = lightIdsArr[i];
	//lightId = LightIds[i];
	if (lightId < 0)
	    break;	

        float4 intencity = LightsData[lightId];
        // Direct
        if (intencity.w == 1) {
            output += PbrM_DirLightContrib((float3)LightsData[lightId + 1],
                intencity,
                shadingCtx,
                matInfo);
        }
        // Point
        if (intencity.w == 2) {
            float4 lightPosData = LightsData[lightId + 1];
            float3 posWorld = (float3)input.PosWorld;

            // Skip point lights that are out of range of the point being shaded.
            //if (length((float3)lightPosData - posWorld) < lightPosData.w)
                output += PbrM_PointLightContrib(posWorld,
                    lightPosData,
                    intencity,
                    shadingCtx,
                    matInfo);
        }
        // Spot
        if (intencity.w == 3) {
            float4 lightPosData = LightsData[lightId + 1];
            float4 lightDirData = LightsData[lightId + 2];
            float3 posWorld = (float3)input.PosWorld;

            // Skip spot lights that are out of range of the point being shaded.
            //if (length((float3)lightPosData - posWorld) < lightPosData.w)
                output += PbrM_SpotLightContrib(posWorld,
                    lightPosData,
                    lightDirData,
                    intencity,
                    shadingCtx,
                    matInfo);
        }
	    i += 1;
    }

    output += EmissionTexture.Sample(LinearSampler, input.Tex) * EmissionFactor;

    output.a = 1;
    return output;
}


float4 PsConstEmissive(PS_INPUT input) : SV_Target
{
    return MeshColor;
}

float4 PSMain(PS_INPUT input) : SV_Target
{    
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}