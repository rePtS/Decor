#include "Defines.hlsli"

static const uint PF_Masked = 0x00000002;       // Poly should be drawn masked.
static const uint PF_Modulated = 0x00000040;    // Modulation transparency.
static const uint PF_Translucent = 0x00000004;	// Poly is transparent.
static const uint PF_SmallWavy = 0x00002000;	// Small wavy pattern (for water/enviro reflection).
static const uint PF_Unlit = 0x00400000;	    // Unlit.
static const uint PF_TwoSided = 0x00000100;	    // Poly is visible from both sides.
static const uint PF_Portal = 0x04000000;	    // Portal between iZones.
static const uint PF_SpecialLit = 0x00100000;	// Only speciallit lights apply to this poly.
static const uint PF_NoSmooth = 0x00000800;	    // Don't smooth textures.

// https://lodev.org/unrealed/lighting/lighting.html#type

static const uint LE_None = 0;          // LE_None: This just a normal light. In contradiction to LT_None, LE_None does not mean that the light is Off.
static const uint LE_TorchWaver = 1;    // LE_TorchWaver: Makes the light move like a torch.
static const uint LE_FireWaver = 2;     // LE_FireWaver: This does the same as LE_TorchWaver, but heavyer. The light will move like fire.
static const uint LE_WateryShimmer = 3; // LE_WateryShimmer: The lighting will move like water. Use it when you create a pool inside a building, to shine on the walls of the building.
static const uint LE_Searchlight = 4;   // LE_Searchlight: The lightbeam will rotate, like a searchlight. Think about Alcatraz. If LightPeriod is 0, the SearchLight will not rotate.
static const uint LE_SlowWave = 5;      // LE_SlowWave: Some kind of animated wave-effect.
static const uint LE_FastWave = 6;      // LE_FastWave: Same as LE_SlowWave, but faster.
static const uint LE_CloudCast = 7;     // LE_CloudCast: Looks to be just steady. This is probably an old remainder of the pre-release version of Unreal.
static const uint LE_StaticSpot = 8;    // LE_StaticSpot: The light will beam in one direction.
static const uint LE_Shock = 9;         // LE_Shock: Another animation for the light.
static const uint LE_Disco = 10;        // LE_Disco: A nice disco-effect.
static const uint LE_Warp = 11;         // LE_Warp: Looks to be just steady. This is probably an old remainder of the pre-release version of Unreal.
static const uint LE_Spotlight = 12;    // LE_SpotLight: The same as LE_StaticSpot.
static const uint LE_NonIncidence = 13; // LE_NonIncidence: like LE_None, but the light will have almost no fading-away at the edges. This makes it look like the radius is larger.
static const uint LE_Shell = 14;        // LE_Shell: If you use this correct (close enough to wall), the light will look like a ring on the wall.
static const uint LE_OmniBumpMap = 15;  // LE_OmniBumpMap: Can be used to create blacklights. This is not what the name suggests, so this is probably an old remainder of the pre-release version of Unreal.
static const uint LE_Interference = 16; // LE_Interference: Another animation for the light.
static const uint LE_Cylinder = 17;     // LE_Cylinder: Used if need a really large light. The lightradius is calculated as a cylinder instead of a sphere with soft edges.
static const uint LE_Rotor = 18;        // LE_Rotor: Rotating light similar to LE_Disco.
static const uint LE_Unused = 19;       // LE_Unused: Exactly the same as LE_None. This is probably an old remainder of the pre-release version of Unreal.

static const uint LT_None = 0;          // LT_None: Source will not give any light. The light is Off.
static const uint LT_Steady = 1;        // LT_Steady: This is the default and makes the lightsource giving light.
static const uint LT_Pulse = 2;         // LT_Pulse: Makes the light pulse. It will become brighter and darker then again brighter and so on. It will not just switch between On, Off, On, Off,... but fade from On to Off to On to Off....
static const uint LT_Blink = 3;         // LT_Blink: The light will randomly go On and Off. It will be most of the times On.
static const uint LT_Flicker = 4;       // LT_Flicker: the light will go randomly On and Off. In contradiction to LT_Blink, it will be most of the times Off.
static const uint LT_Strobe = 5;        // LT_Strobe: The light will switch between On, Off, On, Off,.... In contradiction to LT_Flicker and LT_Blink, this does not happen randomly. In contradiction to LT_Pulse, it will not fade from On to Off to On to Off....
static const uint LT_BackdropLight = 6; // LT_BackdropLight: The light is just steady. This is probably an old remainder of the pre-release version of Unreal.
static const uint LT_SubtlePulse = 7;   // LT_SubtlePulse: Like LT_Pulse, but less heavy.

static const float LIGHT_EDGE_THICKNESS = 0.1f;

static const float PI = 3.14159265f;

cbuffer PerFrameBuffer : register(b0)
{
    float4 fRes;
    matrix ProjectionMatrix;
    matrix ViewMatrix;
    matrix ViewMatrixInv;
    float4 Origin;
    float4 FlashColor;
    float4 DynamicLights[MAX_LIGHTS_DATA_SIZE];
};

cbuffer PerTickBuffer : register(b1)
{
    float4 fTick; // x component - time in seconds, y - random value 0..1, z - normal random 0..1
};

cbuffer PerSceneBuffer : register(b2)
{
    float4 StaticLights[3072];
};

cbuffer PerPolyBuffer : register(b3)
{
    uint4 PolyControl;
    uint4 StaticLightIds[1024];
};

sampler SamLinear : register(s0);
sampler SamPoint : register(s1);

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

float4 FresnelIntegralApprox(float4 f0)
{
    return lerp(f0, 1.f, 0.05f); // Very ad-hoc approximation :-)
}

float ThetaCos(float3 normal, float3 lightDir)
{
    return max(dot(normal, lightDir), 0.);
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

float4 PbrM_AmbPointLightContrib(float3 surfPos,
    float4 lightPos,
    float4 luminance,
    PbrM_ShadingCtx shadingCtx,
    PbrM_MatInfo matInfo)
{
    const float3 dirRaw = surfPos - (float3)lightPos;
    const float  len = length(dirRaw);

    float4 ambLightContrib = PbrM_AmbLightContrib(luminance, shadingCtx, matInfo);
    float attenuation = 1.0f - smoothstep(0, lightPos.w, len);
    
    return ambLightContrib * attenuation;
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
    const float3 dirRaw = surfPos - (float3)lightPos;
    const float  len = length(dirRaw);
    const float3 lightDir = dirRaw / len;
    const float  distSqr = len * len;

    const float thetaCos = ThetaCos(shadingCtx.normal, lightDir);    

    const float4 brdf = PbrM_BRDF(lightDir, shadingCtx, matInfo);

    const float lightEdgeStart = lightPos.w * (1.0f - LIGHT_EDGE_THICKNESS);
    if (len > lightEdgeStart)
    {	    
	    float edgeFactor = 1.0f - smoothstep(lightEdgeStart, lightPos.w, len);
	    return brdf * thetaCos * intensity * edgeFactor / distSqr;
    }
    else
    	return brdf * thetaCos * intensity / distSqr;
}

float DoSpotCone(float4 lightDir, float3 L)
{
    float maxCos = 0.0f;
    float minCos = 0.0f;

    // If the angle is less than 64 degrees, we expand the cone outward, otherwise we narrow the cone inward
    if (lightDir.w < 1.1f)
    {
	    maxCos = cos(lightDir.w);
	    minCos = lerp(0.f, maxCos, 0.6f);
    }
    else
    {
    	minCos = cos(lightDir.w);
	    maxCos = lerp(minCos, 1, 0.8f);
    }

/*    
    // If the cosine angle of the light's direction 
    // vector and the vector from the light source to the point being 
    // shaded is less than minCos, then the spotlight contribution will be 0.
    float minCos = cos(lightDir.w);    
    // If the cosine angle of the light's direction vector
    // and the vector from the light source to the point being shaded
    // is greater than maxCos, then the spotlight contribution will be 1.
    float maxCos = lerp(minCos, 1, 0.5f);
*/
    float cosAngle = dot(normalize((float3)lightDir), normalize(L));
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
    const float3 dirRaw = surfPos - (float3)lightPosData;
    const float  len = length(dirRaw);
    const float3 lightDir = dirRaw / len;

    const float spotIntensity = DoSpotCone(lightDirData, lightDir);
    if (spotIntensity == 0.0f)
        return intensity * 0.0f;
    else
    {
        const float distSqr = len * len;
        const float thetaCos = ThetaCos(shadingCtx.normal, lightDir);

        const float4 brdf = PbrM_BRDF(lightDir, shadingCtx, matInfo);

	const float lightEdgeStart = lightPosData.w * (1.0f - LIGHT_EDGE_THICKNESS);
    	if (len > lightEdgeStart)
    	{
	        float edgeFactor = 1.0f - smoothstep(lightEdgeStart, lightPosData.w, len);
	        return brdf * thetaCos * intensity * spotIntensity * edgeFactor / distSqr;
    	}
    	else
    	    return brdf * thetaCos * intensity * spotIntensity / distSqr;
    }
}

float GetPixelPower(float3 p)
{
   //return 0.3f*p.r + 0.59f*p.g + 0.11f*p.b;
   //p = normalize(p);
   return (p.r + p.g + p.b) / 3.0f;   
}

float4 ApplyViewMatrixAtOrigin(float4 vec)
{
    float w = vec.w;
    vec.w = 0;
    vec = mul(vec - Origin, ViewMatrix);
    vec.w = w;
    
    return vec;
}

float4 ApplyViewMatrix(float4 vec)
{
    float w = vec.w;
    vec.w = 0;
    vec = mul(vec, ViewMatrix);
    vec.w = w;
    
    return vec;
}