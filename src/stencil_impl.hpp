#pragma once
#include "ass_graphics/stencil.h"
#include "stencil_vs.h"
#include "stencil_ps.h"
#include <chrono>
#include <d3d11.h>
#include <set>
#include <unordered_set>
namespace ag
{
// The accepted recipe is CPU-owned. GPU work uses a separate colour target;
// only a completed synchronous submission is copied into Display's source.
// A later asynchronous device failure is handled by normal device recreation.
struct StencilRenderer
{
    static constexpr uint64_t cpu_limit = 64ull * 1024 * 1024, cache_limit = 16ull * 1024 * 1024;
    struct Mesh
    {
        std::vector<uint8_t> key;
        std::vector<D2D1_POINT_2F> vertices;
        ComPtr<ID3D11Buffer> buffer;
        uint64_t bytes() const
        {
            return sizeof(*this) + key.capacity() + vertices.capacity() * sizeof(D2D1_POINT_2F);
        }
    };
    struct Clip
    {
        ComPtr<ID2D1Geometry> geometry; // Frozen in logical root coordinates.
        std::vector<uint8_t> key;
        bool rectangle{};
        D3D11_RECT scissor{};
    };
    struct State
    {
        D2D1_MATRIX_3X2_F matrix = D2D1::Matrix3x2F::Identity();
        std::shared_ptr<Clip> clip;
        std::shared_ptr<ReplayResource> stroke;
        std::shared_ptr<ReplayResource> tile;
        uint32_t argb{0xffffffff}, aa{1}, sampling{1};
        float opacity{1};
        bool test{};
        uint32_t mask{}, bits{}, reference{}, comparison{};
    };
    struct Operation
    {
        uint32_t kind{}, mask{}, bits{}, reference{}, comparison{};
        State state;
        std::shared_ptr<ReplayResource> path;
        std::shared_ptr<Mesh> mesh;
        D2D1_MATRIX_3X2_F gpu = D2D1::Matrix3x2F::Identity();
    };
    struct Scene
    {
        std::vector<Operation> operations;
        std::vector<uint8_t> key;
        std::vector<std::shared_ptr<ReplayResource>> resources;
        uint32_t masks{}, initial[2]{}, w{}, h{};
        float tolerance{};
        uint64_t vertices{}, images{};
        D2D1_MATRIX_3X2_F root = D2D1::Matrix3x2F::Identity();
    } scene;
    Context *context;
    ReplayRenderer resources;
    std::vector<std::shared_ptr<Mesh>> cache;
    uint64_t cache_bytes{};
    std::unordered_multimap<uint64_t, std::weak_ptr<Mesh>> mesh_lookup;
    std::vector<uint8_t> mesh_key;
    ag_stencil_metrics metrics{sizeof(ag_stencil_metrics), 1};
    ComPtr<ID3D11Device> owner;
    ComPtr<ID3D11Texture2D> colour;
    ComPtr<ID3D11RenderTargetView> colour_view;
    ComPtr<ID2D1Bitmap1> bitmap;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::array<ComPtr<ID3D11Texture2D>, 2> masks;
    std::array<ComPtr<ID3D11DepthStencilView>, 2> views;
    std::array<ComPtr<ID3D11DepthStencilState>, 768> states;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> tile_constants;
    ComPtr<ID3D11DepthStencilState> unmasked;
    ComPtr<ID3D11BlendState> paint_blend, write_blend;
    ComPtr<ID3D11RasterizerState> raster;
    uint32_t gpu_w{}, gpu_h{}, gpu_masks{};
    uint64_t revision{1}, gpu_revision{};
    explicit StencilRenderer(Context *c) : context(c), resources(c) {}
    void inherit(const StencilRenderer &previous)
    {
        cache = previous.cache;
        cache_bytes = previous.cache_bytes;
        mesh_lookup = previous.mesh_lookup;
        resources.cache = previous.resources.cache;
        resources.lookup = previous.resources.lookup;
        resources.scene.resources = previous.resources.scene.resources;
    }
    template <class T> static void add(std::vector<uint8_t> &key, const T &v)
    {
        ReplayRenderer::append(key, v);
    }
    static void add_matrix(std::vector<uint8_t> &key, const D2D1_MATRIX_3X2_F &m)
    {
        add(key, m._11);
        add(key, m._12);
        add(key, m._21);
        add(key, m._22);
        add(key, m._31);
        add(key, m._32);
    }
    static void replay_key(std::vector<uint8_t> &key, const ag_replay_command &r)
    {
        add(key, r.kind);
        add(key, r.context);
        add(key, r.resource);
        add(key, r.value);
        for (auto x : r.matrix)
            add(key, x);
        add(key, r.opacity);
        add(key, r.argb);
    }
    static bool empty(const ag_replay_command &r)
    {
        return !r.size && !r.version && !r.kind && !r.context && !r.resource && !r.value && !r.opacity &&
               !r.argb &&
               std::all_of(std::begin(r.matrix), std::end(r.matrix), [](double v) { return v == 0; });
    }
    static void unused_replay(const ag_replay_command &r)
    {
        const bool path = r.kind <= AG_REPLAY_INTERSECT || r.kind == AG_REPLAY_CLEAR ||
                          r.kind == AG_REPLAY_USE_STROKE || r.kind == AG_REPLAY_BITMAP;
        const bool value =
            r.kind == AG_REPLAY_CREATE || r.kind == AG_REPLAY_AA || r.kind == AG_REPLAY_SAMPLING;
        const bool matrix = r.kind == AG_REPLAY_TRANSFORM || r.kind == AG_REPLAY_BITMAP;
        require((path || !r.resource) && (value || !r.value) && (r.kind == AG_REPLAY_COLOUR || !r.argb) &&
                    (r.kind == AG_REPLAY_OPACITY || r.opacity == 0) &&
                    (matrix ||
                     std::all_of(std::begin(r.matrix), std::end(r.matrix), [](double x) { return x == 0; })),
                L"Stencil replay has nonzero unused members");
    }
    static uint64_t texture_bytes(const Scene &s)
    {
        return uint64_t(s.w) * s.h * 4 * (1 + s.masks) + s.images;
    }
    uint64_t texture_bytes() const
    {
        return scene.w ? texture_bytes(scene) : 0;
    }
    uint64_t cpu_bytes(const Scene *next = nullptr) const
    {
        uint64_t n = sizeof(*this) + cache.capacity() * sizeof(std::shared_ptr<Mesh>) + mesh_key.capacity() +
                     mesh_lookup.size() * 64 + mesh_lookup.bucket_count() * 16 +
                     resources.scene.commands.capacity() * sizeof(ag_replay_command) +
                     (resources.scene.resources.capacity() + resources.cache.capacity()) *
                         sizeof(std::shared_ptr<ReplayResource>) +
                     resources.lookup.size() * 64 + resources.lookup.bucket_count() * 16;
        const auto operations = scene.operations.size() + (next ? next->operations.size() : 0);
        std::vector<const Mesh *> meshes;
        std::vector<const ReplayResource *> objects;
        std::vector<const Clip *> clips;
        meshes.reserve(operations + cache.size());
        clips.reserve(operations);
        objects.reserve(scene.resources.size() + (next ? next->resources.size() : 0) +
                        resources.cache.size() + resources.scene.resources.size());
        auto include = [&](const Scene &s)
        {
            n += s.operations.capacity() * sizeof(Operation) + s.key.capacity() +
                 s.resources.capacity() * sizeof(std::shared_ptr<ReplayResource>);
            for (auto &r : s.resources)
                if (r)
                    objects.push_back(r.get());
            for (auto &o : s.operations)
            {
                if (o.mesh)
                    meshes.push_back(o.mesh.get());
                if (o.state.clip)
                    clips.push_back(o.state.clip.get());
            }
        };
        include(scene);
        if (next)
            include(*next);
        for (auto &m : cache)
            meshes.push_back(m.get());
        for (auto &r : resources.cache)
            objects.push_back(r.get());
        for (auto &r : resources.scene.resources)
            if (r)
                objects.push_back(r.get());
        auto unique_bytes = [](auto &values, auto payload)
        {
            std::sort(values.begin(), values.end(), std::less<>{});
            const auto end = std::unique(values.begin(), values.end());
            uint64_t bytes = 0;
            for (auto i = values.begin(); i != end; ++i)
                bytes += payload(*i);
            return bytes;
        };
        n += unique_bytes(meshes, [](auto m) { return m->bytes(); });
        n += unique_bytes(objects, [](auto r) { return r->bytes(); });
        n += unique_bytes(clips, [](auto c) { return sizeof(Clip) + c->key.capacity(); });
        return n;
    }
    // Scene payloads are immutable once retained. Track the two ownership sets
    // while admitting a candidate instead of rebuilding them after each part.
    struct CpuBudget
    {
        const StencilRenderer &renderer;
        const Scene &candidate;
        std::unordered_set<const Mesh *> referenced, cached;
        std::unordered_set<const Clip *> clips;
        uint64_t fixed;
        CpuBudget(const StencilRenderer &r, const Scene &s) : renderer(r), candidate(s)
        {
            fixed = r.cpu_bytes(&s) - containers();
            for (auto &o : r.scene.operations)
            {
                if (o.mesh)
                    referenced.insert(o.mesh.get());
                if (o.state.clip)
                    clips.insert(o.state.clip.get());
            }
            for (auto &m : r.cache)
                cached.insert(m.get());
        }
        uint64_t containers() const
        {
            return renderer.cache.capacity() * sizeof(std::shared_ptr<Mesh>) +
                   renderer.mesh_key.capacity() +
                   renderer.mesh_lookup.size() * 64 + renderer.mesh_lookup.bucket_count() * 16 +
                   candidate.operations.capacity() * sizeof(Operation) + candidate.key.capacity();
        }
        void cache_insert(const Mesh &m)
        {
            if (cached.insert(&m).second && !referenced.contains(&m))
                fixed += m.bytes();
        }
        void cache_erase(const Mesh &m)
        {
            if (cached.erase(&m) && !referenced.contains(&m))
                fixed -= m.bytes();
        }
        void include(const Operation &o)
        {
            if (o.mesh && referenced.insert(o.mesh.get()).second && !cached.contains(o.mesh.get()))
                fixed += o.mesh->bytes();
            if (o.state.clip && clips.insert(o.state.clip.get()).second)
                fixed += sizeof(Clip) + o.state.clip->key.capacity();
        }
        uint64_t bytes() const
        {
            return fixed + containers();
        }
    };
    struct Sink final : ID2D1TessellationSink
    {
        ULONG refs{1};
        HRESULT error{S_OK};
        uint32_t limit;
        std::vector<D2D1_POINT_2F> &vertices;
        Sink(uint32_t n, std::vector<D2D1_POINT_2F> &v) : limit(n), vertices(v) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
        {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (id != __uuidof(IUnknown) && id != __uuidof(ID2D1TessellationSink))
                return E_NOINTERFACE;
            *out = static_cast<ID2D1TessellationSink *>(this);
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++refs;
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            return --refs;
        } // Stack-owned for synchronous Tessellate.
        void STDMETHODCALLTYPE AddTriangles(const D2D1_TRIANGLE *p, UINT count) override
        {
            if (FAILED(error))
                return;
            if (uint64_t(count) * 3 > limit - vertices.size())
            {
                error = E_OUTOFMEMORY;
                return;
            }
            try
            {
                for (UINT i = 0; i < count; ++i)
                    for (auto point : {p[i].point1, p[i].point2, p[i].point3})
                    {
                        if (!std::isfinite(point.x) || !std::isfinite(point.y))
                        {
                            error = E_INVALIDARG;
                            return;
                        }
                        vertices.push_back(point);
                    }
            }
            catch (...)
            {
                error = E_OUTOFMEMORY;
            }
        }
        HRESULT STDMETHODCALLTYPE Close() override
        {
            return error;
        }
    };
    // An axis-aligned integer rectangle permits exact scissoring; a general clip
    // is intersected as geometry and is never replaced by its bounding box.
    static bool rectangle(const ReplayResource &r, D2D1_MATRIX_3X2_F m, D3D11_RECT &rect)
    {
        if (r.segments.size() != 5 || r.segments[0].kind != 0 || r.segments[4].kind != 4)
            return false;
        D2D1_POINT_2F p[4]{};
        for (unsigned i = 0; i < 4; ++i)
        {
            if (i && r.segments[i].kind != 1)
                return false;
            auto x = ReplayRenderer::f(r.segments[i].p[0]), y = ReplayRenderer::f(r.segments[i].p[1]);
            p[i] = {x * m._11 + y * m._21 + m._31, x * m._12 + y * m._22 + m._32};
            if (!finite(p[i].x) || !finite(p[i].y) || std::abs(p[i].x) > 10000000 ||
                std::abs(p[i].y) > 10000000 || p[i].x != std::floor(p[i].x) || p[i].y != std::floor(p[i].y))
                return false;
        }
        for (unsigned i = 0; i < 4; ++i)
        {
            auto a = p[i], b = p[(i + 1) % 4], c = p[(i + 2) % 4];
            if ((a.x == b.x) == (a.y == b.y) || (a.x == b.x) == (b.x == c.x))
                return false;
        }
        rect = {LONG(std::min({p[0].x, p[1].x, p[2].x, p[3].x})),
                LONG(std::min({p[0].y, p[1].y, p[2].y, p[3].y})),
                LONG(std::max({p[0].x, p[1].x, p[2].x, p[3].x})),
                LONG(std::max({p[0].y, p[1].y, p[2].y, p[3].y}))};
        return true;
    }
    std::shared_ptr<Clip> clip(const State &s, const std::shared_ptr<ReplayResource> &path, bool intersect,
                               const Scene &n)
    {
        if (!path)
            return nullptr;
        auto c = std::make_shared<Clip>();
        add(c->key, intersect);
        if (intersect && s.clip)
            c->key.insert(c->key.end(), s.clip->key.begin(), s.clip->key.end());
        add(c->key, path->key.size());
        c->key.insert(c->key.end(), path->key.begin(), path->key.end());
        add_matrix(c->key, s.matrix);
        ComPtr<ID2D1TransformedGeometry> tr;
        check(context->d2d->CreateTransformedGeometry(path->path.Get(), s.matrix, &tr),
              L"Stencil clip transform failed");
        c->geometry = tr;
        c->rectangle = rectangle(*path, s.matrix * n.root, c->scissor);
        if (intersect && s.clip)
        {
            ComPtr<ID2D1PathGeometry> result;
            ComPtr<ID2D1GeometrySink> sink;
            check(context->d2d->CreatePathGeometry(&result), L"Stencil clip allocation failed");
            check(result->Open(&sink), L"Stencil clip sink failed");
            check(s.clip->geometry->CombineWithGeometry(tr.Get(), D2D1_COMBINE_MODE_INTERSECT, nullptr,
                                                        n.tolerance / std::max(1.f, n.root._11), sink.Get()),
                  L"Stencil clip intersection failed");
            check(sink->Close(), L"Stencil clip completion failed");
            c->geometry = result;
            c->rectangle = c->rectangle && s.clip->rectangle;
            if (c->rectangle)
            {
                auto a = s.clip->scissor;
                c->scissor = {
                    std::max(a.left, c->scissor.left), std::max(a.top, c->scissor.top),
                    std::max(std::max(a.left, c->scissor.left), std::min(a.right, c->scissor.right)),
                    std::max(std::max(a.top, c->scissor.top), std::min(a.bottom, c->scissor.bottom))};
            }
        }
        return c;
    }
    std::shared_ptr<Mesh> mesh(Operation &o, Scene &n, uint32_t limit, CpuBudget &budget)
    {
        bool stroke = o.kind == AG_STENCIL_WRITE_STROKE ||
                      (o.kind == AG_STENCIL_REPLAY && o.comparison == AG_REPLAY_DRAW);
        require(!stroke || (o.state.stroke && o.state.stroke->width > 0),
                L"Stencil strokes require positive width");
        auto transform = o.state.matrix * n.root;
        const bool local = !o.state.clip || o.state.clip->rectangle;
        // Largest singular value converts the explicit raster tolerance into a
        // conservative local tolerance. Translation/rotation do not bake vertices.
        double a = transform._11, b = transform._12, c = transform._21, d = transform._22;
        double sum = a * a + b * b + c * c + d * d, det = a * d - b * c;
        double scale = std::sqrt((sum + std::sqrt(std::max(0., sum * sum - 4 * det * det))) / 2);
        float tolerance = local ? float(n.tolerance / std::max(scale, 1e-20)) : n.tolerance;
        require(finite(tolerance) && tolerance > 0, L"Stencil transformed tolerance is unsupported");
        auto &key = mesh_key;
        key.clear();
        key.reserve(sizeof(local) + sizeof(tolerance) + sizeof(size_t) + o.path->key.size() +
                    sizeof(stroke) + (stroke ? o.state.stroke->key.size() : 0) +
                    (local ? 0 : 2 * sizeof(D2D1_MATRIX_3X2_F) + o.state.clip->key.size()));
        add(key, local);
        add(key, tolerance);
        add(key, o.path->key.size());
        key.insert(key.end(), o.path->key.begin(), o.path->key.end());
        add(key, stroke);
        if (stroke)
            key.insert(key.end(), o.state.stroke->key.begin(), o.state.stroke->key.end());
        if (!local)
        {
            add_matrix(key, transform);
            add_matrix(key, n.root);
            key.insert(key.end(), o.state.clip->key.begin(), o.state.clip->key.end());
        }
        o.gpu = local ? transform : D2D1::Matrix3x2F::Identity();
        const auto keyhash = ReplayRenderer::hash(key);
        auto range = mesh_lookup.equal_range(keyhash);
        for (auto i = range.first; i != range.second;)
        {
            auto entry = i->second.lock();
            if (!entry)
            {
                i = mesh_lookup.erase(i);
                continue;
            }
            if (entry->key == key)
            {
                ++metrics.mesh_cache_hits;
                return entry;
            }
            ++i;
        }
        auto m = std::make_shared<Mesh>();
        m->key = key;
        ComPtr<ID2D1Geometry> geometry = o.path->path;
        if (stroke)
        {
            ComPtr<ID2D1PathGeometry> widened;
            ComPtr<ID2D1GeometrySink> sink;
            check(context->d2d->CreatePathGeometry(&widened), L"Stencil stroke geometry failed");
            check(widened->Open(&sink), L"Stencil stroke sink failed");
            check(geometry->Widen(o.state.stroke->width, o.state.stroke->stroke.Get(),
                                  local ? nullptr : &transform, tolerance, sink.Get()),
                  L"Stencil stroke widening failed");
            check(sink->Close(), L"Stencil stroke completion failed");
            geometry = widened;
        }
        else if (!local)
        {
            ComPtr<ID2D1TransformedGeometry> tr;
            check(context->d2d->CreateTransformedGeometry(geometry.Get(), transform, &tr),
                  L"Stencil path transform failed");
            geometry = tr;
        }
        if (!local)
        {
            ComPtr<ID2D1TransformedGeometry> tr;
            ComPtr<ID2D1PathGeometry> result;
            ComPtr<ID2D1GeometrySink> sink;
            check(context->d2d->CreateTransformedGeometry(o.state.clip->geometry.Get(), n.root, &tr),
                  L"Stencil raster clip failed");
            check(context->d2d->CreatePathGeometry(&result), L"Stencil intersection allocation failed");
            check(result->Open(&sink), L"Stencil intersection sink failed");
            check(geometry->CombineWithGeometry(tr.Get(), D2D1_COMBINE_MODE_INTERSECT, nullptr, tolerance,
                                                sink.Get()),
                  L"Stencil geometry intersection failed");
            check(sink->Close(), L"Stencil intersection completion failed");
            geometry = result;
        }
        Sink sink(limit, m->vertices);
        check(geometry->Tessellate(nullptr, tolerance, &sink), L"Stencil tessellation failed");
        check(sink.Close(), L"Stencil mesh vertex budget exceeded");
        ++metrics.mesh_builds;
        while (!cache.empty() && cache_bytes + m->bytes() > cache_limit)
        {
            cache_bytes -= cache.front()->bytes();
            budget.cache_erase(*cache.front());
            cache.erase(cache.begin());
        }
        if (m->bytes() <= cache_limit)
        {
            cache.push_back(m);
            cache_bytes += m->bytes();
            budget.cache_insert(*m);
            mesh_lookup.emplace(keyhash, m);
            if (mesh_lookup.size() > 65536)
            {
                mesh_lookup.clear();
                for (auto &entry : cache)
                    mesh_lookup.emplace(ReplayRenderer::hash(entry->key), entry);
            }
        }
        return m;
    }
    bool submit(const ag_stencil_snapshot &s, uint32_t w, uint32_t h, float lw, float lh, uint64_t available,
                bool gpu_paths = false, uint64_t cpu_allowance = cpu_limit)
    {
        auto begin = std::chrono::steady_clock::now();
        require(s.size == sizeof(s) && s.version == 1 && s.flags == AG_STENCIL_COMPLETE &&
                    s.mask_count <= 2 && s.command_count <= 16384 && s.resource_count <= 4096 &&
                    (!s.command_count || s.commands) && (!s.resource_count || s.resources) &&
                    !s.reserved[0] && !s.reserved[1] && finite(s.flattening_tolerance) &&
                    s.flattening_tolerance > 0 && s.mesh_vertex_limit > 0 && s.mesh_vertex_limit <= 1048576,
                L"Invalid stencil snapshot");
        for (unsigned i = 0; i < 2; ++i)
            require(i < s.mask_count ? s.initial_values[i] <= 255 : s.initial_values[i] == 0,
                    L"Invalid initial stencil byte");
        std::vector<ag_replay_command> ordinary;
        ordinary.reserve(s.command_count);
        for (uint32_t i = 0; i < s.command_count; ++i)
        {
            auto &c = s.commands[i];
            require(c.size == sizeof(c) && c.version == 1 && c.kind >= AG_STENCIL_REPLAY &&
                        c.kind <= AG_STENCIL_PAINT_TILE && !c.reserved,
                    L"Invalid stencil command");
            bool write = c.kind == AG_STENCIL_WRITE_FILL || c.kind == AG_STENCIL_WRITE_STROKE;
            bool test = c.kind == AG_STENCIL_TEST, clear = c.kind == AG_STENCIL_CLEAR;
            bool tile = c.kind == AG_STENCIL_PAINT_TILE;
            require((write || test || clear ? c.mask_index < s.mask_count : c.mask_index == 0) &&
                        (write ? c.path != 0 : c.path == 0) &&
                        (write ? c.clear_bits <= 255 && c.set_bits <= 255 : !c.clear_bits && !c.set_bits) &&
                        (test ? c.test_bits <= 255 : !c.test_bits) &&
                        (test || clear ? c.reference <= 255 : tile || !c.reference) &&
                        (test ? c.comparison == AG_STENCIL_EQUAL || c.comparison == AG_STENCIL_NOT_EQUAL
                              : !c.comparison) &&
                        (c.kind == AG_STENCIL_REPLAY || clear ? c.context == 0 : c.context < 17),
                    L"Invalid stencil operands/unused members");
            if (c.kind == AG_STENCIL_REPLAY)
            {
                unused_replay(c.replay);
                ordinary.push_back(c.replay);
            }
            else
                require(empty(c.replay), L"Stencil operation has nonzero nested replay command");
        }
        for (uint32_t i = 0; i < s.resource_count; ++i)
        {
            auto &r = s.resources[i];
            if (r.kind == AG_REPLAY_PATH)
            {
                require(r.segment_count <= 28000 && (!r.segment_count || r.segments),
                        L"Invalid stencil path storage");
                for (uint32_t j = 0; j < r.segment_count; ++j)
                    require(!r.segments[j].reserved, L"Nonzero stencil path reserved field");
            }
        }
        ag_replay_snapshot replay{sizeof(replay),  1,          uint32_t(ordinary.size()), s.resource_count,
                                  ordinary.data(), s.resources};
        resources.submit(replay, available, ReplayRenderer::Domain::Stencil);
        Scene n;
        n.w = w;
        n.h = h;
        n.masks = s.mask_count;
        n.initial[0] = s.initial_values[0];
        n.initial[1] = s.initial_values[1];
        n.tolerance = s.flattening_tolerance;
        float scale = std::min(float(w) / lw, float(h) / lh);
        n.root = D2D1::Matrix3x2F::Scale(scale, scale) *
                 D2D1::Matrix3x2F::Translation((w - lw * scale) / 2, (h - lh * scale) / 2);
        require(finite(scale) && scale > 0 && finite(n.root._31) && finite(n.root._32),
                L"Stencil source mapping exceeds finite range");
        n.resources = resources.scene.resources;
        n.images = resources.image_bytes();
        require(texture_bytes(n) <= available, L"Stencil candidate texture budget exceeded", E_OUTOFMEMORY);
        add(n.key, n.masks);
        add(n.key, n.initial[0]);
        add(n.key, n.initial[1]);
        add(n.key, n.tolerance);
        add(n.key, s.mesh_vertex_limit);
        add_matrix(n.key, n.root);
        add(n.key, w);
        add(n.key, h);
        for (auto &r : n.resources)
            if (r)
            {
                add(n.key, r->key.size());
                n.key.insert(n.key.end(), r->key.begin(), r->key.end());
            }
        std::array<State, 17> state{};
        std::array<bool, 17> alive{};
        alive[0] = true;
        std::set<const Mesh *> used;
        n.operations.reserve(s.command_count);
        CpuBudget budget(*this, n);
        for (uint32_t i = 0; i < s.command_count; ++i)
        {
            auto &c = s.commands[i];
            add(n.key, c.kind);
            add(n.key, c.context);
            add(n.key, c.mask_index);
            add(n.key, c.path);
            add(n.key, c.clear_bits);
            add(n.key, c.set_bits);
            add(n.key, c.test_bits);
            add(n.key, c.reference);
            add(n.key, c.comparison);
            replay_key(n.key, c.replay);
            require(alive[c.context], L"Stencil context is not alive");
            auto &st = state[c.context];
            Operation o;
            o.kind = c.kind;
            o.mask = c.mask_index;
            if (c.kind == AG_STENCIL_PAINT_TILE)
            {
                require(!c.reference || (c.reference < n.resources.size() &&
                                         n.resources[c.reference]->kind == AG_REPLAY_TILE),
                        L"Tile paint resource reference/type");
                st.tile = c.reference ? n.resources[c.reference] : nullptr;
                continue;
            }
            if (c.kind == AG_STENCIL_TEST)
            {
                st.test = true;
                st.mask = c.mask_index;
                st.bits = c.test_bits;
                st.reference = c.reference;
                st.comparison = c.comparison;
                continue;
            }
            if (c.kind == AG_STENCIL_DISABLE_TEST)
            {
                st.test = false;
                continue;
            }
            if (c.kind == AG_STENCIL_CLEAR)
            {
                o.reference = c.reference;
                n.operations.push_back(std::move(o));
                continue;
            }
            if (c.kind != AG_STENCIL_REPLAY)
            {
                require(c.path < n.resources.size() && n.resources[c.path]->kind == AG_REPLAY_PATH,
                        L"Stencil path reference/type");
                o.state = st;
                o.path = n.resources[c.path];
                o.bits = c.clear_bits | c.set_bits;
                o.reference = c.set_bits;
            }
            else
            {
                auto &r = c.replay;
                require(alive[r.context], L"Stencil replay context is not alive");
                auto &rs = state[r.context];
                auto path = r.resource ? n.resources[r.resource] : nullptr;
                switch (r.kind)
                {
                case AG_REPLAY_CREATE:
                    state[r.value] = rs;
                    alive[r.value] = true;
                    continue;
                case AG_REPLAY_DISPOSE:
                    state[r.context] = State{};
                    alive[r.context] = false;
                    continue;
                case AG_REPLAY_TRANSFORM:
                    rs.matrix = ReplayRenderer::matrix(r.matrix);
                    continue;
                case AG_REPLAY_COLOUR:
                    rs.argb = r.argb;
                    rs.tile.reset();
                    continue;
                case AG_REPLAY_USE_STROKE:
                    rs.stroke = path;
                    continue;
                case AG_REPLAY_OPACITY:
                    rs.opacity = r.opacity;
                    continue;
                case AG_REPLAY_AA:
                    rs.aa = r.value;
                    continue;
                case AG_REPLAY_SAMPLING:
                    rs.sampling = r.value;
                    continue;
                case AG_REPLAY_CLIP:
                case AG_REPLAY_INTERSECT:
                    rs.clip = clip(rs, path, r.kind == AG_REPLAY_INTERSECT, n);
                    continue;
                default:
                    break;
                }
                o.state = rs;
                o.path = path;
                o.comparison = r.kind;
                if (r.kind == AG_REPLAY_BITMAP)
                    o.gpu = ReplayRenderer::matrix(r.matrix) * rs.matrix * n.root;
                if (rs.test || (rs.tile && r.kind != AG_REPLAY_CLEAR))
                    require(r.kind != AG_REPLAY_BITMAP && rs.aa == 0 && rs.opacity == 1 &&
                                (rs.tile || r.kind == AG_REPLAY_CLEAR || (rs.argb >> 24) == 255),
                            L"Masked/tiled paint requires opaque aliased paths");
            }
            if (gpu_paths || o.kind != AG_STENCIL_REPLAY || o.state.test ||
                (o.state.tile && o.comparison != AG_REPLAY_CLEAR))
            {
                o.mesh = mesh(o, n, s.mesh_vertex_limit - uint32_t(n.vertices), budget);
                if (used.insert(o.mesh.get()).second)
                    n.vertices += o.mesh->vertices.size();
                require(n.vertices <= s.mesh_vertex_limit, L"Stencil scene vertex budget exceeded",
                        E_OUTOFMEMORY);
            }
            budget.include(o);
            n.operations.push_back(std::move(o));
            require(budget.bytes() <= cpu_allowance, L"Stencil CPU replacement budget exceeded",
                    E_OUTOFMEMORY);
        }
        require(cpu_bytes(&n) <= cpu_allowance, L"Stencil CPU replacement budget exceeded", E_OUTOFMEMORY);
        const bool changed = scene.key != n.key;
        if (changed)
        {
            require(revision != UINT64_MAX, L"Stencil revision exhausted", E_FAIL);
            scene = std::move(n);
            ++revision;
        }
        metrics.cpu_bytes = cpu_bytes();
        metrics.mesh_vertices = scene.vertices;
        metrics.admission_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        return changed;
    }
    void reset() noexcept
    {
        owner.Reset();
        colour.Reset();
        colour_view.Reset();
        bitmap.Reset();
        brush.Reset();
        for (auto &m : masks)
            m.Reset();
        for (auto &v : views)
            v.Reset();
        for (auto &s : states)
            s.Reset();
        vertex.Reset();
        pixel.Reset();
        layout.Reset();
        constants.Reset();
        tile_constants.Reset();
        unmasked.Reset();
        paint_blend.Reset();
        write_blend.Reset();
        raster.Reset();
        for (auto &m : cache)
            m->buffer.Reset();
        for (auto &o : scene.operations)
            if (o.mesh)
                o.mesh->buffer.Reset();
        resources.reset();
        for (auto &r : scene.resources)
            if (r)
                r->bitmap.Reset();
        gpu_w = gpu_h = gpu_masks = 0;
        gpu_revision = 0;
        metrics.texture_bytes = metrics.buffer_bytes = 0;
    }
    void clear()
    {
        reset();
        scene = {};
        cache.clear();
        cache_bytes = 0;
        mesh_lookup.clear();
        mesh_key.clear();
        resources.scene = {};
        metrics.cpu_bytes = cpu_bytes();
        metrics.mesh_vertices = 0;
    }
    uint32_t state_index(const Operation &o) const
    {
        if (o.kind != AG_STENCIL_REPLAY)
            return o.bits;
        return 256 + (o.state.comparison == AG_STENCIL_NOT_EQUAL ? 256 : 0) + o.state.bits;
    }
    void prepare(ID3D11Device *device, ID2D1DeviceContext *drawing)
    {
        if (owner.Get() == device && gpu_revision == revision && bitmap)
            return;
        if (owner.Get() != device)
            reset();
        owner = device;
        if (!vertex || !pixel || !layout || !constants || !tile_constants || !unmasked || !paint_blend ||
            !write_blend || !raster)
        {
            vertex.Reset();
            pixel.Reset();
            layout.Reset();
            constants.Reset();
            tile_constants.Reset();
            unmasked.Reset();
            paint_blend.Reset();
            write_blend.Reset();
            raster.Reset();
            check(device->CreateVertexShader(stencil_vs, sizeof(stencil_vs), nullptr, &vertex),
                  L"Stencil vertex shader failed");
            check(device->CreatePixelShader(stencil_ps, sizeof(stencil_ps), nullptr, &pixel),
                  L"Stencil pixel shader failed");
            D3D11_INPUT_ELEMENT_DESC element{
                "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
            check(device->CreateInputLayout(&element, 1, stencil_vs, sizeof(stencil_vs), &layout),
                  L"Stencil vertex layout failed");
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = 48;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            check(device->CreateBuffer(&buffer, nullptr, &constants), L"Stencil constant buffer failed");
            buffer.ByteWidth = 64;
            uint32_t initial_tile[16];
            std::fill(std::begin(initial_tile), std::end(initial_tile), 0xff000000);
            D3D11_SUBRESOURCE_DATA tile_data{initial_tile, 0, 0};
            check(device->CreateBuffer(&buffer, &tile_data, &tile_constants), L"Tile constant buffer failed");
            D3D11_DEPTH_STENCIL_DESC disabled{};
            disabled.DepthFunc = D3D11_COMPARISON_ALWAYS;
            disabled.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                                  D3D11_COMPARISON_ALWAYS};
            disabled.BackFace = disabled.FrontFace;
            check(device->CreateDepthStencilState(&disabled, &unmasked), L"Unmasked tile state failed");
            D3D11_BLEND_DESC blend{};
            auto &b = blend.RenderTarget[0];
            b.BlendEnable = TRUE;
            b.SrcBlend = b.SrcBlendAlpha = D3D11_BLEND_ONE;
            b.DestBlend = b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            check(device->CreateBlendState(&blend, &paint_blend), L"Stencil paint blend failed");
            b.RenderTargetWriteMask = 0;
            check(device->CreateBlendState(&blend, &write_blend), L"Stencil no-colour blend failed");
            D3D11_RASTERIZER_DESC r{};
            r.FillMode = D3D11_FILL_SOLID;
            r.CullMode = D3D11_CULL_NONE;
            r.DepthClipEnable = TRUE;
            r.ScissorEnable = TRUE;
            check(device->CreateRasterizerState(&r, &raster), L"Stencil raster state failed");
            metrics.resource_creations += 9;
        }
        if (gpu_w != scene.w || gpu_h != scene.h || gpu_masks != scene.masks || !bitmap)
        {
            // These are working resources; Display's previous source remains
            // untouched on allocation/draw failure. Drop old working overlap first.
            bitmap.Reset();
            brush.Reset();
            colour_view.Reset();
            colour.Reset();
            for (auto &v : views)
                v.Reset();
            for (auto &m : masks)
                m.Reset();
            gpu_w = gpu_h = 0;
            metrics.texture_bytes = 0;
            UINT support{};
            check(device->CheckFormatSupport(DXGI_FORMAT_D24_UNORM_S8_UINT, &support),
                  L"Stencil format query failed");
            require(!scene.masks || (support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL),
                    L"Device does not support D24/S8 masks", DXGI_ERROR_UNSUPPORTED);
            D3D11_TEXTURE2D_DESC d{};
            d.Width = scene.w;
            d.Height = scene.h;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            check(device->CreateTexture2D(&d, nullptr, &colour), L"Stencil candidate colour failed");
            check(device->CreateRenderTargetView(colour.Get(), nullptr, &colour_view),
                  L"Stencil candidate target failed");
            ComPtr<IDXGISurface> surface;
            check(colour.As(&surface), L"Stencil DXGI surface failed");
            auto props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            check(drawing->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap),
                  L"Stencil D2D candidate failed");
            check(drawing->CreateSolidColorBrush(D2D1::ColorF(0), &brush), L"Stencil brush failed");
            d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            for (uint32_t i = 0; i < scene.masks; ++i)
            {
                check(device->CreateTexture2D(&d, nullptr, &masks[i]), L"Stencil plane allocation failed");
                check(device->CreateDepthStencilView(masks[i].Get(), nullptr, &views[i]),
                      L"Stencil plane view failed");
            }
            gpu_w = scene.w;
            gpu_h = scene.h;
            gpu_masks = scene.masks;
            metrics.texture_bytes = uint64_t(gpu_w) * gpu_h * 4 * (1 + gpu_masks);
            metrics.resource_creations += 4 + 2 * scene.masks;
        }
        for (auto &o : scene.operations)
            if (o.mesh)
            {
                auto &m = *o.mesh;
                if (!m.vertices.empty() && !m.buffer)
                {
                    D3D11_BUFFER_DESC b{};
                    b.ByteWidth = UINT(m.vertices.size() * sizeof(D2D1_POINT_2F));
                    b.Usage = D3D11_USAGE_IMMUTABLE;
                    b.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                    D3D11_SUBRESOURCE_DATA data{m.vertices.data(), 0, 0};
                    check(device->CreateBuffer(&b, &data, &m.buffer), L"Stencil mesh buffer failed");
                    ++metrics.resource_creations;
                }
                if (o.kind == AG_STENCIL_REPLAY && !o.state.test)
                    continue;
                auto index = state_index(o);
                if (!states[index])
                {
                    D3D11_DEPTH_STENCIL_DESC d{};
                    d.DepthFunc = D3D11_COMPARISON_ALWAYS;
                    d.StencilEnable = TRUE;
                    d.StencilReadMask = uint8_t(o.kind == AG_STENCIL_REPLAY ? o.state.bits : 255);
                    d.StencilWriteMask = uint8_t(o.kind == AG_STENCIL_REPLAY ? 0 : o.bits);
                    d.FrontFace.StencilFunc = o.kind != AG_STENCIL_REPLAY ? D3D11_COMPARISON_ALWAYS
                                              : o.state.comparison == AG_STENCIL_EQUAL
                                                  ? D3D11_COMPARISON_EQUAL
                                                  : D3D11_COMPARISON_NOT_EQUAL;
                    d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                    d.FrontFace.StencilPassOp =
                        o.kind == AG_STENCIL_REPLAY ? D3D11_STENCIL_OP_KEEP : D3D11_STENCIL_OP_REPLACE;
                    d.BackFace = d.FrontFace;
                    check(device->CreateDepthStencilState(&d, &states[index]), L"Stencil byte state failed");
                    ++metrics.resource_creations;
                }
            }
        std::set<const Mesh *> unique;
        metrics.buffer_bytes = 112;
        for (auto &m : cache)
            unique.insert(m.get());
        for (auto &o : scene.operations)
            if (o.mesh)
                unique.insert(o.mesh.get());
        for (auto m : unique)
            if (m->buffer)
                metrics.buffer_bytes += m->vertices.size() * sizeof(D2D1_POINT_2F);
        gpu_revision = revision;
    }
    struct CompletionCheck
    {
        HRESULT operator()(ID3D11Device *device) const noexcept
        {
            return device->GetDeviceRemovedReason();
        }
    };
    template <class Check = CompletionCheck>
    void draw(ID3D11Device *device, ID3D11DeviceContext *gpu, ID2D1DeviceContext *drawing,
              ID3D11Texture2D *destination, Check completion = {})
    {
        prepare(device, drawing);
        bool d2d = false;
        auto end = [&]
        {
            if (d2d)
            {
                d2d = false;
                auto hr = drawing->EndDraw();
                drawing->SetTarget(nullptr);
                check(hr, L"Stencil D2D batch failed");
            }
            gpu->ClearState();
        };
        try
        {
            drawing->SetTarget(nullptr);
            gpu->ClearState();
            const float black[4]{0, 0, 0, 1};
            gpu->ClearRenderTargetView(colour_view.Get(), black);
            for (uint32_t i = 0; i < scene.masks; ++i)
            {
                gpu->ClearDepthStencilView(views[i].Get(), D3D11_CLEAR_STENCIL, 1, uint8_t(scene.initial[i]));
                ++metrics.mask_clears;
            }
            for (auto &o : scene.operations)
            {
                if (o.kind == AG_STENCIL_CLEAR)
                {
                    end();
                    gpu->ClearDepthStencilView(views[o.mask].Get(), D3D11_CLEAR_STENCIL, 1,
                                               uint8_t(o.reference));
                    ++metrics.mask_clears;
                    continue;
                }
                auto &s = o.state;
                if (o.mesh)
                {
                    end();
                    if (o.mesh->vertices.empty())
                        continue;
                    auto target = colour_view.Get();
                    auto view = o.kind == AG_STENCIL_REPLAY ? (s.test ? views[s.mask].Get() : nullptr)
                                                            : views[o.mask].Get();
                    gpu->OMSetRenderTargets(1, &target, view);
                    gpu->OMSetDepthStencilState(o.kind == AG_STENCIL_REPLAY && !s.test
                                                    ? unmasked.Get()
                                                    : states[state_index(o)].Get(),
                                                o.kind == AG_STENCIL_REPLAY ? s.reference : o.reference);
                    gpu->OMSetBlendState(o.kind == AG_STENCIL_REPLAY ? paint_blend.Get() : write_blend.Get(),
                                         nullptr, 0xffffffff);
                    D3D11_VIEWPORT v{0, 0, float(scene.w), float(scene.h), 0, 1};
                    gpu->RSSetViewports(1, &v);
                    gpu->RSSetState(raster.Get());
                    D3D11_RECT scissor{0, 0, LONG(scene.w), LONG(scene.h)};
                    if (s.clip && s.clip->rectangle)
                    {
                        auto r = s.clip->scissor;
                        scissor = {
                            std::clamp(r.left, 0L, LONG(scene.w)), std::clamp(r.top, 0L, LONG(scene.h)),
                            std::clamp(r.right, 0L, LONG(scene.w)), std::clamp(r.bottom, 0L, LONG(scene.h))};
                    }
                    gpu->RSSetScissorRects(1, &scissor);
                    gpu->IASetInputLayout(layout.Get());
                    gpu->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    auto buffer = o.mesh->buffer.Get();
                    UINT stride = sizeof(D2D1_POINT_2F), offset = 0;
                    gpu->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
                    gpu->VSSetShader(vertex.Get(), nullptr, 0);
                    gpu->PSSetShader(pixel.Get(), nullptr, 0);
                    auto m = o.gpu;
                    float data[12]{m._11,         m._12,         m._21, m._22, m._31, m._32,
                                   2.f / scene.w, 2.f / scene.h, 0,     0,     0,     1};
                    uint32_t argb =
                        o.comparison == AG_REPLAY_CLEAR && o.kind == AG_STENCIL_REPLAY ? 0xff000000 : s.argb;
                    data[8] = float((argb >> 16) & 255) / 255;
                    data[9] = float((argb >> 8) & 255) / 255;
                    data[10] = float(argb & 255) / 255;
                    if (o.kind == AG_STENCIL_REPLAY && s.tile && o.comparison != AG_REPLAY_CLEAR)
                    {
                        // This copies sixteen retained colours, not a CPU raster.
                        // SV_Position chooses source phase independently of coverage.
                        data[11] = 0;
                        gpu->UpdateSubresource(tile_constants.Get(), 0, nullptr, s.tile->pixels.data(), 0, 0);
                    }
                    gpu->UpdateSubresource(constants.Get(), 0, nullptr, data, 0, 0);
                    auto cb = constants.Get();
                    gpu->VSSetConstantBuffers(0, 1, &cb);
                    gpu->PSSetConstantBuffers(0, 1, &cb);
                    cb = tile_constants.Get();
                    gpu->PSSetConstantBuffers(1, 1, &cb);
                    gpu->Draw(UINT(o.mesh->vertices.size()), 0);
                    ++metrics.gpu_draws;
                }
                else
                {
                    if (!d2d)
                    {
                        gpu->ClearState();
                        drawing->SetTarget(bitmap.Get());
                        drawing->SetDpi(96, 96);
                        drawing->BeginDraw();
                        d2d = true;
                    }
                    drawing->SetTransform(scene.root);
                    if (s.clip)
                        drawing->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(),
                                                                  s.clip->geometry.Get(),
                                                                  D2D1_ANTIALIAS_MODE_ALIASED),
                                           nullptr);
                    drawing->SetTransform(s.matrix * scene.root);
                    drawing->SetAntialiasMode(s.aa ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
                                                   : D2D1_ANTIALIAS_MODE_ALIASED);
                    float a = float((s.argb >> 24) & 255) / 255 * s.opacity;
                    brush->SetColor(D2D1::ColorF(float((s.argb >> 16) & 255) / 255,
                                                 float((s.argb >> 8) & 255) / 255, float(s.argb & 255) / 255,
                                                 a));
                    if (o.comparison == AG_REPLAY_DRAW)
                        drawing->DrawGeometry(o.path->path.Get(), brush.Get(),
                                              s.stroke->width ? s.stroke->width : 1, s.stroke->stroke.Get());
                    else if (o.comparison == AG_REPLAY_BITMAP)
                    {
                        auto &r = *o.path;
                        if (!r.bitmap)
                        {
                            auto props = D2D1::BitmapProperties1(
                                D2D1_BITMAP_OPTIONS_NONE,
                                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                96, 96);
                            // Upload before pushing a clip would simplify failure
                            // cleanup; here an upload failure pops the local layer.
                            auto hr = drawing->CreateBitmap(D2D1::SizeU(r.w, r.h), r.pixels.data(), r.w * 4,
                                                            &props, &r.bitmap);
                            if (FAILED(hr))
                            {
                                if (s.clip)
                                    drawing->PopLayer();
                                check(hr, L"Stencil replay image upload failed");
                            }
                        }
                        drawing->SetTransform(o.gpu);
                        drawing->DrawBitmap(r.bitmap.Get(), D2D1::RectF(0, 0, float(r.w), float(r.h)),
                                            s.opacity,
                                            s.sampling ? D2D1_INTERPOLATION_MODE_LINEAR
                                                       : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                    }
                    else
                    {
                        if (o.comparison == AG_REPLAY_CLEAR)
                            brush->SetColor(D2D1::ColorF(0, 0, 0, 1));
                        drawing->FillGeometry(o.path->path.Get(), brush.Get());
                    }
                    if (s.clip)
                        drawing->PopLayer();
                }
            }
            end();
            check(completion(device), L"Stencil source device failed");
            gpu->CopyResource(destination, colour.Get());
            ++metrics.source_draws;
        }
        catch (...)
        {
            if (d2d)
            {
                drawing->EndDraw();
                drawing->SetTarget(nullptr);
            }
            gpu->ClearState();
            throw;
        }
    }
};
} // namespace ag
