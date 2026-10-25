Texture2D image : register(t0);
SamplerState sampling : register(s0);
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V DisplayVS(uint id : SV_VertexID) {
    V v; v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;
}
float4 DisplayPS(V v) : SV_Target { return image.Sample(sampling,v.uv); }
