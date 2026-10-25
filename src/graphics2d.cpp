#include "internal.hpp"
#include "canvas_pointer.hpp"
#include <windowsx.h>
namespace ag
{
void save_pixels(Context *c, const wchar_t *file, UINT w, UINT h, UINT stride, const BYTE *bytes, float dpi)
{
    ComPtr<IWICBitmap> bitmap;
    check(c->wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, stride, stride * h,
                                         const_cast<BYTE *>(bytes), &bitmap),
          L"Readback bitmap failed");
    ComPtr<IWICStream> stream;
    check(c->wic->CreateStream(&stream), L"Readback stream failed");
    check(stream->InitializeFromFilename(file, GENERIC_WRITE), L"Readback file failed");
    ComPtr<IWICBitmapEncoder> encoder;
    check(c->wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), L"Readback encoder failed");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), L"Readback encoder init failed");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr), L"Readback frame failed");
    check(frame->Initialize(nullptr), L"Readback frame init failed");
    check(frame->SetResolution(dpi, dpi), L"Readback resolution failed");
    check(frame->WriteSource(bitmap.Get(), nullptr), L"Readback encode failed");
    check(frame->Commit(), L"Readback frame commit failed");
    check(encoder->Commit(), L"Readback commit failed");
}
void validate_snapshot(const ag_draw *cmds, uint32_t n, const ag_point *points, uint32_t pn, uint32_t path_count)
{
    require(n <= 65536 && pn <= 1048576 && (!n || cmds) && (!pn || points),
            L"Invalid or oversized drawing snapshot");
    int clips = 0;
    for (uint32_t i = 0; i < pn; ++i)
        require(finite(points[i].x) && finite(points[i].y), L"Nonfinite drawing point");
    for (uint32_t i = 0; i < n; ++i)
    {
        auto &c = cmds[i];
        require(c.kind >= AG_LINE && c.kind <= AG_PATH && finite(c.rect.left) && finite(c.rect.right) &&
                    finite(c.rect.top) && finite(c.rect.bottom) && finite(c.width) && c.width >= 0 &&
                    finite(c.font_size) && c.font_size >= 0 && finite(c.color.r) && finite(c.color.g) &&
                    finite(c.color.b) && finite(c.color.a),
                L"Invalid drawing command");
        require(c.kind == AG_PATH ? (c.first_point < path_count && c.point_count == 0) :
                    (c.first_point <= pn && c.point_count <= pn - c.first_point),
                L"Drawing point range exceeds snapshot");
        if (c.kind == AG_TEXT || c.kind == AG_IMAGE || c.kind == AG_SEGMENTS)
            require(wmemchr(c.text, 0, 256), L"Unterminated drawing string");
        if (c.kind == AG_PUSH_CLIP)
            ++clips;
        if (c.kind == AG_POP_CLIP)
            require(--clips >= 0, L"Unbalanced drawing clips");
        if (c.kind == AG_TRANSFORM)
            for (float x : c.matrix)
                require(finite(x), L"Invalid transform");
    }
    require(clips == 0, L"Unbalanced drawing clips");
}
void Painter::prepare(ID2D1RenderTarget *target, const Snapshot &s)
{
    if (owner != target)
    {
        reset();
        owner = target;
        check(target->CreateSolidColorBrush(D2D1::ColorF(0), &brush), L"Brush failed");
    }
    paths.clear();
    paths.resize(s.commands.size());
    ++generation;
    for (size_t i = 0; i < s.commands.size(); ++i)
    {
        auto &c = s.commands[i];
        if (c.kind == AG_TEXT)
        {
            float size = c.font_size > 0 ? c.font_size : context->theme.font_size;
            uint64_t key = uint64_t(std::lround(size * 64)) | (uint64_t(c.flags & 7) << 32);
            if (!formats.contains(key))
            {
                auto f = context->format(size, (c.flags & 1) != 0);
                f->SetTextAlignment(c.flags & 4   ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                    : c.flags & 2 ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                  : DWRITE_TEXT_ALIGNMENT_LEADING);
                formats.emplace(key, Cached<IDWriteTextFormat>{std::move(f), generation});
            }
            formats.at(key).generation = generation;
        }
        if ((c.kind == AG_POLYLINE || c.kind == AG_POLYGON) && c.point_count)
        {
            check(context->d2d->CreatePathGeometry(&paths[i]), L"Path creation failed");
            ComPtr<ID2D1GeometrySink> sink;
            check(paths[i]->Open(&sink), L"Path sink failed");
            auto points = s.points.data() + c.first_point;
            sink->BeginFigure({points[0].x, points[0].y},
                              c.kind == AG_POLYGON ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            for (uint32_t j = 1; j < c.point_count; ++j)
                sink->AddLine({points[j].x, points[j].y});
            sink->EndFigure(c.kind == AG_POLYGON ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            check(sink->Close(), L"Path completion failed");
        }
        if (c.kind == AG_IMAGE && !images.contains(c.text))
        {
            ComPtr<IWICBitmapDecoder> decoder;
            check(context->wic->CreateDecoderFromFilename(c.text, nullptr, GENERIC_READ,
                                                          WICDecodeMetadataCacheOnLoad, &decoder),
                  L"Image decode failed");
            ComPtr<IWICBitmapFrameDecode> frame;
            check(decoder->GetFrame(0, &frame), L"Image frame failed");
            ComPtr<IWICFormatConverter> converter;
            check(context->wic->CreateFormatConverter(&converter), L"Image converter failed");
            check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                        nullptr, 0, WICBitmapPaletteTypeCustom),
                  L"Image conversion failed");
            ComPtr<ID2D1Bitmap> bitmap;
            check(target->CreateBitmapFromWicBitmap(converter.Get(), &bitmap), L"Image upload failed");
            images.emplace(c.text, Cached<ID2D1Bitmap>{std::move(bitmap), generation});
        }
        if (c.kind == AG_IMAGE)
            images.at(c.text).generation = generation;
    }
    // Retain only resources referenced by this snapshot. A generation pass is
    // linear in cache size, unlike scanning all commands once per cached image.
    std::erase_if(images, [&](auto &entry) { return entry.second.generation != generation; });
    std::erase_if(formats, [&](auto &entry) { return entry.second.generation != generation; });
}
void Painter::draw(ID2D1RenderTarget *target, const Snapshot &s)
{
    target->SetTransform(root);
    for (size_t i = 0; i < s.commands.size(); ++i)
    {
        auto &c = s.commands[i];
        brush->SetColor(color(c.color));
        auto r = rect(c.rect);
        float width = c.width > 0 ? c.width : 1;
        switch (c.kind)
        {
        case AG_PATH:
        {
            auto &p = s.retained_paths[c.first_point];
            if (p.fill) target->FillGeometry(p.geometry.Get(), brush.Get());
            else target->DrawGeometry(p.geometry.Get(), brush.Get(), width, p.style.Get());
            break;
        }
        case AG_LINE:
            target->DrawLine({r.left, r.top}, {r.right, r.bottom}, brush.Get(), width);
            break;
        case AG_RECT:
            target->DrawRectangle(r, brush.Get(), width);
            break;
        case AG_FILL_RECT:
            target->FillRectangle(r, brush.Get());
            break;
        case AG_ELLIPSE:
        case AG_FILL_ELLIPSE:
        {
            D2D1_ELLIPSE e{{(r.left + r.right) / 2, (r.top + r.bottom) / 2},
                           std::abs(r.right - r.left) / 2,
                           std::abs(r.bottom - r.top) / 2};
            if (c.kind == AG_ELLIPSE)
                target->DrawEllipse(e, brush.Get(), width);
            else
                target->FillEllipse(e, brush.Get());
            break;
        }
        case AG_TEXT:
        {
            float size = c.font_size > 0 ? c.font_size : context->theme.font_size;
            uint64_t key = uint64_t(std::lround(size * 64)) | (uint64_t(c.flags & 7) << 32);
            target->DrawText(c.text, UINT32(wcslen(c.text)), formats.at(key).value.Get(), r, brush.Get(),
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
            break;
        }
        case AG_POLYLINE:
            if (paths[i])
                target->DrawGeometry(paths[i].Get(), brush.Get(), width);
            break;
        case AG_POLYGON:
            if (paths[i])
                target->FillGeometry(paths[i].Get(), brush.Get());
            break;
        case AG_PUSH_CLIP:
            target->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            break;
        case AG_POP_CLIP:
            target->PopAxisAlignedClip();
            break;
        case AG_TRANSFORM:
            target->SetTransform(D2D1::Matrix3x2F(c.matrix[0], c.matrix[1], c.matrix[2], c.matrix[3],
                                                  c.matrix[4], c.matrix[5]) * root);
            break;
        case AG_IMAGE:
            target->DrawBitmap(images.at(c.text).value.Get(), r, std::clamp(c.color.a, 0.f, 1.f));
            break;
        case AG_SEGMENTS:
        { // Portable seven-segment mechanism adapted from OTC VfdReadout.
            static constexpr int masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
            size_t n = wcslen(c.text);
            if (!n)
                break;
            float h = std::max(1.f, r.bottom - r.top), w = std::min(h * .56f, (r.right - r.left) / float(n)),
                  t = std::max(.5f, h * .085f);
            for (size_t j = 0; j < n; ++j)
            {
                float x = r.left + float(j) * w, y = r.top;
                wchar_t ch = c.text[j];
                int mask = ch >= L'0' && ch <= L'9' ? masks[ch - L'0'] : ch == L'-' ? 0x40 : 0;
                if (ch == L'.' || ch == L':')
                {
                    brush->SetColor(color(c.color));
                    target->FillRectangle(D2D1::RectF(x, y + h - t, x + t, y + h), brush.Get());
                    if (ch == L':')
                        target->FillRectangle(D2D1::RectF(x, y + h * .35f, x + t, y + h * .35f + t),
                                              brush.Get());
                    continue;
                }
                D2D1_RECT_F segments[7] = {{x + t, y, x + w - t, y + t},
                                           {x + w - t, y + t, x + w, y + h / 2 - t / 2},
                                           {x + w - t, y + h / 2 + t / 2, x + w, y + h - t},
                                           {x + t, y + h - t, x + w - t, y + h},
                                           {x, y + h / 2 + t / 2, x + t, y + h - t},
                                           {x, y + t, x + t, y + h / 2 - t / 2},
                                           {x + t, y + h / 2 - t / 2, x + w - t, y + h / 2 + t / 2}};
                for (int k = 0; k < 7; ++k)
                {
                    auto col = c.color;
                    if (!(mask & (1 << k)))
                        col.a *= .14f;
                    brush->SetColor(color(col));
                    target->FillRectangle(segments[k], brush.Get());
                }
            }
            break;
        }
        }
    }
    target->SetTransform(D2D1::Matrix3x2F::Identity());
}
void Surface::create(HWND parent, ag_rect b)
{
    layout_bounds = b;
    has_bounds = true;
    require(IsWindow(parent), L"Invalid surface parent");
    require(GetWindowThreadProcessId(parent, nullptr) == thread, L"Parent belongs to another UI thread",
            RPC_E_WRONG_THREAD);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = module();
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AssGraphics.Surface.1";
    require(RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, L"Surface class failed",
            E_FAIL);
    auto r = pixels(b, dpi(parent));
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, r.left, r.top,
                           r.right - r.left, r.bottom - r.top, parent, nullptr, module(), this);
    wincheck(hwnd != nullptr, L"Surface window failed");
}
void Surface::detach_pointer_ancestors() noexcept
{
    for (HWND ancestor : pointer_ancestors)
        RemoveWindowSubclass(ancestor, ancestor_pointer_proc, reinterpret_cast<UINT_PTR>(this));
    pointer_ancestors.clear();
}
void Surface::emit_pointer(uint32_t message_kind, ag_point position) noexcept
{
    // Registration is stable during dispatch. A nested hide/capture loss queues
    // cancellation until PRESS returns, so callers never receive nested decisions.
    ++pointer_dispatch_depth;
    try { if (pointer_callback) pointer_callback(pointer_user, message_kind, position); }
    catch (...) { last_result=E_FAIL; pointer_cancel_pending=pointer_cancel_pending || pointer_pressed; }
    --pointer_dispatch_depth;
    if (!pointer_dispatch_depth && pointer_cancel_pending)
    {
        pointer_cancel_pending=false;
        if (pointer_pressed) cancel_pointer();
        else emit_pointer(AG_CANVAS_CANCEL, pointer_last);
    }
}
void Surface::cancel_pointer() noexcept
{
    if (!pointer_pressed) return;
    pointer_pressed=false; // Clear before ReleaseCapture reenters our window proc.
    detach_pointer_ancestors();
    if (GetCapture()==hwnd) ReleaseCapture();
    if (pointer_dispatch_depth) pointer_cancel_pending=true;
    else emit_pointer(AG_CANVAS_CANCEL,pointer_last);
}
void Surface::dispose_pointer() noexcept
{
    pointer_disposing=true;
    cancel_pointer();
    detach_pointer_ancestors();
    pointer_callback=nullptr;pointer_user=nullptr;
}
LRESULT CALLBACK Surface::ancestor_pointer_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) noexcept
{
    auto p=reinterpret_cast<Surface*>(data);
    if ((m==WM_SHOWWINDOW && !w) || (m==WM_ENABLE && !w) || m==WM_CANCELMODE ||
        (m==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE) || m==WM_NCDESTROY ||
        (m==WM_WINDOWPOSCHANGING && (reinterpret_cast<WINDOWPOS*>(l)->flags&SWP_HIDEWINDOW)))
        p->cancel_pointer();
    if (m==WM_NCDESTROY) RemoveWindowSubclass(h,ancestor_pointer_proc,id);
    return DefSubclassProc(h,m,w,l);
}
void Surface::begin_pointer(ag_point position)
{
    if (callback_active() || pointer_pressed) return;
    pointer_busy=true;
    try
    {
        // Observe the actual current chain for this gesture, including foreign
        // caller-owned parents. No global hooks, polling or permanent subclasses.
        for (HWND ancestor=GetParent(hwnd);ancestor;ancestor=GetParent(ancestor))
        {
            if (!IsWindowEnabled(ancestor)) { pointer_busy=false;return; }
        }
        for (HWND ancestor=GetParent(hwnd);ancestor;ancestor=GetParent(ancestor))
        {
            pointer_ancestors.push_back(ancestor);
            wincheck(SetWindowSubclass(ancestor,ancestor_pointer_proc,reinterpret_cast<UINT_PTR>(this),
                                      reinterpret_cast<DWORD_PTR>(this)),L"Cannot observe canvas ancestors");
        }
        SetCapture(hwnd); // Return value is the previous owner, not success.
        if (GetCapture()!=hwnd || !IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd))
        {
            if (hwnd && GetCapture()==hwnd) ReleaseCapture();
            detach_pointer_ancestors();pointer_busy=false;return;
        }
        pointer_last=position;pointer_pressed=true;pointer_busy=false;
        emit_pointer(AG_CANVAS_PRESS,position);
    }
    catch (...) { detach_pointer_ancestors();pointer_busy=false;throw; }
}
LRESULT CALLBACK Surface::proc(HWND h, UINT m, WPARAM w, LPARAM l) noexcept
{
    auto p = reinterpret_cast<Surface *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE)
    {
        p = static_cast<Surface *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        p->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    }
    if (!p)
        return DefWindowProcW(h, m, w, l);
    if(p->pointer_callback){
        POINT position{};RECT client{};bool inside=false;ag_point local{};
        if (m==WM_LBUTTONDOWN || m==WM_LBUTTONUP || m==WM_MOUSEMOVE) {
            position={GET_X_LPARAM(l),GET_Y_LPARAM(l)};GetClientRect(h,&client);
            inside=PtInRect(&client,position)!=FALSE;
            local=canvas_local_point(position.x,position.y,dpi(h));
        }
        if(m==WM_LBUTTONDOWN && inside && IsWindowEnabled(h) && IsWindowVisible(h)){
            p->last_result=api([&]{p->begin_pointer(local);});return 0;
        }
        if(m==WM_LBUTTONUP && p->pointer_pressed){
            p->pointer_last=local;
            if (!inside || GetCapture()!=h || !IsWindowVisible(h) || !IsWindowEnabled(h)) p->cancel_pointer();
            else {
                p->pointer_pressed=false;p->detach_pointer_ancestors();
                if(GetCapture()==h)ReleaseCapture();p->emit_pointer(AG_CANVAS_RELEASE,local);
            }
            return 0;
        }
        if (m==WM_MOUSEMOVE && p->pointer_pressed) {p->pointer_last=local;if(!inside)p->cancel_pointer();}
        if(m==WM_CAPTURECHANGED || m==WM_CANCELMODE || m==WM_KILLFOCUS || m==WM_MOUSELEAVE ||
           (m==WM_ENABLE && !w) || (m==WM_SHOWWINDOW && !w) ||
           (m==WM_WINDOWPOSCHANGED && p->pointer_pressed))p->cancel_pointer();
    }
    if (m == WM_NCDESTROY)
    {
        p->cancel_pointer();p->detach_pointer_ancestors();
        SetWindowLongPtrW(h,GWLP_USERDATA,0);
        p->hwnd = nullptr;
        return DefWindowProcW(h, m, w, l);
    }
    if (m == WM_CONTEXTMENU && context_request(p,l))return 0;
    if (m == WM_ERASEBKGND)
        return 1;
    if (m == submit_message)
    {
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if(m==WM_TIMER){p->last_result=api([&]{p->timer(UINT_PTR(w));});return 0;}
    if (m == WM_SIZE)
    {
        p->last_result = api([&] { p->resize(); });
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps{};
        BeginPaint(h, &ps);
        LARGE_INTEGER start{}, end{}, frequency{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        auto allocations_before = cpp_allocations;
        p->last_result = api([&] { p->paint(); });
        p->metrics.last_cpp_allocations = cpp_allocations - allocations_before;
        QueryPerformanceCounter(&end);
        p->metrics.last_render_ms = double(end.QuadPart - start.QuadPart) * 1000 / double(frequency.QuadPart);
        ++p->metrics.frames;
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
struct Canvas final : Surface
{
    Snapshot current, pending;
    bool updated{};
    bool immediate{};
    ComPtr<ID2D1HwndRenderTarget> target;
    Painter painter;
    uint64_t current_sequence{}, rendered_sequence{}, target_generation{1}, rendered_generation{}, successful_frames{};
    Canvas(Context *c) : Surface(Kind::Canvas, c), painter(c) {}
    ~Canvas() override { dispose_pointer(); }
    void adopt()
    {
        std::lock_guard lock(mutex);
        notified = false;
        if (updated)
        {
            std::swap(current, pending);
            current_sequence=metrics.submissions;
            updated = false;
            dirty = true;
        }
    }
    void paint() override
    {
        rendered_generation=0; // An attempted/failed draw cannot retain readiness.
        adopt();
        if (!target)
        {
            RECT r{};
            GetClientRect(hwnd, &r);
            check(context->d2d->CreateHwndRenderTarget(
                      D2D1::RenderTargetProperties(),
                      D2D1::HwndRenderTargetProperties(
                          hwnd, D2D1::SizeU(std::max(1L, r.right), std::max(1L, r.bottom)),
                          immediate ? D2D1_PRESENT_OPTIONS_IMMEDIATELY : D2D1_PRESENT_OPTIONS_NONE),
                      &target),
                  L"2D target creation failed");
            target->SetDpi(float(dpi(hwnd)), float(dpi(hwnd)));
            dirty = true;
        }
        if (dirty)
        {
            painter.prepare(target.Get(), current);
            dirty = false;
        }
        target->BeginDraw();
        target->Clear(color(context->theme.background));
        painter.draw(target.Get(), current);
        HRESULT hr = target->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            reset();
            return;
        }
        check(hr, L"2D drawing failed");
        rendered_sequence=current_sequence;rendered_generation=target_generation;++successful_frames;
    }
    void resize() override
    {
        ++target_generation;rendered_generation=0;
        if (target)
        {
            RECT r{};
            GetClientRect(hwnd, &r);
            check(target->Resize(D2D1::SizeU(std::max(1L, r.right), std::max(1L, r.bottom))),
                  L"2D resize failed");
            target->SetDpi(float(dpi(hwnd)), float(dpi(hwnd)));
        }
    }
    void reset() override
    {
        ++target_generation;rendered_generation=0;
        painter.reset();
        target.Reset();
        dirty = true;
        ++metrics.device_recreations;
    }
};
} // namespace ag
using namespace ag;
namespace {
struct Path final : Object {
    ComPtr<ID2D1PathGeometry> geometry;
    std::array<ComPtr<ID2D1StrokeStyle>,9> styles;
    explicit Path(Context *c) : Object(Kind::Path,c) {}
};
}
ag_result ag_path_create(ag_handle h, const ag_path_command *commands, uint32_t count, ag_handle *out)
{
    return api([&] {
        output(out); auto ctx=get<Context>(h,Kind::Context); ctx->ui();
        require(commands && count && count<=65536,L"Invalid path command count");
        auto p=std::make_unique<Path>(ctx);
        check(ctx->d2d->CreatePathGeometry(&p->geometry),L"Cached path creation failed");
        ComPtr<ID2D1GeometrySink> sink; check(p->geometry->Open(&sink),L"Cached path sink failed");
        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE); bool open=false;
        for(uint32_t i=0;i<count;++i) {
            const auto &c=commands[i];
            require(c.kind>=AG_PATH_MOVE && c.kind<=AG_PATH_CLOSE,L"Invalid path verb");
            for(auto q:{c.p1,c.p2,c.p3}) require(finite(q.x)&&finite(q.y),L"Nonfinite path coordinate");
            if(c.kind==AG_PATH_MOVE) {
                if(open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->BeginFigure({c.p1.x,c.p1.y},D2D1_FIGURE_BEGIN_FILLED); open=true;
            } else {
                require(open,L"Path command before move or after close");
                if(c.kind==AG_PATH_LINE) sink->AddLine({c.p1.x,c.p1.y});
                if(c.kind==AG_PATH_QUADRATIC) sink->AddQuadraticBezier({{c.p1.x,c.p1.y},{c.p2.x,c.p2.y}});
                if(c.kind==AG_PATH_CUBIC) sink->AddBezier({{c.p1.x,c.p1.y},{c.p2.x,c.p2.y},{c.p3.x,c.p3.y}});
                if(c.kind==AG_PATH_CLOSE) {sink->EndFigure(D2D1_FIGURE_END_CLOSED);open=false;}
            }
        }
        if(open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        check(sink->Close(),L"Cached path completion failed"); *out=p.release();
    });
}
namespace ag {
Snapshot copy_snapshot(Context *ctx,const ag_draw *cmds,uint32_t n,const ag_point *points,uint32_t pn,const ag_path_draw *paths,uint32_t path_count) {
        require(path_count<=65536 && (!path_count||paths),L"Invalid path draw count");
        validate_snapshot(cmds,n,points,pn,path_count);
        Snapshot next;
        if(n)next.commands.assign(cmds,cmds+n);
        if(pn)next.points.assign(points,points+pn);
        next.retained_paths.reserve(path_count);
        for(uint32_t i=0;i<path_count;++i) {
            auto &d=paths[i]; auto path=get<Path>(d.path,Kind::Path);path->ui();
            require(path->context==ctx && d.fill<=1 && d.cap<=2 && d.join<=2,L"Invalid path draw style/context");
            Snapshot::PathDraw retained;retained.geometry=path->geometry;retained.fill=d.fill!=0;
            if(!retained.fill) {
                auto cap=d.cap==AG_CAP_ROUND?D2D1_CAP_STYLE_ROUND:d.cap==AG_CAP_SQUARE?D2D1_CAP_STYLE_SQUARE:D2D1_CAP_STYLE_FLAT;
                auto join=d.join==AG_JOIN_ROUND?D2D1_LINE_JOIN_ROUND:d.join==AG_JOIN_BEVEL?D2D1_LINE_JOIN_BEVEL:D2D1_LINE_JOIN_MITER;
                auto props=D2D1::StrokeStyleProperties(cap,cap,cap,join,10);
                auto &cached=path->styles[d.cap*3+d.join];
                if(!cached)check(ctx->d2d->CreateStrokeStyle(props,nullptr,0,&cached),L"Path stroke style failed");
                retained.style=cached;
            }
            next.retained_paths.push_back(std::move(retained));
        }
return next;
}
}
ag_result ag_canvas_submit_paths(ag_handle h,const ag_draw *cmds,uint32_t n,const ag_point *points,
                                uint32_t pn,const ag_path_draw *paths,uint32_t path_count)
{
    return api([&] {
        auto p=get<Canvas>(h,Kind::Canvas);p->ui();
        auto next=copy_snapshot(p->context,cmds,n,points,pn,paths,path_count);
        std::lock_guard lock(p->mutex);p->pending=std::move(next);p->updated=true;p->schedule_locked();
    });
}
ag_result ag_canvas_present_mode(ag_handle h, uint32_t immediate)
{
    return api(
        [&]
        {
            auto p = get<Canvas>(h, Kind::Canvas);
            p->ui();
            require(immediate <= 1, L"Invalid canvas present mode");
            if (p->immediate != (immediate != 0))
            {
                p->immediate = immediate != 0;
                p->reset();
                if(p->hwnd)InvalidateRect(p->hwnd, nullptr, FALSE);
            }
        });
}
ag_result ag_canvas_input(ag_handle h,ag_canvas_pointer_fn callback,void* user)
{
    return api([&]{auto p=get<Canvas>(h,Kind::Canvas);p->ui();
        require(!p->callback_active(),L"Input registration is busy",HRESULT_FROM_WIN32(ERROR_BUSY));
        require(IsWindow(p->hwnd),L"Canvas window no longer exists");
        p->pointer_busy=true;p->cancel_pointer();p->pointer_callback=callback;p->pointer_user=user;p->pointer_busy=false;});
}
ag_result ag_canvas_state(ag_handle h,ag_canvas_render_state* state)
{
    return api([&]{auto p=get<Canvas>(h,Kind::Canvas);p->ui();
        require(state && state->size==sizeof(*state) && state->version==1,L"Invalid canvas render state");
        require(!p->pointer_disposing,L"Canvas is disposing",HRESULT_FROM_WIN32(ERROR_BUSY));
        std::lock_guard lock(p->mutex);
        *state={sizeof(*state),1, uint32_t(IsWindow(p->hwnd) && p->target && p->rendered_generation==p->target_generation &&
            p->metrics.submissions && p->rendered_sequence==p->metrics.submissions && !p->updated && SUCCEEDED(p->last_result)),
            uint32_t(p->updated),p->metrics.submissions,p->rendered_sequence,p->target_generation,p->successful_frames,p->last_result,0};
    });
}
ag_result ag_canvas_create(ag_handle c, void *parent, ag_rect r, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto ctx = get<Context>(c, Kind::Context);
            ctx->ui();
            auto p = std::make_unique<Canvas>(ctx);
            p->create(static_cast<HWND>(parent), r);
            *out = p.release();
        });
}
ag_result ag_canvas_submit(ag_handle h, const ag_draw *cmds, uint32_t n, const ag_point *points, uint32_t pn)
{
    return api(
        [&]
        {
            auto p = get<Canvas>(h, Kind::Canvas);
            validate_snapshot(cmds, n, points, pn);
            std::lock_guard lock(p->mutex);
            // A failed second allocation must not publish a mixture of snapshots.
            p->updated = false;
            p->pending.retained_paths.clear();
            if (n)
                p->pending.commands.assign(cmds, cmds + n);
            else
                p->pending.commands.clear();
            if (pn)
                p->pending.points.assign(points, points + pn);
            else
                p->pending.points.clear();
            p->updated = true;
            p->schedule_locked();
        });
}
ag_result ag_canvas_export_png(ag_handle h, const wchar_t *path, uint32_t w, uint32_t height, float d)
{
    return api(
        [&]
        {
            auto p = get<Canvas>(h, Kind::Canvas);
            p->ui();
            require(path && w > 0 && height > 0 && w <= 16384 && height <= 16384 && d >= 48 && d <= 960,
                    L"Invalid PNG request");
            p->adopt();
            auto ctx = p->context;
            ComPtr<IWICBitmap> bitmap;
            check(ctx->wic->CreateBitmap(w, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                         &bitmap),
                  L"Bitmap creation failed");
            ComPtr<ID2D1RenderTarget> target;
            auto props = D2D1::RenderTargetProperties();
            props.dpiX = d;
            props.dpiY = d;
            check(ctx->d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target),
                  L"Bitmap target failed");
            Painter painter(ctx);
            painter.prepare(target.Get(), p->current);
            target->BeginDraw();
            target->Clear(color(ctx->theme.background));
            painter.draw(target.Get(), p->current);
            check(target->EndDraw(), L"PNG drawing failed");
            ComPtr<IWICStream> stream;
            check(ctx->wic->CreateStream(&stream), L"PNG stream failed");
            check(stream->InitializeFromFilename(path, GENERIC_WRITE), L"PNG file failed");
            ComPtr<IWICBitmapEncoder> encoder;
            check(ctx->wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), L"PNG encoder failed");
            check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), L"PNG encoder init failed");
            ComPtr<IWICBitmapFrameEncode> frame;
            check(encoder->CreateNewFrame(&frame, nullptr), L"PNG frame failed");
            check(frame->Initialize(nullptr), L"PNG frame init failed");
            check(frame->SetSize(w, height), L"PNG size failed");
            check(frame->SetResolution(d, d), L"PNG resolution failed");
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
            check(frame->SetPixelFormat(&format), L"PNG format failed");
            check(frame->WriteSource(bitmap.Get(), nullptr), L"PNG encoding failed");
            check(frame->Commit(), L"PNG frame commit failed");
            check(encoder->Commit(), L"PNG commit failed");
        });
}
ag_result ag_device_reset(ag_handle h)
{
    return api(
        [&]
        {
            auto o = object(h);
            o->ui();
            require(o->kind == Kind::Canvas || o->kind == Kind::Chart || o->kind == Kind::Scene || o->kind == Kind::Display,
                    L"Not a drawing surface");
            auto p = static_cast<Surface *>(o);
            p->reset();
            if(p->hwnd)InvalidateRect(p->hwnd, nullptr, FALSE);
        });
}
ag_result ag_surface_metrics(ag_handle h, ag_metrics *m)
{
    return api(
        [&]
        {
            auto o = object(h);
            o->ui();
            require(m && (o->kind == Kind::Canvas || o->kind == Kind::Chart || o->kind == Kind::Scene || o->kind == Kind::Display),
                    L"Invalid surface metrics");
            auto p = static_cast<Surface *>(o);
            std::lock_guard lock(p->mutex);
            p->get_metrics(*m);
            m->pending = p->notified;
            check(p->last_result, L"Last surface render failed");
        });
}
