cbuffer StencilConstants : register(b0)
{
    float4 xform0; // m11, m12, m21, m22; row-vector logical-to-raster mapping
    float4 xform1; // dx, dy, 2/width, 2/height
    float4 colour;
};
float4 StencilVS(float2 p : POSITION) : SV_POSITION
{
    float2 q = p.x * xform0.xy + p.y * xform0.zw + xform1.xy;
    return float4(q.x * xform1.z - 1, 1 - q.y * xform1.w, 0, 1);
}
float4 StencilPS() : SV_TARGET
{
    return colour;
}
