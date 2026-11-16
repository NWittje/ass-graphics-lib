// Matches the native buffer: row-vector, left-handed view/projection matrices.
cbuffer SceneConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 options; // inverse width/height, blur radius in pixels, reserved
    float4 sunDirectionEnabled, sun, sky, ground, fogColor, eyeFogStart, fogRange;
    float4 radialOriginRange, radialColor;
    float4 skyHorizon, skyZenith, skyRight, skyUp, skyForward;
    float4 vertexFogColor;
};
struct Vertex { float3 position : POSITION; float3 normal : NORMAL; float4 color : COLOR; };
struct Pixel { float4 position : SV_POSITION; float3 normal : NORMAL; float4 color : COLOR; float3 world : TEXCOORD0; };
Pixel SceneVS(Vertex v)
{
    Pixel o;
    o.position = mul(float4(v.position, 1), viewProjection);
    o.normal = v.normal;
    o.color = v.color;
    o.world = v.position;
    return o;
}
float4 ScenePS(Pixel p) : SV_TARGET
{
    // Zero normals receive ambient light instead of normalize(0) NaNs.
    float3 normal = p.normal * rsqrt(max(dot(p.normal, p.normal), 1e-12));
    if (sunDirectionEnabled.w != 0) {
        float hemisphere = saturate(normal.y * .5 + .5);
        float3 fill = lerp(ground.rgb * ground.w, sky.rgb * sky.w, hemisphere);
        float3 lit = p.color.rgb * (fill + sun.rgb * sun.w * max(dot(normal, sunDirectionEnabled.xyz), 0));
        if (vertexFogColor.w != 0)
            return float4(lerp(lit, vertexFogColor.rgb, saturate(p.color.a)), 1);
        if (fogRange.y != 0) {
            float radial = saturate((distance(p.world.xz, radialOriginRange.xy) - radialOriginRange.z) /
                                    (radialOriginRange.w - radialOriginRange.z));
            return float4(lerp(lit, radialColor.rgb, radial), p.color.a);
        }
        float amount = saturate((distance(p.world, eyeFogStart.xyz) - eyeFogStart.w) /
                                (fogRange.x - eyeFogStart.w));
        return float4(lerp(lit, fogColor.rgb, amount), p.color.a);
    }
    float light = .3 + .7 * abs(dot(normal, normalize(float3(.4, .8, -.6))));
    if (vertexFogColor.w != 0)
        return float4(lerp(p.color.rgb * light, vertexFogColor.rgb, saturate(p.color.a)), 1);
    if (fogRange.y != 0) {
        float radial = saturate((distance(p.world.xz, radialOriginRange.xy) - radialOriginRange.z) /
                                (radialOriginRange.w - radialOriginRange.z));
        return float4(lerp(p.color.rgb * light, radialColor.rgb, radial), p.color.a);
    }
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
float4 SkyPS(QuadPixel p) : SV_TARGET
{
    float3 ray = normalize(skyForward.xyz + skyRight.xyz * ((p.uv.x * 2 - 1) * skyRight.w)
                           + skyUp.xyz * ((1 - p.uv.y * 2) * skyUp.w));
    return float4(lerp(skyHorizon.rgb, skyZenith.rgb, saturate(ray.y)), 1);
}
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
