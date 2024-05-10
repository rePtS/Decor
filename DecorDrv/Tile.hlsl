#include "Common.hlsli"

Texture2D TexDiffuse : register(t0);

struct STile
{
    float4 XYPos : Position0; //Left, right, top, bottom in pixel coordinates
    float4 ZPos : Position1; //Z coordinate
    float4 TexCoord : TexCoord0; //Left, right, top, bottom    
    float3 Color : TexCoord1;
    uint PolyFlags : BlendIndices0;
};

struct VSOut
{
    float4 Pos : SV_Position;
    float2 TexCoord : TexCoord0;
    float3 Color : TexCoord1;
    uint PolyFlags : BlendIndices0;
};

VSOut VSMain(const STile Tile, const uint VertexID : SV_VertexID)
{
    VSOut Output;    

    const uint IndexX = VertexID / 2;
    const uint IndexY = 3 - VertexID % 2;
        
    if (Tile.PolyFlags & PF_NoSmooth)
        Output.Pos = float4(-1.0f + 2.0f * (Tile.XYPos[IndexX] * fRes.z), 1.0f - 2.0f * (Tile.XYPos[IndexY] * fRes.w), 1.0f, 1.0f);
    else
        Output.Pos = mul(float4(Tile.XYPos[IndexX], Tile.XYPos[IndexY], Tile.ZPos[0], 1.0f), ProjectionMatrix);

    Output.TexCoord = float2(Tile.TexCoord[IndexX], Tile.TexCoord[IndexY]);
    Output.Color = Tile.Color;
    Output.PolyFlags = Tile.PolyFlags;
    return Output;
}

float3 PSMain(const VSOut Input) : SV_Target2
{
    if (Input.PolyFlags & (PF_Masked | PF_Modulated))
    {
        clip(TexDiffuse.Sample(SamPoint, Input.TexCoord).a - 0.5f);
    }

    if (Input.PolyFlags & PF_Modulated)
    {
        return TexDiffuse.Sample(SamLinear, Input.TexCoord).rgb;
    }

    const float3 Diffuse = Input.PolyFlags & PF_NoSmooth ?
        TexDiffuse.Sample(SamPoint, Input.TexCoord).rgb :
        TexDiffuse.Sample(SamLinear, Input.TexCoord).rgb;
    
    const float3 Color = Diffuse * Input.Color.rgb;

    return Color;
}
