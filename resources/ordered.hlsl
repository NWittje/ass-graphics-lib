cbuffer Geometry : register(b0)
{
    float4 xform0;
    float4 xform1;
    float4 colour;
};
struct Vertex
{
    float4 position : SV_Position;
    nointerpolation uint material : MATERIAL0;
};
Vertex OrderedVS(float2 p : POSITION, uint material : MATERIAL0)
{
    float2 q=p.x*xform0.xy+p.y*xform0.zw+xform1.xy;
    Vertex result;
    result.position=float4(q.x*xform1.z-1,1-q.y*xform1.w,0,1);
    result.material=material;
    return result;
}
