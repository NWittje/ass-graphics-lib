#include "internal.hpp"
#include "refresh.hpp"
#include "tray_icon.hpp"
#include "choice_decision.hpp"
#include <utility>
#include <charconv>
#include <shobjidl.h>
#include <unordered_map>
namespace ag
{
// Use the same invariant representation as from_chars/settings, regardless of
// the application's C locale (for example a German decimal comma).
static std::wstring numeric_text(double value, int precision)
{
    char buffer[96]{};
    auto [end, ec] =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, precision);
    require(ec == std::errc(), L"Numeric formatting failed");
    return std::wstring(buffer, end);
}
struct Tray;
static constexpr UINT viewport_message = WM_APP + 0x322;
static constexpr UINT choice_settle_message = WM_APP + 0x323;
static constexpr wchar_t window_property[] = L"ASS.Graphics.Window";
static constexpr wchar_t button_edges_property[] = L"ASS.Graphics.ButtonEdges";
static void cancel_button_tree(HWND root) noexcept
{
    // Ancestor hiding does not reliably send WM_SHOWWINDOW to every child.
    // Visit only opted-in library buttons, before hiding/rebuilding the tree.
    if (GetPropW(root, button_edges_property))
        SendMessageW(root, WM_CANCELMODE, 0, 0);
    EnumChildWindows(root, [](HWND child, LPARAM) -> BOOL
    {
        if (GetPropW(child, button_edges_property))
            SendMessageW(child, WM_CANCELMODE, 0, 0);
        return TRUE;
    }, 0);
}
static HWND enclosing_panel(HWND child) noexcept
{
    for (; child; child = GetParent(child))
        if (GetPropW(child, L"ASS.Graphics.ScrollPanel"))
            return child;
    return nullptr;
}
static bool route_panel_wheel(HWND from, WPARAM w, LPARAM l) noexcept
{
    if (GET_KEYSTATE_WPARAM(w) & (MK_CONTROL | MK_SHIFT))
        return false;
    const auto root = GetAncestor(from, GA_ROOT);
    if (!GetPropW(root, window_property))
        return false;
    POINT point{SHORT(LOWORD(l)), SHORT(HIWORD(l))};
    ScreenToClient(root, &point);
    RECT bounds{};
    GetClientRect(root, &bounds);
    if (!PtInRect(&bounds, point))
        return false;
    auto hovered = root;
    for (;;)
    {
        const auto child = ChildWindowFromPointEx(hovered, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
        if (!child || child == hovered)
            break;
        MapWindowPoints(hovered, child, &point, 1);
        hovered = child;
    }
    const auto destination = enclosing_panel(hovered);
    // Compare owning panels so parent propagation cannot route back to its child.
    if (!destination || destination == enclosing_panel(from))
        return false;
    SendMessageW(destination, WM_MOUSEWHEEL, w, l);
    return true;
}
struct Window final : Object
{
    struct Shortcut {uint32_t key,modifiers,id;};std::vector<Shortcut> shortcuts;
    ag_event_fn callback{};
    void *user{};
    Tray *tray{};
    bool exiting{};
    bool defer_close{};
    HWND dialog_owner{}; bool owner_disabled{};
    bool borderless{}; WINDOWPLACEMENT restored{sizeof(WINDOWPLACEMENT)};
    DWORD restored_style{},restored_exstyle{};
    uint32_t minimum_width{}, minimum_height{};
    HANDLE instance_mutex{};
    UINT activate_message{};
    std::unique_ptr<RefreshClock> refresh;
    DisplayRate refresh_rate;
    HMONITOR refresh_monitor{};
    double refresh_multiplier{};
    uint64_t refresh_callbacks{};
    Window(Context *c) : Object(Kind::Window, c) {}
    ~Window()
    {
        refresh.reset();
        if(owner_disabled && IsWindow(dialog_owner))EnableWindow(dialog_owner,TRUE);
        if (hwnd)
            DestroyWindow(hwnd);
        if (instance_mutex)
            CloseHandle(instance_mutex);
    }
    void event(uint32_t k)
    {
        if (callback)
            callback(user, 0, k, 0);
    }
    void refresh_configure(bool topology = false)
    {
        if (!refresh)
            return;
        auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (topology || monitor != refresh_monitor)
        {
            refresh_rate = display_rate(hwnd);
            refresh_monitor = monitor;
        }
        const bool visible = IsWindowVisible(hwnd) && !IsIconic(hwnd);
        refresh->configure(visible ? std::min(1000.0, refresh_rate.hz * refresh_multiplier) : 0);
    }
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM) noexcept;
};
struct Tray final : Object
{
    Window *window;
    TrayNotification notification;
    NOTIFYICONDATAW &icon = notification.data;
    UINT taskbar_message{};
    bool status_popup{};
    std::array<std::wstring, 2> labels;
    std::array<uint32_t, 2> states{};
    std::array<int, 2> label_widths{};
    static constexpr UINT status_item = 0x7ffe;
    Tray(Window *w, const wchar_t *text) : Object(Kind::Tray, w->context), window(w)
    {
        require(!w->tray, L"Window already has a tray icon");
        taskbar_message = RegisterWindowMessageW(L"TaskbarCreated");
        notification.create(w->hwnd, text, tray_message);
        w->tray = this;
    }
    ~Tray()
    {
        window->tray = nullptr;
    }
    void restore()
    {
        notification.restore();
    }
    void menu()
    {
        HMENU menu = CreatePopupMenu();
        wincheck(menu != nullptr, L"Tray menu creation failed");
        if (status_popup)
        {
            AppendMenuW(menu, MF_OWNERDRAW | MF_GRAYED, status_item, nullptr);
            AppendMenuW(menu, MF_OWNERDRAW, 3, nullptr);
        }
        else
        {
            AppendMenuW(menu, MF_STRING, 1, L"Show");
            AppendMenuW(menu, MF_STRING, 2, L"Hide");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 3, L"Exit");
        }
        POINT p{};
        GetCursorPos(&p);
        SetForegroundWindow(window->hwnd);
        UINT selected =
            TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, window->hwnd, nullptr);
        DestroyMenu(menu);
        PostMessageW(window->hwnd, WM_NULL, 0, 0);
        if (selected == 1)
        {
            ShowWindow(window->hwnd, SW_RESTORE);
            SetForegroundWindow(window->hwnd);
        }
        else if (selected == 2)
            ShowWindow(window->hwnd, SW_HIDE);
        else if (selected == 3)
        {
            window->exiting = true;
            PostMessageW(window->hwnd, WM_CLOSE, 0, 0);
        }
    }
    bool measure(MEASUREITEMSTRUCT *item)
    {
        if (!status_popup || item->CtlType != ODT_MENU)
            return false;
        HDC dc = GetDC(window->hwnd);
        const auto old = SelectObject(dc, context->font(0, false, dpi(window->hwnd)));
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SIZE size{};
        for (size_t i = 0; i < labels.size(); ++i)
        {
            GetTextExtentPoint32W(dc, labels[i].c_str(), int(labels[i].size()), &size);
            label_widths[i] = size.cx;
        }
        SelectObject(dc, old);
        ReleaseDC(window->hwnd, dc);
        item->itemHeight = metrics.tmHeight + MulDiv(16, dpi(window->hwnd), 96);
        item->itemWidth =
            UINT(std::max(MulDiv(160, dpi(window->hwnd), 96),
                          label_widths[0] + label_widths[1] + MulDiv(48, dpi(window->hwnd), 96)));
        return true;
    }
    bool draw(DRAWITEMSTRUCT *item)
    {
        if (!status_popup || item->CtlType != ODT_MENU)
            return false;
        const bool selected = item->itemID == 3 && (item->itemState & ODS_SELECTED);
        FillRect(item->hDC, &item->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
        const auto old = SelectObject(item->hDC, context->font(0, false, dpi(window->hwnd)));
        const int saved = SaveDC(item->hDC);
        SetBkMode(item->hDC, TRANSPARENT);
        RECT area = item->rcItem;
        area.left += MulDiv(12, dpi(window->hwnd), 96);
        if (item->itemID == status_item)
        {
            for (size_t i = 0; i < labels.size(); ++i)
            {
                const auto &t = context->theme;
                const auto c = states[i] == 1   ? t.warning
                               : states[i] == 2 ? t.error
                               : states[i] == 3 ? t.success
                                                : t.foreground;
                SetTextColor(item->hDC, RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255)));
                DrawTextW(item->hDC, labels[i].c_str(), -1, &area,
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                area.left += label_widths[i] + MulDiv(24, dpi(window->hwnd), 96);
            }
        }
        else
        {
            SetTextColor(item->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
            DrawTextW(item->hDC, L"Exit", -1, &area, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        RestoreDC(item->hDC, saved);
        SelectObject(item->hDC, old);
        return true;
    }
};
LRESULT CALLBACK Window::proc(HWND h, UINT m, WPARAM w, LPARAM l) noexcept
{
    auto *p = reinterpret_cast<Window *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE)
    {
        p = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        p->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        if (!SetPropW(h, window_property, p))
        {
            p->hwnd = nullptr;
            return FALSE;
        }
    }
    if (!p)
        return DefWindowProcW(h, m, w, l);
    try
    {
        if (p->activate_message && m == p->activate_message)
        {
            ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
            return 0;
        }
        if (p->tray && m == p->tray->taskbar_message)
        {
            p->tray->restore();
            return 0;
        }
        switch (m)
        {
        case WM_KEYDOWN:
            if(!p->dialog_owner && IsWindowEnabled(h)){uint32_t mods=(GetKeyState(VK_SHIFT)<0?1:0)|(GetKeyState(VK_CONTROL)<0?2:0)|(GetKeyState(VK_MENU)<0?4:0);for(auto &s:p->shortcuts)if(s.key==w && (s.modifiers&7)==mods){if((!(l & (LPARAM(1)<<30)) || (s.modifiers&AG_SHORTCUT_REPEAT)) && p->callback)p->callback(p->user,s.id,AG_SHORTCUT,0);return 0;}}
            break;
        case WM_CONTEXTMENU:
            if(context_request(p,l))return 0;
            break;
        case WM_COMMAND:
            if(p->dialog_owner && (LOWORD(w)==IDOK || LOWORD(w)==IDCANCEL) && !l){p->event(LOWORD(w)==IDOK?AG_DIALOG_ACCEPT:AG_DIALOG_CANCEL);return 0;}
            break;
        case WM_MOUSEWHEEL:
            if (route_panel_wheel(h, w, l))
                return 0;
            break;
        case refresh_message:
            if (p->refresh)
            {
                try
                {
                    if (p->refresh->active() && IsWindowVisible(h) && !IsIconic(h))
                    {
                        ++p->refresh_callbacks;
                        p->event(AG_REFRESH);
                    }
                }
                catch (...)
                {
                    if (p->refresh)
                        p->refresh->complete();
                    throw;
                }
                if (p->refresh)
                    p->refresh->complete();
            }
            return 0;
        case dispatch_message:
            if (p->callback)
                p->callback(p->user, uint32_t(w), AG_DISPATCH, 0);
            return 0;
        case WM_MEASUREITEM:
            if (p->tray && p->tray->measure(reinterpret_cast<MEASUREITEMSTRUCT *>(l)))
                return TRUE;
            break;
        case WM_DRAWITEM:
            if (p->tray && p->tray->draw(reinterpret_cast<DRAWITEMSTRUCT *>(l)))
                return TRUE;
            break;
        case WM_DISPLAYCHANGE:
            p->refresh_configure(true);
            break;
        case WM_MOVE:
        case WM_SHOWWINDOW:
            if (m == WM_SHOWWINDOW && !w)
                cancel_button_tree(h);
            // Visibility is final after ShowWindow completes, so also configure
            // on WM_WINDOWPOSCHANGED below.
            p->refresh_configure();
            break;
        case WM_WINDOWPOSCHANGED:
            if (reinterpret_cast<WINDOWPOS *>(l)->flags & SWP_HIDEWINDOW)
                cancel_button_tree(h);
            if (reinterpret_cast<WINDOWPOS *>(l)->flags & SWP_SHOWWINDOW)
                RedrawWindow(h, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
            p->refresh_configure();
            break;
        case WM_GETMINMAXINFO:
            if (p->minimum_width && p->minimum_height)
            {
                auto info = reinterpret_cast<MINMAXINFO *>(l);
                RECT r{0, 0, MulDiv(int(p->minimum_width), dpi(h), 96),
                       MulDiv(int(p->minimum_height), dpi(h), 96)};
                AdjustWindowRectExForDpi(&r, DWORD(GetWindowLongPtrW(h, GWL_STYLE)), FALSE,
                                         DWORD(GetWindowLongPtrW(h, GWL_EXSTYLE)), dpi(h));
                info->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
                return 0;
            }
            break;
        case WM_CLOSE:
            if (p->tray && !p->exiting)
            {
                ShowWindow(h, SW_HIDE);
                return 0;
            }
            p->event(AG_CLOSE);
            if (!p->defer_close)
                DestroyWindow(h);
            return 0;
        case WM_TIMER:
            if (p->callback)
                p->callback(p->user, uint32_t(w), AG_TIMER, 0);
            return 0;
        case WM_SIZE:
            if (w == SIZE_MINIMIZED)
                cancel_button_tree(h);
            p->refresh_configure();
            p->event(AG_RESIZE);
            return 0;
        case WM_DPICHANGED:
        {
            auto r = reinterpret_cast<RECT *>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            for (auto o : p->context->objects)
                if (o->hwnd && IsChild(h, o->hwnd))
                {
                    SendMessageW(o->hwnd, WM_SETFONT,
                                 reinterpret_cast<WPARAM>(p->context->font(0, false, HIWORD(w))), TRUE);
                    if (o->tooltip)
                        SendMessageW(o->tooltip, WM_SETFONT,
                                     reinterpret_cast<WPARAM>(p->context->font(0, false, HIWORD(w))), TRUE);
                    if (o->has_bounds)
                        ag_bounds_set(o, o->layout_bounds);
                }
            p->event(AG_RESIZE);
            return 0;
        }
        case WM_ERASEBKGND:
        {
            auto c = p->context->theme.background;
            HBRUSH b = CreateSolidBrush(RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255)));
            RECT r{};
            GetClientRect(h, &r);
            FillRect(reinterpret_cast<HDC>(w), &r, b);
            DeleteObject(b);
            return 1;
        }
        case tray_message:
            if (p->tray)
            {
                auto code = LOWORD(l);
                if (code == WM_CONTEXTMENU || code == WM_RBUTTONUP)
                    p->tray->menu();
                else if (code == NIN_SELECT || code == NIN_KEYSELECT)
                {
                    ShowWindow(h, SW_RESTORE);
                    SetForegroundWindow(h);
                }
            }
            return 0;
        case WM_NCDESTROY:
            if(p->owner_disabled && IsWindow(p->dialog_owner)){EnableWindow(p->dialog_owner,TRUE);p->owner_disabled=false;}
            RemovePropW(h, window_property);
            p->refresh.reset();
            p->hwnd = nullptr;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;
        }
    }
    catch (...)
    {
        error = L"Window callback failed";
    }
    return DefWindowProcW(h, m, w, l);
}
struct Control final : Object
{
    struct ParentDispatcher
    {
        HWND parent{};
        std::unordered_map<HWND, Control *> children;
        unsigned active_calls{};
        bool attached{};
    };
    static constexpr wchar_t dispatcher_property[] = L"ASS.Graphics.ParentDispatcher";
    static constexpr UINT_PTR dispatcher_id = 0x41534750;
    static void detach_dispatcher(ParentDispatcher *dispatcher) noexcept
    {
        if (dispatcher->attached)
        {
            RemoveWindowSubclass(dispatcher->parent, parent_proc, dispatcher_id);
            RemovePropW(dispatcher->parent, dispatcher_property);
            dispatcher->attached = false;
        }
        if (!dispatcher->active_calls)
            delete dispatcher;
    }
    static void unregister_child(HWND parent, HWND child) noexcept
    {
        if (!child)
            return;
        auto dispatcher = static_cast<ParentDispatcher *>(GetPropW(parent, dispatcher_property));
        if (!dispatcher)
            return;
        dispatcher->children.erase(child);
        if (dispatcher->children.empty())
            detach_dispatcher(dispatcher);
    }
    void register_child()
    {
        if (auto dispatcher = static_cast<ParentDispatcher *>(GetPropW(parent, dispatcher_property)))
        {
            require(dispatcher->children.emplace(hwnd, this).second,
                    L"Duplicate child dispatch registration");
            return;
        }
        auto dispatcher = std::make_unique<ParentDispatcher>();
        dispatcher->parent = parent;
        dispatcher->children.emplace(hwnd, this);
        wincheck(SetWindowSubclass(parent, parent_proc, dispatcher_id,
                                   reinterpret_cast<DWORD_PTR>(dispatcher.get())),
                 L"Parent dispatch installation failed");
        if (!SetPropW(parent, dispatcher_property, dispatcher.get()))
        {
            const DWORD code = GetLastError();
            RemoveWindowSubclass(parent, parent_proc, dispatcher_id);
            throw Failure{code ? HRESULT_FROM_WIN32(code) : E_FAIL, L"Parent dispatch identity failed"};
        }
        dispatcher->attached = true;
        dispatcher.release();
    }
    uint32_t type, id;
    HWND parent{}, value_label{};
    std::wstring unit;
    uint32_t style_flags{}, event_flags{}, silent_text{}, update_depth{};
    bool update_visible{}, update_suppressed{}, choice_tracking{};
    ChoiceDecisionState choice_decision;
    bool choice_decision_dispatching{};
    uint32_t button_source{}, button_cancelled{}, button_programmatic{};
    bool button_click_blocked{};
    bool viewport_enabled{}, viewport_pending{};
    bool tab_equal_width{};
    float tab_height_dips{};
    uint32_t viewport_serial{};
    int viewport_width{-1}, viewport_height{-1}, viewport_scroll{-1};
    float extent_dip{};
    ag_event_fn callback;
    void *user;
    double minimum{}, maximum{1}, step{.01};
    std::mutex mutex;
    std::wstring pending, cached;
    bool dirty{}, notified{};
    uint32_t state{}, pending_state{};
    int scroll{}, extent{}, wheel_remainder{};
    std::vector<Control *> pages;
    Control *tabs{};
    Control(Context *c, const ag_control_desc &d, ag_event_fn cb, void *u)
        : Object(Kind::Control, c), type(d.kind), id(d.id), callback(cb), user(u), minimum(d.minimum),
          maximum(d.maximum), step(d.step)
    {
    }
    ~Control()
    {
        if (hwnd)
            cancel_button_tree(hwnd);
        if (tabs)
            std::erase(tabs->pages, this);
        for (auto page : pages)
            page->tabs = nullptr;
        unregister_child(parent, hwnd);
        if (hwnd)
            DestroyWindow(hwnd);
        if (value_label)
            DestroyWindow(value_label);
    }
    int native_layout_height(int requested) const override
    {
        if (type != AG_CHOICE)
            return requested;
        // Native COMBOBOX height includes its expanded list; layout_bounds is
        // the caller's row. Measure the actual font without scanning items.
        const auto row = SendMessageW(hwnd, CB_GETITEMHEIGHT, 0, 0);
        const auto field = SendMessageW(hwnd, CB_GETITEMHEIGHT, WPARAM(-1), 0);
        require(row > 0 && row <= 1000000 && field > 0 && field <= 1000000,
                L"Choice font metrics unavailable");
        RECT collapsed{};
        wincheck(GetWindowRect(hwnd, &collapsed), L"Choice bounds unavailable");
        const int edge = std::max(1, GetSystemMetricsForDpi(SM_CYEDGE, dpi(hwnd)));
        const int closed_height = std::max(int(field), int(collapsed.bottom - collapsed.top));
        constexpr int visible_rows = 8;
        return std::max(requested, closed_height + visible_rows * int(row) + 2 * edge);
    }
    void event(uint32_t event_kind, double value)
    {
        if (callback)
            callback(user, id, event_kind, value);
    }
    void button_finish(uint32_t event_kind)
    {
        const auto source = std::exchange(button_source, 0u);
        if (!source)
            return;
        if (event_kind == AG_BUTTON_CANCEL)
        {
            button_cancelled |= source;
            button_click_blocked = true;
            SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
        }
        // Clear first: native capture release and callbacks can reenter us.
        event(event_kind, source);
    }
    void button_cancel()
    {
        if (!button_source)
            return;
        button_finish(AG_BUTTON_CANCEL);
    }
    bool dropdown_open() const
    {
        return type == AG_CHOICE && (choice_tracking || SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0));
    }
    bool choice_decision_enabled() const noexcept {
        return type == AG_CHOICE && (event_flags & AG_CONTROL_NOTIFY_CHOICE_DECISION);
    }
    bool choice_interaction_busy() const {
        // Default callers retain the old native/tracking query. Opt-in callers
        // also see unresolved close settlement and decision callback dispatch.
        return dropdown_open() || (choice_decision_enabled() &&
               (choice_decision.active() || choice_decision_dispatching));
    }
    void choice_mutation_check() const {
        require(!choice_decision_enabled() ||
                !choice_interaction_busy(),
                L"Choice decision interaction is active", HRESULT_FROM_WIN32(ERROR_BUSY));
    }
    void choice_emit(ChoiceDecision decision) {
        if (decision.kind == ChoiceDecisionKind::none) return;
        choice_decision_dispatching = true;
        try {
            event(decision.kind == ChoiceDecisionKind::accepted ? AG_CHOICE_ACCEPTED : AG_CHOICE_CANCELLED,
                  double(decision.index));
        } catch (...) { choice_decision_dispatching = false; throw; }
        choice_decision_dispatching = false;
    }
    void begin_update()
    {
        require(update_depth < 1024, L"Control batch nesting limit");
        if (!update_depth++)
        {
            update_visible = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
            update_suppressed = update_visible;
            if (update_suppressed)
                SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        }
    }
    void end_update()
    {
        require(update_depth != 0, L"Unbalanced control batch end");
        if (--update_depth)
            return;
        if (update_suppressed)
            SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        ShowWindow(hwnd, update_visible ? SW_SHOWNA : SW_HIDE);
        if (value_label)
            ShowWindow(value_label, update_visible ? SW_SHOWNA : SW_HIDE);
        update_suppressed = false;
        if (update_visible)
            RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void notify_viewport(bool force = false)
    {
        if (!viewport_enabled)
            return;
        RECT r{};
        GetClientRect(hwnd, &r);
        const bool changed =
            viewport_width != r.right || viewport_height != r.bottom || viewport_scroll != scroll;
        viewport_width = r.right;
        viewport_height = r.bottom;
        viewport_scroll = scroll;
        if ((force || changed) && !viewport_pending)
        {
            wincheck(PostMessageW(hwnd, viewport_message, viewport_serial, 0),
                     L"Viewport notification failed");
            viewport_pending = true;
        }
    }
    uint32_t navigation(WPARAM key) const
    {
        if (dropdown_open() || (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000))
            return 0;
        if (key == VK_TAB && (event_flags & AG_CONTROL_NAV_TAB))
            return (GetKeyState(VK_SHIFT) & 0x8000) ? AG_NAV_TAB_PREVIOUS : AG_NAV_TAB_NEXT;
        if (event_flags & AG_CONTROL_NAV_VERTICAL)
            switch (key)
            {
            case VK_UP:
                return AG_NAV_UP;
            case VK_DOWN:
                return AG_NAV_DOWN;
            case VK_PRIOR:
                return AG_NAV_PAGE_UP;
            case VK_NEXT:
                return AG_NAV_PAGE_DOWN;
            case VK_HOME:
                return AG_NAV_HOME;
            case VK_END:
                return AG_NAV_END;
            }
        if (event_flags & AG_CONTROL_NAV_ACTIVATE)
        {
            if (key == VK_RETURN)
                return AG_NAV_ENTER;
            if (key == VK_ESCAPE)
                return AG_NAV_ESCAPE;
        }
        return 0;
    }
    double value()
    {
        if (type == AG_CHOICE)
            return double(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
        if (type == AG_TABS)
            return TabCtrl_GetCurSel(hwnd);
        if (type == AG_CHECKBOX)
            return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
        if (type == AG_SLIDER)
            return std::min(maximum, minimum + SendMessageW(hwnd, TBM_GETPOS, 0, 0) * step);
        wchar_t text[128]{};
        GetWindowTextW(hwnd, text, 128);
        std::string narrow;
        for (wchar_t c : std::wstring(text))
        {
            require(c < 128, L"Numeric fields require invariant decimal notation");
            narrow += char(c);
        }
        double v{};
        auto [end, ec] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), v);
        require(ec == std::errc() && end == narrow.data() + narrow.size() && std::isfinite(v) &&
                    v >= minimum && v <= maximum,
                L"Invalid or out-of-range numeric value");
        return v;
    }
    void set_value(double v)
    {
        if (type == AG_CHOICE || type == AG_TABS)
        {
            const auto count =
                type == AG_CHOICE ? SendMessageW(hwnd, CB_GETCOUNT, 0, 0) : LRESULT(pages.size());
            require(std::isfinite(v) && std::floor(v) == v && v >= 0 && v < count, L"Invalid selection");
            if (type == AG_CHOICE)
            {
                choice_mutation_check();
                SendMessageW(hwnd, CB_SETCURSEL, WPARAM(v), 0);
            }
            else
            {
                TabCtrl_SetCurSel(hwnd, int(v));
                page_layout();
            }
            return;
        }
        require(std::isfinite(v) && v >= minimum && v <= maximum, L"Value outside control range");
        if (type == AG_CHECKBOX)
        {
            SetWindowLongPtrW(hwnd, GWL_STYLE,
                              (GetWindowLongPtrW(hwnd, GWL_STYLE) & ~LONG_PTR(BS_TYPEMASK)) |
                                  BS_AUTOCHECKBOX);
            SendMessageW(hwnd, BM_SETCHECK, v != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else if (type == AG_SLIDER)
        {
            v = ag_slider_quantize(v, minimum, maximum, step);
            auto position =
                v == maximum ? std::ceil((maximum - minimum) / step) : std::round((v - minimum) / step);
            SendMessageW(hwnd, TBM_SETPOS, TRUE, LPARAM(position));
            update_label();
        }
        else
        {
            auto text = numeric_text(v, 17);
            ++silent_text;
            SetWindowTextW(hwnd, text.c_str());
            --silent_text;
        }
    }
    void update_label()
    {
        if (!value_label)
            return;
        auto label = numeric_text(value(), 6) + (unit.empty() ? L"" : L" " + unit);
        SetWindowTextW(value_label, label.c_str());
    }
    void status_apply()
    {
        std::unique_lock lock(mutex);
        notified = false;
        if (!dirty)
            return;
        DWORD a{}, b{};
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
        if (GetFocus() == hwnd && (a != b || (GetKeyState(VK_LBUTTON) & 0x8000)))
        {
            SetTimer(hwnd, 1, 60, nullptr);
            return;
        }
        if (pending != cached)
        {
            int line = int(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
            SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            SetWindowTextW(hwnd, pending.c_str());
            auto count = DWORD(pending.size());
            SendMessageW(hwnd, EM_SETSEL, std::min(a, count), std::min(b, count));
            int after = int(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
            SendMessageW(hwnd, EM_LINESCROLL, 0, line - after);
            SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
            cached.swap(pending);
        }
        state = pending_state;
        dirty = false;
        KillTimer(hwnd, 1);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    void page_layout()
    {
        RECT r{};
        GetClientRect(hwnd, &r);
        if (tab_equal_width && !pages.empty())
        {
            RECT first{};
            if (TabCtrl_GetItemRect(hwnd, 0, &first))
            {
                if (!tab_height_dips)
                    tab_height_dips = float(first.bottom - first.top) * 96.f / dpi(hwnd);
                const int width = std::max(1, (int(r.right) - 2 * int(first.left)) / int(pages.size()));
                const int height = std::max(1, int(tab_height_dips * dpi(hwnd) / 96.f + .5f));
                TabCtrl_SetItemSize(hwnd, std::min(width, 65535), std::min(height, 65535));
            }
        }
        TabCtrl_AdjustRect(hwnd, FALSE, &r);
        int active = TabCtrl_GetCurSel(hwnd);
        for (size_t i = 0; i < pages.size(); ++i)
            if (pages[i]->hwnd)
            {
                const auto page = pages[i];
                RECT previous{};
                GetWindowRect(page->hwnd, &previous);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&previous), 2);
                if (!EqualRect(&previous, &r))
                    MoveWindow(page->hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
                const bool visible = int(i) == active;
                if (page->update_depth)
                {
                    page->update_visible = visible;
                    if (!visible)
                        cancel_button_tree(page->hwnd);
                }
                else if (bool(GetWindowLongPtrW(page->hwnd, GWL_STYLE) & WS_VISIBLE) != visible)
                {
                    ShowWindow(page->hwnd, visible ? SW_SHOWNA : SW_HIDE);
                    // A native tab-page reveal alone can leave a nested renderer
                    // without an update region. Queue the completed subtree once;
                    // do not pump painting or activate the window during layout.
                    if (visible)
                        RedrawWindow(page->hwnd, nullptr, nullptr,
                                     RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
                }
            }
    }
    void scroll_to(int y)
    {
        RECT r{};
        GetClientRect(hwnd, &r);
        int next = std::clamp(y, 0, std::max(0, extent - int(r.bottom)));
        ScrollWindowEx(hwnd, 0, scroll - next, nullptr, nullptr, nullptr, nullptr,
                       SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        scroll = next;
        SCROLLINFO si{
            sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, extent - 1), UINT(r.bottom), scroll,
            0};
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        notify_viewport();
    }
    void draw_button(const DRAWITEMSTRUCT &item)
    {
        const auto &t = context->theme;
        const bool selected = (style_flags & AG_STYLE_SELECTED) || (item.itemState & ODS_SELECTED);
        const bool enabled = !(item.itemState & ODS_DISABLED);
        auto bg = t.background;
        if (selected)
        {
            bg.r = (bg.r * 3 + t.accent.r) / 4;
            bg.g = (bg.g * 3 + t.accent.g) / 4;
            bg.b = (bg.b * 3 + t.accent.b) / 4;
        }
        HBRUSH brush = CreateSolidBrush(RGB(BYTE(bg.r * 255), BYTE(bg.g * 255), BYTE(bg.b * 255)));
        FillRect(item.hDC, &item.rcItem, brush);
        DeleteObject(brush);
        const int saved = SaveDC(item.hDC);
        const auto fg = enabled ? t.foreground : t.muted;
        SetTextColor(item.hDC, RGB(BYTE(fg.r * 255), BYTE(fg.g * 255), BYTE(fg.b * 255)));
        SetBkMode(item.hDC, TRANSPARENT);
        SelectObject(item.hDC, context->font(0, selected, dpi(hwnd)));
        if (style_flags & AG_STYLE_FOLDOUT)
        {
            const auto &r = item.rcItem;
            const int mid = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
            const int width = std::max(1, std::min(int(r.right - r.left) / 3, MulDiv(4, dpi(hwnd), 96)));
            const int sign = style_flags & AG_STYLE_EXPANDED ? -1 : 1;
            const POINT points[]{{mid - sign * width, y - MulDiv(4, dpi(hwnd), 96)},
                                 {mid + sign * width, y},
                                 {mid - sign * width, y + MulDiv(4, dpi(hwnd), 96)}};
            HPEN pen = CreatePen(PS_SOLID, std::max(1, MulDiv(1, dpi(hwnd), 96)), GetTextColor(item.hDC));
            const auto old = SelectObject(item.hDC, pen);
            Polyline(item.hDC, points, 3);
            SelectObject(item.hDC, old);
            DeleteObject(pen);
        }
        else
        {
            wchar_t text[1024]{};
            GetWindowTextW(hwnd, text, 1024);
            RECT area = item.rcItem;
            area.left += MulDiv(8, dpi(hwnd), 96);
            area.right -= MulDiv(8, dpi(hwnd), 96);
            const UINT alignment = style_flags & AG_STYLE_ALIGN_RIGHT    ? DT_RIGHT
                                   : style_flags & AG_STYLE_ALIGN_CENTER ? DT_CENTER
                                                                         : DT_LEFT;
            DrawTextW(item.hDC, text, -1, &area,
                      alignment | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT))
        {
            RECT focus = item.rcItem;
            InflateRect(&focus, -1, -1);
            DrawFocusRect(item.hDC, &focus);
        }
        RestoreDC(item.hDC, saved);
    }
    static LRESULT CALLBACK parent_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) noexcept
    {
        auto dispatcher = reinterpret_cast<ParentDispatcher *>(data);
        ++dispatcher->active_calls;
        struct ActiveCall
        {
            ParentDispatcher *dispatcher;
            ~ActiveCall()
            {
                if (!--dispatcher->active_calls && !dispatcher->attached)
                    delete dispatcher;
            }
        } active{dispatcher};
        if ((m == WM_SHOWWINDOW && !w) ||
            (m == WM_WINDOWPOSCHANGING && (reinterpret_cast<WINDOWPOS *>(l)->flags & SWP_HIDEWINDOW)) ||
            (m == WM_ENABLE && !w))
            cancel_button_tree(h);
        if (m == WM_NCDESTROY)
        {
            detach_dispatcher(dispatcher);
            return DefSubclassProc(h, m, w, l);
        }
        HWND child{};
        switch (m)
        {
        case WM_COMMAND:
        case WM_HSCROLL:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            child = reinterpret_cast<HWND>(l);
            break;
        case WM_NOTIFY:
            if (l)
                child = reinterpret_cast<NMHDR *>(l)->hwndFrom;
            break;
        case WM_DRAWITEM:
            if (l && reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlType != ODT_MENU)
                child = reinterpret_cast<DRAWITEMSTRUCT *>(l)->hwndItem;
            break;
        default:
            return DefSubclassProc(h, m, w, l);
        }
        const auto found = dispatcher->children.find(child);
        if (found == dispatcher->children.end())
            return DefSubclassProc(h, m, w, l);
        // Never retain an iterator across application callbacks: a callback can
        // legitimately create/remove other controls and rehash this index.
        auto p = found->second;
        try
        {
            if (m == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT *>(l)->hwndItem == p->hwnd &&
                p->type == AG_BUTTON && (p->style_flags & (AG_STYLE_FLAT | AG_STYLE_FOLDOUT)))
            {
                p->draw_button(*reinterpret_cast<DRAWITEMSTRUCT *>(l));
                return TRUE;
            }
            if (m == WM_COMMAND && reinterpret_cast<HWND>(l) == p->hwnd)
            {
                UINT n = HIWORD(w);
                if (n == BN_CLICKED && (p->type == AG_BUTTON || p->type == AG_CHECKBOX))
                {
                    // Native keyboard cancellation can synthesize a late click.
                    // Only opted-in cancelled holds suppress it; BM_CLICK and
                    // the existing ordinary-button behavior remain available.
                    if (!(p->type == AG_BUTTON && (p->event_flags & AG_CONTROL_NOTIFY_BUTTON_EDGES) &&
                          p->button_click_blocked && !p->button_programmatic))
                        p->event(AG_CLICK, p->type == AG_CHECKBOX ? p->value() : 0);
                }
                else if (n == CBN_SELCHANGE && p->type == AG_CHOICE)
                    p->event(AG_VALUE_CHANGED, p->value());
                else if (n == CBN_SELENDOK && p->choice_decision_enabled())
                    p->choice_emit(p->choice_decision.accept(int32_t(p->value())));
                else if (n == CBN_SELENDCANCEL && p->choice_decision_enabled())
                    p->choice_emit(p->choice_decision.cancel());
                else if ((n == CBN_DROPDOWN || n == CBN_CLOSEUP) && p->type == AG_CHOICE)
                {
                    p->choice_tracking = n == CBN_DROPDOWN;
                    if (p->choice_decision_enabled()) {
                        if (p->choice_tracking) p->choice_decision.open(int32_t(p->value()));
                        else {
                            p->choice_decision.close();
                            if (p->choice_decision.active())
                                wincheck(PostMessageW(p->hwnd, choice_settle_message,
                                                      WPARAM(p->choice_decision.ticket()), 0),
                                         L"Choice settlement scheduling failed");
                        }
                    }
                    if (p->event_flags & AG_CONTROL_NOTIFY_DROPDOWN)
                        p->event(AG_DROPDOWN_CHANGED, p->choice_tracking ? 1 : 0);
                }
                else if (n == EN_CHANGE && (p->event_flags & AG_CONTROL_NOTIFY_TEXT) && !p->silent_text &&
                         (p->type == AG_EDIT || p->type == AG_NUMERIC))
                    p->event(AG_TEXT_CHANGED, 0);
                else if (n == EN_KILLFOCUS)
                {
                    if (p->type == AG_NUMERIC)
                        p->event(AG_VALUE_CHANGED, p->value());
                    p->event(AG_FOCUS_LOST, 0);
                }
            }
            else if (m == WM_HSCROLL && reinterpret_cast<HWND>(l) == p->hwnd && p->type == AG_SLIDER)
            {
                p->update_label();
                p->event(AG_VALUE_CHANGED, p->value());
            }
            else if (m == WM_NOTIFY && reinterpret_cast<NMHDR *>(l)->hwndFrom == p->hwnd &&
                     reinterpret_cast<NMHDR *>(l)->code == TCN_SELCHANGE)
            {
                p->page_layout();
                p->event(AG_VALUE_CHANGED, p->value());
            }
            else if ((m == WM_CTLCOLOREDIT || m == WM_CTLCOLORSTATIC) &&
                     reinterpret_cast<HWND>(l) == p->hwnd &&
                     (p->type == AG_STATUS || p->type == AG_INDICATOR))
            {
                auto &t = p->context->theme;
                auto c = p->state == 1   ? t.warning
                         : p->state == 2 ? t.error
                         : p->state == 3 ? t.success
                                         : t.foreground;
                SetTextColor(reinterpret_cast<HDC>(w),
                             RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255)));
                SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            }
        }
        catch (const Failure &e)
        {
            error = e.message;
        }
        catch (...)
        {
            error = L"Control callback failed";
        }
        return DefSubclassProc(h, m, w, l);
    }
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) noexcept
    {
        auto p = reinterpret_cast<Control *>(data);
        try
        {
            if (m == BM_CLICK && (p->event_flags & AG_CONTROL_NOTIFY_BUTTON_EDGES))
            {
                p->button_cancel();
                // BM_CLICK sends synthetic down/up messages through this same
                // subclass. Preserve its click without treating it as a hold.
                ++p->button_programmatic;
                const auto result = DefSubclassProc(h, m, w, l);
                --p->button_programmatic;
                return result;
            }
            if ((p->event_flags & AG_CONTROL_NOTIFY_BUTTON_EDGES) && !p->button_programmatic)
            {
                if (m == WM_KILLFOCUS || m == WM_CAPTURECHANGED || m == WM_CANCELMODE ||
                    m == WM_DESTROY || (m == WM_ENABLE && !w) || (m == WM_SHOWWINDOW && !w) ||
                    (m == WM_KEYDOWN && w == VK_ESCAPE) ||
                    (m == WM_WINDOWPOSCHANGING && (reinterpret_cast<WINDOWPOS *>(l)->flags & SWP_HIDEWINDOW)))
                    p->button_cancel();
                const bool mouse_down = m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK;
                const bool space_down = m == WM_KEYDOWN && w == VK_SPACE;
                if (mouse_down || space_down)
                {
                    const uint32_t source = mouse_down ? 1u : 2u;
                    if (p->button_source && p->button_source != source)
                        return 0;
                    // A repeated Space after cancellation must not restart a hold.
                    if (space_down && (l & (LPARAM(1) << 30)) && !p->button_source)
                        return 0;
                    const auto result = DefSubclassProc(h, m, w, l);
                    if ((p->event_flags & AG_CONTROL_NOTIFY_BUTTON_EDGES) && !p->button_source &&
                        IsWindowVisible(h) && IsWindowEnabled(h) &&
                        (mouse_down ? GetCapture() == h : GetFocus() == h))
                    {
                        p->button_cancelled &= ~source;
                        p->button_click_blocked = false;
                        p->button_source = source;
                        p->event(AG_BUTTON_PRESS, source);
                    }
                    return result;
                }
                if (m == WM_LBUTTONUP || (m == WM_KEYUP && w == VK_SPACE))
                {
                    const uint32_t source = m == WM_LBUTTONUP ? 1u : 2u;
                    if (p->button_source == source)
                    {
                        RECT client{};
                        GetClientRect(h, &client);
                        POINT point{SHORT(LOWORD(l)), SHORT(HIWORD(l))};
                        const bool normal = IsWindowVisible(h) && IsWindowEnabled(h) &&
                            (source == 1 ? GetCapture() == h && PtInRect(&client, point) : GetFocus() == h);
                        p->button_finish(normal ? AG_BUTTON_RELEASE : AG_BUTTON_CANCEL);
                    }
                    if (p->button_cancelled & source)
                    {
                        p->button_cancelled &= ~source;
                        return 0;
                    }
                }
            }
            if (m == viewport_message)
            {
                if (p->viewport_enabled && p->viewport_pending && uint32_t(w) == p->viewport_serial)
                {
                    p->viewport_pending = false;
                    p->event(AG_VIEWPORT_CHANGED, p->scroll * 96.0 / dpi(h));
                }
                return 0;
            }
            if (m == WM_GETDLGCODE)
            {
                const auto message = reinterpret_cast<MSG *>(l);
                const auto key = message ? message->wParam : w;
                if ((!message || message->message == WM_KEYDOWN) && p->navigation(key))
                    return DefSubclassProc(h, m, w, l) | DLGC_WANTMESSAGE;
            }
            if (m == WM_KEYDOWN)
                if (const auto nav = p->navigation(w))
                {
                    p->event(AG_NAVIGATE, nav);
                    return 0;
                }
            if (m == WM_CHAR && (w == VK_TAB || w == VK_RETURN || w == VK_ESCAPE) && p->navigation(w))
                return 0;
            if (m == WM_KILLFOCUS && (p->event_flags & AG_CONTROL_NOTIFY_FOCUS) && p->type != AG_EDIT &&
                p->type != AG_NUMERIC)
                p->event(AG_FOCUS_LOST, 0);
            if (m == WM_MOUSEWHEEL && !(p->type == AG_CHOICE && SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0)) &&
                route_panel_wheel(h, w, l))
                return 0;
            if (m == WM_MOUSEWHEEL && !(GET_KEYSTATE_WPARAM(w) & (MK_CONTROL | MK_SHIFT)) &&
                (p->type == AG_EDIT || p->type == AG_NUMERIC || p->type == AG_BUTTON ||
                 p->type == AG_CHECKBOX ||
                 (p->type == AG_CHOICE && !SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0))))
            {
                for (auto ancestor = p->parent; ancestor; ancestor = GetParent(ancestor))
                    if (GetPropW(ancestor, L"ASS.Graphics.ScrollPanel"))
                        // Let Windows propagate once, without a closed combo
                        // consuming the wheel as an unintended value change.
                        return DefWindowProcW(h, m, w, l);
            }
            if (m == WM_SETFOCUS)
            {
                // Native Tab/click/SetFocus all share the same reveal rule.
                // Reveal the actual descendant through each nested panel.
                for (HWND ancestor = GetParent(h); ancestor; ancestor = GetParent(ancestor))
                    if (auto panel = GetPropW(ancestor, L"ASS.Graphics.ScrollPanel"))
                        check(ag_panel_reveal(panel, h), L"Focused child reveal failed");
                if (p->event_flags & AG_CONTROL_NOTIFY_FOCUS)
                    p->event(AG_FOCUS_GAINED, 0);
            }
            if ((p->type == AG_NUMERIC || p->type == AG_EDIT) && m == WM_GETDLGCODE)
            {
                const auto message = reinterpret_cast<MSG *>(l);
                if ((message && message->message == WM_KEYDOWN && message->wParam == VK_RETURN) ||
                    w == VK_RETURN)
                    return DefSubclassProc(h, m, w, l) | DLGC_WANTMESSAGE;
            }
            if ((p->type == AG_NUMERIC || p->type == AG_EDIT) && (m == WM_KEYDOWN || m == WM_CHAR) &&
                w == VK_RETURN)
            {
                if (m == WM_KEYDOWN)
                    p->event(AG_VALUE_CHANGED, p->type == AG_NUMERIC ? p->value() : 0);
                return 0;
            }
            if (p->type == AG_CHECKBOX &&
                (m == BM_CLICK || m == WM_LBUTTONDOWN || (m == WM_KEYDOWN && w == VK_SPACE)) &&
                SendMessageW(h, BM_GETCHECK, 0, 0) == BST_INDETERMINATE)
            {
                SetWindowLongPtrW(h, GWL_STYLE,
                                  (GetWindowLongPtrW(h, GWL_STYLE) & ~LONG_PTR(BS_TYPEMASK)) |
                                      BS_AUTOCHECKBOX);
                SendMessageW(h, BM_SETCHECK, BST_UNCHECKED, 0);
            }
            if (m == choice_settle_message && p->choice_decision_enabled()) {
                p->choice_emit(p->choice_decision.settle(uint64_t(w), !p->dropdown_open()));
                return 0;
            }
            if (m == submit_message || (m == WM_TIMER && w == 1))
            {
                p->status_apply();
                return 0;
            }
            if (m == WM_WINDOWPOSCHANGED && p->value_label)
            {
                RECT r{};
                GetWindowRect(h, &r);
                MapWindowPoints(nullptr, p->parent, reinterpret_cast<POINT *>(&r), 2);
                MoveWindow(p->value_label, r.left, r.bottom, r.right - r.left, MulDiv(24, dpi(h), 96), TRUE);
                SendMessageW(p->value_label, WM_SETFONT,
                             reinterpret_cast<WPARAM>(p->context->font(0, false, dpi(h))), TRUE);
            }
            if (m == WM_SIZE)
            {
                if (p->type == AG_PANEL)
                {
                    p->extent = int(p->extent_dip * dpi(h) / 96);
                    p->scroll_to(p->scroll);
                }
                if (p->type == AG_TABS)
                    p->page_layout();
            }
            if (p->type == AG_PANEL)
            {
                // STATIC otherwise returns HTTRANSPARENT and consumes native
                // scrollbar button presses. Use normal non-client behavior.
                if (m == WM_NCHITTEST || m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONDBLCLK)
                    return DefWindowProcW(h, m, w, l);
                if (m == WM_LBUTTONDOWN)
                {
                    SetFocus(h);
                    return 0;
                }
                if (m == WM_MOUSEWHEEL && !(GET_KEYSTATE_WPARAM(w) & (MK_CONTROL | MK_SHIFT)))
                {
                    UINT lines = 3;
                    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                    p->wheel_remainder += GET_WHEEL_DELTA_WPARAM(w);
                    const int notches = p->wheel_remainder / WHEEL_DELTA;
                    p->wheel_remainder %= WHEEL_DELTA;
                    RECT client{};
                    GetClientRect(h, &client);
                    const auto page = uint64_t(std::max(0L, client.bottom));
                    const auto line = uint64_t(std::max(1, MulDiv(24, GetDpiForWindow(h), 96)));
                    const int distance = int(lines == WHEEL_PAGESCROLL ? page : std::min(page, line * lines));
                    p->scroll_to(p->scroll - notches * distance);
                    return 0;
                }
                if (m == WM_VSCROLL)
                {
                    SCROLLINFO si{sizeof(si), SIF_ALL};
                    GetScrollInfo(h, SB_VERT, &si);
                    int y = p->scroll;
                    switch (LOWORD(w))
                    {
                    case SB_LINEUP:
                        y -= MulDiv(24, GetDpiForWindow(h), 96);
                        break;
                    case SB_LINEDOWN:
                        y += MulDiv(24, GetDpiForWindow(h), 96);
                        break;
                    case SB_PAGEUP:
                        y -= si.nPage;
                        break;
                    case SB_PAGEDOWN:
                        y += si.nPage;
                        break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION:
                        y = si.nTrackPos;
                        break;
                    case SB_TOP:
                        y = 0;
                        break;
                    case SB_BOTTOM:
                        y = p->extent;
                        break;
                    }
                    p->scroll_to(y);
                    return 0;
                }
            }
            if (p->choice_decision_enabled() &&
                (m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_CHAR || m == WM_UNICHAR || m == WM_MOUSEWHEEL)) {
                // A closed native keyboard/type-ahead change accepts after native
                // processing. Popup epochs exclude trailing close notifications.
                const auto before = p->choice_decision.input(int32_t(p->value()), p->dropdown_open());
                const auto result = DefSubclassProc(h, m, w, l);
                p->choice_emit(p->choice_decision.finish_input(before, int32_t(p->value()), p->dropdown_open()));
                return result;
            }
            if (m == WM_NCDESTROY)
            {
                RemovePropW(h, button_edges_property);
                unregister_child(p->parent, h);
                if (p->type == AG_PANEL)
                    RemovePropW(h, L"ASS.Graphics.ScrollPanel");
                p->hwnd = nullptr;
                RemoveWindowSubclass(h, proc, reinterpret_cast<UINT_PTR>(p));
            }
        }
        catch (...)
        {
            error = L"Control update failed";
        }
        return DefSubclassProc(h, m, w, l);
    }
    void create(HWND parent_window, const ag_control_desc &d)
    {
        layout_bounds = d.bounds;
        has_bounds = true;
        parent = parent_window;
        require(IsWindow(parent), L"Invalid parent window");
        require(GetWindowThreadProcessId(parent, nullptr) == thread, L"Parent belongs to another UI thread",
                RPC_E_WRONG_THREAD);
        DWORD style = WS_CHILD | WS_VISIBLE;
        const wchar_t *cls = L"STATIC";
        DWORD ex{};
        switch (type)
        {
        case AG_LABEL:
        case AG_INDICATOR:
            style |= SS_LEFT;
            break;
        case AG_BUTTON:
            cls = L"BUTTON";
            style |= BS_PUSHBUTTON | WS_TABSTOP;
            break;
        case AG_CHOICE:
            cls = L"COMBOBOX";
            style |= CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP;
            break;
        case AG_CHECKBOX:
            cls = L"BUTTON";
            style |= BS_AUTOCHECKBOX | WS_TABSTOP;
            break;
        case AG_EDIT:
        case AG_NUMERIC:
            cls = L"EDIT";
            style |= ES_AUTOHSCROLL | WS_TABSTOP;
            ex = WS_EX_CLIENTEDGE;
            break;
        case AG_STATUS:
            cls = L"EDIT";
            style |= ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_HSCROLL | WS_VSCROLL |
                     WS_TABSTOP;
            ex = WS_EX_CLIENTEDGE;
            break;
        case AG_SLIDER:
            cls = TRACKBAR_CLASSW;
            style |= TBS_AUTOTICKS | WS_TABSTOP;
            require(std::isfinite(step) && std::isfinite(minimum) && std::isfinite(maximum) && step > 0 &&
                        maximum >= minimum && (maximum - minimum) / step <= 1000000,
                    L"Invalid slider range");
            break;
        case AG_PANEL:
            style |= WS_VSCROLL | WS_CLIPCHILDREN;
            ex = WS_EX_CONTROLPARENT;
            break;
        case AG_TABS:
            cls = WC_TABCONTROLW;
            style |= WS_TABSTOP | WS_CLIPCHILDREN;
            ex = WS_EX_CONTROLPARENT;
            break;
        default:
            throw Failure{E_INVALIDARG, L"Unknown control kind"};
        }
        auto r = pixels(d.bounds, dpi(parent));
        hwnd = CreateWindowExW(ex, cls, d.text ? d.text : L"", style, r.left, r.top, r.right - r.left,
                               r.bottom - r.top, parent, reinterpret_cast<HMENU>(UINT_PTR(id)), module(),
                               nullptr);
        wincheck(hwnd != nullptr, L"Control creation failed");
        if (type == AG_PANEL)
            wincheck(SetPropW(hwnd, L"ASS.Graphics.ScrollPanel", this), L"Panel identity unavailable");
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(context->font(0, false, dpi(parent))), TRUE);
        wincheck(SetWindowSubclass(hwnd, proc, reinterpret_cast<UINT_PTR>(this),
                                   reinterpret_cast<DWORD_PTR>(this)),
                 L"Control dispatch installation failed");
        register_child();
        // Reuse the later layout rule after B612 supplies native font metrics.
        if (type == AG_CHOICE)
            check(ag_bounds_set(this, layout_bounds), L"Choice initial layout failed");
        if (type == AG_STATUS)
            SendMessageW(hwnd, EM_SETLIMITTEXT, 1024 * 1024, 0);
        if (type == AG_SLIDER)
        {
            unit = d.text ? d.text : L"";
            SendMessageW(hwnd, TBM_SETRANGEMAX, FALSE, LPARAM(std::ceil((maximum - minimum) / step)));
            value_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, r.left,
                                          r.bottom, r.right - r.left, 24, parent, nullptr, module(), nullptr);
            SendMessageW(value_label, WM_SETFONT, reinterpret_cast<WPARAM>(context->font()), TRUE);
            set_value(d.value);
        }
        if (type == AG_NUMERIC || type == AG_CHECKBOX)
            set_value(d.value);
        if (d.tooltip && *d.tooltip)
            check(ag_tooltip_set(this, d.tooltip), L"Control tooltip creation failed");
    }
};
int panel_scroll_offset(HWND parent) noexcept
{
    const auto panel = static_cast<Control *>(GetPropW(parent, L"ASS.Graphics.ScrollPanel"));
    return panel ? panel->scroll : 0;
}
} // namespace ag
using namespace ag;
ag_result ag_window_create(ag_handle c, const wchar_t *title, int32_t w, int32_t h, ag_event_fn cb,
                           void *user, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto context = get<Context>(c, Kind::Context);
            context->ui();
            require(w > 0 && h > 0, L"Invalid window dimensions");
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = Window::proc;
            wc.hInstance = module();
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = L"AssGraphics.Window.1";
            require(RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                    L"Window class failed", E_FAIL);
            auto p = std::make_unique<Window>(context);
            p->callback = cb;
            p->user = user;
            UINT window_dpi = GetDpiForSystem();
            RECT desired{0, 0, MulDiv(w, window_dpi, 96), MulDiv(h, window_dpi, 96)};
            AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, window_dpi);
            p->hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, title ? title : L"",
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                      desired.right - desired.left, desired.bottom - desired.top, nullptr,
                                      nullptr, module(), p.get());
            wincheck(p->hwnd != nullptr, L"Window creation failed");
            *out = p.release();
        });
}
ag_result ag_window_show(ag_handle h, uint32_t s)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            if(p->dialog_owner){
                if(s && !p->owner_disabled){p->owner_disabled=IsWindowEnabled(p->dialog_owner)!=FALSE;if(p->owner_disabled)EnableWindow(p->dialog_owner,FALSE);}
                if(!s && p->owner_disabled){EnableWindow(p->dialog_owner,TRUE);p->owner_disabled=false;}
            }
            ShowWindow(p->hwnd, s ? SW_RESTORE : SW_HIDE);
        });
}
ag_result ag_window_run(ag_handle h, int32_t *exit)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            MSG m{};
            int32_t code = 0;
            const auto dispatch = [&](MSG &message)
            {
                if (message.message == WM_QUIT)
                {
                    code = int32_t(message.wParam);
                    return false;
                }
                if (!IsWindow(p->hwnd))
                    return false;
                // Fair paint servicing must still realize a virtual panel's newest
                // viewport first. Deliver its already-posted notification before
                // entering any descendant paint procedure; stale queued copies
                // subsequently do nothing. Callbacks may replace other children.
                if (message.message == WM_PAINT)
                    for (HWND ancestor = message.hwnd; ancestor; ancestor = GetParent(ancestor))
                        if (auto raw = GetPropW(ancestor, L"ASS.Graphics.ScrollPanel"))
                        {
                            auto panel = static_cast<Control *>(raw);
                            if (panel->viewport_enabled && panel->viewport_pending)
                                SendMessageW(ancestor, viewport_message, panel->viewport_serial, 0);
                        }
                if (message.hwnd && !IsWindow(message.hwnd))
                    return IsWindow(p->hwnd) != FALSE;
                const HWND root = message.hwnd ? GetAncestor(message.hwnd, GA_ROOT) : nullptr;
                const bool library_window = root && GetPropW(root, window_property);
                if(library_window && message.message==WM_KEYDOWN){auto target=static_cast<Window *>(GetPropW(root,window_property));if(!target->dialog_owner && IsWindowEnabled(root)){uint32_t modifiers=(GetKeyState(VK_SHIFT)<0?1:0)|(GetKeyState(VK_CONTROL)<0?2:0)|(GetKeyState(VK_MENU)<0?4:0);for(auto &shortcut:target->shortcuts)if(shortcut.key==message.wParam && shortcut.modifiers==modifiers){if(!(message.lParam & (LPARAM(1)<<30)) && target->callback)target->callback(target->user,shortcut.id,AG_SHORTCUT,0);return IsWindow(p->hwnd)!=FALSE;}}}
                if(library_window && message.message==WM_KEYDOWN && (message.wParam==VK_RETURN || message.wParam==VK_ESCAPE)) {
                    auto dialog=static_cast<Window *>(GetPropW(root,window_property));
                    bool combo=SendMessageW(GetFocus(),CB_GETDROPPEDSTATE,0,0)!=0;
                    if(dialog->dialog_owner && !combo && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000)) {
                        auto focused=GetFocus();wchar_t class_name[32]{};GetClassNameW(focused,class_name,32);
                        const auto button_style=GetWindowLongPtrW(focused,GWL_STYLE)&BS_TYPEMASK;
                        if(message.wParam==VK_RETURN && GetAncestor(focused,GA_ROOT)==root && !_wcsicmp(class_name,L"Button") && (button_style==BS_PUSHBUTTON || button_style==BS_DEFPUSHBUTTON))
                            SendMessageW(focused,BM_CLICK,0,0);
                        else dialog->event(message.wParam==VK_RETURN?AG_DIALOG_ACCEPT:AG_DIALOG_CANCEL);
                        return IsWindow(p->hwnd)!=FALSE;
                    }
                }
                if (!library_window || !IsDialogMessageW(root, &message))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                return IsWindow(p->hwnd) != FALSE;
            };
            while (IsWindow(p->hwnd))
            {
                // High-rate posted telemetry must not outrank generated timer,
                // input and paint messages forever. One message per class is a
                // bounded fairness pass, followed by ordinary FIFO dispatch.
                bool alive = true;
                constexpr UINT ranges[][2] = {{WM_KEYFIRST, WM_KEYLAST},
                                              {WM_MOUSEFIRST, WM_MOUSELAST},
                                              {WM_TIMER, WM_TIMER},
                                              {WM_PAINT, WM_PAINT}};
                for (const auto &range : ranges)
                    if (PeekMessageW(&m, nullptr, range[0], range[1], PM_REMOVE) && !dispatch(m))
                    {
                        alive = false;
                        break;
                    }
                if (!alive)
                    break;
                BOOL r = GetMessageW(&m, nullptr, 0, 0);
                wincheck(r != -1, L"Message loop failed");
                if (!r)
                {
                    code = int32_t(m.wParam);
                    break;
                }
                if (!dispatch(m))
                    break;
            }
            if (exit)
                *exit = code;
        });
}
ag_result ag_window_close(ag_handle h)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            p->exiting = true;
            SendMessageW(p->hwnd, WM_CLOSE, 0, 0);
        });
}
ag_result ag_window_fit(ag_handle h)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            RECT r{};
            GetWindowRect(p->hwnd, &r);
            MONITORINFO mi{sizeof(mi)};
            wincheck(GetMonitorInfoW(MonitorFromWindow(p->hwnd, MONITOR_DEFAULTTONEAREST), &mi),
                     L"Monitor lookup failed");
            int width = std::min(r.right - r.left, mi.rcWork.right - mi.rcWork.left),
                height = std::min(r.bottom - r.top, mi.rcWork.bottom - mi.rcWork.top);
            MoveWindow(p->hwnd, std::clamp(r.left, mi.rcWork.left, mi.rcWork.right - width),
                       std::clamp(r.top, mi.rcWork.top, mi.rcWork.bottom - height), width, height, TRUE);
        });
}
ag_result ag_tray_create(ag_handle h, const wchar_t *t, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            *out = new Tray(p, t ? t : L"Application");
        });
}
ag_result ag_single_instance(ag_handle h, const wchar_t *id, uint32_t *primary)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(id && *id && primary && !p->instance_mutex, L"Invalid instance request");
            std::wstring name = L"Local\\AssGraphics.";
            name += id;
            p->activate_message = RegisterWindowMessageW(name.c_str());
            p->instance_mutex = CreateMutexW(nullptr, FALSE, name.c_str());
            DWORD err = GetLastError();
            wincheck(p->instance_mutex != nullptr, L"Instance mutex failed");
            *primary = err != ERROR_ALREADY_EXISTS;
            if (!*primary)
                PostMessageW(HWND_BROADCAST, p->activate_message, 0, 0);
        });
}
ag_result ag_control_create(ag_handle h, void *parent, const ag_control_desc *d, ag_event_fn cb, void *u,
                            ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto c = get<Context>(h, Kind::Context);
            c->ui();
            require(d, L"Null control descriptor");
            auto p = std::make_unique<Control>(c, *d, cb, u);
            p->create(static_cast<HWND>(parent), *d);
            *out = p.release();
        });
}
ag_result ag_control_value_set(ag_handle h, double v)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            p->set_value(v);
        });
}
ag_result ag_control_value_get(ag_handle h, double *v)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(v, L"Null output");
            *v = p->value();
        });
}
ag_result ag_control_text_set(ag_handle h, const wchar_t *s)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(s, L"Null text");
            if (p->type == AG_SLIDER)
            {
                p->unit = s;
                p->update_label();
                return;
            }
            ++p->silent_text;
            SetWindowTextW(p->hwnd, s);
            --p->silent_text;
        });
}
ag_result ag_panel_extent(ag_handle h, float height)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_PANEL && height >= 0 && height <= 1000000 && finite(height),
                    L"Invalid panel extent");
            p->extent_dip = height;
            p->extent = int(height * dpi(p->hwnd) / 96);
            p->scroll_to(p->scroll);
        });
}
ag_result ag_tab_add(ag_handle h, const wchar_t *title, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_TABS && title, L"Invalid tab");
            ag_control_desc d{};
            d.kind = AG_PANEL;
            auto page = std::make_unique<Control>(p->context, d, nullptr, nullptr);
            page->create(p->hwnd, d);
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t *>(title);
            require(TabCtrl_InsertItem(p->hwnd, int(p->pages.size()), &item) >= 0, L"Tab insertion failed",
                    E_FAIL);
            page->tabs = p;
            page->has_bounds = false;
            p->pages.push_back(page.get());
            p->page_layout();
            *out = page.release();
        });
}
ag_result ag_tab_equal_width(ag_handle h, uint32_t enabled)
{
    return api([&]
    {
        auto p = get<Control>(h, Kind::Control);
        p->ui();
        require(p->type == AG_TABS && enabled <= 1, L"Invalid equal-width tab option");
        p->tab_equal_width = enabled != 0;
        const auto style = GetWindowLongPtrW(p->hwnd, GWL_STYLE);
        SetWindowLongPtrW(p->hwnd, GWL_STYLE, enabled ? style | TCS_FIXEDWIDTH : style & ~LONG_PTR(TCS_FIXEDWIDTH));
        p->page_layout();
        InvalidateRect(p->hwnd, nullptr, TRUE);
    });
}
ag_result ag_status_submit(ag_handle h, const wchar_t *text, uint32_t state)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            require((p->type == AG_STATUS || p->type == AG_INDICATOR) && text && state <= 3,
                    L"Invalid status update");
            size_t n = wcsnlen_s(text, 1024 * 1024 + 1);
            require(n <= 1024 * 1024, L"Status snapshot exceeds 1 Mi characters");
            std::lock_guard lock(p->mutex);
            p->pending.assign(text, n);
            p->pending_state = state;
            p->dirty = true;
            if (!p->notified)
            {
                wincheck(PostMessageW(p->hwnd, submit_message, 0, 0), L"Status notification failed");
                p->notified = true;
            }
        });
}

ag_result ag_window_defer_close(ag_handle h, uint32_t defer)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            p->defer_close = defer != 0;
        });
}
ag_result ag_window_timer(ag_handle h, uint32_t id, uint32_t milliseconds)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(id != 0, L"Timer id must be nonzero");
            if (milliseconds)
                wincheck(SetTimer(p->hwnd, id, milliseconds, nullptr) != 0, L"Timer creation failed");
            else
                KillTimer(p->hwnd, id);
        });
}
ag_result ag_window_title(ag_handle h, const wchar_t *title)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(title, L"Null title");
            wincheck(SetWindowTextW(p->hwnd, title), L"Set title failed");
        });
}
ag_result ag_client_bounds(ag_handle h, ag_rect *bounds)
{
    return api(
        [&]
        {
            auto p = static_cast<Object *>(h);
            require(p && bounds, L"Null bounds request");
            p->ui();
            RECT r{};
            wincheck(GetClientRect(p->hwnd, &r), L"Client bounds unavailable");
            float scale = 96.f / dpi(p->hwnd);
            *bounds = {0, 0, r.right * scale, r.bottom * scale};
        });
}
ag_result ag_control_enabled(ag_handle h, uint32_t enabled)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            EnableWindow(p->hwnd, enabled != 0);
        });
}
ag_result ag_control_text_get(ag_handle h, wchar_t *text, uint32_t capacity, uint32_t *required)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            std::wstring value(size_t(GetWindowTextLengthW(p->hwnd)) + 1, 0);
            value.resize(size_t(GetWindowTextW(p->hwnd, value.data(), int(value.size()))));
            copy_text(value, text, capacity, required);
        });
}
ag_result ag_control_text_extent(ag_handle h, float width_dips, ag_point *size_dips)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_LABEL && size_dips && finite(width_dips) && width_dips > 0 &&
                        width_dips <= 1000000 && IsWindow(p->hwnd),
                    L"Invalid label measurement request");
            const auto style = DWORD(GetWindowLongPtrW(p->hwnd, GWL_STYLE));
            const auto alignment = style & SS_TYPEMASK;
            require(alignment == SS_LEFT || alignment == SS_CENTER || alignment == SS_RIGHT,
                    L"Unsupported native label style");
            const int length = GetWindowTextLengthW(p->hwnd);
            require(length >= 0 && length <= 1048576, L"Label text exceeds measurement bound");
            std::wstring text(size_t(length) + 1, 0);
            text.resize(size_t(GetWindowTextW(p->hwnd, text.data(), int(text.size()))));
            if (text.empty())
            {
                *size_dips = {};
                return;
            }
            struct LabelDc
            {
                HWND hwnd;
                HDC dc;
                HGDIOBJ previous{};
                ~LabelDc()
                {
                    if (previous && previous != HGDI_ERROR)
                        SelectObject(dc, previous);
                    if (dc)
                        ReleaseDC(hwnd, dc);
                }
            } drawing{p->hwnd, GetDC(p->hwnd)};
            wincheck(drawing.dc != nullptr, L"Label device context unavailable");
            const auto font = reinterpret_cast<HFONT>(SendMessageW(p->hwnd, WM_GETFONT, 0, 0));
            require(font != nullptr, L"Label font unavailable");
            drawing.previous = SelectObject(drawing.dc, font);
            wincheck(drawing.previous && drawing.previous != HGDI_ERROR, L"Label font selection failed");
            const float scale = dpi(p->hwnd) / 96.f;
            RECT bounds{0, 0, std::max(1L, std::lround(width_dips * scale)), 0};
            UINT flags = DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS;
            if (alignment == SS_CENTER)
                flags |= DT_CENTER;
            else if (alignment == SS_RIGHT)
                flags |= DT_RIGHT;
            if (style & SS_NOPREFIX)
                flags |= DT_NOPREFIX;
            if (style & SS_EDITCONTROL)
                flags |= DT_EDITCONTROL;
            if (GetWindowLongPtrW(p->hwnd, GWL_EXSTYLE) & WS_EX_RTLREADING)
                flags |= DT_RTLREADING;
            wincheck(DrawTextW(drawing.dc, text.c_str(), int(text.size()), &bounds, flags) != 0,
                     L"Label text measurement failed");
            // CALCRECT can widen a rectangle for an overlong word and then reflow
            // later right-aligned lines against that width. Measure height again
            // against the actual fixed width, with an empty clip region so GDI
            // performs layout without touching the window's pixels.
            const int saved = SaveDC(drawing.dc);
            wincheck(saved != 0, L"Label measurement DC save failed");
            if (IntersectClipRect(drawing.dc, 0, 0, 0, 0) == ERROR)
            {
                RestoreDC(drawing.dc, saved);
                wincheck(false, L"Label measurement clip failed");
            }
            RECT fixed{0, 0, std::max(1L, std::lround(width_dips * scale)), LONG_MAX};
            const int height =
                DrawTextW(drawing.dc, text.c_str(), int(text.size()), &fixed, flags & ~DT_CALCRECT);
            RestoreDC(drawing.dc, saved);
            wincheck(height != 0, L"Label fixed-width measurement failed");
            *size_dips = {bounds.right / scale, height / scale};
        });
}
ag_result ag_control_events(ag_handle h, uint32_t flags)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(!(flags & ~255u), L"Unknown native control event flags");
            require(!(flags & AG_CONTROL_NOTIFY_CHOICE_DECISION) || p->type == AG_CHOICE,
                    L"Choice decisions require a choice control");
            if ((flags ^ p->event_flags) & AG_CONTROL_NOTIFY_CHOICE_DECISION) {
                p->choice_mutation_check();
                require(!p->dropdown_open(), L"Cannot change decision mode while open", HRESULT_FROM_WIN32(ERROR_BUSY));
                p->choice_decision.invalidate();
            }
            require(!(flags & AG_CONTROL_NOTIFY_TEXT) || p->type == AG_EDIT || p->type == AG_NUMERIC,
                    L"Text notifications require an edit control");
            require(!(flags & AG_CONTROL_NOTIFY_DROPDOWN) || p->type == AG_CHOICE,
                    L"Dropdown notifications require a choice");
            require(!(flags & AG_CONTROL_NOTIFY_BUTTON_EDGES) || p->type == AG_BUTTON,
                    L"Button edges require a button");
            if (flags & AG_CONTROL_NOTIFY_BUTTON_EDGES)
                wincheck(SetPropW(p->hwnd, button_edges_property, p), L"Button edge identity failed");
            else
            {
                cancel_button_tree(p->hwnd);
                RemovePropW(p->hwnd, button_edges_property);
            }
            p->event_flags = flags;
        });
}
ag_result ag_control_activity(ag_handle h, uint32_t *flags)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(flags, L"Null control activity output");
            auto focus = GetFocus();
            *flags = (focus == p->hwnd || (focus && IsChild(p->hwnd, focus)) ? AG_ACTIVITY_FOCUSED : 0u) |
                     (p->dropdown_open() ? AG_ACTIVITY_DROPDOWN : 0u);
        });
}
ag_result ag_control_update_begin(ag_handle h)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            p->begin_update();
        });
}
ag_result ag_control_update_end(ag_handle h)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            p->end_update();
        });
}
ag_result ag_choice_replace(ag_handle h, const wchar_t *const *items, uint32_t count, int32_t selected)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            p->choice_mutation_check();
            require(p->type == AG_CHOICE && count <= 65535 && (items || !count) && selected >= -1 &&
                        (selected == -1 || uint32_t(selected) < count),
                    L"Invalid choice replacement");
            require(!p->dropdown_open(), L"Cannot replace an open dropdown", HRESULT_FROM_WIN32(ERROR_BUSY));
            size_t bytes{};
            for (uint32_t i = 0; i < count; ++i)
            {
                require(items[i], L"Null choice string");
                const auto length = wcsnlen_s(items[i], 1048577);
                require(length <= 1048576, L"Choice string exceeds bound");
                bytes += (length + 1) * sizeof(wchar_t);
                require(bytes <= 16 * 1024 * 1024, L"Choice string storage exceeds bound");
            }
            p->begin_update();
            struct EndBatch
            {
                Control *p;
                ~EndBatch()
                {
                    p->end_update();
                }
            } end{p};
            SendMessageW(p->hwnd, CB_RESETCONTENT, 0, 0);
            if (count && SendMessageW(p->hwnd, CB_INITSTORAGE, count, LPARAM(bytes)) == CB_ERRSPACE)
                throw Failure{E_OUTOFMEMORY, L"Choice storage allocation failed"};
            for (uint32_t i = 0; i < count; ++i)
                if (SendMessageW(p->hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i])) < 0)
                {
                    SendMessageW(p->hwnd, CB_RESETCONTENT, 0, 0);
                    throw Failure{E_OUTOFMEMORY, L"Choice population failed"};
                }
            SendMessageW(p->hwnd, CB_SETCURSEL, WPARAM(selected), 0);
        });
}
ag_result ag_choice_preferred_width(ag_handle h, float *width_dips)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_CHOICE && width_dips, L"Invalid choice width query");
            HDC dc = GetDC(p->hwnd);
            wincheck(dc != nullptr, L"Choice measurement DC unavailable");
            struct Dc
            {
                HWND hwnd;
                HDC dc;
                HGDIOBJ old{};
                ~Dc()
                {
                    if (old && old != HGDI_ERROR)
                        SelectObject(dc, old);
                    ReleaseDC(hwnd, dc);
                }
            } drawing{p->hwnd, dc};
            drawing.old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(p->hwnd, WM_GETFONT, 0, 0)));
            wincheck(drawing.old && drawing.old != HGDI_ERROR, L"Choice measurement font unavailable");
            const auto count = SendMessageW(p->hwnd, CB_GETCOUNT, 0, 0);
            require(count >= 0 && count <= 65535, L"Choice count exceeds measurement bound");
            LONG widest{};
            std::wstring text;
            for (LRESULT i = 0; i < count; ++i)
            {
                const auto length = SendMessageW(p->hwnd, CB_GETLBTEXTLEN, WPARAM(i), 0);
                require(length >= 0 && length <= 1048576, L"Choice text exceeds measurement bound");
                text.resize(size_t(length) + 1);
                require(SendMessageW(p->hwnd, CB_GETLBTEXT, WPARAM(i),
                                     reinterpret_cast<LPARAM>(text.data())) >= 0,
                        L"Choice text unavailable");
                SIZE size{};
                wincheck(GetTextExtentPoint32W(dc, text.c_str(), int(length), &size),
                         L"Choice text measurement failed");
                widest = std::max(widest, size.cx);
            }
            COMBOBOXINFO info{sizeof(info)};
            wincheck(GetComboBoxInfo(p->hwnd, &info), L"Native dropdown geometry unavailable");
            RECT client{};
            GetClientRect(p->hwnd, &client);
            const int overhead = std::max(0L, client.right - (info.rcItem.right - info.rcItem.left));
            *width_dips = (widest + overhead) * 96.f / dpi(p->hwnd) + 4;
        });
}
ag_result ag_panel_viewport_notify(ag_handle h, uint32_t enabled)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_PANEL && enabled <= 1, L"Invalid viewport notification request");
            ++p->viewport_serial;
            p->viewport_pending = false;
            p->viewport_enabled = enabled != 0;
            p->notify_viewport(true);
        });
}
ag_result ag_panel_viewport_get(ag_handle h, ag_rect *viewport)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_PANEL && viewport, L"Invalid viewport query");
            RECT r{};
            GetClientRect(p->hwnd, &r);
            const float scale = 96.f / dpi(p->hwnd);
            *viewport = {0, p->scroll * scale, r.right * scale, (p->scroll + r.bottom) * scale};
        });
}
ag_result ag_choice_add(ag_handle h, const wchar_t *text)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            p->choice_mutation_check();
            require(p->type == AG_CHOICE && text, L"Invalid choice item");
            require(SendMessageW(p->hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)) >= 0,
                    L"Choice insertion failed", E_OUTOFMEMORY);
        });
}
ag_result ag_message_box(ag_handle h, void *parent, const wchar_t *title, const wchar_t *text,
                         uint32_t is_error)
{
    return api(
        [&]
        {
            auto c = get<Context>(h, Kind::Context);
            c->ui();
            require(title && text, L"Null message");
            wincheck(MessageBoxW(static_cast<HWND>(parent), text, title,
                                 MB_OK | (is_error ? MB_ICONERROR : MB_ICONINFORMATION)) != 0,
                     L"Message box failed");
        });
}
ag_result ag_file_dialog(ag_handle h, void *parent, uint32_t mode, const wchar_t *title,
                         const wchar_t *filter_name, const wchar_t *filter_pattern,
                         const wchar_t *default_name, ag_file_fn selected, void *user, uint32_t *accepted)
{
    return api(
        [&]
        {
            auto c = get<Context>(h, Kind::Context);
            c->ui();
            require(accepted && selected && (mode <= 2 || mode == 4), L"Invalid file dialog request");
            *accepted = 0;
            ComPtr<IFileDialog> dialog;
            check(CoCreateInstance(mode == 1 ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)),
                  L"File dialog unavailable");
            FILEOPENDIALOGOPTIONS options{};
            check(dialog->GetOptions(&options), L"File dialog options unavailable");
            options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
            if (mode == 2)
                options |= FOS_PICKFOLDERS;
            else if (mode != 1)
                options |= FOS_FILEMUSTEXIST;
            if (mode == 4)
                options |= FOS_ALLOWMULTISELECT;
            check(dialog->SetOptions(options), L"File dialog options rejected");
            if (title)
                check(dialog->SetTitle(title), L"File dialog title rejected");
            if (default_name)
                check(dialog->SetFileName(default_name), L"File dialog filename rejected");
            if (filter_name && filter_pattern && mode != 2)
            {
                COMDLG_FILTERSPEC filter{filter_name, filter_pattern};
                check(dialog->SetFileTypes(1, &filter), L"File filter rejected");
            }
            const HRESULT result = dialog->Show(static_cast<HWND>(parent));
            if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                return;
            check(result, L"File dialog failed");
            const auto emit = [&](IShellItem *item)
            {
                PWSTR path{};
                check(item->GetDisplayName(SIGDN_FILESYSPATH, &path), L"Selected path unavailable");
                // The C callback must not throw, but still release COM memory if a foreign caller does.
                try
                {
                    selected(user, path);
                }
                catch (...)
                {
                    CoTaskMemFree(path);
                    throw;
                }
                CoTaskMemFree(path);
            };
            if (mode == 4)
            {
                ComPtr<IFileOpenDialog> open;
                check(dialog.As(&open), L"Open dialog unavailable");
                ComPtr<IShellItemArray> items;
                check(open->GetResults(&items), L"Selection unavailable");
                DWORD count{};
                check(items->GetCount(&count), L"Selection count unavailable");
                for (DWORD i = 0; i < count; ++i)
                {
                    ComPtr<IShellItem> item;
                    check(items->GetItemAt(i, &item), L"Selection unavailable");
                    emit(item.Get());
                }
            }
            else
            {
                ComPtr<IShellItem> item;
                check(dialog->GetResult(&item), L"Selection unavailable");
                emit(item.Get());
            }
            *accepted = 1;
        });
}
ag_result ag_window_minimum(ag_handle h, uint32_t width, uint32_t height)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(width <= 16384 && height <= 16384, L"Invalid minimum size");
            p->minimum_width = width;
            p->minimum_height = height;
        });
}
ag_result ag_window_export_png(ag_handle h, const wchar_t *path)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(path && *path, L"Missing PNG path");
            RECT r{};
            wincheck(GetClientRect(p->hwnd, &r), L"Window unavailable");
            HDC source = GetDC(p->hwnd), memory = CreateCompatibleDC(source);
            HBITMAP bitmap = CreateCompatibleBitmap(source, r.right, r.bottom);
            if (!source || !memory || !bitmap)
            {
                if (bitmap)
                    DeleteObject(bitmap);
                if (memory)
                    DeleteDC(memory);
                if (source)
                    ReleaseDC(p->hwnd, source);
                throw Failure{E_OUTOFMEMORY, L"Window capture allocation failed"};
            }
            auto old = SelectObject(memory, bitmap);
            const auto printed = PrintWindow(p->hwnd, memory, PW_CLIENTONLY);
            SelectObject(memory, old);
            DeleteDC(memory);
            ReleaseDC(p->hwnd, source);
            ComPtr<IWICBitmap> image;
            const auto result =
                p->context->wic->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &image);
            DeleteObject(bitmap);
            wincheck(printed, L"Native window capture failed");
            check(result, L"Window bitmap unavailable");
            ComPtr<IWICStream> stream;
            check(p->context->wic->CreateStream(&stream), L"PNG stream failed");
            check(stream->InitializeFromFilename(path, GENERIC_WRITE), L"PNG file unavailable");
            ComPtr<IWICBitmapEncoder> encoder;
            check(p->context->wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder),
                  L"PNG encoder failed");
            check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
                  L"PNG encoder initialization failed");
            ComPtr<IWICBitmapFrameEncode> frame;
            check(encoder->CreateNewFrame(&frame, nullptr), L"PNG frame failed");
            check(frame->Initialize(nullptr), L"PNG frame initialization failed");
            check(frame->WriteSource(image.Get(), nullptr), L"PNG write failed");
            check(frame->Commit(), L"PNG frame commit failed");
            check(encoder->Commit(), L"PNG commit failed");
        });
}

ag_result ag_window_refresh(ag_handle h, double multiplier)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(std::isfinite(multiplier) && (multiplier == 0 || (multiplier >= .25 && multiplier <= 4)),
                    L"Invalid refresh multiplier");
            if (multiplier && !p->refresh)
                p->refresh = std::make_unique<RefreshClock>(p->hwnd);
            p->refresh_multiplier = multiplier;
            p->refresh_configure(true);
        });
}
ag_result ag_window_refresh_info(ag_handle h, ag_refresh_info *info)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(info, L"Null refresh info");
            *info = {};
            info->monitor_hz = p->refresh_rate.hz;
            info->target_hz = std::min(1000.0, p->refresh_rate.hz * p->refresh_multiplier);
            info->known = p->refresh_rate.known;
            info->suspended = !p->refresh || !p->refresh->active();
            info->callbacks = p->refresh_callbacks;
            if (p->refresh)
            {
                info->high_resolution = p->refresh->high_resolution();
                info->error = p->refresh->error();
                info->posted = p->refresh->posted();
                info->coalesced = p->refresh->coalesced();
            }
        });
}
ag_result ag_window_post(ag_handle h, uint32_t id)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            wincheck(PostMessageW(p->hwnd, dispatch_message, id, 0), L"Deferred UI dispatch failed");
        });
}
ag_result ag_tray_status(ag_handle h, const wchar_t *left, uint32_t left_state, const wchar_t *right,
                         uint32_t right_state)
{
    return api(
        [&]
        {
            auto p = get<Tray>(h, Kind::Tray);
            p->ui();
            require(left && right && left_state <= 3 && right_state <= 3, L"Invalid tray status");
            require(wcsnlen_s(left, 257) <= 256 && wcsnlen_s(right, 257) <= 256,
                    L"Tray status text exceeds 256 units");
            if (p->status_popup && p->labels[0] == left && p->labels[1] == right &&
                p->states[0] == left_state && p->states[1] == right_state)
                return;
            std::array<std::wstring, 2> text{left, right};
            p->labels.swap(text);
            p->states = {left_state, right_state};
            p->status_popup = true;
        });
}
static Object *window_object(ag_handle h)
{
    auto p = static_cast<Object *>(h);
    require(p, L"Null window object");
    p->ui();
    require(IsWindow(p->hwnd), L"Object has no live window");
    return p;
}
ag_result ag_tooltip_set(ag_handle h, const wchar_t *text)
{
    return api(
        [&]
        {
            if (object(h)->kind == Kind::Tray)
            {
                auto tray = get<Tray>(h, Kind::Tray);
                tray->ui();
                require(text && wcsnlen_s(text, 128) < 128, L"Tray tooltip exceeds 127 UTF-16 units");
                tray->notification.tooltip(text);
                return;
            }
            auto p = window_object(h);
            require(text && wcsnlen_s(text, 4097) <= 4096, L"Invalid tooltip text");
            if (p->tooltip_text == text)
                return;
            if (!*text)
            {
                if (p->tooltip)
                    DestroyWindow(p->tooltip);
                p->tooltip = nullptr;
                p->tooltip_text.clear();
                return;
            }
            p->tooltip_text = text;
            const HWND parent = GetParent(p->hwnd) ? GetParent(p->hwnd) : p->hwnd;
            TOOLINFOW ti{sizeof(ti)};
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = parent;
            ti.uId = reinterpret_cast<UINT_PTR>(p->hwnd);
            ti.lpszText = p->tooltip_text.data();
            if (!p->tooltip)
            {
                p->tooltip = CreateWindowExW(
                    WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, module(), nullptr);
                wincheck(p->tooltip != nullptr, L"Tooltip creation failed");
                wincheck(BOOL(SendMessageW(p->tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti))),
                         L"Tooltip attachment failed");
                SendMessageW(p->tooltip, TTM_SETMAXTIPWIDTH, 0, 520);
                SendMessageW(p->tooltip, WM_SETFONT,
                             reinterpret_cast<WPARAM>(p->context->font(0, false, dpi(p->hwnd))), TRUE);
            }
            else
                SendMessageW(p->tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
        });
}
ag_result ag_visible(ag_handle h, uint32_t visible)
{
    return api(
        [&]
        {
            auto p = window_object(h);
            const bool was_visible = (GetWindowLongPtrW(p->hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
            if (!visible)
                cancel_button_tree(p->hwnd);
            if (p->kind == Kind::Control && static_cast<Control *>(p)->update_depth)
            {
                static_cast<Control *>(p)->update_visible = visible != 0;
                return;
            }
            ShowWindow(p->hwnd, visible ? SW_SHOW : SW_HIDE);
            if (p->kind == Kind::Control)
            {
                const auto c = static_cast<Control *>(p);
                if (c->value_label)
                    ShowWindow(c->value_label, visible ? SW_SHOW : SW_HIDE);
                if (visible && !was_visible)
                    RedrawWindow(c->hwnd, nullptr, nullptr,
                                 RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
        });
}
ag_result ag_focus(ag_handle h)
{
    return api(
        [&]
        {
            auto p = window_object(h);
            SetFocus(p->hwnd);
            require(GetFocus() == p->hwnd, L"Control cannot receive focus", E_FAIL);
        });
}
ag_result ag_pointer_position(ag_handle h, ag_point *position, uint32_t *inside)
{
    return api(
        [&]
        {
            auto p = window_object(h);
            require(position && inside, L"Null pointer result");
            POINT point{};
            RECT r{};
            wincheck(GetCursorPos(&point), L"Pointer position unavailable");
            wincheck(ScreenToClient(p->hwnd, &point), L"Pointer conversion failed");
            GetClientRect(p->hwnd, &r);
            const float scale = 96.f / dpi(p->hwnd);
            *position = {point.x * scale, point.y * scale};
            *inside = IsWindowVisible(p->hwnd) && PtInRect(&r, point);
        });
}
ag_result ag_control_style(ag_handle h, uint32_t flags)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require((flags & ~63u) == 0 && (flags & 48u) != 48u, L"Invalid control style flags");
            require(!(flags & 15u) || p->type == AG_BUTTON, L"Flat/foldout styles require a button");
            if (flags && p->style_flags == flags)
                return;
            auto style = GetWindowLongPtrW(p->hwnd, GWL_STYLE);
            if (p->type == AG_BUTTON)
            {
                style = (style & ~LONG_PTR(BS_TYPEMASK | BS_LEFT | BS_RIGHT | BS_CENTER)) |
                        ((flags & (AG_STYLE_FLAT | AG_STYLE_FOLDOUT)) ? BS_OWNERDRAW : BS_PUSHBUTTON);
                style |= flags & AG_STYLE_ALIGN_RIGHT    ? BS_RIGHT
                         : flags & AG_STYLE_ALIGN_CENTER ? BS_CENTER
                                                         : BS_LEFT;
            }
            else if (p->type == AG_LABEL || p->type == AG_INDICATOR)
                style = (style & ~LONG_PTR(SS_TYPEMASK)) | (flags & AG_STYLE_ALIGN_RIGHT    ? SS_RIGHT
                                                            : flags & AG_STYLE_ALIGN_CENTER ? SS_CENTER
                                                                                            : SS_LEFT);
            else if (p->type == AG_EDIT || p->type == AG_NUMERIC || p->type == AG_STATUS)
                style =
                    (style & ~LONG_PTR(ES_CENTER | ES_RIGHT)) | (flags & AG_STYLE_ALIGN_RIGHT    ? ES_RIGHT
                                                                 : flags & AG_STYLE_ALIGN_CENTER ? ES_CENTER
                                                                                                 : ES_LEFT);
            else
                require(flags == 0, L"Alignment is not supported for this control kind");
            p->style_flags = flags;
            SetWindowLongPtrW(p->hwnd, GWL_STYLE, style);
            SetWindowPos(p->hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            InvalidateRect(p->hwnd, nullptr, TRUE);
        });
}
ag_result ag_control_indeterminate(ag_handle h, uint32_t mixed)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_CHECKBOX, L"Mixed state requires checkbox");
            auto style = GetWindowLongPtrW(p->hwnd, GWL_STYLE);
            SetWindowLongPtrW(p->hwnd, GWL_STYLE,
                              (style & ~LONG_PTR(BS_TYPEMASK)) | (mixed ? BS_3STATE : BS_AUTOCHECKBOX));
            SendMessageW(p->hwnd, BM_SETCHECK, mixed ? BST_INDETERMINATE : BST_UNCHECKED, 0);
        });
}
ag_result ag_choice_clear(ag_handle h)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            p->choice_mutation_check();
            require(p->type == AG_CHOICE, L"Not a choice control");
            SendMessageW(p->hwnd, CB_RESETCONTENT, 0, 0);
        });
}
ag_result ag_choice_open(ag_handle h, uint32_t *open)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_CHOICE && open, L"Invalid dropdown query");
            *open = p->choice_interaction_busy();
        });
}
ag_result ag_panel_scroll_get(ag_handle h, float *position)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_PANEL && position, L"Invalid panel scroll query");
            *position = p->scroll * 96.f / dpi(p->hwnd);
        });
}
ag_result ag_panel_scroll_set(ag_handle h, float position)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            require(p->type == AG_PANEL && finite(position) && position >= 0 && position <= 1000000,
                    L"Invalid panel scroll position");
            p->scroll_to(int(std::lround(position * dpi(p->hwnd) / 96)));
        });
}
ag_result ag_panel_reveal(ag_handle h, void *child_window)
{
    return api(
        [&]
        {
            auto p = get<Control>(h, Kind::Control);
            p->ui();
            const HWND child = child_window ? static_cast<HWND>(child_window) : GetFocus();
            require(p->type == AG_PANEL && IsChild(p->hwnd, child), L"Reveal requires a panel descendant");
            RECT r{}, client{};
            GetWindowRect(child, &r);
            MapWindowPoints(nullptr, p->hwnd, reinterpret_cast<POINT *>(&r), 2);
            GetClientRect(p->hwnd, &client);
            if (r.top < 0)
                p->scroll_to(p->scroll + r.top);
            else if (r.bottom > client.bottom)
                p->scroll_to(p->scroll +
                             (r.bottom - r.top > client.bottom ? r.top : r.bottom - client.bottom));
        });
}
static RECT window_chrome(Window *p)
{
    RECT r{};
    wincheck(AdjustWindowRectExForDpi(&r, DWORD(GetWindowLongPtrW(p->hwnd, GWL_STYLE)),
                                      GetMenu(p->hwnd) != nullptr,
                                      DWORD(GetWindowLongPtrW(p->hwnd, GWL_EXSTYLE)), dpi(p->hwnd)),
             L"Window chrome dimensions unavailable");
    return r;
}
ag_result ag_window_work_area(ag_handle h, ag_rect *available)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(available, L"Null work area");
            MONITORINFO monitor{sizeof(monitor)};
            wincheck(GetMonitorInfoW(MonitorFromWindow(p->hwnd, MONITOR_DEFAULTTONEAREST), &monitor),
                     L"Monitor work area unavailable");
            const auto chrome = window_chrome(p);
            const float scale = 96.f / dpi(p->hwnd);
            *available = {
                0, 0,
                std::max(0L, monitor.rcWork.right - monitor.rcWork.left - (chrome.right - chrome.left)) *
                    scale,
                std::max(0L, monitor.rcWork.bottom - monitor.rcWork.top - (chrome.bottom - chrome.top)) *
                    scale};
        });
}
ag_result ag_window_client_size(ag_handle h, uint32_t width, uint32_t height)
{
    return api(
        [&]
        {
            auto p = get<Window>(h, Kind::Window);
            p->ui();
            require(width && height && width <= 16384 && height <= 16384, L"Invalid client size");
            MONITORINFO monitor{sizeof(monitor)};
            RECT current{};
            wincheck(GetMonitorInfoW(MonitorFromWindow(p->hwnd, MONITOR_DEFAULTTONEAREST), &monitor),
                     L"Monitor work area unavailable");
            wincheck(GetWindowRect(p->hwnd, &current), L"Window bounds unavailable");
            const auto chrome = window_chrome(p);
            const auto &work = monitor.rcWork;
            const auto w = std::min(work.right - work.left,
                                    MulDiv(int(width), dpi(p->hwnd), 96) + chrome.right - chrome.left);
            const auto v = std::min(work.bottom - work.top,
                                    MulDiv(int(height), dpi(p->hwnd), 96) + chrome.bottom - chrome.top);
            wincheck(SetWindowPos(p->hwnd, nullptr, std::clamp(current.left, work.left, work.right - w),
                                  std::clamp(current.top, work.top, work.bottom - v), w, v,
                                  SWP_NOZORDER | SWP_NOACTIVATE),
                     L"Client size change failed");
        });
}
#include "window_additions.inc"

ag_result ag_window_shortcut(ag_handle h,uint32_t key,uint32_t modifiers,uint32_t id){return api([&]{auto w=get<Window>(h,Kind::Window);w->ui();require(key>0&&key<=255&&(modifiers&~uint32_t(7|AG_SHORTCUT_REPEAT))==0,L"Invalid shortcut");auto i=std::find_if(w->shortcuts.begin(),w->shortcuts.end(),[&](auto &v){return v.key==key&&(v.modifiers&7)==(modifiers&7);});if(i!=w->shortcuts.end()){if(!id)w->shortcuts.erase(i);else {i->id=id;i->modifiers=modifiers;}}else if(id){require(w->shortcuts.size()<32,L"Shortcut limit exceeded");w->shortcuts.push_back({key,modifiers,id});}});}
