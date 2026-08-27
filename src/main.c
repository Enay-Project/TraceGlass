#include <windows.h>
#include <commctrl.h>
#include "gui.h"
#include "logger.h"
#include "version.h"

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE previous_instance,
    PWSTR command_line,
    int show_command
) {
    INITCOMMONCONTROLSEX common_controls;
    int result;
    (void)previous_instance;
    (void)command_line;

    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_LISTVIEW_CLASSES |
        ICC_TREEVIEW_CLASSES |
        ICC_TAB_CLASSES |
        ICC_BAR_CLASSES;
    InitCommonControlsEx(&common_controls);

    logger_initialize();
    logger_write(L"INFO", L"TraceGlass %s started", TRACEGLASS_VERSION_STRING);
    result = gui_run(instance, show_command);
    logger_write(L"INFO", L"TraceGlass stopped with result %d", result);
    logger_shutdown();
    return result;
}
