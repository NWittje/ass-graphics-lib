// Contract1. Trusted caller implements ag_apply / ag_resolve with bounded
// same-pixel work. These declarations are not an untrusted shader sandbox.
#ifdef AG_ORDERED_RESOLVE
Texture2D<uint> ag_plane0 : register(t0);
Texture2D<uint> ag_plane1 : register(t1);
float4 ag_resolve(uint2 pixel);
float4 AGOrderedResolve(float4 position : SV_Position) : SV_Target0
{
    return ag_resolve(uint2(position.xy));
}
#else
RasterizerOrderedTexture2D<uint> ag_plane0 : register(u0);
RasterizerOrderedTexture2D<uint> ag_plane1 : register(u1);
StructuredBuffer<uint4> ag_records : register(t0);
struct AGOrderedInput
{
    float4 position : SV_Position;
    nointerpolation uint material : MATERIAL0;
};
void ag_apply(uint2 pixel, uint record);
[earlydepthstencil]
void AGOrderedMaterial(AGOrderedInput input)
{
    ag_apply(uint2(input.position.xy), input.material);
}
#endif
