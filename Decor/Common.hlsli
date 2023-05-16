#include "Defines.hlsli"

static const uint PF_Masked = 0x00000002;       // Poly should be drawn masked.
static const uint PF_Modulated = 0x00000040;    // Modulation transparency.
static const uint PF_Translucent = 0x00000004;	// Poly is transparent.
static const uint PF_SmallWavy = 0x00002000;	// Small wavy pattern (for water/enviro reflection).

static const uint PACKED_MAX_SLICE_DATA_SIZE = MAX_SLICE_DATA_SIZE / 4;
static const uint PACKED_MAX_LIGHTS_INDEX_SIZE = MAX_LIGHTS_INDEX_SIZE / 4;
static const uint SLICE_MAX_INDEX = SLICE_NUMBER - 1;
static const float SLICE_THICKNESS = (FAR_CLIPPING_DISTANCE - NEAR_CLIPPING_DISTANCE) / (float)SLICE_NUMBER;

cbuffer CBufGlobal : register(b0)
{
    float4 fRes;
    matrix ProjectionMatrix;
    matrix ViewMatrix;
    float4 LightDir;
    
    uint4  IndexesOfFirstLightsInSlices[PACKED_MAX_SLICE_DATA_SIZE];
    uint4  LightIndexesFromAllSlices[PACKED_MAX_LIGHTS_INDEX_SIZE];
    float4 Lights[MAX_LIGHTS_DATA_SIZE];
};

sampler SamLinear : register(s0);
sampler SamPoint : register(s1);
