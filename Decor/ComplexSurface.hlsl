#include "Defines.hlsli"
#include "Common.hlsli"

Texture2D TexDiffuse : register(t0);
Texture2D TexLight : register(t1);
Texture2D TexNoise : register(t2);

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
    //const float4 baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.5f, 0.5f, 1.f); // ѕока будем использовать фиксированный baseColor, но потом его нужно будет брать из TexDiffuse    
    float4 baseColor;
    if (input.TexFlags & 0x00000001)
        baseColor = TexDiffuse.Sample(SamLinear, input.TexCoord) * float4(0.5f, 0.5f, 0.5f, 1.0f);
    else
        baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.5f, 0.5f, 0.5f, 1.f);

    //const float4 metalRoughness = MetalRoughnessTexture.Sample(LinearSampler, input.Tex) * MetallicRoughnessFactor;
    //const float4 metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.f, 0.4f, 0.f, 0.f); // ѕока будем использовать фиксированный metalRoughness
    float4 metalRoughness = float4(1.0f, 1.0f, 1.0f, 1.0f) * float4(0.f, 0.4f, 0.f, 0.f); // ѕока будем использовать фиксированный metalRoughness

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
    matInfo.occlusion = 1.0f; // ѕока будем использовать фиксированный matInfo.occlusion

    return matInfo;
}


float4 PSMain(const VSOut input) : SV_Target
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
    
    	return Color;
    }

    if (input.PolyFlags & PF_Masked)
    {
        clip(TexDiffuse.Sample(SamPoint, input.TexCoord).a - 0.5f);
    }
    
    if (input.PolyFlags & PF_Unlit)
    {
        //const float3 Diffuse = TexDiffuse.Sample(SamLinear, input.TexCoord).rgb;
        //return float4(Diffuse, 1.0f);
	    return TexDiffuse.Sample(SamLinear, input.TexCoord).rgba;
    }

    PbrM_ShadingCtx shadingCtx;    
    if (input.PolyFlags & PF_Portal)	
	shadingCtx.normal = normalize(input.Normal + 0.05f * TexNoise.Sample(SamLinear, input.TexCoord + float2(0.0001f * fTimeInSeconds.x, 0.0f)).rgb);
    else
	shadingCtx.normal = normalize(input.Normal);

    //shadingCtx.normal = normalize(TexNoise.Sample(SamLinear, input.TexCoord).rgb);
    //shadingCtx.normal = normalize(input.Normal + 0.5 * normalize(TexDiffuse.Sample(SamLinear, input.TexCoord).rgb));
    //shadingCtx.normal = normalize(input.Normal + 0.05f * TexNoise.Sample(SamLinear, input.TexCoord + float2(0.0001f * fTimeInSeconds.x, 0.0f)).rgb);
    //shadingCtx.normal = normalize(input.Normal + 0.05f * TexNoise.Sample(SamLinear, input.TexCoord).rgb);
    //shadingCtx.normal = normalize(input.Normal); //ComputeNormal(input); - now used input.Normal for testing    
    shadingCtx.viewDir = normalize((float3)input.PosWorld);

    const PbrM_MatInfo matInfo = PbrM_ComputeMatInfo(input);

    float4 output = float4(0, 0, 0, 0);
    float4 ambientLightLuminance = float4(0.02, 0.02, 0.02, 1);
    float3 directionalLightVector = normalize((float3)LightDir);

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
    
    //if (slice == 0)
    //    output = float4(0.1, 0.0, 0.3, 1);
    //if (slice == 1)
    //    output = float4(0.7, 0.0, 0.3, 1);    

    uint lightId = 0;
    uint lightType = 0;
    for (uint i = firstLightsInSlices[slice]; i < firstLightsInSlices[slice+1]; ++i)
    {
        lightId = LightIndexesFromAllSlices[i >> 2][i & 3];
        float4 intencity = Lights[lightId];

        lightType = (uint)intencity.w;
        if (bool(lightType & LIGHT_SPECIAL) != bool(input.PolyFlags & PF_SpecialLit))
            continue;
        
        // Point
        if (lightType & LIGHT_POINT) {
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

        // Spot
        if (lightType & LIGHT_SPOT) {
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

    // Original lightmap
    if (input.TexFlags & 0x00000002)
    {
        const float3 Light = TexLight.Sample(SamLinear, input.TexCoord1).bgr * 2.0f; // 2.0f;
	//const float lightAverage = (Light.r + Light.g + Light.b) / 3.0f;
        //output.rgb *= Light;
	//output.rgb *= (Light * 1.5f);
	//output.rgb *= (1.5f * Light + 0.3f); //!!!
	//output.rgb = output.rgb * 3.0f * Light;	
	//output.rgb *= 0.5f;
        //output.rgb = output.rgb * 0.2f + output.rgb * (0.8f * Light);
	//output.rgb = Light;
	//float p1 = GetPixelPower(Light.rgb);
	//float p2 = GetPixelPower(output.rgb);
	//float k = exp(-500.0f * pow(p1 - p2, 2.0f));
	//output.rgb = output.rgb * k + output.rgb * ((1.0f - k) * Light);
	//if (k > 0.5f)
	//    output.rgb *= float3(1, 0, 0);
	//else
	//    output.rgb *= float3(0, 1, 0);
    }

    //output.rgb = input.Normal;
    //output.rgb = TexNoise.Sample(SamLinear, input.TexCoord).rgb;
    //output.rgb = fTimeInSeconds.rgb;

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