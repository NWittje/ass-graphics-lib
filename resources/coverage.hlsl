cbuffer Geometry : register(b0)
{
    float4 xform0;
    float4 xform1;
    float4 colour;
};
struct CoverageVertex {
    float4 position : SV_Position;
    nointerpolation uint material : MATERIAL0;
    float2 coverage : TEXCOORD0;
};
CoverageVertex CoverageVS(float2 p : POSITION, uint material : MATERIAL0, float2 uv : TEXCOORD0)
{
    CoverageVertex result;
    result.position = float4(p.x*xform1.z-1,1-p.y*xform1.w,0,1);
    result.material = material;
    result.coverage = uv;
    return result;
}
