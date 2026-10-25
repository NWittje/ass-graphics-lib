#include <ass_graphics/graphics.hpp>
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        ass::graphics::Context c;
        ass::graphics::Window window(c, L"ASS Graphics — basic application", 600, 300);
        ag_control_desc label{};
        label.kind = AG_LABEL;
        label.bounds = {24, 24, 540, 64};
        label.text = L"B612 • Native Windows application";
        ass::graphics::Control greeting(c, window.hwnd(), label);
        window.fit();
        window.show();
        return window.run();
    }
    catch (const std::exception &e)
    {
        MessageBoxA(nullptr, e.what(), "ASS Graphics", MB_ICONERROR);
        return 1;
    }
}
