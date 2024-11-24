#include "CommonSurface.hlsli"

SPoly VSMain(const SPoly Input)
{    
    return Input;
}

float4 PSMain(const VSOut input) : SV_Target0
{
    return GetSurfacePixel(input);
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
        output.PosView = In[i].Pos;
        output.PosWorld = mul(In[i].Pos, ViewMatrixInv) + Origin;
        output.Normal = vn;
        output.TexCoord = In[i].TexCoord;
        output.TexCoord1 = In[i].TexCoord1;
        output.PolyFlags = In[i].PolyFlags;
        output.TexFlags = In[i].TexFlags;
        outputStream.Append(output);
    }

    outputStream.RestartStrip();
}