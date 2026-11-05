#pragma once
#include "ass_graphics/ordered.h"
#include "ordered_vs.h"
#include <d3d11_3.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include "ordered_images_impl.hpp"

namespace ag
{
// Trusted precompiled programs are admitted cold. Reflection checks the resource
// contract; it cannot prove bounded execution or same-pixel addressing.
struct OrderedProgramData
{
    uint32_t contract{1}, image_mask{};
    std::vector<uint8_t> material, resolve, constants;
    ComPtr<ID3D11PixelShader> material_gpu, resolve_gpu;
    ComPtr<ID3D11Buffer> constants_gpu;
    ComPtr<ID3D11Device> device;
    uint64_t bytes() const
    {
        return sizeof(*this) + material.capacity() + resolve.capacity() + constants.capacity();
    }
    void reset()
    {
        material_gpu.Reset();
        resolve_gpu.Reset();
        constants_gpu.Reset();
        device.Reset();
    }
    void prepare(ID3D11Device *d)
    {
        if (device.Get() == d && material_gpu && resolve_gpu && (constants.empty() || constants_gpu))
            return;
        reset();
        check(d->CreatePixelShader(material.data(), material.size(), nullptr, &material_gpu),
              L"Ordered material shader failed");
        check(d->CreatePixelShader(resolve.data(), resolve.size(), nullptr, &resolve_gpu),
              L"Ordered resolve shader failed");
        if (!constants.empty())
        {
            D3D11_BUFFER_DESC b{};
            b.ByteWidth = UINT(constants.size());
            b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            b.Usage = D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA data{constants.data(), 0, 0};
            check(d->CreateBuffer(&b, &data, &constants_gpu), L"Ordered constants failed");
        }
        device = d;
    }
    static uint32_t reflect(const std::vector<uint8_t> &code, bool material, uint32_t constants,
                            uint32_t contract = 1)
    {
        ComPtr<ID3D11ShaderReflection> r;
        check(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&r)), L"Ordered shader reflection failed");
        D3D11_SHADER_DESC desc{};
        check(r->GetDesc(&desc), L"Ordered shader description failed");
        require(D3D11_SHVER_GET_TYPE(desc.Version) == D3D11_SHVER_PIXEL_SHADER &&
                    D3D11_SHVER_GET_MAJOR(desc.Version) == 5 && D3D11_SHVER_GET_MINOR(desc.Version) == 0,
                L"Ordered shaders require ps_5_0");
        // DXBC's ROV feature bit has no named constant in d3d11shader.h.
        constexpr UINT64 rov = 0x1000, early = D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL;
        const auto flags = r->GetRequiresFlags();
        require(material ? flags == (rov | early) : flags == 0, L"Ordered shader requirements mismatch");
        require(desc.InputParameters == (material ? 2u : 1u) && desc.OutputParameters == (material ? 0u : 1u),
                L"Ordered shader signature mismatch");
        bool position = false, index = false;
        for (UINT i = 0; i < desc.InputParameters; ++i)
        {
            D3D11_SIGNATURE_PARAMETER_DESC p{};
            check(r->GetInputParameterDesc(i, &p), L"Ordered input reflection failed");
            if (p.SystemValueType == D3D_NAME_POSITION && p.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32 &&
                p.Mask == 15)
                position = true;
            else if (material && !_stricmp(p.SemanticName, "MATERIAL") && p.SemanticIndex == 0 &&
                     p.SystemValueType == D3D_NAME_UNDEFINED &&
                     p.ComponentType == D3D_REGISTER_COMPONENT_UINT32 && p.Mask == 1)
                index = true;
            else
                require(false, L"Unexpected ordered shader input");
        }
        require(position && (!material || index), L"Missing ordered shader input");
        if (!material)
        {
            D3D11_SIGNATURE_PARAMETER_DESC p{};
            check(r->GetOutputParameterDesc(0, &p), L"Ordered output reflection failed");
            require(p.SystemValueType == D3D_NAME_TARGET && p.SemanticIndex == 0 && p.Mask == 15 &&
                        p.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32,
                    L"Ordered resolve output mismatch");
        }
        uint32_t bindings = 0;
        for (UINT i = 0; i < desc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC b{};
            check(r->GetResourceBindingDesc(i, &b), L"Ordered binding reflection failed");
            require(b.BindCount == 1, L"Ordered binding arrays are unsupported");
            uint32_t bit = 0;
            if (b.Type == D3D_SIT_CBUFFER && b.BindPoint == 1 && constants)
            {
                D3D11_SHADER_BUFFER_DESC cb{};
                check(r->GetConstantBufferByName(b.Name)->GetDesc(&cb),
                      L"Ordered constant reflection failed");
                require(cb.Size == constants, L"Ordered constant size mismatch");
                bit = 8;
            }
            else if (material && b.Type == D3D_SIT_STRUCTURED && b.BindPoint == 0 &&
                     b.Dimension == D3D_SRV_DIMENSION_BUFFER)
                bit = 4;
            else if (material && contract == 2 && b.Type == D3D_SIT_TEXTURE &&
                     b.BindPoint >= 1 && b.BindPoint <= 2 &&
                     b.Dimension == D3D_SRV_DIMENSION_TEXTURE2D && b.ReturnType == D3D_RETURN_TYPE_UINT)
                bit = 16u << (b.BindPoint - 1);
            else if (b.Type == (material ? D3D_SIT_UAV_RWTYPED : D3D_SIT_TEXTURE) && b.BindPoint < 2 &&
                     b.Dimension == D3D_SRV_DIMENSION_TEXTURE2D && b.ReturnType == D3D_RETURN_TYPE_UINT)
                bit = 1u << b.BindPoint;
            require(bit && !(bindings & bit), L"Unexpected ordered resource binding");
            bindings |= bit;
        }
        require((bindings & 7) == (material ? 7u : 3u), L"Missing ordered resource binding");
        // A shared constants block may be optimized out of either program.
        return (bindings >> 4) & 3;
    }
};
struct OrderedProgram final : Object
{
    std::shared_ptr<OrderedProgramData> data;
    std::weak_ptr<int> owner;
    OrderedProgram(Context *c, std::shared_ptr<OrderedProgramData> p, const std::shared_ptr<int> &o)
        : Object(Kind::OrderedProgram, c), data(std::move(p)), owner(o)
    {
    }
};

struct OrderedRenderer
{
    struct Vertex
    {
        float x, y;
        uint32_t record;
    };
    struct Run
    {
        uint32_t operation, first, count;
    };
    struct Scene
    {
        std::unique_ptr<StencilRenderer> coverage;
        std::shared_ptr<OrderedProgramData> program;
        std::array<std::shared_ptr<OrderedImageData>, 2> images;
        std::vector<Vertex> vertices;
        std::vector<Run> runs;
        std::vector<ag_ordered_record> records;
        uint64_t coverage_bytes{};
        uint32_t initial[2]{};
        ComPtr<ID3D11Buffer> vertex_gpu, records_gpu;
        ComPtr<ID3D11ShaderResourceView> records_view;
        uint64_t bytes() const
        {
            return sizeof(*this) + coverage_bytes + vertices.capacity() * sizeof(Vertex) +
                   runs.capacity() * sizeof(Run) + records.capacity() * sizeof(ag_ordered_record);
        }
        void reset()
        {
            vertex_gpu.Reset();
            records_gpu.Reset();
            records_view.Reset();
            coverage->reset();
        }
    };
    Context *context;
    std::shared_ptr<int> identity = std::make_shared<int>(0);
    std::vector<std::weak_ptr<OrderedProgramData>> programs;
    std::vector<std::weak_ptr<OrderedImageData>> images;
    std::shared_ptr<Scene> latest, pending, bound;
    StencilRenderer lane;
    uint64_t lane_cpu_bytes;
    ag_ordered_caps caps{sizeof(ag_ordered_caps), 1};
    ag_ordered_state stats{sizeof(ag_ordered_state), 1};
    uint64_t requested{}, pending_sequence{}, issued{};
    uint64_t admission_sequence{};
    double admission_ms{};
    ComPtr<ID3D11Device> owner;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11InputLayout> layout;
    std::array<ComPtr<ID3D11Texture2D>, 2> planes;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> uavs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> srvs;
    ComPtr<ID3D11Query> query;
    uint32_t width{}, height{};
    explicit OrderedRenderer(Context *c) : context(c), lane(c), lane_cpu_bytes(lane.cpu_bytes()) {}
    static ag_ordered_caps observe(ID3D11Device *d)
    {
        ag_ordered_caps out{sizeof(out), 1};
        out.device_known = 1;
        out.feature_level = d->GetFeatureLevel();
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> base;
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 info{};
        if (SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&base)) &&
            SUCCEEDED(base.As(&adapter)) && SUCCEEDED(adapter->GetDesc1(&info)))
        {
            out.adapter_known = 1;
            out.luid_low = info.AdapterLuid.LowPart;
            out.luid_high = info.AdapterLuid.HighPart;
            out.vendor = info.VendorId;
            out.device = info.DeviceId;
            out.software = !!(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
            wcsncpy_s(out.adapter_name, info.Description, _TRUNCATE);
        }
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
        out.options_result = d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options, sizeof(options));
        out.options_known = SUCCEEDED(out.options_result);
        out.rov_supported = out.options_known && options.ROVsSupported;
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 format{DXGI_FORMAT_R32_UINT, 0};
        out.format_result = d->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &format, sizeof(format));
        out.format_known = SUCCEEDED(out.format_result);
        out.r32_load = out.format_known && (format.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
        out.r32_store =
            out.format_known && (format.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE);
        out.available = out.feature_level >= D3D_FEATURE_LEVEL_11_1 && out.adapter_known &&
                        out.rov_supported && out.r32_load && out.r32_store;
        return out;
    }
    uint64_t texture_bytes(uint32_t w, uint32_t h) const
    {
        uint64_t bytes = uint64_t(w) * h * 20;
        for (auto &entry : images)
            if (auto image = entry.lock()) bytes += image->texture_bytes();
        return bytes;
    }
    uint64_t cpu_bytes(const Scene *extra = nullptr) const
    {
        uint64_t bytes = sizeof(*this) + programs.capacity() * sizeof(programs[0]) +
                         images.capacity() * sizeof(images[0]);
        std::set<const Scene *> scenes;
        for (auto p : std::array<const Scene *, 4>{latest.get(), pending.get(), bound.get(), extra})
            if (p)
                scenes.insert(p);
        for (auto p : scenes)
            bytes += p->bytes();
        for (auto &p : programs)
            if (auto v = p.lock())
                bytes += v->bytes();
        for (auto &p : images)
            if (auto v = p.lock()) bytes += v->bytes();
        // The GPU lane holds another shallow operation/resource list; geometry
        // lives in the scene above and is deliberately counted conservatively.
        bytes += lane_cpu_bytes;
        return bytes;
    }
    std::shared_ptr<OrderedProgramData> program(const ag_ordered_program_desc &d)
    {
        require(d.size == sizeof(d) && d.version == 1 && (d.contract == 1 || d.contract == 2) &&
                    d.trusted == 1 && d.material &&
                    d.resolve && d.material_bytes && d.resolve_bytes && d.material_bytes <= 1048576 &&
                    d.resolve_bytes <= 1048576 && d.constant_bytes <= 4096 && !(d.constant_bytes % 16) &&
                    (!d.constant_bytes || d.constants) && !d.reserved0 && !d.reserved[0] && !d.reserved[1],
                L"Invalid trusted ordered program");
        std::erase_if(programs, [](auto &p) { return p.expired(); });
        require(programs.size() < 16, L"Too many retained ordered programs", E_OUTOFMEMORY);
        auto p = std::make_shared<OrderedProgramData>();
        p->contract = d.contract;
        p->material.assign(static_cast<const uint8_t *>(d.material),
                           static_cast<const uint8_t *>(d.material) + d.material_bytes);
        p->resolve.assign(static_cast<const uint8_t *>(d.resolve),
                          static_cast<const uint8_t *>(d.resolve) + d.resolve_bytes);
        if (d.constant_bytes)
            p->constants.assign(static_cast<const uint8_t *>(d.constants),
                                static_cast<const uint8_t *>(d.constants) + d.constant_bytes);
        p->image_mask = OrderedProgramData::reflect(p->material, true, d.constant_bytes, d.contract);
        OrderedProgramData::reflect(p->resolve, false, d.constant_bytes);
        require(cpu_bytes() + p->bytes() + 16 * sizeof(programs[0]) <= StencilRenderer::cpu_limit,
                L"Ordered program CPU budget exceeded", E_OUTOFMEMORY);
        programs.reserve(16);
        programs.push_back(p);
        stats.cpu_bytes = cpu_bytes();
        return p;
    }
    std::shared_ptr<OrderedImageData> image(const ag_ordered_image_desc &desc, ID3D11Device *device,
                                           uint32_t w, uint32_t h, uint64_t available)
    {
        const auto packed = OrderedImageData::qualify(desc);
        std::erase_if(images, [](auto &entry) { return entry.expired(); });
        require(images.size() < 16, L"Too many retained ordered images", E_OUTOFMEMORY);
        require(texture_bytes(w, h) <= available && packed <= available - texture_bytes(w, h),
                L"Ordered image texture budget exceeded", E_OUTOFMEMORY);
        require(cpu_bytes() + sizeof(OrderedImageData) + packed + 16 * sizeof(images[0]) <=
                    StencilRenderer::cpu_limit, L"Ordered image CPU budget exceeded", E_OUTOFMEMORY);
        auto p = std::make_shared<OrderedImageData>();
        p->width = desc.width; p->height = desc.height;
        p->pixels.resize(size_t(packed / 4));
        require(cpu_bytes() + p->bytes() + 16 * sizeof(images[0]) <= StencilRenderer::cpu_limit,
                L"Ordered image allocated CPU budget exceeded", E_OUTOFMEMORY);
        for (uint32_t y = 0; y < desc.height; ++y)
            memcpy(p->pixels.data() + size_t(y) * desc.width,
                   static_cast<const uint8_t *>(desc.pixels) + size_t(y) * desc.stride, size_t(desc.width) * 4);
        if (owner.Get() != device)
        {
            reset();
            owner = device;
            caps = observe(device);
            require(caps.available, L"Ordered image device support unavailable", DXGI_ERROR_UNSUPPORTED);
        }
        p->prepare(device, stats.resource_creations);
        images.reserve(16); images.push_back(p); stats.cpu_bytes = cpu_bytes();
        return p;
    }
    static bool compatible(const StencilRenderer::Operation &a, const StencilRenderer::Operation &b)
    {
        if (a.kind != AG_STENCIL_REPLAY || b.kind != AG_STENCIL_REPLAY ||
            memcmp(&a.gpu, &b.gpu, sizeof(a.gpu)) || a.state.test != b.state.test)
            return false;
        if (a.state.test &&
            (a.state.mask != b.state.mask || a.state.bits != b.state.bits ||
             a.state.reference != b.state.reference || a.state.comparison != b.state.comparison))
            return false;
        // General clips were baked into coverage. Rectangles remain scissors.
        const bool ar = a.state.clip && a.state.clip->rectangle, br = b.state.clip && b.state.clip->rectangle;
        return ar == br &&
               (!ar || !memcmp(&a.state.clip->scissor, &b.state.clip->scissor, sizeof(D3D11_RECT)));
    }
    void submit(const ag_ordered_snapshot &s, std::shared_ptr<OrderedProgramData> p, uint32_t w, uint32_t h,
                float lw, float lh, uint64_t available,
                std::array<std::shared_ptr<OrderedImageData>, 2> input_images = {})
    {
        auto admission_start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 2; ++i)
            require(!(p->image_mask & (1u << i)) || input_images[i], L"Required ordered image slot is empty");
        require(s.size == sizeof(s) && s.version == 1 && s.sequence > requested && s.command_count <= 16383 &&
                    s.resource_count <= 4096 && (!s.command_count || s.commands) &&
                    (!s.resource_count || s.resources) && s.record_count && s.record_count <= 65536 &&
                    s.records && !(s.flags & ~AG_ORDERED_SEPARATE_PARTS) && !s.reserved[0] && !s.reserved[1],
                L"Invalid ordered snapshot/sequence");
        require(texture_bytes(w, h) <= available, L"Ordered texture budget exceeded", E_OUTOFMEMORY);
        for (uint32_t i = 0; i < s.resource_count; ++i)
            require(s.resources[i].kind == AG_REPLAY_PATH || s.resources[i].kind == AG_REPLAY_STROKE,
                    L"Ordered materials accept path/stroke resources only");
        std::vector<ag_stencil_command> commands;
        std::vector<uint32_t> indices;
        commands.reserve(s.command_count + 1);
        indices.reserve(s.command_count);
        ag_stencil_command aa{};
        aa.size = sizeof(aa);
        aa.version = 1;
        aa.kind = AG_STENCIL_REPLAY;
        aa.replay.size = sizeof(aa.replay);
        aa.replay.version = 1;
        aa.replay.kind = AG_REPLAY_AA;
        commands.push_back(aa);
        for (uint32_t i = 0; i < s.command_count; ++i)
        {
            const auto &c = s.commands[i];
            require(c.size == sizeof(c) && c.version == 1 && !c.reserved[0] && !c.reserved[1],
                    L"Invalid ordered command");
            if (c.kind == AG_ORDERED_STATE)
            {
                require(!c.context && !c.path && !c.record && c.state.kind >= AG_STENCIL_REPLAY &&
                            c.state.kind <= AG_STENCIL_DISABLE_TEST,
                        L"Invalid ordered state");
                if (c.state.kind == AG_STENCIL_REPLAY)
                {
                    auto k = c.state.replay.kind;
                    require(k == AG_REPLAY_CREATE || k == AG_REPLAY_DISPOSE || k == AG_REPLAY_TRANSFORM ||
                                k == AG_REPLAY_CLIP || k == AG_REPLAY_INTERSECT || k == AG_REPLAY_USE_STROKE,
                            L"Unsupported ordered replay state");
                }
                commands.push_back(c.state);
                if (c.state.kind == AG_STENCIL_CLEAR || c.state.kind == AG_STENCIL_WRITE_FILL ||
                    c.state.kind == AG_STENCIL_WRITE_STROKE)
                    indices.push_back(UINT32_MAX);
            }
            else
            {
                require((c.kind == AG_ORDERED_FILL || c.kind == AG_ORDERED_STROKE) &&
                            c.record < s.record_count && !c.state.size && !c.state.version && !c.state.kind &&
                            !c.state.context && !c.state.mask_index && !c.state.path && !c.state.clear_bits &&
                            !c.state.set_bits && !c.state.test_bits && !c.state.reference &&
                            !c.state.comparison && !c.state.reserved &&
                            StencilRenderer::empty(c.state.replay),
                        L"Invalid ordered material part");
                auto r = aa;
                r.replay.kind = c.kind == AG_ORDERED_FILL ? AG_REPLAY_FILL : AG_REPLAY_DRAW;
                r.replay.context = c.context;
                r.replay.resource = c.path;
                commands.push_back(r);
                indices.push_back(c.record);
            }
        }
        auto next = std::make_shared<Scene>();
        next->coverage = std::make_unique<StencilRenderer>(context);
        if (latest)
            next->coverage->inherit(*latest->coverage);
        next->program = std::move(p);
        next->images = std::move(input_images);
        ag_stencil_snapshot stencil{sizeof(stencil),
                                    1,
                                    uint32_t(commands.size()),
                                    s.resource_count,
                                    commands.data(),
                                    s.resources,
                                    2,
                                    {0, 0},
                                    AG_STENCIL_COMPLETE,
                                    s.flattening_tolerance,
                                    s.mesh_vertex_limit,
                                    {0, 0}};
        const auto held = cpu_bytes();
        require(held < StencilRenderer::cpu_limit, L"Ordered CPU budget exceeded", E_OUTOFMEMORY);
        next->coverage->submit(stencil, w, h, lw, lh, available - uint64_t(w) * h * 8, true,
                               StencilRenderer::cpu_limit - held);
        next->coverage_bytes = next->coverage->cpu_bytes();
        auto &ops = next->coverage->scene.operations;
        require(ops.size() == indices.size(), L"Ordered operation mapping failed", E_UNEXPECTED);
        uint64_t vertices = 0;
        for (auto &o : ops)
            if (o.kind == AG_STENCIL_REPLAY)
                vertices += o.mesh->vertices.size();
        require(vertices <= s.mesh_vertex_limit && vertices <= 1048576,
                L"Ordered packed vertex limit exceeded", E_OUTOFMEMORY);
        require(cpu_bytes(next.get()) + next->coverage_bytes + vertices * sizeof(Vertex) +
                        uint64_t(s.record_count) * 16 + ops.size() * sizeof(Run) <=
                    StencilRenderer::cpu_limit,
                L"Ordered CPU replacement budget exceeded", E_OUTOFMEMORY);
        next->vertices.reserve(size_t(vertices));
        next->runs.reserve(ops.size());
        next->records.assign(s.records, s.records + s.record_count);
        for (uint32_t i = 0; i < ops.size(); ++i)
        {
            auto &o = ops[i];
            const uint32_t first = uint32_t(next->vertices.size());
            if (o.kind == AG_STENCIL_REPLAY)
                for (auto v : o.mesh->vertices)
                    next->vertices.push_back({v.x, v.y, indices[i]});
            const auto count = uint32_t(next->vertices.size()) - first;
            if (!(s.flags & AG_ORDERED_SEPARATE_PARTS) && !next->runs.empty() &&
                compatible(ops[next->runs.back().operation], o))
                next->runs.back().count += count;
            else
                next->runs.push_back({i, first, count});
        }
        next->initial[0] = s.initial[0];
        next->initial[1] = s.initial[1];
        require(cpu_bytes(next.get()) + next->coverage_bytes <= StencilRenderer::cpu_limit,
                L"Ordered retained CPU budget exceeded", E_OUTOFMEMORY);
        latest = std::move(next);
        requested = s.sequence;
        stats.accepted = requested;
        stats.cpu_bytes = cpu_bytes();
        stats.vertex_bytes = latest->vertices.size() * sizeof(Vertex);
        stats.record_bytes = latest->records.size() * 16;
        admission_sequence = s.sequence;
        admission_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - admission_start)
                .count();
    }
    void reset()
    {
        // Called only on owner device retirement/reset. D3D retains queued object
        // references; a slow event alone never causes cancellation or recreation.
        if (pending)
            pending->reset();
        if (latest)
            latest->reset();
        if (bound)
            bound->reset();
        pending.reset();
        bound.reset();
        lane.clear();
        lane_cpu_bytes = lane.cpu_bytes();
        owner.Reset();
        vertex.Reset();
        layout.Reset();
        query.Reset();
        for (auto &v : planes)
            v.Reset();
        for (auto &v : uavs)
            v.Reset();
        for (auto &v : srvs)
            v.Reset();
        for (auto &v : programs)
            if (auto p = v.lock())
                p->reset();
        for (auto &v : images)
            if (auto p = v.lock()) p->reset();
        width = height = 0;
        issued = 0;
        stats.pending = stats.available = 0;
        ++stats.device_epoch;
    }
    void prepare(ID3D11Device *d, ID2D1DeviceContext *drawing, const std::shared_ptr<Scene> &s)
    {
        if (owner.Get() != d)
        {
            reset();
            owner = d;
            caps = observe(d);
            require(caps.available, L"Ordered device support unavailable", DXGI_ERROR_UNSUPPORTED);
        }
        const bool scene_changed = bound != s;
        if (scene_changed)
        {
            lane.scene = s->coverage->scene;
            lane_cpu_bytes = lane.cpu_bytes();
            ++lane.revision;
            bound = s;
        }
        auto before = lane.metrics.resource_creations;
        lane.prepare(d, drawing);
        if (scene_changed)
        {
            stats.cpu_bytes = cpu_bytes();
            require(stats.cpu_bytes <= StencilRenderer::cpu_limit, L"Ordered prepared CPU budget exceeded",
                    E_OUTOFMEMORY);
        }
        stats.resource_creations += lane.metrics.resource_creations - before;
        if (!vertex || !layout || !query)
        {
            // A failed Create* can leave earlier members populated. Admission
            // must recognize a complete set before treating it as reusable.
            vertex.Reset();
            layout.Reset();
            query.Reset();
            check(d->CreateVertexShader(ordered_vs, sizeof(ordered_vs), nullptr, &vertex),
                  L"Ordered vertex shader failed");
            D3D11_INPUT_ELEMENT_DESC e[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"MATERIAL", 0, DXGI_FORMAT_R32_UINT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            check(d->CreateInputLayout(e, 2, ordered_vs, sizeof(ordered_vs), &layout),
                  L"Ordered input layout failed");
            D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0};
            check(d->CreateQuery(&q, &query), L"Ordered completion query failed");
            stats.resource_creations += 3;
        }
        if (!planes[0] || !planes[1] || !uavs[0] || !uavs[1] || !srvs[0] || !srvs[1])
        {
            for (unsigned i = 0; i < 2; ++i)
            {
                uavs[i].Reset();
                srvs[i].Reset();
                planes[i].Reset();
                D3D11_TEXTURE2D_DESC t{};
                t.Width = lane.scene.w;
                t.Height = lane.scene.h;
                t.MipLevels = t.ArraySize = t.SampleDesc.Count = 1;
                t.Format = DXGI_FORMAT_R32_UINT;
                t.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
                check(d->CreateTexture2D(&t, nullptr, &planes[i]), L"Ordered integer plane failed");
                check(d->CreateUnorderedAccessView(planes[i].Get(), nullptr, &uavs[i]),
                      L"Ordered integer UAV failed");
                check(d->CreateShaderResourceView(planes[i].Get(), nullptr, &srvs[i]),
                      L"Ordered integer SRV failed");
                stats.resource_creations += 3;
            }
            width = lane.scene.w;
            height = lane.scene.h;
        }
        const bool program_created = s->program->device.Get() != d || !s->program->material_gpu ||
                                     !s->program->resolve_gpu ||
                                     (!s->program->constants.empty() && !s->program->constants_gpu);
        s->program->prepare(d);
        if (program_created)
            stats.resource_creations += 2 + !s->program->constants.empty();
        for (auto &image : s->images)
            if (image) image->prepare(d, stats.resource_creations);
        if (!s->records_gpu || !s->records_view)
        {
            s->records_view.Reset();
            s->records_gpu.Reset();
            D3D11_BUFFER_DESC b{};
            b.ByteWidth = UINT(s->records.size() * 16);
            b.Usage = D3D11_USAGE_IMMUTABLE;
            b.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            b.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            b.StructureByteStride = 16;
            D3D11_SUBRESOURCE_DATA data{s->records.data(), 0, 0};
            check(d->CreateBuffer(&b, &data, &s->records_gpu), L"Ordered material records failed");
            D3D11_SHADER_RESOURCE_VIEW_DESC v{};
            v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            v.Buffer.NumElements = UINT(s->records.size());
            check(d->CreateShaderResourceView(s->records_gpu.Get(), &v, &s->records_view),
                  L"Ordered record view failed");
            stats.resource_creations += 2;
        }
        if (!s->vertex_gpu && !s->vertices.empty())
        {
            D3D11_BUFFER_DESC b{};
            b.ByteWidth = UINT(s->vertices.size() * sizeof(Vertex));
            b.Usage = D3D11_USAGE_IMMUTABLE;
            b.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data{s->vertices.data(), 0, 0};
            check(d->CreateBuffer(&b, &data, &s->vertex_gpu), L"Ordered packed vertices failed");
            ++stats.resource_creations;
        }
    }
    bool ready_for_work(ID3D11Device *d, ID3D11DeviceContext *gpu)
    {
        if (!pending)
            return latest && issued != requested;
        BOOL complete = FALSE;
        auto hr = gpu->GetData(query.Get(), &complete, sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(hr))
        {
            stats.failure = hr;
            stats.device_lost = FAILED(d->GetDeviceRemovedReason());
            check(hr, L"Ordered completion failed");
        }
        if (hr == S_OK && complete)
        {
            check(d->GetDeviceRemovedReason(), L"Ordered device completion failed");
            return true;
        }
        return false;
    }
    bool progress(ID3D11Device *d, ID3D11DeviceContext *gpu, ID2D1DeviceContext *drawing,
                  ID3D11Texture2D *destination, ID3D11VertexShader *fullscreen, bool ready)
    {
        bool changed = false;
        if (pending && ready)
        {
            gpu->CopyResource(destination, lane.colour.Get());
            stats.completed = pending_sequence;
            stats.available = 1;
            stats.pending = 0;
            pending.reset();
            changed = true;
        }
        if (pending || !latest || issued == requested)
            return changed;
        // Admission/creation happen before mutating working coverage. The visible
        // source is touched only after its event query reports completion.
        prepare(d, drawing, latest);
        auto s = latest;
        drawing->SetTarget(nullptr);
        gpu->ClearState();
        for (unsigned i = 0; i < 2; ++i)
        {
            const UINT clear[4]{s->initial[i], s->initial[i], s->initial[i], s->initial[i]};
            gpu->ClearUnorderedAccessViewUint(uavs[i].Get(), clear);
            gpu->ClearDepthStencilView(lane.views[i].Get(), D3D11_CLEAR_STENCIL, 1, 0);
        }
        for (auto run : s->runs)
        {
            auto &o = lane.scene.operations[run.operation];
            gpu->ClearState();
            if (o.kind == AG_STENCIL_CLEAR)
            {
                gpu->ClearDepthStencilView(lane.views[o.mask].Get(), D3D11_CLEAR_STENCIL, 1,
                                           uint8_t(o.reference));
                continue;
            }
            auto &st = o.state;
            bool paint = o.kind == AG_STENCIL_REPLAY;
            if ((paint && !run.count) || (!paint && o.mesh->vertices.empty()))
                continue;
            D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
            gpu->RSSetViewports(1, &viewport);
            gpu->RSSetState(lane.raster.Get());
            D3D11_RECT clip{0, 0, LONG(width), LONG(height)};
            if (st.clip && st.clip->rectangle)
            {
                auto r = st.clip->scissor;
                clip = {std::clamp(r.left, 0L, LONG(width)), std::clamp(r.top, 0L, LONG(height)),
                        std::clamp(r.right, 0L, LONG(width)), std::clamp(r.bottom, 0L, LONG(height))};
            }
            gpu->RSSetScissorRects(1, &clip);
            gpu->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            auto matrix = o.gpu;
            const float constants[12]{matrix._11, matrix._12, matrix._21,  matrix._22,
                                      matrix._31, matrix._32, 2.f / width, 2.f / height,
                                      0,          0,          0,           1};
            gpu->UpdateSubresource(lane.constants.Get(), 0, nullptr, constants, 0, 0);
            auto cb = lane.constants.Get();
            gpu->VSSetConstantBuffers(0, 1, &cb);
            UINT offset = 0;
            if (paint)
            {
                ID3D11UnorderedAccessView *u[] = {uavs[0].Get(), uavs[1].Get()};
                gpu->OMSetRenderTargetsAndUnorderedAccessViews(
                    0, nullptr, st.test ? lane.views[st.mask].Get() : nullptr, 0, 2, u, nullptr);
                gpu->OMSetDepthStencilState(
                    st.test ? lane.states[lane.state_index(o)].Get() : lane.unmasked.Get(), st.reference);
                gpu->IASetInputLayout(layout.Get());
                UINT stride = sizeof(Vertex);
                auto vb = s->vertex_gpu.Get();
                gpu->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                gpu->VSSetShader(vertex.Get(), nullptr, 0);
                gpu->PSSetShader(s->program->material_gpu.Get(), nullptr, 0);
                auto view = s->records_view.Get();
                gpu->PSSetShaderResources(0, 1, &view);
                if (s->program->contract == 2)
                {
                    ID3D11ShaderResourceView *image_views[] = {
                        s->images[0] ? s->images[0]->view.Get() : nullptr,
                        s->images[1] ? s->images[1]->view.Get() : nullptr};
                    gpu->PSSetShaderResources(1, 2, image_views);
                }
                cb = s->program->constants_gpu.Get();
                gpu->PSSetConstantBuffers(1, 1, &cb);
                gpu->Draw(run.count, run.first);
                ++stats.material_draws;
            }
            else
            {
                gpu->OMSetRenderTargets(0, nullptr, lane.views[o.mask].Get());
                gpu->OMSetDepthStencilState(lane.states[lane.state_index(o)].Get(), o.reference);
                gpu->IASetInputLayout(lane.layout.Get());
                UINT stride = sizeof(D2D1_POINT_2F);
                auto vb = o.mesh->buffer.Get();
                gpu->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                gpu->VSSetShader(lane.vertex.Get(), nullptr, 0);
                gpu->Draw(UINT(o.mesh->vertices.size()), 0);
                ++stats.mask_draws;
            }
        }
        // UAVs are explicitly unbound before SRV resolve, including the no-parts
        // case. One final colour resolve covers the complete source surface.
        gpu->ClearState();
        auto target = lane.colour_view.Get();
        gpu->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        gpu->RSSetViewports(1, &viewport);
        gpu->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gpu->VSSetShader(fullscreen, nullptr, 0);
        gpu->PSSetShader(s->program->resolve_gpu.Get(), nullptr, 0);
        ID3D11ShaderResourceView *views[] = {srvs[0].Get(), srvs[1].Get()};
        gpu->PSSetShaderResources(0, 2, views);
        auto cb = s->program->constants_gpu.Get();
        gpu->PSSetConstantBuffers(1, 1, &cb);
        gpu->Draw(3, 0);
        ++stats.resolves;
        gpu->ClearState();
        gpu->End(query.Get());
        // Display closes its enclosing timing interval and submits once, or
        // presents. Keeping the flush there includes this work in GPU timing.
        pending = s;
        pending_sequence = requested;
        issued = requested;
        stats.submitted = requested;
        stats.pending = 1;
        ++stats.submissions;
        stats.failure = 0;
        stats.device_lost = 0;
        return changed;
    }
};
} // namespace ag
