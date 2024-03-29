#ifndef __DEFINES_HLSLI__
#define __DEFINES_HLSLI__

#define CONVERT_SRGB_INPUT_TO_LINEAR
#define CONVERT_LINEAR_OUTPUT_TO_SRGB

//#define USE_SMOOTH_REFRACTION_APPROX
//#define USE_ROUGH_REFRACTION_APPROX

#define NEAR_CLIPPING_DISTANCE 1.0f
#define FAR_CLIPPING_DISTANCE 32760.0f
#define MAX_LIGHTS_DATA_SIZE 1024
#define MAX_LIGHTS_INDEX_SIZE 1024 // must be multiple of 16

// Types of light sources that are transmitted to the shader
#define LIGHT_POINT 1
#define LIGHT_SPOT 2
#define LIGHT_AMBIENT 3
#define LIGHT_POINT_AMBIENT 4

#define LIGHT_TYPE_MASK 7
#define LIGHT_SPECIAL_MASK 8

#endif // __DEFINES_HLSLI__