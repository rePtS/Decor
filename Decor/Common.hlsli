static const uint PF_Masked = 0x00000002;
static const uint PF_Modulated = 0x00000040;

cbuffer CBufGlobal : register(b0)
{
    float4 fRes;
    matrix ProjectionMatrix;
    matrix ViewMatrix;
    float4 LightDir;
    
    uint4  IndexesOfFirstLightsInSlices[4];
    uint4  LightIndexesFromAllSlices[256];    
    float4 Lights[1024];
};

sampler SamLinear : register(s0);
sampler SamPoint : register(s1);
