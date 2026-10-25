// Matches the native buffer: row-vector, left-handed view/projection matrices.
cbuffer SceneConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 options; // inverse width/height, blur radius in pixels, reserved
};
struct Vertex { float3 position : POSITION; float3 normal : NORMAL; float4 color : COLOR; };
struct Pixel { float4 position : SV_POSITION; float3 normal : NORMAL; float4 color : COLOR; };
Pixel SceneVS(Vertex v)
{
    Pixel o;
    o.position = mul(float4(v.position, 1), viewProjection);
    o.normal = v.normal;
    o.color = v.color;
    return o;
}
float4 ScenePS(Pixel p) : SV_TARGET
{
    // Zero normals receive ambient light instead of normalize(0) NaNs.
    float3 normal = p.normal * rsqrt(max(dot(p.normal, p.normal), 1e-12));
    float light = .3 + .7 * abs(dot(normal, normalize(float3(.4, .8, -.6))));
    return float4(p.color.rgb * light, p.color.a);
}
struct QuadPixel { float4 position : SV_POSITION; float2 uv : TEXCOORD; };
QuadPixel QuadVS(uint id : SV_VertexID)
{
    // One oversized triangle covers the target without a quad vertex buffer.
    QuadPixel o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.position = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);
float4 BlurPS(QuadPixel p) : SV_TARGET
{
    // Five taps; radius zero is unblurred. UI overlays are drawn afterward.
    float2 stepSize = options.xy * options.z;
    float4 c = sceneTexture.Sample(sceneSampler, p.uv) * .4;
    c += sceneTexture.Sample(sceneSampler, p.uv + float2(stepSize.x, 0)) * .15;
    c += sceneTexture.Sample(sceneSampler, p.uv - float2(stepSize.x, 0)) * .15;
    c += sceneTexture.Sample(sceneSampler, p.uv + float2(0, stepSize.y)) * .15;
    c += sceneTexture.Sample(sceneSampler, p.uv - float2(0, stepSize.y)) * .15;
    return c;
}
