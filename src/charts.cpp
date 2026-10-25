#include "charts_extracted.hpp"
namespace ag
{
struct ChartSnapshot
{
    std::vector<ag_chart_panel> panels;
    std::vector<ag_series> series;
    std::vector<ag_sample> samples;
};
struct Chart final : Surface
{
    ChartSnapshot current, pending;
    bool updated{};
    charts::D2DStatusGraphRenderer renderer;
    std::vector<charts::TimeSeriesGraphPanel> panels;
    std::vector<charts::GraphSeriesStyle> styles;
    std::vector<const wchar_t *> labels;
    std::vector<charts::TimeSeriesGraphSample> samples;
    Chart(Context *c) : Surface(Kind::Chart, c), renderer(c) {}
    void adopt()
    {
        {
            std::lock_guard lock(mutex);
            notified = false;
            if (updated)
            {
                std::swap(current, pending);
                updated = false;
                dirty = true;
            }
        }
        if (dirty)
        {
            styles.resize(current.series.size());
            labels.resize(current.series.size());
            samples.resize(current.samples.size());
            panels.resize(current.panels.size());
            for (size_t i = 0; i < styles.size(); ++i)
            {
                auto &s = current.series[i];
                styles[i] = {uint8_t(s.color_index % 3), charts::GraphLinePattern(s.pattern), s.width};
                labels[i] = s.label;
            }
            for (size_t i = 0; i < samples.size(); ++i)
            {
                auto &s = current.samples[i];
                samples[i].age_seconds = s.age_seconds;
                samples[i].valid_mask = s.valid_mask;
                for (int j = 0; j < 18; ++j)
                {
                    samples[i].values[j] = s.values[j];
                    if (!finite(s.values[j]))
                        samples[i].valid_mask &= ~(1u << j);
                }
            }
            for (size_t i = 0; i < panels.size(); ++i)
            {
                auto &s = current.panels[i];
                auto &d = panels[i];
                d = {};
                d.title = s.title;
                d.annotation_text = s.annotation[0] ? s.annotation : nullptr;
                d.vertical_unit_label = s.unit[0] ? s.unit : nullptr;
                d.horizontal_axis_label = s.horizontal_label[0] ? s.horizontal_label : nullptr;
                d.samples = s.sample_count ? samples.data() + s.first_sample : nullptr;
                d.sample_count = s.sample_count;
                d.series_count = s.series_count;
                d.series_labels = s.series_count ? labels.data() + s.first_series : nullptr;
                d.series_styles = s.series_count ? styles.data() + s.first_series : nullptr;
                d.time_window_seconds = s.time_window;
                d.default_scale = s.default_scale;
                d.maximum_scale = s.maximum_scale;
                d.symmetric_log_linear_threshold = s.symlog_threshold;
                d.height_weight = s.height_weight;
                d.layout_column = s.column;
                d.vertical_scale_mode = s.flags & 1 ? charts::GraphVerticalScaleMode::SymmetricLogarithmic
                                                    : charts::GraphVerticalScaleMode::Linear;
                d.unipolar_zero_floor = (s.flags & 2) != 0;
                d.show_no_signal_text = (s.flags & 4) != 0;
                d.show_axis_direction_arrows = (s.flags & 8) != 0;
                d.draw_series_in_reverse_order = (s.flags & 16) != 0;
            }
            dirty = false;
        }
    }
    void paint() override
    {
        adopt();
        check(renderer.Render(hwnd, panels.data(), panels.size()), L"Chart rendering failed");
    }
    void resize() override
    {
        renderer.Resize(hwnd);
    }
    void reset() override
    {
        renderer.Reset();
        dirty = true;
        ++metrics.device_recreations;
    }
};
} // namespace ag
using namespace ag;
ag_result ag_chart_create(ag_handle c, void *parent, ag_rect r, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto ctx = get<Context>(c, Kind::Context);
            ctx->ui();
            auto p = std::make_unique<Chart>(ctx);
            p->create(static_cast<HWND>(parent), r);
            *out = p.release();
        });
}
ag_result ag_chart_export_png(ag_handle h, const wchar_t *file, uint32_t w, uint32_t height, float dpi)
{
    return api(
        [&]
        {
            auto p = get<Chart>(h, Kind::Chart);
            p->ui();
            require(file && w && height && w <= 16384 && height <= 16384 && dpi >= 48 && dpi <= 960,
                    L"Invalid chart export");
            p->adopt();
            auto c = p->context;
            ComPtr<IWICBitmap> bitmap;
            check(
                c->wic->CreateBitmap(w, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap),
                L"Chart bitmap failed");
            auto props = D2D1::RenderTargetProperties();
            props.dpiX = props.dpiY = dpi;
            ComPtr<ID2D1RenderTarget> target;
            check(c->d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target),
                  L"Chart bitmap target failed");
            charts::D2DStatusGraphRenderer renderer(c);
            check(renderer.UseTarget(target.Get()), L"Chart export target failed");
            check(renderer.Render(nullptr, p->panels.data(), p->panels.size()),
                  L"Chart export drawing failed");
            ComPtr<IWICBitmapLock> lock;
            check(bitmap->Lock(nullptr, WICBitmapLockRead, &lock), L"Chart bitmap lock failed");
            UINT stride{}, size{};
            BYTE *data{};
            check(lock->GetStride(&stride), L"Chart stride failed");
            check(lock->GetDataPointer(&size, &data), L"Chart pixels failed");
            save_pixels(c, file, w, height, stride, data, dpi);
        });
}
ag_result ag_chart_submit(ag_handle h, const ag_chart_panel *panels, uint32_t n, const ag_series *series,
                          uint32_t sn, const ag_sample *samples, uint32_t count)
{
    return api(
        [&]
        {
            auto p = get<Chart>(h, Kind::Chart);
            require(n <= 64 && sn <= 1152 && count <= 1048576 && (!n || panels) && (!sn || series) &&
                        (!count || samples),
                    L"Invalid chart snapshot size");
            for (uint32_t i = 0; i < n; ++i)
            {
                auto &q = panels[i];
                require(wmemchr(q.title, 0, 128) && wmemchr(q.unit, 0, 32) &&
                            wmemchr(q.horizontal_label, 0, 64) && wmemchr(q.annotation, 0, 128) &&
                            q.first_sample <= count && q.sample_count <= count - q.first_sample &&
                            q.first_series <= sn && q.series_count <= sn - q.first_series &&
                            q.series_count <= 18 && q.column < 64 && q.time_window > 0 &&
                            std::isfinite(q.time_window) && q.default_scale > 0 && finite(q.default_scale) &&
                            q.maximum_scale >= q.default_scale && finite(q.maximum_scale) &&
                            q.symlog_threshold > 0 && finite(q.symlog_threshold) && q.height_weight > 0 &&
                            finite(q.height_weight),
                        L"Invalid chart panel");
            }
            for (uint32_t i = 0; i < sn; ++i)
                require(wmemchr(series[i].label, 0, 64) && series[i].pattern <= AG_DOTTED &&
                            series[i].width > 0 && finite(series[i].width),
                        L"Invalid series style");
            for (uint32_t i = 0; i < count; ++i)
                require(std::isfinite(samples[i].age_seconds), L"Invalid sample age");
            std::lock_guard lock(p->mutex);
            p->updated = false;
            if (n)
                p->pending.panels.assign(panels, panels + n);
            else
                p->pending.panels.clear();
            if (sn)
                p->pending.series.assign(series, series + sn);
            else
                p->pending.series.clear();
            if (count)
                p->pending.samples.assign(samples, samples + count);
            else
                p->pending.samples.clear();
            p->updated = true;
            p->schedule_locked();
        });
}
