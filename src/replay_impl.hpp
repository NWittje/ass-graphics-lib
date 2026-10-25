#pragma once
#include "ass_graphics/replay.h"
#include <cstring>
#include <limits>
#include <unordered_map>
namespace ag
{
struct ReplayResource
{
    uint32_t kind{}, winding{}, cap{}, join{}, w{}, h{};
    float width{}, miter{}, phase{};
    std::vector<ag_replay_segment> segments;
    std::vector<float> dashes;
    std::vector<uint8_t> pixels, key;
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1StrokeStyle1> stroke;
    ComPtr<ID2D1Bitmap1> bitmap;
    size_t bytes() const
    {
        return sizeof(*this) + segments.capacity() * sizeof(ag_replay_segment) + dashes.capacity() * 4 +
               pixels.capacity() + key.capacity();
    }
};
struct ReplayScene
{
    std::vector<ag_replay_command> commands;
    std::vector<std::shared_ptr<ReplayResource>> resources;
};
struct ReplayRenderer
{
    Context *context;
    ReplayScene scene;
    std::vector<std::shared_ptr<ReplayResource>> cache;
    std::unordered_multimap<uint64_t, std::weak_ptr<ReplayResource>> lookup;
    ag_replay_metrics metrics{sizeof(ag_replay_metrics), 1};
    ID2D1DeviceContext *target{};
    explicit ReplayRenderer(Context *c) : context(c) {}
    template <class T> static void append(std::vector<uint8_t> &b, const T &v)
    {
        auto p = reinterpret_cast<const uint8_t *>(&v);
        b.insert(b.end(), p, p + sizeof v);
    }
    static float f(double v)
    {
        require(std::isfinite(v) && std::abs(v) <= 10000000, L"Replay coordinate exceeds supported range");
        return float(v);
    }
    static D2D1_MATRIX_3X2_F matrix(const double *v)
    {
        return {f(v[0]), f(v[1]), f(v[2]), f(v[3]), f(v[4]), f(v[5])};
    }
    static uint64_t hash(const std::vector<uint8_t> &key)
    {
        uint64_t h = 14695981039346656037ull;
        for (auto b : key)
        {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }
    std::shared_ptr<ReplayResource> resource(const ag_replay_resource &d)
    {
        require(d.size == sizeof d && d.version == 1 && d.kind >= 1 && d.kind <= 3,
                L"Invalid replay resource descriptor");
        auto r = std::make_shared<ReplayResource>();
        r->kind = d.kind;
        append(r->key, d.kind);
        if (d.kind == AG_REPLAY_PATH)
        {
            require(d.winding <= 1 && d.segment_count <= 28000 && (!d.segment_count || d.segments),
                    L"Invalid replay path");
            r->winding = d.winding;
            append(r->key, d.winding);
            append(r->key, d.segment_count);
            if (d.segment_count)
                r->segments.assign(d.segments, d.segments + d.segment_count);
            for (auto &s : r->segments)
            {
                require(s.kind <= 4, L"Invalid replay path segment");
                append(r->key, s.kind);
                unsigned n = s.kind == 4 ? 0 : s.kind == 3 ? 6 : s.kind == 2 ? 4 : 2;
                for (unsigned j = 0; j < n; j++)
                {
                    f(s.p[j]);
                    append(r->key, s.p[j]);
                }
            }
        }
        else if (d.kind == AG_REPLAY_STROKE)
        {
            require(finite(d.width) && d.width >= 0 && finite(d.miter) && d.miter >= 1 && finite(d.phase) &&
                        d.phase >= 0 && d.cap <= 2 && d.join <= 2 && d.dash_count <= 65536 &&
                        (!d.dash_count || d.dashes),
                    L"Invalid replay stroke");
            require(d.width > 0 || !d.dash_count, L"Dashed hairline is unsupported");
            r->width = d.width;
            r->miter = d.miter;
            r->phase = d.phase;
            r->cap = d.cap;
            r->join = d.join;
            append(r->key, d.width);
            append(r->key, d.miter);
            append(r->key, d.phase);
            append(r->key, d.cap);
            append(r->key, d.join);
            append(r->key, d.dash_count);
            double sum = 0;
            for (uint32_t i = 0; i < d.dash_count; i++)
            {
                auto x = d.dashes[i];
                require(finite(x) && x >= 0, L"Invalid replay dash");
                sum += x;
                r->dashes.push_back(x);
                append(r->key, x);
            }
            require(!d.dash_count || sum > 0, L"Empty replay dash pattern");
        }
        else
        {
            require(d.image_width && d.image_height && d.image_width <= 16384 && d.image_height <= 16384 &&
                        d.stride == uint64_t(d.image_width) * 4 &&
                        d.pixel_bytes == uint64_t(d.stride) * d.image_height &&
                        d.pixel_bytes <= 1536 * 1024 && d.pixels,
                    L"Invalid replay image");
            r->w = d.image_width;
            r->h = d.image_height;
            append(r->key, r->w);
            append(r->key, r->h);
            auto p = static_cast<const uint8_t *>(d.pixels);
            r->pixels.assign(p, p + d.pixel_bytes);
            r->key.insert(r->key.end(), p, p + d.pixel_bytes);
        }
        auto keyhash = hash(r->key);
        auto range = lookup.equal_range(keyhash);
        for (auto i = range.first; i != range.second;)
        {
            auto c = i->second.lock();
            if (!c)
            {
                i = lookup.erase(i);
                continue;
            }
            if (c->key == r->key)
            {
                ++metrics.cache_hits;
                return c;
            }
            ++i;
        }
        if (r->kind == AG_REPLAY_PATH)
        {
            check(context->d2d->CreatePathGeometry(&r->path), L"Replay path creation failed");
            ComPtr<ID2D1GeometrySink> s;
            check(r->path->Open(&s), L"Replay path sink failed");
            s->SetFillMode(r->winding ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);
            D2D1_POINT_2F origin{};
            bool open = false, closed = false;
            for (auto &v : r->segments)
            {
                auto point = [&](unsigned i) { return D2D1::Point2F(f(v.p[i]), f(v.p[i + 1])); };
                if (v.kind == 0)
                {
                    if (open)
                        s->EndFigure(D2D1_FIGURE_END_OPEN);
                    origin = point(0);
                    s->BeginFigure(origin, D2D1_FIGURE_BEGIN_FILLED);
                    open = true;
                    closed = false;
                }
                else
                {
                    if (!open && closed && v.kind != 4)
                    {
                        s->BeginFigure(origin, D2D1_FIGURE_BEGIN_FILLED);
                        open = true;
                    }
                    if (v.kind == 4 && !open && closed)
                        continue;
                    require(open, L"Replay segment without move");
                    if (v.kind == 1)
                        s->AddLine(point(0));
                    else if (v.kind == 2)
                        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(point(0), point(2)));
                    else if (v.kind == 3)
                        s->AddBezier(D2D1::BezierSegment(point(0), point(2), point(4)));
                    else
                    {
                        s->EndFigure(D2D1_FIGURE_END_CLOSED);
                        open = false;
                        closed = true;
                    }
                }
            }
            if (open)
                s->EndFigure(D2D1_FIGURE_END_OPEN);
            check(s->Close(), L"Replay path completion failed");
        }
        else if (r->kind == AG_REPLAY_STROKE)
        {
            auto cap = d.cap == 0   ? D2D1_CAP_STYLE_FLAT
                       : d.cap == 1 ? D2D1_CAP_STYLE_ROUND
                                    : D2D1_CAP_STYLE_SQUARE;
            auto join = d.join == 0   ? D2D1_LINE_JOIN_MITER
                        : d.join == 1 ? D2D1_LINE_JOIN_ROUND
                                      : D2D1_LINE_JOIN_BEVEL;
            std::vector<float> dash = r->dashes;
            if (dash.size() % 2)
            {
                auto copy = dash;
                dash.insert(dash.end(), copy.begin(), copy.end());
            }
            for (auto &v : dash)
                v /= r->width;
            auto props = D2D1::StrokeStyleProperties1(
                cap, cap, cap, join, r->miter, dash.empty() ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM,
                r->width ? r->phase / r->width : 0,
                r->width ? D2D1_STROKE_TRANSFORM_TYPE_NORMAL : D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE);
            check(context->d2d->CreateStrokeStyle(props, dash.data(), UINT(dash.size()), &r->stroke),
                  L"Replay stroke creation failed");
        }
        ++metrics.resource_creations;
        size_t retained = 0;
        for (auto &c : cache)
            retained += c->bytes();
        while (!cache.empty() && retained + r->bytes() > 3 * 1024 * 1024)
        {
            retained -= cache.front()->bytes();
            cache.erase(cache.begin());
        }
        if (r->bytes() <= 3 * 1024 * 1024)
        {
            cache.push_back(r);
            lookup.emplace(keyhash, r);
            if (lookup.size() > 65536)
            {
                lookup.clear();
                for (auto &v : cache)
                    lookup.emplace(hash(v->key), v);
            }
        }
        return r;
    }
    bool submit(const ag_replay_snapshot &d, uint64_t texture_available)
    {
        require(d.size == sizeof d && d.version == 1 && d.command_count <= 65536 &&
                    d.resource_count <= 65536 && (!d.command_count || d.commands) &&
                    (!d.resource_count || d.resources),
                L"Invalid replay snapshot");
        require(uint64_t(d.command_count) * sizeof(ag_replay_command) +
                        uint64_t(d.resource_count + 1) * sizeof(std::shared_ptr<ReplayResource>) <=
                    3 * 1024 * 1024,
                L"Replay CPU descriptor budget exceeded", E_OUTOFMEMORY);
        ReplayScene next;
        next.resources.resize(d.resource_count + 1);
        size_t bytes = size_t(d.command_count) * sizeof(ag_replay_command) +
                       size_t(d.resource_count + 1) * sizeof(std::shared_ptr<ReplayResource>);
        for (uint32_t i = 0; i < d.resource_count; i++)
        {
            auto &r = d.resources[i];
            require(r.id == i + 1, L"Replay resource order");
            next.resources[r.id] = resource(r);
            bytes += next.resources[r.id]->bytes();
            require(bytes <= 3 * 1024 * 1024, L"Replay snapshot CPU budget exceeded", E_OUTOFMEMORY);
        }
        std::array<bool, 17> contexts{}, strokes{};
        contexts[0] = true;
        for (uint32_t i = 0; i < d.command_count; i++)
        {
            auto c = d.commands[i];
            require(c.size == sizeof c && c.version == 1 && c.kind >= 1 && c.kind <= AG_REPLAY_CLEAR &&
                        c.context < 17 && contexts[c.context],
                    L"Invalid replay command/context");
            if (c.kind == AG_REPLAY_CREATE)
            {
                require(c.value > 0 && c.value < 17 && !contexts[c.value], L"Invalid replay child");
                contexts[c.value] = true;
                strokes[c.value] = strokes[c.context];
            }
            if (c.kind == AG_REPLAY_DISPOSE)
            {
                require(c.context != 0, L"Cannot dispose replay root");
                contexts[c.context] = false;
            }
            unsigned expected = 0;
            if (c.kind <= AG_REPLAY_INTERSECT || c.kind == AG_REPLAY_CLEAR)
                expected = 1;
            else if (c.kind == AG_REPLAY_USE_STROKE)
                expected = 2;
            else if (c.kind == AG_REPLAY_BITMAP)
                expected = 3;
            if (expected)
                require((c.kind == AG_REPLAY_CLIP && !c.resource) ||
                            (c.resource > 0 && c.resource < next.resources.size() &&
                             next.resources[c.resource]->kind == expected),
                        L"Replay resource type/reference");
            if (c.kind == AG_REPLAY_USE_STROKE)
                strokes[c.context] = true;
            if (c.kind == AG_REPLAY_DRAW)
                require(strokes[c.context], L"Replay drawing without stroke");
            if (c.kind == AG_REPLAY_TRANSFORM || c.kind == AG_REPLAY_BITMAP)
                matrix(c.matrix);
            if (c.kind == AG_REPLAY_OPACITY)
                require(finite(c.opacity) && c.opacity >= 0 && c.opacity <= 1, L"Invalid replay opacity");
            if (c.kind == AG_REPLAY_AA || c.kind == AG_REPLAY_SAMPLING)
                require(c.value <= 1, L"Unsupported replay sampling/hint");
            next.commands.push_back(c);
        }
        require(bytes <= 3 * 1024 * 1024, L"Replay snapshot CPU budget exceeded", E_OUTOFMEMORY);
        uint64_t imagebytes = 0;
        for (auto &r : next.resources)
            if (r && r->kind == 3)
                imagebytes += r->pixels.size();
        require(imagebytes <= texture_available, L"Replay texture budget exceeded", E_OUTOFMEMORY);
        bool changed = scene.resources != next.resources || scene.commands.size() != next.commands.size();
        if (!changed)
            for (size_t i = 0; i < next.commands.size(); i++)
            {
                auto &a = scene.commands[i];
                auto &b = next.commands[i];
                if (a.kind != b.kind || a.context != b.context || a.resource != b.resource ||
                    a.value != b.value || a.argb != b.argb || a.opacity != b.opacity ||
                    !std::equal(std::begin(a.matrix), std::end(a.matrix), std::begin(b.matrix)))
                {
                    changed = true;
                    break;
                }
            }
        scene = std::move(next);
        for (auto &r : cache)
            if (r->bitmap &&
                std::find(scene.resources.begin(), scene.resources.end(), r) == scene.resources.end())
                r->bitmap.Reset();
        metrics.cpu_bytes = bytes;
        metrics.commands = d.command_count;
        metrics.cache_bytes = 0;
        for (auto &c : cache)
            metrics.cache_bytes += c->bytes();
        return changed;
    }
    void reset()
    {
        target = nullptr;
        for (auto &c : cache)
            c->bitmap.Reset();
        for (auto &c : scene.resources)
            if (c)
                c->bitmap.Reset();
    }
    uint64_t image_bytes() const
    {
        uint64_t n = 0;
        for (auto &r : scene.resources)
            if (r && r->kind == 3)
                n += r->pixels.size();
        return n;
    }
    void draw(ID2D1DeviceContext *t, D2D1_MATRIX_3X2_F root)
    {
        if (target != t)
        {
            reset();
            target = t;
        }
        struct State
        {
            D2D1_MATRIX_3X2_F matrix = D2D1::Matrix3x2F::Identity();
            ComPtr<ID2D1Geometry> clip;
            std::shared_ptr<ReplayResource> stroke;
            uint32_t argb = 0xffffffff, aa = 1, sampling = 1;
            float opacity = 1;
        };
        std::array<State, 17> state{};
        ComPtr<ID2D1SolidColorBrush> brush;
        check(t->CreateSolidColorBrush(D2D1::ColorF(0), &brush), L"Replay brush failed");
        bool layer = false;
        ID2D1Geometry *activeClip = nullptr;
        auto pop = [&]
        {
            if (layer)
            {
                t->PopLayer();
                layer = false;
            }
            activeClip = nullptr;
        };
        try
        {
            for (auto &c : scene.commands)
            {
                auto &s = state[c.context];
                auto r = c.resource ? scene.resources[c.resource] : nullptr;
                switch (c.kind)
                {
                case AG_REPLAY_CREATE:
                    state[c.value] = s;
                    break;
                case AG_REPLAY_DISPOSE:
                    state[c.context] = State{};
                    break;
                case AG_REPLAY_TRANSFORM:
                    s.matrix = matrix(c.matrix);
                    break;
                case AG_REPLAY_COLOUR:
                    s.argb = c.argb;
                    break;
                case AG_REPLAY_USE_STROKE:
                    s.stroke = r;
                    break;
                case AG_REPLAY_OPACITY:
                    s.opacity = c.opacity;
                    break;
                case AG_REPLAY_AA:
                    s.aa = c.value;
                    break;
                case AG_REPLAY_SAMPLING:
                    s.sampling = c.value;
                    break;
                case AG_REPLAY_CLIP:
                case AG_REPLAY_INTERSECT:
                {
                    ComPtr<ID2D1Geometry> mask;
                    if (r)
                    {
                        ComPtr<ID2D1TransformedGeometry> tr;
                        check(context->d2d->CreateTransformedGeometry(r->path.Get(), s.matrix, &tr),
                              L"Replay clip transform failed");
                        mask = tr;
                    }
                    if (c.kind == AG_REPLAY_INTERSECT && s.clip)
                    {
                        ComPtr<ID2D1PathGeometry> combined;
                        ComPtr<ID2D1GeometrySink> sink;
                        check(context->d2d->CreatePathGeometry(&combined), L"Replay clip creation failed");
                        check(combined->Open(&sink), L"Replay clip sink failed");
                        check(s.clip->CombineWithGeometry(mask.Get(), D2D1_COMBINE_MODE_INTERSECT, nullptr,
                                                          sink.Get()),
                              L"Replay clip intersection failed");
                        check(sink->Close(), L"Replay clip completion failed");
                        mask = combined;
                    }
                    s.clip = mask;
                    break;
                }
                default:
                {
                    if (activeClip != s.clip.Get())
                    {
                        pop();
                        t->SetTransform(root);
                        if (s.clip)
                        {
                            t->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), s.clip.Get(),
                                                                D2D1_ANTIALIAS_MODE_ALIASED),
                                         nullptr);
                            layer = true;
                            activeClip = s.clip.Get();
                        }
                    }
                    t->SetTransform(s.matrix * root);
                    t->SetAntialiasMode(s.aa ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
                                             : D2D1_ANTIALIAS_MODE_ALIASED);
                    auto a = float((s.argb >> 24) & 255) / 255 * s.opacity;
                    brush->SetColor(D2D1::ColorF(float((s.argb >> 16) & 255) / 255,
                                                 float((s.argb >> 8) & 255) / 255, float(s.argb & 255) / 255,
                                                 a));
                    if (c.kind == AG_REPLAY_DRAW)
                    {
                        require(bool(s.stroke), L"Replay drawing without stroke");
                        t->DrawGeometry(r->path.Get(), brush.Get(), s.stroke->width ? s.stroke->width : 1,
                                        s.stroke->stroke.Get());
                    }
                    else if (c.kind == AG_REPLAY_FILL)
                        t->FillGeometry(r->path.Get(), brush.Get());
                    else if (c.kind == AG_REPLAY_CLEAR)
                    {
                        brush->SetColor(D2D1::ColorF(0, 0, 0, 1));
                        t->FillGeometry(r->path.Get(), brush.Get());
                    }
                    else if (c.kind == AG_REPLAY_BITMAP)
                    {
                        if (!r->bitmap)
                        {
                            auto props = D2D1::BitmapProperties1(
                                D2D1_BITMAP_OPTIONS_NONE,
                                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                96, 96);
                            check(t->CreateBitmap(D2D1::SizeU(r->w, r->h), r->pixels.data(), r->w * 4, &props,
                                                  &r->bitmap),
                                  L"Replay image upload failed");
                        }
                        t->SetTransform(matrix(c.matrix) * s.matrix * root);
                        t->DrawBitmap(r->bitmap.Get(), D2D1::RectF(0, 0, float(r->w), float(r->h)), s.opacity,
                                      s.sampling ? D2D1_INTERPOLATION_MODE_LINEAR
                                                 : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                    }
                    break;
                }
                }
            }
            pop();
            t->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        catch (...)
        {
            pop();
            throw;
        }
    }
};
} // namespace ag
