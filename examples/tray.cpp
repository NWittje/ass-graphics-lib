#include <ass_graphics/graphics.hpp>
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        ass::graphics::Context c;
        ass::graphics::Window window(c, L"ASS Graphics — tray application", 620, 340);
        if (!window.single_instance(L"AssGraphics.TrayExample"))
            return 0;
        ass::graphics::Tray tray(window, L"ASS Graphics example");
        ag_control_desc d{};
        d.kind = AG_STATUS;
        d.bounds = {16, 16, 580, 250};
        ass::graphics::Control status(c, window.hwnd(), d);
        status.status(L"Ready\r\nClose hides this window. Use the tray menu to exit.\r\nThis text can be "
                      L"selected and copied.");
        window.show();
        return window.run();
    }
    catch (const std::exception &e)
    {
        MessageBoxA(nullptr, e.what(), "ASS Graphics", MB_ICONERROR);
        return 1;
    }
}
