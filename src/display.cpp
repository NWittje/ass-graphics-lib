#include "internal.hpp"
#include "replay_impl.hpp"
#include "display_ps.h"
#include "display_vs.h"
#include <chrono>
#include <cstring>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <set>
namespace ag
{
namespace
{
constexpr uint64_t MiB = 1024 * 1024;
template <class T> void descriptor(const T *p)
{
    require(p && p->size == sizeof(T) && p->version == 1, L"Unsupported display descriptor size/version");
}
bool name_ok(const char *s)
{
    auto end = static_cast<const char *>(memchr(s, 0, 64));
    if (!end)
        return false;
    return std::all_of(s, end, [](char c) { return c >= 32 && c < 127; });
}
struct Texture
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> raw, encoded;
    uint32_t w{}, h{}, format{};
    bool mipmaps{};
};
struct ConstantBuffer
{
    uint32_t slot{}, bytes{};
    std::vector<BYTE> data;
    std::vector<ag_constant_binding> bindings;
    ComPtr<ID3D11Buffer> gpu;
};
struct Pass
{
    ag_effect_pass desc{};
    std::vector<BYTE> vs, ps, parameters;
    std::vector<ag_effect_input> inputs;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    std::vector<BYTE> padded;
    std::vector<ConstantBuffer> extra;
};
struct Lookup
{
    ag_lookup_texture desc{};
    std::vector<BYTE> pixels;
};
struct Graph
{
    std::vector<Pass> passes;
    std::vector<Lookup> lookups;
    uint32_t history{}, temporal{};
};
struct Targets
{
    Texture source, source_mipped, output;
    std::vector<Texture> passes, lookups, history;
    ComPtr<ID2D1Bitmap1> bitmap;
    uint64_t bytes{};
    uint32_t vw{}, vh{}, left{}, top{};
};
struct alignas(16) Frame
{
    float source[4], output[4], viewport[4], time[4], inputs[16][4];
};
void dimensions(float *v, uint32_t w, uint32_t h)
{
    v[0] = float(w);
    v[1] = float(h);
    v[2] = 1.f / w;
    v[3] = 1.f / h;
}
ag_display_desc config(const ag_display_desc *d)
{
    descriptor(d);
    auto r = *d;
    require(d->warp <= 1 && d->raster_policy <= 1 && finite(d->logical_width) && finite(d->logical_height) &&
                d->logical_width > 0 && d->logical_height > 0,
            L"Invalid display configuration");
    require(d->output_width <= 16384 && d->output_height <= 16384, L"Display dimensions exceed FL11 limits");
    require(!d->raster_policy || (d->source_width && d->source_height && d->source_width <= 16384 &&
                                  d->source_height <= 16384),
            L"Invalid fixed source dimensions");
    if (!r.texture_budget)
        r.texture_budget = 256 * MiB;
    require(r.texture_budget <= 1024 * MiB, L"Display budget exceeds 1GiB");
    return r;
}
Graph graph_copy(const ag_effect_graph *g)
{
    Graph out;
    if (!g)
        return out;
    descriptor(g);
    require(g->pass_count <= 16 && g->lookup_count <= 16 && g->history_count <= 2 && g->temporal <= 1 &&
                (!g->pass_count || g->passes) && (!g->lookup_count || g->lookups),
            L"Effect graph limits exceeded");
    out.history = g->history_count;
    out.temporal = g->temporal || out.history;
    uint64_t cpu_bytes = 0;
    std::map<std::string, uint32_t> names;
    for (uint32_t i = 0; i < g->lookup_count; ++i)
    {
        auto &d = g->lookups[i];
        descriptor(&d);
        require(name_ok(d.name) && d.format <= 2 && d.mipmaps <= 1 && d.width && d.height &&
                    d.width <= 16384 && d.height <= 16384,
                L"Invalid lookup texture");
        uint64_t row = uint64_t(d.width) * (d.format == 2 ? 8 : 4),
                 bytes = uint64_t(d.stride) * (d.height - 1) + row;
        require(d.stride >= row && bytes <= d.bytes && d.pixels && d.bytes <= 256 * MiB,
                L"Invalid lookup pixel bounds");
        cpu_bytes += d.bytes;
        require(cpu_bytes <= 256 * MiB, L"Effect CPU payload budget exceeded");
        if (d.name[0])
            require(names.emplace(d.name, i).second, L"Duplicate lookup name");
        Lookup l;
        l.desc = d;
        l.desc.pixels = nullptr;
        l.pixels.assign(static_cast<const BYTE *>(d.pixels), static_cast<const BYTE *>(d.pixels) + d.bytes);
        out.lookups.push_back(std::move(l));
    }
    names.clear();
    uint64_t shaders = 0;
    for (uint32_t i = 0; i < g->pass_count; ++i)
    {
        auto &d = g->passes[i];
        descriptor(&d);
        require(name_ok(d.name) && d.input_count <= 16 && (!d.input_count || d.inputs) &&
                    d.scale_x_kind <= 2 && d.scale_y_kind <= 2 && d.format <= 2 && d.srgb <= 1 &&
                    d.mipmaps <= 1 && !(d.format == 2 && d.srgb) && finite(d.scale_x) && finite(d.scale_y) &&
                    d.scale_x > 0 && d.scale_y > 0,
                L"Invalid effect pass");
        require(d.vertex_bytes <= 4 * MiB && d.pixel_bytes <= 4 * MiB &&
                    (!d.vertex_bytes || d.vertex_bytecode) && (!d.pixel_bytes || d.pixel_bytecode) &&
                    d.parameter_bytes <= 65536 && (!d.parameter_bytes || d.parameters) &&
                    (!d.parameter_bytes || (d.parameter_slot >= 1 && d.parameter_slot <= 13)),
                L"Invalid shader/constant buffer bounds");
        shaders += uint64_t(d.vertex_bytes) + d.pixel_bytes;
        require(shaders <= 16 * MiB, L"Shader bytecode budget exceeded");
        Pass p;
        p.desc = d;
        p.desc.inputs = nullptr;
        p.desc.parameters = nullptr;
        p.desc.vertex_bytecode = p.desc.pixel_bytecode = nullptr;
        if (d.vertex_bytes)
            p.vs.assign(static_cast<const BYTE *>(d.vertex_bytecode),
                        static_cast<const BYTE *>(d.vertex_bytecode) + d.vertex_bytes);
        if (d.pixel_bytes)
            p.ps.assign(static_cast<const BYTE *>(d.pixel_bytecode),
                        static_cast<const BYTE *>(d.pixel_bytecode) + d.pixel_bytes);
        if (d.parameter_bytes)
            p.parameters.assign(static_cast<const BYTE *>(d.parameters),
                                static_cast<const BYTE *>(d.parameters) + d.parameter_bytes);
        uint32_t slots = 0;
        for (uint32_t j = 0; j < d.input_count; ++j)
        {
            auto in = d.inputs[j];
            descriptor(&in);
            require(in.kind <= 4 && in.slot < 16 && in.linear <= 1 && in.repeat <= 1 && in.mipmaps <= 1 &&
                        in.srgb <= 1 && name_ok(in.name) && !(slots & (1u << in.slot)),
                    L"Invalid or duplicate effect input slot");
            slots |= 1u << in.slot;
            if (in.kind == AG_INPUT_NAMED)
            {
                auto n = names.find(in.name);
                require(n != names.end(), L"Named input must refer to an earlier pass");
                in.kind = AG_INPUT_PASS;
                in.index = n->second;
            }
            if (in.kind == AG_INPUT_PASS)
                require(in.index < i && (!in.mipmaps || out.passes[in.index].desc.mipmaps) &&
                            (!in.srgb || out.passes[in.index].desc.format != 2),
                        L"Invalid pass input");
            if (in.kind == AG_INPUT_LOOKUP)
                require(in.index < out.lookups.size() &&
                            (!in.mipmaps || out.lookups[in.index].desc.mipmaps) &&
                            (!in.srgb || out.lookups[in.index].desc.format != 2),
                        L"Invalid lookup input");
            if (in.kind == AG_INPUT_HISTORY)
                require(in.index < out.history, L"History input exceeds retained frames");
            p.inputs.push_back(in);
        }
        if (d.scale_x_kind == AG_SCALE_INPUT || d.scale_y_kind == AG_SCALE_INPUT)
            require(d.scale_input < d.input_count, L"Missing scaling input");
        if (d.name[0])
            require(names.emplace(d.name, i).second, L"Duplicate pass alias");
        out.passes.push_back(std::move(p));
    }
    return out;
}
struct Display final : Surface
{
    ag_display_desc desc{};
    Graph graph;
    Targets targets;
    Snapshot commands;
    Painter painter;
    ReplayRenderer replay{context};
    bool replay_active{};
    bool source_dirty{true}, prepared{}, offscreen{}, targets_dirty{true};
    uint64_t revision{}, candidate_bytes{};
    uint32_t candidate_count{};
    uint32_t output_w{}, output_h{};
    ag_display_metrics stats{sizeof(ag_display_metrics), 1};
    double elapsed{}, previous{};
    uint64_t frame{};
    std::chrono::steady_clock::time_point retry{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> back;
    ComPtr<ID2D1Device> d2d;
    ComPtr<ID2D1DeviceContext> drawing;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> frame_buffer;
    std::array<ComPtr<ID3D11SamplerState>, 8> samplers;
    struct Query
    {
        ComPtr<ID3D11Query> disjoint, start, end;
        bool pending{};
    };
    std::array<Query, 4> queries;
    uint32_t query_index{};
    Display(Context *c, ag_display_desc d)
        : Surface(Kind::Display, c), desc(d), painter(c), output_w(d.output_width), output_h(d.output_height)
    {
    }
    ~Display()
    {
        reset();
    }
    void shader(Pass &p)
    {
        check(device->CreateVertexShader(p.vs.empty() ? display_vs : p.vs.data(),
                                         p.vs.empty() ? sizeof(display_vs) : p.vs.size(), nullptr, &p.vertex),
              L"Effect vertex bytecode rejected");
        check(device->CreatePixelShader(p.ps.empty() ? display_ps : p.ps.data(),
                                        p.ps.empty() ? sizeof(display_ps) : p.ps.size(), nullptr, &p.pixel),
              L"Effect pixel bytecode rejected");
        if (!p.parameters.empty())
        {
            D3D11_BUFFER_DESC b{};
            b.ByteWidth = (UINT(p.parameters.size()) + 15) & ~15u;
            b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            p.padded.resize(b.ByteWidth);
            std::copy(p.parameters.begin(), p.parameters.end(), p.padded.begin());
            D3D11_SUBRESOURCE_DATA data{p.padded.data()};
            check(device->CreateBuffer(&b, &data, &p.constants), L"Effect constant buffer creation failed");
        }
    }
    void extra_shader(ConstantBuffer &b)
    {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = UINT(b.data.size());
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{b.data.data()};
        check(device->CreateBuffer(&d, &initial, &b.gpu), L"Typed constant creation failed");
    }
    void initialize()
    {
        if (device)
            return;
        require(std::chrono::steady_clock::now() >= retry, L"Graphics device retry pending",
                HRESULT_FROM_WIN32(ERROR_RETRY));
        try
        {
            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
            flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
            D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0, actual{};
            auto create = [&]
            {
                return D3D11CreateDevice(nullptr, desc.warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                                         nullptr, flags, &requested, 1, D3D11_SDK_VERSION, &device, &actual,
                                         &immediate);
            };
            auto hr = create();
            if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
            {
                flags &= ~D3D11_CREATE_DEVICE_DEBUG;
                hr = create();
            }
            check(hr, L"Display requires a D3D11 FL11 device");
            ComPtr<IDXGIDevice> dxgi;
            check(device.As(&dxgi), L"Display DXGI device failed");
            check(context->d2d->CreateDevice(dxgi.Get(), &d2d), L"Display shared D2D device failed");
            check(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &drawing),
                  L"Display drawing context failed");
            check(device->CreateVertexShader(display_vs, sizeof(display_vs), nullptr, &vertex),
                  L"Display vertex shader failed");
            check(device->CreatePixelShader(display_ps, sizeof(display_ps), nullptr, &pixel),
                  L"Display pixel shader failed");
            D3D11_BUFFER_DESC b{};
            b.ByteWidth = sizeof(Frame);
            b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            check(device->CreateBuffer(&b, nullptr, &frame_buffer), L"Display frame constants failed");
            for (uint32_t i = 0; i < 8; ++i)
            {
                D3D11_SAMPLER_DESC s{};
                s.Filter = i & 1 ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
                s.AddressU = s.AddressV = s.AddressW =
                    i & 2 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
                s.MaxLOD = i & 4 ? D3D11_FLOAT32_MAX : 0;
                check(device->CreateSamplerState(&s, &samplers[i]), L"Display sampler failed");
            }
            for (auto &q : queries)
            {
                D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
                check(device->CreateQuery(&d, &q.disjoint), L"GPU timestamp query failed");
                d.Query = D3D11_QUERY_TIMESTAMP;
                check(device->CreateQuery(&d, &q.start), L"GPU start query failed");
                check(device->CreateQuery(&d, &q.end), L"GPU end query failed");
            }
            for (auto &p : graph.passes)
            {
                shader(p);
                for (auto &extra : p.extra)
                    extra_shader(extra);
            }
            ++stats.device_recreations;
            targets_dirty = true;
            source_dirty = true;
        }
        catch (...)
        {
            reset();
            retry = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            if (hwnd)
                SetTimer(hwnd, 0xcda, 1000, nullptr);
            throw;
        }
    }
    Texture texture(uint32_t w, uint32_t h, uint32_t fmt, bool srgb, bool mip, uint64_t &bytes,
                    bool d2d_target = false)
    {
        require(w && h && w <= 16384 && h <= 16384, L"Effect dimensions exceed FL11 limits");
        uint64_t allocation = 0;
        for (uint32_t x = w, y = h;; x = std::max(1u, x / 2), y = std::max(1u, y / 2))
        {
            allocation += uint64_t(x) * y * (fmt == 2 ? 8 : 4);
            if (!mip || (x == 1 && y == 1))
                break;
        }
        auto retained = targets.bytes + candidate_bytes + replay.image_bytes();
        require(retained <= desc.texture_budget && bytes <= desc.texture_budget - retained &&
                    allocation <= desc.texture_budget - retained - bytes,
                L"Display texture budget exceeded", E_OUTOFMEMORY);
        bytes += allocation;
        Texture t;
        t.w = w;
        t.h = h;
        t.format = fmt;
        t.mipmaps = mip;
        DXGI_FORMAT base = fmt == 0   ? DXGI_FORMAT_B8G8R8A8_TYPELESS
                           : fmt == 1 ? DXGI_FORMAT_R8G8B8A8_TYPELESS
                                      : DXGI_FORMAT_R16G16B16A16_FLOAT;
        DXGI_FORMAT raw = fmt == 0   ? DXGI_FORMAT_B8G8R8A8_UNORM
                          : fmt == 1 ? DXGI_FORMAT_R8G8B8A8_UNORM
                                     : base;
        DXGI_FORMAT enc = fmt == 0 ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = mip ? 0 : 1;
        d.ArraySize = 1;
        d.Format = d2d_target ? raw : base;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = mip ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        check(device->CreateTexture2D(&d, nullptr, &t.texture), L"Display texture allocation failed");
        D3D11_RENDER_TARGET_VIEW_DESC r{};
        r.Format = srgb ? enc : raw;
        r.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        check(device->CreateRenderTargetView(t.texture.Get(), &r, &t.target), L"Display target view failed");
        D3D11_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format = raw;
        s.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        s.Texture2D.MipLevels = UINT(-1);
        check(device->CreateShaderResourceView(t.texture.Get(), &s, &t.raw), L"Display sampling view failed");
        if (fmt != 2 && !d2d_target)
        {
            s.Format = enc;
            check(device->CreateShaderResourceView(t.texture.Get(), &s, &t.encoded),
                  L"Display sRGB view failed");
        }
        const float clear[4]{0, 0, 0, 1};
        immediate->ClearRenderTargetView(t.target.Get(), clear);
        if (mip)
            immediate->GenerateMips(t.raw.Get());
        return t;
    }
    const Texture &input(const ag_effect_input &i, const Targets &t) const
    {
        if (i.kind == AG_INPUT_PASS)
            return t.passes[i.index];
        if (i.kind == AG_INPUT_LOOKUP)
            return t.lookups[i.index];
        if (i.kind == AG_INPUT_HISTORY)
            return t.history[i.index];
        return (i.mipmaps || i.srgb) ? t.source_mipped : t.source;
    }
    Targets prepare(const Graph &g, uint32_t w, uint32_t h, bool reuse = false)
    {
        Targets t;
        if (!w || !h)
            return t;
        double scale = std::min(double(w) / desc.logical_width, double(h) / desc.logical_height);
        t.vw = std::max(1u, UINT(std::lround(desc.logical_width * scale)));
        t.vh = std::max(1u, UINT(std::lround(desc.logical_height * scale)));
        t.left = (w - t.vw) / 2;
        t.top = (h - t.vh) / 2;
        uint32_t sw = desc.raster_policy ? desc.source_width : t.vw,
                 sh = desc.raster_policy ? desc.source_height : t.vh;
        // Account for two swap buffers even though DXGI owns their allocations.
        t.bytes = offscreen ? 0 : uint64_t(w) * h * 8;
        require(t.bytes <= desc.texture_budget, L"Swap buffers exceed texture budget", E_OUTOFMEMORY);
        if (reuse)
        {
            t.source = targets.source;
            t.source_mipped = targets.source_mipped;
            t.bitmap = targets.bitmap;
        }
        else
        {
            t.source = texture(sw, sh, 0, false, false, t.bytes, true);
            t.source_mipped = texture(sw, sh, 0, false, true, t.bytes);
        }
        t.output = texture(w, h, 0, false, false, t.bytes);
        for (auto &l : g.lookups)
        {
            auto r = texture(l.desc.width, l.desc.height, l.desc.format, false, l.desc.mipmaps != 0, t.bytes);
            immediate->UpdateSubresource(r.texture.Get(), 0, nullptr, l.pixels.data(), l.desc.stride, 0);
            if (r.mipmaps)
                immediate->GenerateMips(r.raw.Get());
            t.lookups.push_back(std::move(r));
        }
        for (uint32_t i = 0; i < g.history; ++i)
            t.history.push_back(texture(sw, sh, 0, false, true, t.bytes));
        for (auto &p : g.passes)
        {
            auto size = [&](uint32_t k, float factor, bool x)
            {
                double base = 1;
                if (k == AG_SCALE_VIEWPORT)
                    base = x ? t.vw : t.vh;
                if (k == AG_SCALE_INPUT)
                {
                    auto &in = input(p.inputs[p.desc.scale_input], t);
                    base = x ? in.w : in.h;
                }
                double result = std::round(base * factor);
                require(result >= 1 && result <= 16384, L"Invalid effect target size");
                return UINT(result);
            };
            t.passes.push_back(texture(size(p.desc.scale_x_kind, p.desc.scale_x, true),
                                       size(p.desc.scale_y_kind, p.desc.scale_y, false), p.desc.format,
                                       p.desc.srgb != 0, p.desc.mipmaps != 0, t.bytes));
        }
        if (reuse)
        {
            uint64_t shared = uint64_t(sw) * sh * 4;
            for (uint32_t x = sw, y = sh;; x = std::max(1u, x / 2), y = std::max(1u, y / 2))
            {
                shared += uint64_t(x) * y * 4;
                if (x == 1 && y == 1)
                    break;
            }
            t.bytes += shared;
            return t;
        }
        ComPtr<IDXGISurface> surface;
        check(t.source.texture.As(&surface), L"Display source DXGI surface failed");
        auto props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
        check(drawing->CreateBitmapFromDxgiSurface(surface.Get(), &props, &t.bitmap),
              L"Display D2D source target failed");
        return t;
    }
    void clear_bindings()
    {
        ID3D11ShaderResourceView *none[16]{};
        immediate->PSSetShaderResources(0, 16, none);
        immediate->VSSetShaderResources(0, 16, none);
        immediate->OMSetRenderTargets(0, nullptr, nullptr);
    }
    void swap_resize(uint32_t w, uint32_t h)
    {
        if (offscreen)
            return;
        clear_bindings();
        back.Reset();
        if (swap)
            check(swap->ResizeBuffers(2, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0), L"Display swap resize failed");
        else
        {
            ComPtr<IDXGIDevice> dx;
            check(device.As(&dx), L"Display DXGI failed");
            ComPtr<IDXGIAdapter> a;
            check(dx->GetAdapter(&a), L"Display adapter failed");
            ComPtr<IDXGIFactory2> f;
            check(a->GetParent(IID_PPV_ARGS(&f)), L"Display factory failed");
            DXGI_SWAP_CHAIN_DESC1 d{};
            d.Width = w;
            d.Height = h;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            d.BufferCount = 2;
            d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            check(f->CreateSwapChainForHwnd(device.Get(), hwnd, &d, nullptr, nullptr, &swap),
                  L"Display flip swap creation failed");
            f->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        }
        ComPtr<ID3D11Texture2D> b;
        check(swap->GetBuffer(0, IID_PPV_ARGS(&b)), L"Display backbuffer failed");
        check(device->CreateRenderTargetView(b.Get(), nullptr, &back), L"Display back view failed");
    }
    void ensure()
    {
        initialize();
        if (!targets_dirty)
            return;
        bool reuse = desc.raster_policy && targets.source.w == desc.source_width &&
                     targets.source.h == desc.source_height && targets.source.texture;
        auto next = prepare(graph, output_w, output_h, reuse);
        swap_resize(output_w, output_h);
        drawing->SetTarget(nullptr);
        if (!reuse)
        {
            painter.reset();
            source_dirty = true;
            prepared = false;
        }
        targets = std::move(next);
        targets_dirty = false;
        require(targets.bytes + candidate_bytes + replay.image_bytes() <= desc.texture_budget,
                L"Replay texture budget exceeded", E_OUTOFMEMORY);
        stats.texture_bytes = targets.bytes + replay.image_bytes();
        stats.source_width = targets.source.w;
        stats.source_height = targets.source.h;
        stats.viewport_width = targets.vw;
        stats.viewport_height = targets.vh;
    }
    void query_collect()
    {
        for (auto &q : queries)
            if (q.pending)
            {
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
                UINT64 a{}, b{};
                if (immediate->GetData(q.disjoint.Get(), &d, sizeof(d), D3D11_ASYNC_GETDATA_DONOTFLUSH) ==
                        S_OK &&
                    immediate->GetData(q.start.Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) ==
                        S_OK &&
                    immediate->GetData(q.end.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
                {
                    q.pending = false;
                    stats.gpu_known = !d.Disjoint && d.Frequency;
                    stats.gpu_ms = stats.gpu_known ? double(b - a) * 1000 / d.Frequency : 0;
                }
            }
    }
    void quad(ID3D11RenderTargetView *target, uint32_t w, uint32_t h, ID3D11VertexShader *vs,
              ID3D11PixelShader *ps)
    {
        immediate->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
        immediate->RSSetViewports(1, &viewport);
        immediate->IASetInputLayout(nullptr);
        immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        immediate->VSSetShader(vs, nullptr, 0);
        immediate->PSSetShader(ps, nullptr, 0);
    }
    void render(double seconds, bool force = false)
    {
        require(std::isfinite(seconds) && seconds >= 0, L"Invalid display elapsed time");
        elapsed = seconds;
        stats.suspended = !output_w || !output_h ||
                          (!offscreen && (!IsWindowVisible(hwnd) || IsIconic(GetAncestor(hwnd, GA_ROOT))));
        if (stats.suspended)
            return;
        auto start = std::chrono::steady_clock::now();
        auto allocations = cpp_allocations;
        ensure();
        if (replay_active && !force && !source_dirty && !graph.temporal && !stats.error)
        {
            stats.cpu_ms = 0;
            stats.cpp_allocations = cpp_allocations - allocations;
            return;
        }
        query_collect();
        auto &q = queries[query_index++ % queries.size()];
        bool measuring = !q.pending;
        if (measuring)
        {
            immediate->Begin(q.disjoint.Get());
            immediate->End(q.start.Get());
        }
        clear_bindings();
        if (source_dirty)
        {
            drawing->SetTarget(targets.bitmap.Get());
            drawing->SetDpi(96, 96);
            if (!prepared)
            {
                if (!replay_active)
                    painter.prepare(drawing.Get(), commands);
                prepared = true;
            }
            drawing->BeginDraw();
            drawing->Clear(D2D1::ColorF(0, 0, 0, 1));
            float scale = std::min(float(targets.source.w) / desc.logical_width,
                                   float(targets.source.h) / desc.logical_height);
            // Painter commands carry logical transforms; multiply their transform by
            // a root raster transform without modifying the retained CPU snapshot.
            painter.root =
                D2D1::Matrix3x2F::Scale(scale, scale) *
                D2D1::Matrix3x2F::Translation((targets.source.w - desc.logical_width * scale) / 2,
                                              (targets.source.h - desc.logical_height * scale) / 2);
            if (replay_active)
                replay.draw(drawing.Get(), painter.root);
            else
                painter.draw(drawing.Get(), commands);
            check(drawing->EndDraw(), L"Display source drawing failed");
            drawing->SetTarget(nullptr);
            immediate->CopySubresourceRegion(targets.source_mipped.texture.Get(), 0, 0, 0, 0,
                                             targets.source.texture.Get(), 0, nullptr);
            immediate->GenerateMips(targets.source_mipped.raw.Get());
            source_dirty = false;
            ++stats.source_draws;
        }
        Frame constants{};
        dimensions(constants.source, targets.source.w, targets.source.h);
        dimensions(constants.viewport, targets.vw, targets.vh);
        constants.time[0] = float(seconds);
        constants.time[1] = float(std::max(0., seconds - previous));
        constants.time[2] = float(frame);
        for (size_t i = 0; i < graph.passes.size(); ++i)
        {
            clear_bindings();
            auto &p = graph.passes[i];
            auto &out = targets.passes[i];
            quad(out.target.Get(), out.w, out.h, p.vertex.Get(), p.pixel.Get());
            dimensions(constants.output, out.w, out.h);
            std::memset(constants.inputs, 0, sizeof(constants.inputs));
            for (auto &in : p.inputs)
            {
                auto &t = input(in, targets);
                dimensions(constants.inputs[in.slot], t.w, t.h);
                auto srv = in.srgb ? t.encoded.Get() : t.raw.Get();
                require(srv, L"sRGB input format unavailable");
                auto sampler = samplers[in.linear + 2 * in.repeat + 4 * in.mipmaps].Get();
                immediate->PSSetShaderResources(in.slot, 1, &srv);
                immediate->VSSetShaderResources(in.slot, 1, &srv);
                immediate->PSSetSamplers(in.slot, 1, &sampler);
                immediate->VSSetSamplers(in.slot, 1, &sampler);
            }
            immediate->UpdateSubresource(frame_buffer.Get(), 0, nullptr, &constants, 0, 0);
            auto cb = frame_buffer.Get();
            immediate->VSSetConstantBuffers(0, 1, &cb);
            immediate->PSSetConstantBuffers(0, 1, &cb);
            if (p.constants)
            {
                cb = p.constants.Get();
                immediate->VSSetConstantBuffers(p.desc.parameter_slot, 1, &cb);
                immediate->PSSetConstantBuffers(p.desc.parameter_slot, 1, &cb);
            }
            for (auto &buffer : p.extra)
            {
                for (auto &binding : buffer.bindings)
                {
                    auto dst = buffer.data.data() + binding.offset;
                    if (binding.kind == AG_CONSTANT_FRAME_UINT)
                    {
                        uint32_t n = uint32_t(frame);
                        memcpy(dst, &n, 4);
                    }
                    else if (binding.kind == AG_CONSTANT_TIME_FLOAT)
                    {
                        float n = float(seconds);
                        memcpy(dst, &n, 4);
                    }
                    else
                    {
                        const float *v = binding.kind == AG_CONSTANT_SOURCE_SIZE   ? constants.source
                                         : binding.kind == AG_CONSTANT_OUTPUT_SIZE ? constants.output
                                         : binding.kind == AG_CONSTANT_VIEWPORT_SIZE
                                             ? constants.viewport
                                             : constants.inputs[binding.index];
                        memcpy(dst, v, 16);
                    }
                }
                immediate->UpdateSubresource(buffer.gpu.Get(), 0, nullptr, buffer.data.data(), 0, 0);
                auto b = buffer.gpu.Get();
                immediate->VSSetConstantBuffers(buffer.slot, 1, &b);
                immediate->PSSetConstantBuffers(buffer.slot, 1, &b);
            }
            immediate->Draw(3, 0);
            clear_bindings();
            if (out.mipmaps)
                immediate->GenerateMips(out.raw.Get());
        }
        clear_bindings();
        const float black[4]{0, 0, 0, 1};
        immediate->ClearRenderTargetView(targets.output.target.Get(), black);
        quad(targets.output.target.Get(), output_w, output_h, vertex.Get(), pixel.Get());
        D3D11_VIEWPORT viewport{
            float(targets.left), float(targets.top), float(targets.vw), float(targets.vh), 0, 1};
        immediate->RSSetViewports(1, &viewport);
        auto srv = targets.passes.empty() ? targets.source.raw.Get() : targets.passes.back().raw.Get();
        auto sampler = samplers[1].Get();
        immediate->PSSetShaderResources(0, 1, &srv);
        immediate->PSSetSamplers(0, 1, &sampler);
        immediate->Draw(3, 0);
        clear_bindings();
        for (size_t i = targets.history.size(); i > 1; --i)
            immediate->CopyResource(targets.history[i - 1].texture.Get(),
                                    targets.history[i - 2].texture.Get());
        if (!targets.history.empty())
        {
            immediate->CopySubresourceRegion(targets.history[0].texture.Get(), 0, 0, 0, 0,
                                             targets.source.texture.Get(), 0, nullptr);
            immediate->GenerateMips(targets.history[0].raw.Get());
        }
        if (measuring)
        {
            immediate->End(q.end.Get());
            immediate->End(q.disjoint.Get());
            q.pending = true;
        }
        if (swap)
        {
            ComPtr<ID3D11Texture2D> b;
            check(swap->GetBuffer(0, IID_PPV_ARGS(&b)), L"Display present buffer failed");
            immediate->CopyResource(b.Get(), targets.output.texture.Get());
            check(swap->Present(0, 0), L"Display presentation failed");
        }
        ++frame;
        ++stats.frames;
        previous = seconds;
        stats.temporal = graph.temporal;
        stats.cpp_allocations = cpp_allocations - allocations;
        stats.cpu_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        stats.error = 0;
    }
    void get_metrics(ag_metrics &out) override
    {
        out = metrics;
        out.frames = stats.frames;
        out.last_render_ms = stats.cpu_ms;
        out.device_recreations = uint32_t(stats.device_recreations);
        out.last_cpp_allocations = stats.cpp_allocations;
    }
    void timer(UINT_PTR id) override
    {
        if (id == 0xcda)
        {
            KillTimer(hwnd, id);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    void paint() override
    {
        try
        {
            render(elapsed, true);
        }
        catch (const Failure &e)
        {
            last_result = e.hr;
            stats.error = uint32_t(e.hr);
            if (e.hr == DXGI_ERROR_DEVICE_REMOVED || e.hr == DXGI_ERROR_DEVICE_RESET ||
                e.hr == D2DERR_RECREATE_TARGET)
            {
                reset();
                retry = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                if (hwnd)
                    SetTimer(hwnd, 0xcda, 1000, nullptr);
            }
            throw;
        }
    }
    void resize() override
    {
        ++revision;
        RECT r{};
        GetClientRect(hwnd, &r);
        output_w = std::max(0L, r.right);
        output_h = std::max(0L, r.bottom);
        targets_dirty = true;
    }
    void reset() override
    {
        replay.reset();
        ++revision;
        if (hwnd)
            KillTimer(hwnd, 0xcda);
        if (immediate)
        {
            immediate->ClearState();
            immediate->Flush();
        }
        if (drawing)
            drawing->SetTarget(nullptr);
        painter.reset();
        targets = {};
        back.Reset();
        swap.Reset();
        drawing.Reset();
        d2d.Reset();
        frame_buffer.Reset();
        vertex.Reset();
        pixel.Reset();
        for (auto &p : graph.passes)
        {
            p.vertex.Reset();
            p.pixel.Reset();
            p.constants.Reset();
            for (auto &b : p.extra)
                b.gpu.Reset();
        }
        for (auto &s : samplers)
            s.Reset();
        for (auto &q : queries)
            q = {};
        immediate.Reset();
        device.Reset();
        targets_dirty = true;
        source_dirty = true;
        prepared = false;
        stats.texture_bytes = 0;
        stats.gpu_known = 0;
    }
    void export_png(const wchar_t *path)
    {
        require(path && path[0], L"PNG destination required");
        render(elapsed);
        require(!stats.suspended, L"Cannot export suspended display");
        D3D11_TEXTURE2D_DESC d{};
        targets.output.texture->GetDesc(&d);
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.BindFlags = d.MiscFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        check(device->CreateTexture2D(&d, nullptr, &staging), L"Display readback allocation failed");
        immediate->CopyResource(staging.Get(), targets.output.texture.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        check(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m), L"Display readback failed");
        try
        {
            save_pixels(context, path, output_w, output_h, m.RowPitch, static_cast<const BYTE *>(m.pData),
                        96);
        }
        catch (...)
        {
            immediate->Unmap(staging.Get(), 0);
            throw;
        }
        immediate->Unmap(staging.Get(), 0);
        ++stats.readbacks;
    }
};
struct Candidate final : Object
{
    Display *owner;
    uint64_t revision;
    ag_display_desc desc;
    Graph graph;
    Targets targets;
    uint32_t w, h;
    uint64_t reserved{};
    ~Candidate()
    {
        if (std::find(context->objects.begin(), context->objects.end(), owner) != context->objects.end())
        {
            owner->candidate_bytes -= reserved;
            --owner->candidate_count;
        }
    }
    Candidate(Display *p)
        : Object(Kind::DisplayCandidate, p->context), owner(p), revision(p->revision), desc(p->desc),
          w(p->output_w), h(p->output_h)
    {
        ++p->candidate_count;
    }
};
Candidate *candidate(ag_handle h)
{
    auto c = get<Candidate>(h, Kind::DisplayCandidate);
    c->ui();
    require(std::find(c->context->objects.begin(), c->context->objects.end(), c->owner) !=
                c->context->objects.end(),
            L"Candidate display no longer exists");
    require(c->revision == c->owner->revision && c->w == c->owner->output_w && c->h == c->owner->output_h,
            L"Candidate invalidated by display resize/reset");
    return c;
}
Display *display(ag_handle h)
{
    auto p = get<Display>(h, Kind::Display);
    p->ui();
    return p;
}
} // namespace
} // namespace ag
using namespace ag;
ag_result ag_display_create(ag_handle h, void *parent, ag_rect bounds, const ag_display_desc *d,
                            ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto c = get<Context>(h, Kind::Context);
            c->ui();
            auto p = std::make_unique<Display>(c, config(d));
            p->offscreen = parent == nullptr;
            if (parent)
            {
                p->create(static_cast<HWND>(parent), bounds);
                p->resize();
            }
            p->initialize();
            *out = p.release();
        });
}
ag_result ag_display_configure(ag_handle h, const ag_display_desc *d)
{
    return api(
        [&]
        {
            auto p = display(h);
            auto next = config(d);
            require(next.warp == p->desc.warp, L"Device selection requires a new surface");
            p->initialize();
            auto old = p->desc;
            p->desc = next;
            try
            {
                auto t = p->prepare(p->graph, p->output_w, p->output_h);
                p->drawing->SetTarget(nullptr);
                p->painter.reset();
                p->targets = std::move(t);
                p->targets_dirty = false;
                p->source_dirty = true;
                p->prepared = false;
                ++p->revision;
            }
            catch (...)
            {
                p->desc = old;
                throw;
            }
        });
}
ag_result ag_display_output_size(ag_handle h, uint32_t w, uint32_t height)
{
    return api(
        [&]
        {
            auto p = display(h);
            require(p->offscreen && w <= 16384 && height <= 16384,
                    L"Explicit output size requires an offscreen display");
            if (p->output_w != w || p->output_h != height)
            {
                p->output_w = w;
                p->output_h = height;
                p->targets_dirty = true;
            }
        });
}
ag_result ag_display_submit(ag_handle h, const ag_draw *cmd, uint32_t n, const ag_point *points, uint32_t pn,
                            const ag_path_draw *paths, uint32_t path_count)
{
    return api(
        [&]
        {
            auto p = display(h);
            auto next = copy_snapshot(p->context, cmd, n, points, pn, paths, path_count);
            p->replay_active = false;
            p->commands = std::move(next);
            p->source_dirty = true;
            p->prepared = false;
            ++p->metrics.submissions;
            if (p->hwnd)
                InvalidateRect(p->hwnd, nullptr, FALSE);
        });
}
ag_result ag_display_submit_replay(ag_handle h, const ag_replay_snapshot *s)
{
    return api(
        [&]
        {
            auto p = display(h);
            descriptor(s);
            require(p->targets.bytes + p->candidate_bytes <= p->desc.texture_budget,
                    L"Replay texture budget exceeded", E_OUTOFMEMORY);
            bool changed =
                p->replay.submit(*s, p->desc.texture_budget - p->targets.bytes - p->candidate_bytes);
            require(p->targets.bytes + p->candidate_bytes + p->replay.image_bytes() <= p->desc.texture_budget,
                    L"Replay texture budget exceeded", E_OUTOFMEMORY);
            changed = changed || !p->replay_active;
            p->replay_active = true;
            if (changed)
            {
                p->source_dirty = true;
                p->prepared = false;
            }
            ++p->metrics.submissions;
            if (p->hwnd && changed)
                InvalidateRect(p->hwnd, nullptr, FALSE);
        });
}
ag_result ag_display_replay_metrics(ag_handle h, ag_replay_metrics *out)
{
    return api(
        [&]
        {
            descriptor(out);
            *out = display(h)->replay.metrics;
        });
}
ag_result ag_display_effects(ag_handle h, const ag_effect_graph *g)
{
    return api(
        [&]
        {
            auto p = display(h);
            auto next = graph_copy(g);
            p->initialize();
            for (auto &pass : next.passes)
                p->shader(pass);
            auto t = p->prepare(next, p->output_w, p->output_h);
            p->drawing->SetTarget(nullptr);
            p->painter.reset();
            p->graph = std::move(next);
            p->targets = std::move(t);
            p->targets_dirty = false;
            p->source_dirty = true;
            p->prepared = false;
            p->stats.temporal = p->graph.temporal;
            ++p->revision;
        });
}
ag_result ag_display_parameters(ag_handle h, uint32_t i, const void *data, uint32_t bytes)
{
    return api(
        [&]
        {
            auto p = display(h);
            require(i < p->graph.passes.size(), L"Invalid effect parameter pass");
            auto &pass = p->graph.passes[i];
            require(bytes == pass.parameters.size() && (!bytes || data),
                    L"Parameter size must match the installed layout");
            if (bytes)
            {
                p->initialize();
                std::memcpy(pass.padded.data(), data, bytes);
                std::memcpy(pass.parameters.data(), data, bytes);
                p->immediate->UpdateSubresource(pass.constants.Get(), 0, nullptr, pass.padded.data(), 0, 0);
            }
        });
}
ag_result ag_display_render(ag_handle h, double seconds)
{
    return api(
        [&]
        {
            auto p = display(h);
            try
            {
                p->render(seconds);
            }
            catch (const Failure &e)
            {
                p->stats.error = uint32_t(e.hr);
                if (e.hr == DXGI_ERROR_DEVICE_REMOVED || e.hr == DXGI_ERROR_DEVICE_RESET ||
                    e.hr == D2DERR_RECREATE_TARGET)
                {
                    p->reset();
                    p->retry = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    if (p->hwnd)
                        SetTimer(p->hwnd, 0xcda, 1000, nullptr);
                }
                throw;
            }
        });
}
ag_result ag_display_export_png(ag_handle h, const wchar_t *path)
{
    return api([&] { display(h)->export_png(path); });
}
ag_result ag_display_metrics_get(ag_handle h, ag_display_metrics *out)
{
    return api(
        [&]
        {
            descriptor(out);
            auto p = display(h);
            if (p->immediate)
                p->query_collect();
            *out = p->stats;
            out->texture_bytes = p->targets.bytes + p->replay.image_bytes();
            out->source_width = p->targets.source.w;
            out->source_height = p->targets.source.h;
            out->viewport_width = p->targets.vw;
            out->viewport_height = p->targets.vh;
        });
}
ag_result ag_display_capabilities(ag_handle h, ag_display_caps *out)
{
    return api(
        [&]
        {
            descriptor(out);
            auto p = display(h);
            p->initialize();
            *out = {sizeof(*out), 1, D3D_FEATURE_LEVEL_11_0, 16384, 16, 16, 16, 2};
            auto supported = [&](DXGI_FORMAT f)
            {
                UINT flags{};
                return SUCCEEDED(p->device->CheckFormatSupport(f, &flags)) &&
                       (flags & D3D11_FORMAT_SUPPORT_RENDER_TARGET) &&
                       (flags & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
            };
            out->bgra8 = supported(DXGI_FORMAT_B8G8R8A8_UNORM);
            out->rgba8 = supported(DXGI_FORMAT_R8G8B8A8_UNORM);
            out->rgba16_float = supported(DXGI_FORMAT_R16G16B16A16_FLOAT);
            out->mipmaps = 1;
        });
}

ag_result ag_display_prepare(ag_handle h, const ag_display_desc *d, const ag_effect_graph *g, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto p = display(h);
            require(p->candidate_count < 4,
                    L"Four display candidates already prepared; abort unused candidates");
            p->initialize();
            auto c = std::make_unique<Candidate>(p);
            if (d)
                c->desc = config(d);
            require(c->desc.warp == p->desc.warp, L"Candidate cannot replace device selection");
            c->graph = graph_copy(g);
            p->initialize();
            for (auto &pass : c->graph.passes)
                p->shader(pass);
            auto old = p->desc;
            p->desc = c->desc;
            try
            {
                c->targets = p->prepare(c->graph, p->output_w, p->output_h);
            }
            catch (...)
            {
                p->desc = old;
                throw;
            }
            p->desc = old;
            if (!p->offscreen && p->output_w && p->output_h)
                p->swap_resize(p->output_w, p->output_h);
            c->reserved = c->targets.bytes;
            p->candidate_bytes += c->reserved;
            *out = c.release();
        });
}
ag_result ag_display_candidate_constants(ag_handle h, uint32_t pass, uint32_t slot, const void *data,
                                         uint32_t bytes, const ag_constant_binding *bindings, uint32_t count)
{
    return api(
        [&]
        {
            auto c = candidate(h);
            require(pass < c->graph.passes.size() && slot >= 1 && slot <= 13 && bytes && bytes <= 65536 &&
                        data && count <= 128 && (!count || bindings),
                    L"Invalid typed constant descriptor");
            auto &p = c->graph.passes[pass];
            require(p.parameters.empty() || slot != p.desc.parameter_slot, L"Constant register conflict");
            for (auto &b : p.extra)
                require(b.slot != slot, L"Duplicate constant register");
            ConstantBuffer b;
            b.slot = slot;
            b.bytes = bytes;
            b.data.resize((bytes + 15) & ~15u);
            memcpy(b.data.data(), data, bytes);
            for (uint32_t i = 0; i < count; ++i)
            {
                auto v = bindings[i];
                descriptor(&v);
                uint32_t width = v.kind >= AG_CONSTANT_FRAME_UINT ? 4 : 16;
                require(v.kind <= AG_CONSTANT_TIME_FLOAT && v.index < 16 && v.offset % 4 == 0 &&
                            uint64_t(v.offset) + width <= bytes,
                        L"Typed constant exceeds buffer");
                for (auto &old : b.bindings)
                {
                    auto ow = old.kind >= AG_CONSTANT_FRAME_UINT ? 4u : 16u;
                    require(v.offset >= old.offset + ow || old.offset >= v.offset + width,
                            L"Overlapping typed constants");
                }
                b.bindings.push_back(v);
            }
            c->owner->extra_shader(b);
            p.extra.push_back(std::move(b));
        });
}
ag_result ag_display_commit(ag_handle h, ag_handle handle)
{
    return api(
        [&]
        {
            auto p = display(h);
            auto c = candidate(handle);
            require(c->owner == p, L"Candidate belongs to another display");
            p->drawing->SetTarget(nullptr);
            p->painter.reset();
            p->candidate_bytes -= c->reserved;
            c->reserved = 0;
            p->desc = c->desc;
            p->graph = std::move(c->graph);
            p->targets = std::move(c->targets);
            p->targets_dirty = false;
            p->source_dirty = true;
            p->prepared = false;
            p->stats.temporal = p->graph.temporal;
            ++p->revision;
        });
}
ag_result ag_display_abort(ag_handle h)
{
    return api(
        [&]
        {
            if (h)
                get<Candidate>(h, Kind::DisplayCandidate)->ui();
            check(ag_destroy(h), L"Display candidate disposal failed");
        });
}
ag_result ag_display_constants(ag_handle h, uint32_t pass, uint32_t slot, const void *data, uint32_t bytes)
{
    return api(
        [&]
        {
            auto p = display(h);
            require(pass < p->graph.passes.size() && data, L"Invalid constant update");
            auto &list = p->graph.passes[pass].extra;
            auto b = std::find_if(list.begin(), list.end(), [&](auto &v) { return v.slot == slot; });
            require(b != list.end() && bytes == b->bytes, L"Constant layout mismatch");
            p->initialize();
            memcpy(b->data.data(), data, bytes);
            p->immediate->UpdateSubresource(b->gpu.Get(), 0, nullptr, b->data.data(), 0, 0);
        });
}
ag_result ag_display_history_clear(ag_handle h)
{
    return api(
        [&]
        {
            auto p = display(h);
            if (!p->immediate)
                return;
            const float black[4]{0, 0, 0, 1};
            for (auto &t : p->targets.history)
            {
                p->immediate->ClearRenderTargetView(t.target.Get(), black);
                if (t.mipmaps)
                    p->immediate->GenerateMips(t.raw.Get());
            }
        });
}
