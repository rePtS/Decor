#include "Common.hlsli"

Texture2D TexNoise : register(t2);
Texture2D TexSolid : register(t4);
Texture2D TexWater : register(t5);
Texture2D TexTile : register(t6);

struct SPoly
{
    float4 Pos : Position0;
    float2 TexCoord : TexCoord0;
};

struct VSOut
{
    float4 Pos : SV_Position;
    float2 TexCoord : TexCoord0;
};

VSOut VSMain(const SPoly Input)
{
    VSOut Output;
    Output.Pos = Input.Pos;
    Output.TexCoord = Input.TexCoord;
    return Output;
}

float4 PSMain(const VSOut input) : SV_Target
{
    const float4 Solid = TexSolid.Sample(SamPoint, input.TexCoord).rgba;
    const float4 Water = TexWater.Sample(SamPoint, input.TexCoord).rgba;
    const float3 Tile = TexTile.Sample(SamPoint, input.TexCoord).rgb;

    if (Water.a > Solid.a)
    {
        float2 noiseUV = TexNoise.Sample(SamLinear, input.TexCoord + float2(0.0001f * fTick.x, 0.0f)).xy;
        float4 reflectedSolid = TexSolid.Sample(SamPoint, input.TexCoord + 0.005f * noiseUV).rgba;
        
        if (Water.a < reflectedSolid.a)
            reflectedSolid = Solid;
        
        const float4 _WaterFogColor = float4(0, 0.3f, 0.8f, 1.0f);
        const float _WaterFogDensity = 1.1f;
        float depthDifference = (1.0f / reflectedSolid.a - 1.0f / Water.a) * 0.001f;

        float fogFactor = exp2(-_WaterFogDensity * depthDifference);
        return lerp(_WaterFogColor, reflectedSolid, fogFactor) + Water + float4(Tile, 1.0f);
    }

    return Solid + float4(Tile, 1.0f);
}