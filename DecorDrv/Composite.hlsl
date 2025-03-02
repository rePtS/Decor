#include "Common.hlsli"

Texture2D TexNoise : register(t2);
Texture2D TexSolid : register(t4);
Texture2D TexWater : register(t5);
Texture2D TexTile : register(t6);
Texture2D TexGlass : register(t7);

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

static const float4 _WaterFogColor = float4(0.025f, 0.024f, 0.021f, 1.0f);
static const float _WaterFogDensity = 1.1f;
static const float _MinGlassSolidDepthDiff = 0.05f; // Min delta between glass and solid surface (if the distance is less, then the surface is glass anyway)

bool IsUnderwater(float screenY)
{
    return (FrameControl & 1) && (screenY > ScreenWaterLevel);
}

float4 AddUnderWaterFog(float4 color, float distance, float screenY)
{
    if (IsUnderwater(screenY))
    {
        float depth = (1.0f / distance) * 0.001f;
        float fogFactor = exp2(-_WaterFogDensity * depth * 5.0f);
        return lerp(_WaterFogColor * 2.0f, color, fogFactor) + float4(0, 0, 0.04f, 0);
    }
    else
        return color;
}

float4 PSMain(const VSOut input) : SV_Target
{
    const float4 Solid = TexSolid.Sample(SamPoint, input.TexCoord).rgba;
    const float4 Water = TexWater.Sample(SamPoint, input.TexCoord).rgba;
    const float4 Tile = TexTile.Sample(SamPoint, input.TexCoord).rgba;
    const float4 Glass = TexGlass.Sample(SamPoint, input.TexCoord).rgba;
    
    if (Water.a > Solid.a)
    {
        float2 noiseUV = TexNoise.Sample(SamLinear, input.TexCoord + float2(0.0001f * fTick.x, 0.0f)).xy;
        float4 refractedSolid = TexSolid.Sample(SamPoint, input.TexCoord + 0.005f * noiseUV).rgba;
        
        if (Water.a < refractedSolid.a)
            refractedSolid = Solid;

        float depthDifference = (1.0f / refractedSolid.a - 1.0f / Water.a) * 0.001f;
        
        float fogFactor = exp2(-_WaterFogDensity * depthDifference * 20.0f);
        return AddUnderWaterFog(lerp(_WaterFogColor, refractedSolid, fogFactor) + Water, Water.a, input.TexCoord.y) + FlashColor + Glass + Tile;        
    }
    
    float glassSolidDepthDiff = abs((Glass.a - Solid.a) / Solid.a);
    
    if (Glass.a > Solid.a || glassSolidDepthDiff < _MinGlassSolidDepthDiff)
        return AddUnderWaterFog(Solid, Solid.a, input.TexCoord.y) + FlashColor + Glass + Tile;
    else
        return AddUnderWaterFog(Solid, Solid.a, input.TexCoord.y) + FlashColor + Tile;
}
