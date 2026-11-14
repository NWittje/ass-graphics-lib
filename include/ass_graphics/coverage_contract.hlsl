#ifndef AG_COVERAGE_CONTRACT
#define AG_COVERAGE_CONTRACT
struct AGCoverageInput {
    float4 position : SV_Position;
    nointerpolation uint material : MATERIAL0;
    float2 coverage : TEXCOORD0;
};
RasterizerOrderedTexture2D<uint> ag_plane0 : register(u0);
RasterizerOrderedTexture2D<uint> ag_plane1 : register(u1);
StructuredBuffer<uint4> ag_records : register(t0);
Texture2D<uint> ag_image0 : register(t1);
Texture2D<uint> ag_image1 : register(t2);
Texture2D<float4> ag_coverage : register(t3);
// .g: inner coverage, .r: outer coverage excluding the inner contribution.
// Each complete logical path reaches the material program once at each pixel.
float2 ag_path_coverage(AGCoverageInput input) {
    float4 value=ag_coverage.Load(int3(uint2(input.coverage),0));
    return value.gr;
}
#endif
