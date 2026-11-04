cbuffer StencilConstants : register(b0)
{
    float4 xform0; // m11, m12, m21, m22; row-vector logical-to-raster mapping
    float4 xform1; // dx, dy, 2/width, 2/height
    float4 colour;
};
cbuffer TileColours : register(b1)
{
    uint4 tileRows[4]; // Sixteen final opaque ARGB8 colours, source-raster anchored.
};
float4 StencilVS(float2 p : POSITION) : SV_POSITION
{
    float2 q = p.x * xform0.xy + p.y * xform0.zw + xform1.xy;
    return float4(q.x * xform1.z - 1, 1 - q.y * xform1.w, 0, 1);
}
float4 StencilPS(float4 position : SV_POSITION) : SV_TARGET
{
    // Opaque solid paint has alpha1. Alpha0 is an internal uniform selector;
    // caller alpha never reaches this selector because admission is opaque-only.
    if (colour.a == 0)
    {
        uint2 p = uint2(floor(position.xy)) & 3;
        uint c = tileRows[p.y][p.x];
        return float4(float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255, float(c & 255) / 255, 1);
    }
    return colour;
}
