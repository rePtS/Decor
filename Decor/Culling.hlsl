cbuffer cbFrame : register(b0)
{
    matrix ViewMtrx;
    float4 CameraPos;
    matrix ProjectionMtrx;
};

cbuffer cbSceneNode : register(b1)
{
    matrix WorldMtrx;
    float4 MeshColor;
    uint4 Control; // в компоненте x сюда передаем идентификатор узла
    uint4 LightIds[16];
};

struct VS_INPUT
{
    float4 Pos      : POSITION;
};

struct PS_INPUT
{
    float4 PosProj  : SV_POSITION;    
};


PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    
    float4 posWorld = mul(input.Pos, WorldMtrx);
    output.PosProj = mul(posWorld, ViewMtrx);
    output.PosProj = mul(output.PosProj, ProjectionMtrx);    

    return output;
}

uint PSMain(PS_INPUT input) : SV_Target
{
    return Control.x; // возвращаем иденфтификатор узла
}