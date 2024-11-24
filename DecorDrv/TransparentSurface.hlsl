#include "CommonSurface.hlsli"

float4 PSMain(const VSOut input) : SV_Target2
{
    return GetSurfacePixel(input);
}
