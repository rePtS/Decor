#include "Defines.hlsli"
#include "Common.hlsli"

Texture2D TexDiffuse : register(t0);

struct SPoly
{
    float4 Pos : Position0;
    float3 Normal : Normal0;
    float3 Color : Color0;
    float3 Fog : Color1;
    float2 TexCoord : TexCoord0;
    uint PolyFlags : BlendIndices0;
};

struct VSOut
{
    float4 Pos : SV_Position;
    float3 Normal : Normal0;
    float3 Color : Color0;
    float3 Fog : Color1;
    float2 TexCoord : TexCoord0;
    uint PolyFlags : BlendIndices0;
    float4 PosView : Position1;
};

VSOut VSMain(const SPoly Input)
{
    VSOut Output;
    Output.Pos = mul(Input.Pos, ProjectionMatrix);
    Output.PosView = Input.Pos;	
    Output.Normal = -normalize(Input.Normal);
    Output.Color = Input.Color;
    Output.Fog = Input.Fog;
    Output.TexCoord = Input.TexCoord;
    Output.PolyFlags = Input.PolyFlags;
    return Output;
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
        Color = TexDiffuse.Sample(SamLinear, input.TexCoord);
        Color.a = input.Pos.z;
        return Color;
    }
    
    const float3 Diffuse = TexDiffuse.Sample(SamLinear, input.TexCoord).rgb;
    
    if (input.PolyFlags & PF_Translucent && !any(Diffuse))
    {
        clip(-1.0f);
    }
    
    Color.rgb *= Diffuse;
    Color.rgb += input.Fog;
    
    // Hack for correct rendering of weapons near transparent objects
    if (input.PosView.z < 60)
        Color.a = 1;
    else
        Color.a = input.Pos.z;
    
    return Color;
}