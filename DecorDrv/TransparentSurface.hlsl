#include "CommonSurface.hlsli"

float4 PSMain(const VSOut input) : SV_Target3
{
    return GetSurfacePixel(input);
}
