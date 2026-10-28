#include <array>
#include <ass_graphics/graphics.hpp>
#include <cmath>
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        using namespace ass::graphics;
        Context c;
        Window w(c, L"ASS Graphics — feature gallery", 1080, 800);
        ag_control_desc d{};
        d.kind = AG_LABEL;
        d.bounds = {20, 12, 1020, 40};
        d.text = L"B612 • Shared native drawing, charts, controls and 3D";
        Control title(c, w.hwnd(), d);
        d.kind = AG_SLIDER;
        d.text = L"gain";
        d.bounds = {20, 55, 360, 90};
        d.minimum = 0;
        d.maximum = 1;
        d.step = .01;
        d.value = .65;
        d.tooltip = L"A generic slider with a value readout";
        Control slider(c, w.hwnd(), d);
        d.kind = AG_STATUS;
        d.bounds = {390, 48, 1030, 140};
        d.text = L"";
        Control status(c, w.hwnd(), d);
        status.status(L"LIVE STATUS\r\nSynthetic example; no simulator connection.\r\nSelect and copy this "
                      L"text without losing the view.");
        Canvas canvas(c, w.hwnd(), {20, 145, 360, 320});
        std::array<ag_draw, 3> commands{};
        commands[0].kind = AG_TEXT;
        commands[0].rect = {12, 10, 320, 42};
        commands[0].color = {.1f, .12f, .15f, 1};
        wcscpy_s(commands[0].text, L"Direct2D + external B612");
        commands[1].kind = AG_SEGMENTS;
        commands[1].rect = {16, 55, 300, 125};
        commands[1].color = {.1f, .55f, .4f, 1};
        wcscpy_s(commands[1].text, L"123.45");
        commands[2].kind = AG_LINE;
        commands[2].rect = {12, 145, 310, 145};
        commands[2].width = 2;
        commands[2].color = {.16f, .4f, .78f, 1};
        canvas.submit(commands);
        Chart chart(c, w.hwnd(), {20, 335, 1030, 705});
        ag_chart_panel panel{};
        wcscpy_s(panel.title, L"Synthetic telemetry");
        wcscpy_s(panel.unit, L"unit");
        wcscpy_s(panel.horizontal_label, L"seconds");
        panel.sample_count = 300;
        panel.series_count = 3;
        panel.time_window = 15;
        panel.default_scale = 1;
        panel.maximum_scale = 2;
        panel.symlog_threshold = .01f;
        panel.height_weight = 1;
        std::array<ag_series, 3> series{};
        for (int i = 0; i < 3; ++i)
        {
            swprintf_s(series[i].label, L"Signal %d", i + 1);
            series[i].color_index = i;
            series[i].pattern = i;
            series[i].width = 2;
        }
        std::array<ag_sample, 300> samples{};
        for (size_t i = 0; i < samples.size(); ++i)
        {
            samples[i].age_seconds = i * .05;
            samples[i].valid_mask = 7;
            for (int j = 0; j < 3; ++j)
                samples[i].values[j] = float(sin(i * .04 + j) * .8);
        }
        chart.submit(std::span(&panel, 1), series, samples);
        Scene scene(c, w.hwnd(), {390, 150, 1030, 320});
        std::array<ag_vertex, 3> vertices{{{{-1, -.6f, 0}, {0, 0, -1}, {.16f, .4f, .78f, 1}},
                                           {{0, .8f, 0}, {0, 0, -1}, {.2f, .7f, .4f, 1}},
                                           {{1, -.6f, 0}, {0, 0, -1}, {.8f, .3f, .3f, 1}}}};
        std::array<uint32_t, 3> indices{0, 1, 2};
        scene.mesh(vertices, indices);
        w.fit();
        w.show();
        return w.run();
    }
    catch (const std::exception &e)
    {
        MessageBoxA(nullptr, e.what(), "ASS Graphics", MB_ICONERROR);
        return 1;
    }
}
