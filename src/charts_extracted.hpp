// Extracted from moog_connector/include/moog/win_gui_common.h; see docs/provenance.md.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "internal.hpp"

#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <deque>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace ag::charts
{
template <class T> void ReleaseCom(T *&p) noexcept
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}
inline D2D1_COLOR_F D2DColor(COLORREF c, float a = 1) noexcept
{
    return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, a);
}

constexpr int kStatusWindowDefaultWidth = 900;
constexpr int kStatusWindowDefaultHeight = 720;
constexpr int kStatusWindowMinWidth = 760;
constexpr int kStatusWindowMinHeight = 640;
constexpr int kStatusMargin = 10;
constexpr int kStatusControlGap = 8;
constexpr int kStatusControlHeight = 32;
constexpr int kStatusDebugTextHeight = 208;
constexpr int kStatusGraphFrameMs = 33;
constexpr int kStatusTextRefreshMs = 250;

constexpr int kConfigTooltipInitialDelayMs = 250;
constexpr int kConfigTooltipReshowDelayMs = 80;
constexpr int kConfigTooltipAutopopDelayMs = 10000;
constexpr int kConfigTooltipMaxWidth = 520;

constexpr float kDefaultGraphScale = 0.20f;
constexpr float kMaximumGraphScale = 0.50f;
constexpr float kGraphScaleHeadroom = 1.06f;
constexpr float kGraphScaleReturnTauSeconds = 1.75f;
constexpr float kStandardMotionGraphLinearScaleG = 0.30f;
constexpr float kStandardMotionGraphAngularScaleDegS2 = 30.0f;
constexpr std::size_t kGraphAxisColorCount = 3;
constexpr std::size_t kMaxGraphSeries = 18;

enum class GraphLinePattern : std::uint8_t
{
    Solid = 0,
    Dashed = 1,
    LongDashed = 2,
    ShortDashed = 3,
    DashDot = 4,
    TenPixelDash = 5,
    Dotted = 6
};

inline constexpr std::size_t kGraphLinePatternCount = static_cast<std::size_t>(GraphLinePattern::Dotted) + 1u;
static_assert(static_cast<std::uint8_t>(GraphLinePattern::Solid) == 0u);
static_assert(static_cast<std::uint8_t>(GraphLinePattern::Dashed) == 1u);
static_assert(static_cast<std::uint8_t>(GraphLinePattern::LongDashed) == 2u);
static_assert(static_cast<std::uint8_t>(GraphLinePattern::ShortDashed) == 3u);
static_assert(static_cast<std::uint8_t>(GraphLinePattern::DashDot) == 4u);
static_assert(static_cast<std::uint8_t>(GraphLinePattern::TenPixelDash) == 5u);
static_assert(static_cast<std::uint8_t>(GraphLinePattern::Dotted) == 6u);

constexpr float kStandardMotionGraphStrokeWidth = 2.0f;
constexpr GraphLinePattern kStandardTranslationalGraphLinePattern = GraphLinePattern::Solid;
constexpr GraphLinePattern kStandardRotationalGraphLinePattern = GraphLinePattern::TenPixelDash;
// Direct2D custom dash lengths are multiples of stroke width. The standard
// 2 px motion trace therefore renders this 5/5 pattern as 10 px on/off.
inline constexpr std::array<float, 2> kTenPixelDashPattern{5.0f, 5.0f};

enum class GraphVerticalScaleMode : std::uint8_t
{
    Linear = 0,
    SymmetricLogarithmic = 1
};

struct GraphAxisTick
{
    float value = 0.0f;
    const wchar_t *label = nullptr;
};

struct GraphSeriesStyle
{
    std::uint8_t axis_color_index = 0;
    GraphLinePattern line_pattern = GraphLinePattern::Solid;
    float stroke_width = kStandardMotionGraphStrokeWidth;
};

inline constexpr std::array<const wchar_t *, 3> kStandardTranslationalAxisLabels{L"X/Surge", L"Y/Sway",
                                                                                 L"Z/Heave"};
inline constexpr std::array<const wchar_t *, 6> kStandardMotionAxisLabels{L"X/Surge", L"Y/Sway", L"Z/Heave",
                                                                          L"Roll",    L"Pitch",  L"Yaw"};

constexpr GraphSeriesStyle TranslationalGraphSeriesStyle(
    std::size_t axis, GraphLinePattern line_pattern = kStandardTranslationalGraphLinePattern,
    float stroke_width = kStandardMotionGraphStrokeWidth) noexcept
{
    return {static_cast<std::uint8_t>(axis % kGraphAxisColorCount), line_pattern, stroke_width};
}

constexpr GraphSeriesStyle AngularGraphSeriesStyle(
    std::size_t axis, float stroke_width = kStandardMotionGraphStrokeWidth) noexcept
{
    return {static_cast<std::uint8_t>(axis % kGraphAxisColorCount), kStandardRotationalGraphLinePattern,
            stroke_width};
}

constexpr GraphSeriesStyle StandardMotionGraphSeriesStyle(std::size_t series) noexcept
{
    const std::size_t motion_axis = series % 6u;
    return motion_axis < 3u ? TranslationalGraphSeriesStyle(motion_axis)
                            : AngularGraphSeriesStyle(motion_axis - 3u);
}

inline constexpr std::array<GraphSeriesStyle, 3> kStandardTranslationalGraphSeriesStyles{
    TranslationalGraphSeriesStyle(0u), TranslationalGraphSeriesStyle(1u), TranslationalGraphSeriesStyle(2u)};

inline constexpr std::array<GraphSeriesStyle, 6> kStandardMotionGraphSeriesStyles{
    StandardMotionGraphSeriesStyle(0u), StandardMotionGraphSeriesStyle(1u),
    StandardMotionGraphSeriesStyle(2u), StandardMotionGraphSeriesStyle(3u),
    StandardMotionGraphSeriesStyle(4u), StandardMotionGraphSeriesStyle(5u)};

static_assert(kStandardMotionGraphSeriesStyles[0].line_pattern == GraphLinePattern::Solid);
static_assert(kStandardMotionGraphSeriesStyles[1].line_pattern == GraphLinePattern::Solid);
static_assert(kStandardMotionGraphSeriesStyles[2].line_pattern == GraphLinePattern::Solid);
static_assert(kStandardMotionGraphSeriesStyles[3].line_pattern == GraphLinePattern::TenPixelDash);
static_assert(kStandardMotionGraphSeriesStyles[4].line_pattern == GraphLinePattern::TenPixelDash);
static_assert(kStandardMotionGraphSeriesStyles[5].line_pattern == GraphLinePattern::TenPixelDash);
static_assert(kStandardMotionGraphStrokeWidth * kTenPixelDashPattern[0] == 10.0f);
static_assert(kStandardMotionGraphStrokeWidth * kTenPixelDashPattern[1] == 10.0f);

inline COLORREF StatusSeriesColorRef(std::size_t index) noexcept
{
    constexpr std::array<COLORREF, kGraphAxisColorCount> kColors{RGB(210, 58, 62), RGB(42, 140, 76),
                                                                 RGB(42, 102, 198)};
    return kColors[index % kColors.size()];
}

inline D2D1_COLOR_F StatusSeriesColor(std::size_t index) noexcept
{
    return D2DColor(StatusSeriesColorRef(index));
}

struct StatusIndicator
{
    const wchar_t *label = nullptr;
    bool ok = false;
    // A non-faulted stage that is still starting, reacquiring, or awaiting its
    // first observation. Existing two-field initializers remain green/red.
    bool waiting = false;
};

struct TimeSeriesGraphSample
{
    double age_seconds = 0.0;
    std::array<float, kMaxGraphSeries> values{};
    std::uint32_t valid_mask = 0xFFFFFFFFu;
};

struct TimeSeriesGraphPanel
{
    const wchar_t *title = nullptr;
    const wchar_t *annotation_text = nullptr;
    const wchar_t *waiting_text = L"No telemetry yet";
    const wchar_t *no_signal_text = L"No nonzero signal";
    const wchar_t *scale_suffix = L"";
    const wchar_t *vertical_unit_label = nullptr;
    const wchar_t *horizontal_axis_label = nullptr;
    const wchar_t *const *series_labels = nullptr;
    const GraphSeriesStyle *series_styles = nullptr;
    const GraphAxisTick *horizontal_ticks = nullptr;
    const TimeSeriesGraphSample *samples = nullptr;
    std::size_t horizontal_tick_count = 0;
    std::size_t sample_count = 0;
    std::size_t series_count = 0;
    double time_window_seconds = 15.0;
    float default_scale = kDefaultGraphScale;
    float maximum_scale = kMaximumGraphScale;
    float symmetric_log_linear_threshold = 0.01f;
    float legend_swatch_width = 10.0f;
    std::size_t layout_column = 0u;
    float height_weight = 1.0f;
    float footer_height = 0.0f;
    GraphVerticalScaleMode vertical_scale_mode = GraphVerticalScaleMode::Linear;
    std::uint32_t series_background_underlay_mask = 0u;
    bool unipolar_zero_floor = false;
    bool show_no_signal_text = false;
    bool show_axis_direction_arrows = false;
    bool draw_series_in_reverse_order = false;
};

class NativeTooltipManager
{
  public:
    NativeTooltipManager() = default;
    NativeTooltipManager(const NativeTooltipManager &) = delete;
    NativeTooltipManager &operator=(const NativeTooltipManager &) = delete;

    ~NativeTooltipManager()
    {
        Destroy();
    }

    HWND Ensure(HWND parent)
    {
        if (tooltip_ != nullptr && IsWindow(tooltip_))
        {
            return tooltip_;
        }

        tooltip_ = CreateWindowExW(
            WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (tooltip_ != nullptr)
        {
            SendMessageW(tooltip_, TTM_ACTIVATE, TRUE, 0);
            SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_INITIAL, kConfigTooltipInitialDelayMs);
            SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_RESHOW, kConfigTooltipReshowDelayMs);
            SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_AUTOPOP, kConfigTooltipAutopopDelayMs);
            SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, kConfigTooltipMaxWidth);
            SetWindowPos(tooltip_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return tooltip_;
    }

    void Add(HWND parent, HWND control, const wchar_t *text)
    {
        if (parent == nullptr || control == nullptr || text == nullptr || text[0] == L'\0')
        {
            return;
        }
        HWND tooltip = Ensure(parent);
        if (tooltip == nullptr)
        {
            return;
        }

        Remove(control);
        tooltip_texts_.emplace_back(text);
        const wchar_t *stable_text = tooltip_texts_.back().c_str();
        control_text_[control] = stable_text;

        TOOLINFOW tool{};
        tool.cbSize = ToolInfoSize();
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = parent;
        tool.uId = reinterpret_cast<UINT_PTR>(control);
        tool.lpszText = const_cast<LPWSTR>(stable_text);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    void Remove(HWND control)
    {
        if (tooltip_ == nullptr || control == nullptr)
        {
            return;
        }
        TOOLINFOW tool{};
        tool.cbSize = ToolInfoSize();
        tool.uFlags = TTF_IDISHWND;
        tool.hwnd = GetParent(control);
        tool.uId = reinterpret_cast<UINT_PTR>(control);
        SendMessageW(tooltip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        control_text_.erase(control);
    }

    void Hide()
    {
        if (tooltip_ != nullptr)
        {
            SendMessageW(tooltip_, TTM_POP, 0, 0);
        }
    }

    void Clear()
    {
        control_text_.clear();
        tooltip_texts_.clear();
    }

    void Destroy()
    {
        if (tooltip_ != nullptr)
        {
            DestroyWindow(tooltip_);
            tooltip_ = nullptr;
        }
        Clear();
    }

    HWND Handle() const noexcept
    {
        return tooltip_;
    }

  private:
    static UINT ToolInfoSize() noexcept
    {
#ifdef TTTOOLINFOW_V2_SIZE
        return TTTOOLINFOW_V2_SIZE;
#else
        return sizeof(TOOLINFOW);
#endif
    }

    HWND tooltip_ = nullptr;
    std::deque<std::wstring> tooltip_texts_{};
    std::unordered_map<HWND, const wchar_t *> control_text_{};
};

class D2DStatusGraphRenderer
{
  public:
    explicit D2DStatusGraphRenderer(ag::Context *context) : context_(context) {}
    void Reset()
    {
        DiscardDeviceResources();
    }
    HRESULT UseTarget(ID2D1RenderTarget *target)
    {
        DiscardDeviceResources();
        HRESULT hr = EnsureIndependentResources();
        if (FAILED(hr))
            return hr;
        render_target_ = target;
        target->AddRef();
        return CreateTargetBrushes();
    }
    ag::Context *context_;
    D2DStatusGraphRenderer(const D2DStatusGraphRenderer &) = delete;
    D2DStatusGraphRenderer &operator=(const D2DStatusGraphRenderer &) = delete;

    ~D2DStatusGraphRenderer()
    {
        DiscardDeviceResources();
        for (ID2D1StrokeStyle *&stroke_style : stroke_styles_)
        {
            ReleaseCom(stroke_style);
        }
        ReleaseCom(text_format_left_);
        ReleaseCom(text_format_right_);
        ReleaseCom(text_format_center_);
        ReleaseCom(small_text_format_left_);
        ReleaseCom(axis_text_format_right_);
        ReleaseCom(write_factory_);
        ReleaseCom(factory_);
    }

    HRESULT Render(HWND window, const TimeSeriesGraphPanel *panels, std::size_t panel_count,
                   const StatusIndicator *indicators = nullptr, std::size_t indicator_count = 0,
                   float bottom_reserved_height = 0.0f)
    {
        HRESULT result = EnsureDeviceResources(window);
        if (FAILED(result))
        {
            return result;
        }

        RECT client{};
        GetClientRect(window, &client);
        const float width = render_target_->GetSize().width;
        const float height = render_target_->GetSize().height;
        const float content_height = std::max(1.0f, height - std::max(0.0f, bottom_reserved_height));

        UpdatePanelScales(panels, panel_count);

        render_target_->BeginDraw();
        render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
        render_target_->Clear(ag::color(context_->theme.background));

        const float indicator_header_height = indicator_count > 0u ? 24.0f : 0.0f;
        const float panel_gap = 6.0f;
        const float column_gap = 8.0f;
        std::size_t column_count = 1u;
        for (std::size_t panel_index = 0; panel_index < panel_count; ++panel_index)
        {
            const std::size_t safe_column = std::min(panels[panel_index].layout_column, panel_count - 1u);
            column_count = std::max(column_count, safe_column + 1u);
        }

        column_weights_.assign(column_count, 0.0f);
        column_panel_counts_.assign(column_count, 0u);
        column_placed_counts_.assign(column_count, 0u);
        column_next_tops_.assign(column_count, indicator_header_height);
        for (std::size_t panel_index = 0; panel_index < panel_count; ++panel_index)
        {
            const std::size_t column = std::min(panels[panel_index].layout_column, column_count - 1u);
            column_weights_[column] += PositiveFiniteOr(panels[panel_index].height_weight, 1.0f);
            ++column_panel_counts_[column];
        }

        const float total_column_gap =
            column_gap * static_cast<float>(column_count > 0u ? column_count - 1u : 0u);
        const float available_width = std::max(1.0f, width - total_column_gap);
        const float column_width = available_width / static_cast<float>(column_count);

        panel_rects_.resize(panel_count);
        for (std::size_t panel_index = 0; panel_index < panel_count; ++panel_index)
        {
            const std::size_t column = std::min(panels[panel_index].layout_column, column_count - 1u);
            const float total_panel_gap =
                panel_gap * static_cast<float>(
                                column_panel_counts_[column] > 0u ? column_panel_counts_[column] - 1u : 0u);
            const float available_height =
                std::max(1.0f, content_height - indicator_header_height - total_panel_gap);
            const float top = column_next_tops_[column];
            const bool last_in_column = column_placed_counts_[column] + 1u == column_panel_counts_[column];
            const float panel_height =
                last_in_column
                    ? std::max(1.0f, content_height - top)
                    : available_height * (PositiveFiniteOr(panels[panel_index].height_weight, 1.0f) /
                                          std::max(1.0f, column_weights_[column]));
            const float left = static_cast<float>(column) * (column_width + column_gap);
            const float right = column + 1u == column_count ? width : std::min(width, left + column_width);
            const D2D1_RECT_F panel_rect =
                D2D1::RectF(left, top, right, std::min(content_height, top + panel_height));
            panel_rects_[panel_index] = panel_rect;
            const float scale = panel_index < panel_scales_.size() ? panel_scales_[panel_index]
                                                                   : panels[panel_index].default_scale;
            DrawPanel(panel_rect, panels[panel_index], scale);
            column_next_tops_[column] = panel_rect.bottom + panel_gap;
            ++column_placed_counts_[column];
        }
        DrawStatusIndicators(indicators, indicator_count, width);

        result = render_target_->EndDraw();
        if (result == D2DERR_RECREATE_TARGET)
        {
            DiscardDeviceResources();
        }
        return result;
    }

    void Resize(HWND window) noexcept
    {
        if (render_target_ == nullptr)
        {
            return;
        }
        RECT client{};
        GetClientRect(window, &client);
        const D2D1_SIZE_U size =
            D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, client.right - client.left)),
                        static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top)));
        ag::ComPtr<ID2D1HwndRenderTarget> target;
        if (SUCCEEDED(render_target_->QueryInterface(IID_PPV_ARGS(&target))))
            target->Resize(size);
        render_target_->SetDpi(float(GetDpiForWindow(window)), float(GetDpiForWindow(window)));
    }

    bool PanelRect(std::size_t panel_index, D2D1_RECT_F &panel_rect) const noexcept
    {
        if (panel_index >= panel_rects_.size())
        {
            return false;
        }
        panel_rect = panel_rects_[panel_index];
        return true;
    }

  private:
    HRESULT EnsureIndependentResources()
    {
        HRESULT result = S_OK;
        if (factory_ == nullptr)
        {
            factory_ = context_->d2d.Get();
            factory_->AddRef();
            result = S_OK;
            if (FAILED(result))
            {
                return result;
            }
        }
        if (stroke_styles_[static_cast<std::size_t>(GraphLinePattern::Dashed)] == nullptr)
        {
            static constexpr FLOAT kDashes[] = {6.0f, 4.0f};
            result = CreateStrokeStyle(GraphLinePattern::Dashed, kDashes, std::size(kDashes));
        }
        if (SUCCEEDED(result) &&
            stroke_styles_[static_cast<std::size_t>(GraphLinePattern::LongDashed)] == nullptr)
        {
            static constexpr FLOAT kDashes[] = {12.0f, 5.0f};
            result = CreateStrokeStyle(GraphLinePattern::LongDashed, kDashes, std::size(kDashes));
        }
        if (SUCCEEDED(result) &&
            stroke_styles_[static_cast<std::size_t>(GraphLinePattern::ShortDashed)] == nullptr)
        {
            static constexpr FLOAT kDashes[] = {3.0f, 3.0f};
            result = CreateStrokeStyle(GraphLinePattern::ShortDashed, kDashes, std::size(kDashes));
        }
        if (SUCCEEDED(result) &&
            stroke_styles_[static_cast<std::size_t>(GraphLinePattern::DashDot)] == nullptr)
        {
            static constexpr FLOAT kDashes[] = {8.0f, 3.0f, 1.0f, 3.0f};
            result = CreateStrokeStyle(GraphLinePattern::DashDot, kDashes, std::size(kDashes));
        }
        if (SUCCEEDED(result) &&
            stroke_styles_[static_cast<std::size_t>(GraphLinePattern::TenPixelDash)] == nullptr)
        {
            result = CreateStrokeStyle(GraphLinePattern::TenPixelDash, kTenPixelDashPattern.data(),
                                       static_cast<UINT32>(kTenPixelDashPattern.size()));
        }
        if (SUCCEEDED(result) &&
            stroke_styles_[static_cast<std::size_t>(GraphLinePattern::Dotted)] == nullptr)
        {
            result = CreateStrokeStyle(GraphLinePattern::Dotted, nullptr, 0u, D2D1_DASH_STYLE_DOT,
                                       D2D1_CAP_STYLE_ROUND);
            ID2D1StrokeStyle *const dotted =
                stroke_styles_[static_cast<std::size_t>(GraphLinePattern::Dotted)];
            if (SUCCEEDED(result) && (dotted == nullptr || dotted->GetStartCap() != D2D1_CAP_STYLE_ROUND ||
                                      dotted->GetEndCap() != D2D1_CAP_STYLE_ROUND ||
                                      dotted->GetDashCap() != D2D1_CAP_STYLE_ROUND ||
                                      dotted->GetDashStyle() != D2D1_DASH_STYLE_DOT))
            {
                ReleaseCom(stroke_styles_[static_cast<std::size_t>(GraphLinePattern::Dotted)]);
                result = E_FAIL;
            }
        }
        if (FAILED(result))
        {
            return result;
        }
        if (write_factory_ == nullptr)
        {
            result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown **>(&write_factory_));
            if (FAILED(result))
            {
                return result;
            }
        }
        if (text_format_left_ == nullptr)
        {
            result = CreateTextFormat(12.0f, DWRITE_TEXT_ALIGNMENT_LEADING, text_format_left_);
        }
        if (SUCCEEDED(result) && text_format_right_ == nullptr)
        {
            result = CreateTextFormat(12.0f, DWRITE_TEXT_ALIGNMENT_TRAILING, text_format_right_);
        }
        if (SUCCEEDED(result) && text_format_center_ == nullptr)
        {
            result = CreateTextFormat(12.0f, DWRITE_TEXT_ALIGNMENT_CENTER, text_format_center_);
        }
        if (SUCCEEDED(result) && small_text_format_left_ == nullptr)
        {
            result = CreateTextFormat(11.0f, DWRITE_TEXT_ALIGNMENT_LEADING, small_text_format_left_);
        }
        if (SUCCEEDED(result) && axis_text_format_right_ == nullptr)
        {
            result = CreateTextFormat(10.0f, DWRITE_TEXT_ALIGNMENT_TRAILING, axis_text_format_right_);
        }
        return result;
    }

    HRESULT EnsureDeviceResources(HWND window)
    {
        HRESULT result = EnsureIndependentResources();
        if (FAILED(result) || render_target_ != nullptr)
        {
            return result;
        }

        RECT client{};
        GetClientRect(window, &client);
        const D2D1_SIZE_U size =
            D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, client.right - client.left)),
                        static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top)));
        const D2D1_RENDER_TARGET_PROPERTIES render_props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
            0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props =
            D2D1::HwndRenderTargetProperties(window, size, D2D1_PRESENT_OPTIONS_NONE);

        ID2D1HwndRenderTarget *window_target = nullptr;
        result = factory_->CreateHwndRenderTarget(render_props, hwnd_props, &window_target);
        render_target_ = window_target;
        if (FAILED(result))
        {
            return result;
        }

        render_target_->SetDpi(float(GetDpiForWindow(window)), float(GetDpiForWindow(window)));
        return CreateTargetBrushes();
    }

    HRESULT CreateTargetBrushes()
    {
        HRESULT result = CreateBrush(D2D1::ColorF(0xFFFFFF), panel_brush_);
        if (SUCCEEDED(result))
        {
            result = CreateBrush(ag::color(context_->theme.foreground), text_brush_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateBrush(ag::color(context_->theme.muted), muted_text_brush_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateBrush(D2D1::ColorF(0xE0E0E0), grid_brush_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateBrush(D2D1::ColorF(0x969696), axis_brush_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateBrush(D2D1::ColorF(0x16854B), status_ok_brush_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateBrush(D2D1::ColorF(0xC27A00), status_waiting_brush_);
        }
        if (SUCCEEDED(result))
        {
            result = CreateBrush(D2D1::ColorF(0xB42318), status_bad_brush_);
        }
        for (std::size_t axis = 0; SUCCEEDED(result) && axis < axis_brushes_.size(); ++axis)
        {
            result = CreateBrush(StatusSeriesColor(axis), axis_brushes_[axis]);
        }
        if (FAILED(result))
        {
            DiscardDeviceResources();
        }
        return result;
    }

    HRESULT CreateTextFormat(float size, DWRITE_TEXT_ALIGNMENT alignment, IDWriteTextFormat *&target)
    {
        HRESULT result = write_factory_->CreateTextFormat(
            context_->theme.font_family,
            wcscmp(context_->theme.font_family, L"B612") == 0 ? context_->collection.Get() : nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"",
            &target);
        if (SUCCEEDED(result))
        {
            target->SetTextAlignment(alignment);
            target->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        return result;
    }

    HRESULT CreateBrush(const D2D1_COLOR_F &color, ID2D1SolidColorBrush *&brush)
    {
        return render_target_->CreateSolidColorBrush(color, &brush);
    }

    HRESULT CreateStrokeStyle(GraphLinePattern pattern, const FLOAT *dashes, UINT32 dash_count,
                              D2D1_DASH_STYLE dash_style = D2D1_DASH_STYLE_CUSTOM,
                              D2D1_CAP_STYLE cap_style = D2D1_CAP_STYLE_FLAT)
    {
        const std::size_t index = static_cast<std::size_t>(pattern);
        const bool custom_dash = dash_style == D2D1_DASH_STYLE_CUSTOM;
        if (factory_ == nullptr || index >= stroke_styles_.size() ||
            (custom_dash && (dashes == nullptr || dash_count == 0u)) ||
            (!custom_dash && (dashes != nullptr || dash_count != 0u)))
        {
            return E_INVALIDARG;
        }
        return factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(cap_style, cap_style, cap_style,
                                                                       D2D1_LINE_JOIN_MITER, 10.0f,
                                                                       dash_style, 0.0f),
                                           dashes, dash_count, &stroke_styles_[index]);
    }

    ID2D1StrokeStyle *StrokeStyle(GraphLinePattern pattern) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(pattern);
        return index < stroke_styles_.size() ? stroke_styles_[index] : nullptr;
    }

    void DiscardDeviceResources() noexcept
    {
        for (ID2D1SolidColorBrush *&brush : axis_brushes_)
        {
            ReleaseCom(brush);
        }
        ReleaseCom(axis_brush_);
        ReleaseCom(grid_brush_);
        ReleaseCom(muted_text_brush_);
        ReleaseCom(text_brush_);
        ReleaseCom(panel_brush_);
        ReleaseCom(status_bad_brush_);
        ReleaseCom(status_waiting_brush_);
        ReleaseCom(status_ok_brush_);
        ReleaseCom(render_target_);
    }

    struct ScaleBounds
    {
        float minimum = kDefaultGraphScale;
        float maximum = kMaximumGraphScale;
    };

    static float PositiveFiniteOr(float value, float fallback) noexcept
    {
        return std::isfinite(value) && value > 0.0f ? value : fallback;
    }

    static ScaleBounds PanelScaleBounds(const TimeSeriesGraphPanel &panel) noexcept
    {
        ScaleBounds bounds{};
        bounds.minimum = std::max(0.001f, PositiveFiniteOr(panel.default_scale, kDefaultGraphScale));
        bounds.maximum = std::max(bounds.minimum, PositiveFiniteOr(panel.maximum_scale, kMaximumGraphScale));
        return bounds;
    }

    static float SafeClamp(float value, float minimum, float maximum) noexcept
    {
        const float safe_minimum = std::isfinite(minimum) ? minimum : std::max(0.001f, kDefaultGraphScale);
        const float safe_maximum = std::max(safe_minimum, std::isfinite(maximum) ? maximum : safe_minimum);
        const float safe_value = std::isfinite(value) ? value : safe_minimum;
        return std::clamp(safe_value, safe_minimum, safe_maximum);
    }

    void UpdatePanelScales(const TimeSeriesGraphPanel *panels, std::size_t panel_count)
    {
        LARGE_INTEGER frequency{};
        LARGE_INTEGER now{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&now);

        double dt = 1.0 / 30.0;
        if (last_scale_update_qpc_ != 0 && frequency.QuadPart > 0 && now.QuadPart >= last_scale_update_qpc_)
        {
            dt = static_cast<double>(now.QuadPart - last_scale_update_qpc_) /
                 static_cast<double>(frequency.QuadPart);
        }
        last_scale_update_qpc_ = now.QuadPart;
        dt = std::clamp(dt, 0.0, 0.5);

        if (panel_scales_.size() < panel_count)
        {
            panel_scales_.resize(panel_count, kDefaultGraphScale);
        }

        const float return_alpha =
            std::clamp(1.0f - std::exp(-static_cast<float>(dt) / kGraphScaleReturnTauSeconds), 0.0f, 1.0f);
        for (std::size_t panel_index = 0; panel_index < panel_count; ++panel_index)
        {
            const TimeSeriesGraphPanel &panel = panels[panel_index];
            const ScaleBounds bounds = PanelScaleBounds(panel);
            float desired = bounds.minimum;
            const std::size_t series_count = std::min(panel.series_count, kMaxGraphSeries);
            for (std::size_t sample_index = 0; sample_index < panel.sample_count; ++sample_index)
            {
                const TimeSeriesGraphSample &sample = panel.samples[sample_index];
                if (sample.age_seconds < 0.0 || sample.age_seconds > panel.time_window_seconds)
                {
                    continue;
                }
                for (std::size_t series = 0; series < series_count; ++series)
                {
                    if ((sample.valid_mask & (1u << series)) == 0u)
                    {
                        continue;
                    }
                    if (!std::isfinite(sample.values[series]))
                    {
                        continue;
                    }
                    const float magnitude = panel.unipolar_zero_floor ? std::max(0.0f, sample.values[series])
                                                                      : std::abs(sample.values[series]);
                    desired = std::max(desired, magnitude * kGraphScaleHeadroom);
                }
            }
            desired = SafeClamp(desired, bounds.minimum, bounds.maximum);
            float &scale = panel_scales_[panel_index];
            if (!std::isfinite(scale) || scale <= 0.0f)
            {
                scale = bounds.minimum;
            }
            if (desired >= scale)
            {
                scale = desired;
            }
            else
            {
                scale += (desired - scale) * return_alpha;
            }
            scale = SafeClamp(scale, bounds.minimum, bounds.maximum);
        }
    }

    static float SymmetricLogNormalized(float value, float scale, float linear_threshold) noexcept
    {
        const float safe_scale = std::max(0.001f, scale);
        const float safe_threshold = std::clamp(
            std::isfinite(linear_threshold) ? linear_threshold : safe_scale * 0.1f, 0.000001f, safe_scale);
        const float finite_value = std::isfinite(value) ? value : 0.0f;
        const float magnitude = std::min(std::abs(finite_value), safe_scale);
        const float denominator = std::log1p(safe_scale / safe_threshold);
        const float normalized =
            denominator > 0.0f ? std::log1p(magnitude / safe_threshold) / denominator : 0.0f;
        return std::copysign(std::clamp(normalized, 0.0f, 1.0f), finite_value);
    }

    static float GraphY(float value, float scale, const D2D1_RECT_F &plot, GraphVerticalScaleMode scale_mode,
                        float symmetric_log_linear_threshold) noexcept
    {
        const float safe_scale = std::max(0.001f, scale);
        const float height = std::max(1.0f, plot.bottom - plot.top);
        const float center = plot.top + height * 0.5f;
        const float half_height = height * 0.45f;
        const float finite_value = std::isfinite(value) ? value : 0.0f;
        const float normalized =
            scale_mode == GraphVerticalScaleMode::SymmetricLogarithmic
                ? SymmetricLogNormalized(finite_value, safe_scale, symmetric_log_linear_threshold)
                : SafeClamp(finite_value / safe_scale, -1.0f, 1.0f);
        return center - normalized * half_height;
    }

    static float GraphYUnipolar(float value, float scale, const D2D1_RECT_F &plot) noexcept
    {
        const float safe_scale = std::max(0.001f, scale);
        const float height = std::max(1.0f, plot.bottom - plot.top);
        const float finite_value = std::isfinite(value) ? value : 0.0f;
        const float normalized = std::clamp(finite_value / safe_scale, 0.0f, 1.0f);
        return plot.bottom - normalized * height;
    }

    static float GraphYForPanel(float value, float scale, const D2D1_RECT_F &plot,
                                const TimeSeriesGraphPanel &panel) noexcept
    {
        return panel.unipolar_zero_floor ? GraphYUnipolar(value, scale, plot)
                                         : GraphY(value, scale, plot, panel.vertical_scale_mode,
                                                  panel.symmetric_log_linear_threshold);
    }

    static UINT32 TextLength(const wchar_t *text) noexcept
    {
        return text != nullptr ? static_cast<UINT32>(std::wcslen(text)) : 0u;
    }

    void DrawTextClipped(const wchar_t *text, IDWriteTextFormat *format, const D2D1_RECT_F &rect,
                         ID2D1Brush *brush)
    {
        if (text == nullptr || format == nullptr || brush == nullptr)
        {
            return;
        }
        render_target_->DrawText(text, TextLength(text), format, rect, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP,
                                 DWRITE_MEASURING_MODE_NATURAL);
    }

    void DrawStatusIndicator(const wchar_t *label, bool ok, bool waiting, float x, float y)
    {
        if (label == nullptr || status_ok_brush_ == nullptr || status_waiting_brush_ == nullptr ||
            status_bad_brush_ == nullptr)
        {
            return;
        }
        ID2D1SolidColorBrush *brush =
            ok ? status_ok_brush_ : (waiting ? status_waiting_brush_ : status_bad_brush_);
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 6.0f, y + 8.0f), 5.0f, 5.0f), brush);
        DrawTextClipped(label, small_text_format_left_, D2D1::RectF(x + 16.0f, y, x + 132.0f, y + 18.0f),
                        text_brush_);
    }

    void DrawStatusIndicators(const StatusIndicator *indicators, std::size_t indicator_count, float width)
    {
        if (indicators == nullptr || indicator_count == 0)
        {
            return;
        }
        constexpr float kIndicatorWidth = 132.0f;
        const float start =
            std::max(8.0f, width - (static_cast<float>(indicator_count) * kIndicatorWidth) - 8.0f);
        for (std::size_t index = 0; index < indicator_count; ++index)
        {
            DrawStatusIndicator(indicators[index].label, indicators[index].ok, indicators[index].waiting,
                                start + static_cast<float>(index) * kIndicatorWidth, 4.0f);
        }
    }

    void DrawPanel(const D2D1_RECT_F &panel_rect, const TimeSeriesGraphPanel &panel, float scale)
    {
        const D2D1_RECT_F inset_panel = D2D1::RectF(panel_rect.left + 0.5f, panel_rect.top + 0.5f,
                                                    panel_rect.right - 0.5f, panel_rect.bottom - 0.5f);
        render_target_->FillRectangle(inset_panel, panel_brush_);
        render_target_->DrawRectangle(inset_panel, grid_brush_, 1.0f);

        DrawTextClipped(panel.title, text_format_left_,
                        D2D1::RectF(panel_rect.left + 6.0f, panel_rect.top + 1.0f, panel_rect.right - 6.0f,
                                    panel_rect.top + 20.0f),
                        text_brush_);

        if (panel.annotation_text != nullptr)
        {
            const D2D1_RECT_F text_rect = D2D1::RectF(panel_rect.left + 10.0f, panel_rect.top + 24.0f,
                                                      panel_rect.right - 10.0f, panel_rect.bottom - 6.0f);
            DrawTextClipped(panel.annotation_text, small_text_format_left_, text_rect, text_brush_);
            return;
        }

        const float footer_height =
            std::isfinite(panel.footer_height) ? std::max(0.0f, panel.footer_height) : 0.0f;
        const bool has_custom_axis = panel.vertical_unit_label != nullptr ||
                                     panel.horizontal_axis_label != nullptr ||
                                     (panel.horizontal_ticks != nullptr && panel.horizontal_tick_count > 0u);
        const float legend_area_width = std::max(1.0f, panel_rect.right - panel_rect.left - 100.0f);
        const size_t legend_columns = std::max(size_t{1}, size_t(legend_area_width / 150.0f));
        const size_t legend_rows =
            std::max(size_t{1}, (panel.series_count + legend_columns - 1) / legend_columns);
        const D2D1_RECT_F plot = D2D1::RectF(
            panel_rect.left + 90.0f, panel_rect.top + (has_custom_axis ? 38.0f : 22.0f),
            panel_rect.right - 10.0f, panel_rect.bottom - 18.0f * float(legend_rows) - footer_height);
        if (plot.right <= plot.left || plot.bottom <= plot.top)
        {
            return;
        }

        render_target_->DrawRectangle(plot, axis_brush_, 1.0f);
        const bool custom_ticks = panel.horizontal_ticks != nullptr && panel.horizontal_tick_count > 0u;
        if (custom_ticks)
        {
            for (std::size_t tick_index = 0; tick_index < panel.horizontal_tick_count; ++tick_index)
            {
                const GraphAxisTick &tick = panel.horizontal_ticks[tick_index];
                if (!std::isfinite(tick.value) || tick.value < (panel.unipolar_zero_floor ? 0.0f : -scale) ||
                    tick.value > scale)
                {
                    continue;
                }
                const float tick_y = GraphYForPanel(tick.value, scale, plot, panel);
                const bool zero_tick = std::abs(tick.value) <= 0.000001f;
                render_target_->DrawLine(D2D1::Point2F(plot.left, tick_y), D2D1::Point2F(plot.right, tick_y),
                                         zero_tick ? axis_brush_ : grid_brush_, zero_tick ? 1.0f : 0.8f);

                wchar_t generated_label[32]{};
                const wchar_t *tick_label = tick.label;
                if (tick_label == nullptr)
                {
                    std::swprintf(generated_label, std::size(generated_label), L"%.3g",
                                  static_cast<double>(tick.value));
                    tick_label = generated_label;
                }
                DrawTextClipped(
                    tick_label, axis_text_format_right_,
                    D2D1::RectF(panel_rect.left + 3.0f, tick_y - 7.0f, plot.left - 4.0f, tick_y + 7.0f),
                    muted_text_brush_);
            }
        }
        else
        {
            const float zero_y = GraphYForPanel(0.0f, scale, plot, panel);
            render_target_->DrawLine(D2D1::Point2F(plot.left, zero_y), D2D1::Point2F(plot.right, zero_y),
                                     axis_brush_, 1.0f);
            render_target_->DrawLine(
                D2D1::Point2F(plot.left, GraphYForPanel(scale * 0.5f, scale, plot, panel)),
                D2D1::Point2F(plot.right, GraphYForPanel(scale * 0.5f, scale, plot, panel)), grid_brush_,
                1.0f);
            if (!panel.unipolar_zero_floor)
            {
                render_target_->DrawLine(
                    D2D1::Point2F(plot.left, GraphYForPanel(-scale * 0.5f, scale, plot, panel)),
                    D2D1::Point2F(plot.right, GraphYForPanel(-scale * 0.5f, scale, plot, panel)), grid_brush_,
                    1.0f);
            }

            wchar_t y_label[64]{};
            if (panel.unipolar_zero_floor)
            {
                std::swprintf(y_label, std::size(y_label), L"0..%.3g%ls", static_cast<double>(scale),
                              panel.scale_suffix != nullptr ? panel.scale_suffix : L"");
            }
            else
            {
                std::swprintf(y_label, std::size(y_label), L"+/-%.3g%ls", static_cast<double>(scale),
                              panel.scale_suffix != nullptr ? panel.scale_suffix : L"");
            }
            DrawTextClipped(
                y_label, text_format_right_,
                D2D1::RectF(panel_rect.left + 4.0f, plot.top - 3.0f, plot.left - 3.0f, plot.top + 18.0f),
                muted_text_brush_);
        }

        if (panel.vertical_unit_label != nullptr)
        {
            DrawTextClipped(
                panel.vertical_unit_label, small_text_format_left_,
                D2D1::RectF(plot.left + 2.0f, panel_rect.top + 20.0f, plot.right, plot.top - 1.0f),
                muted_text_brush_);
        }

        if (panel.show_axis_direction_arrows)
        {
            constexpr float kArrowLength = 5.0f;
            constexpr float kArrowHalfWidth = 3.0f;
            render_target_->DrawLine(D2D1::Point2F(plot.left, plot.top),
                                     D2D1::Point2F(plot.left - kArrowHalfWidth, plot.top + kArrowLength),
                                     axis_brush_, 1.0f);
            render_target_->DrawLine(D2D1::Point2F(plot.left, plot.top),
                                     D2D1::Point2F(plot.left + kArrowHalfWidth, plot.top + kArrowLength),
                                     axis_brush_, 1.0f);

            const float zero_y = GraphYForPanel(0.0f, scale, plot, panel);
            render_target_->DrawLine(D2D1::Point2F(plot.right, zero_y),
                                     D2D1::Point2F(plot.right - kArrowLength, zero_y - kArrowHalfWidth),
                                     axis_brush_, 1.0f);
            render_target_->DrawLine(D2D1::Point2F(plot.right, zero_y),
                                     D2D1::Point2F(plot.right - kArrowLength, zero_y + kArrowHalfWidth),
                                     axis_brush_, 1.0f);
        }

        if (panel.horizontal_axis_label != nullptr)
            DrawTextClipped(panel.horizontal_axis_label, text_format_right_,
                            D2D1::RectF(std::max(plot.left, plot.right - 160.0f), panel_rect.top + 20.0f,
                                        plot.right, plot.top - 1.0f),
                            muted_text_brush_);

        if (panel.samples == nullptr || panel.sample_count == 0)
        {
            DrawTextClipped(panel.waiting_text, text_format_center_, plot, muted_text_brush_);
            return;
        }

        const std::size_t series_count = std::min(panel.series_count, kMaxGraphSeries);
        bool have_signal = false;
        for (std::size_t draw_index = 0; draw_index < series_count; ++draw_index)
        {
            const std::size_t series =
                panel.draw_series_in_reverse_order ? series_count - 1u - draw_index : draw_index;
            const GraphSeriesStyle style = panel.series_styles != nullptr
                                               ? panel.series_styles[series]
                                               : StandardMotionGraphSeriesStyle(series);
            ID2D1SolidColorBrush *series_brush = axis_brushes_[style.axis_color_index % axis_brushes_.size()];
            ID2D1StrokeStyle *stroke_style = StrokeStyle(style.line_pattern);
            const float stroke_width =
                std::isfinite(style.stroke_width) ? std::max(0.5f, style.stroke_width) : 1.8f;
            ID2D1PathGeometry *geometry = nullptr;
            ID2D1GeometrySink *sink = nullptr;
            HRESULT geometry_result = factory_->CreatePathGeometry(&geometry);
            if (SUCCEEDED(geometry_result))
            {
                geometry_result = geometry->Open(&sink);
            }
            bool figure_open = false;
            if (SUCCEEDED(geometry_result))
            {
                for (std::size_t sample_index = 0; sample_index < panel.sample_count; ++sample_index)
                {
                    const TimeSeriesGraphSample &sample = panel.samples[sample_index];
                    const bool sample_valid =
                        sample.age_seconds >= 0.0 && sample.age_seconds <= panel.time_window_seconds &&
                        (sample.valid_mask & (1u << series)) != 0u && std::isfinite(sample.values[series]);
                    if (!sample_valid)
                    {
                        if (figure_open)
                        {
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                            figure_open = false;
                        }
                        continue;
                    }
                    const float value = sample.values[series];
                    have_signal = have_signal || std::abs(value) > 0.000001f;
                    const float x =
                        plot.right -
                        static_cast<float>((sample.age_seconds / std::max(0.001, panel.time_window_seconds)) *
                                           static_cast<double>(plot.right - plot.left));
                    const D2D1_POINT_2F current = D2D1::Point2F(x, GraphYForPanel(value, scale, plot, panel));
                    if (!figure_open)
                    {
                        sink->BeginFigure(current, D2D1_FIGURE_BEGIN_HOLLOW);
                        figure_open = true;
                    }
                    else
                    {
                        sink->AddLine(current);
                    }
                }
                if (figure_open)
                {
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                }
                geometry_result = sink->Close();
            }
            if (sink != nullptr)
            {
                sink->Release();
            }
            if (SUCCEEDED(geometry_result) && geometry != nullptr)
            {
                if ((panel.series_background_underlay_mask & (std::uint32_t{1} << series)) != 0u)
                {
                    // Knock out an underlying coincident solid trace before
                    // drawing a dashed diagnostic. Otherwise the solid trace
                    // fills every transparent dash gap.
                    render_target_->DrawGeometry(geometry, panel_brush_, stroke_width + 1.0f, nullptr);
                }
                render_target_->DrawGeometry(geometry, series_brush, stroke_width, stroke_style);
            }
            if (geometry != nullptr)
            {
                geometry->Release();
            }

            const wchar_t *label = panel.series_labels != nullptr && panel.series_labels[series] != nullptr
                                       ? panel.series_labels[series]
                                       : L"";
            const float legend_width = (plot.right - plot.left) / float(legend_columns);
            const float legend_left = plot.left + float(series % legend_columns) * legend_width;
            const float legend_top = plot.bottom + float(series / legend_columns) * 18.0f;
            const float legend_mid_y = legend_top + 8.0f;
            const float legend_swatch_width =
                std::clamp(PositiveFiniteOr(panel.legend_swatch_width, 10.0f), 10.0f, 40.0f);
            render_target_->DrawLine(D2D1::Point2F(legend_left, legend_mid_y),
                                     D2D1::Point2F(legend_left + legend_swatch_width, legend_mid_y),
                                     series_brush, stroke_width, stroke_style);
            const D2D1_RECT_F legend =
                D2D1::RectF(legend_left + legend_swatch_width + 3.0f, legend_top + 1.0f,
                            legend_left + legend_width - 1.0f, legend_top + 17.0f);
            DrawTextClipped(label, small_text_format_left_, legend, series_brush);
        }

        if (panel.show_no_signal_text && !have_signal)
        {
            DrawTextClipped(panel.no_signal_text, text_format_center_, plot, muted_text_brush_);
        }
    }

    ID2D1Factory *factory_ = nullptr;
    IDWriteFactory *write_factory_ = nullptr;
    IDWriteTextFormat *text_format_left_ = nullptr;
    IDWriteTextFormat *text_format_right_ = nullptr;
    IDWriteTextFormat *text_format_center_ = nullptr;
    IDWriteTextFormat *small_text_format_left_ = nullptr;
    IDWriteTextFormat *axis_text_format_right_ = nullptr;
    ID2D1RenderTarget *render_target_ = nullptr;
    ID2D1SolidColorBrush *panel_brush_ = nullptr;
    ID2D1SolidColorBrush *text_brush_ = nullptr;
    ID2D1SolidColorBrush *muted_text_brush_ = nullptr;
    ID2D1SolidColorBrush *grid_brush_ = nullptr;
    ID2D1SolidColorBrush *axis_brush_ = nullptr;
    ID2D1SolidColorBrush *status_ok_brush_ = nullptr;
    ID2D1SolidColorBrush *status_waiting_brush_ = nullptr;
    ID2D1SolidColorBrush *status_bad_brush_ = nullptr;
    std::array<ID2D1SolidColorBrush *, kGraphAxisColorCount> axis_brushes_{};
    std::array<ID2D1StrokeStyle *, kGraphLinePatternCount> stroke_styles_{};
    std::vector<float> panel_scales_{};
    std::vector<D2D1_RECT_F> panel_rects_{};
    std::vector<float> column_weights_{};
    std::vector<float> column_next_tops_{};
    std::vector<std::size_t> column_panel_counts_{};
    std::vector<std::size_t> column_placed_counts_{};
    LONGLONG last_scale_update_qpc_ = 0;
};

inline void MoveChild(HWND parent, int id, int x, int y, int width, int height, BOOL repaint = TRUE)
{
    if (HWND child = GetDlgItem(parent, id))
    {
        MoveWindow(child, x, y, width, height, repaint);
    }
}

inline RECT StatusGraphRectForWindow(HWND window, int bottom_reserved_height)
{
    RECT client{};
    GetClientRect(window, &client);
    client.left += kStatusMargin;
    client.top += kStatusMargin;
    client.right -= kStatusMargin;
    client.bottom = std::max<LONG>(client.top, client.bottom - bottom_reserved_height - (3 * kStatusMargin));
    return client;
}

} // namespace ag::charts
