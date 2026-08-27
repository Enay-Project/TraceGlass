#include "detection.h"

#include <strsafe.h>
#include "utils.h"

static BOOL is_office_process(const WCHAR *name) {
    static const WCHAR *office_names[] = {
        L"WINWORD.EXE", L"EXCEL.EXE", L"OUTLOOK.EXE",
        L"POWERPNT.EXE", L"MSACCESS.EXE"
    };
    size_t index;
    for (index = 0; index < ARRAYSIZE(office_names); ++index) {
        if (string_equals_insensitive(name, office_names[index])) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL is_script_interpreter(const WCHAR *name) {
    static const WCHAR *interpreter_names[] = {
        L"powershell.exe", L"pwsh.exe", L"wscript.exe",
        L"cscript.exe", L"mshta.exe"
    };
    size_t index;
    for (index = 0; index < ARRAYSIZE(interpreter_names); ++index) {
        if (string_equals_insensitive(name, interpreter_names[index])) {
            return TRUE;
        }
    }
    return FALSE;
}

static void initialize_alert(TraceGlassEvent *alert, const TraceGlassEvent *source) {
    *alert = *source;
    alert->type = EVENT_ALERT;
    alert->severity = ALERT_INFO;
    alert->rule_name[0] = L'\0';
}

size_t detection_evaluate(
    const TraceGlassEvent *event,
    TraceGlassEvent *alerts,
    size_t alert_capacity
) {
    size_t count = 0;
    TraceGlassEvent *alert;
    if (event == NULL || alerts == NULL || alert_capacity == 0 || event->type == EVENT_ALERT) {
        return 0;
    }

    if (event->type == EVENT_PROCESS_START &&
        is_office_process(event->parent_name) &&
        is_script_interpreter(event->process_name) &&
        count < alert_capacity) {
        alert = &alerts[count++];
        initialize_alert(alert, event);
        alert->severity = ALERT_MEDIUM;
        copy_wstring(
            alert->rule_name,
            ARRAYSIZE(alert->rule_name),
            L"Office spawned script interpreter"
        );
        StringCchPrintfW(
            alert->details,
            ARRAYSIZE(alert->details),
            L"Office application spawned a script interpreter: %s -> %s",
            event->parent_name,
            event->process_name
        );
    }

    if (event->type == EVENT_PROCESS_START &&
        (string_contains_insensitive(event->executable_path, L"\\AppData\\Local\\Temp\\") ||
         string_contains_insensitive(event->executable_path, L"\\Windows\\Temp\\")) &&
        count < alert_capacity) {
        alert = &alerts[count++];
        initialize_alert(alert, event);
        alert->severity = ALERT_MEDIUM;
        copy_wstring(
            alert->rule_name,
            ARRAYSIZE(alert->rule_name),
            L"Executable in temporary directory"
        );
        StringCchPrintfW(
            alert->details,
            ARRAYSIZE(alert->details),
            L"Executable is running from a temporary directory: %s",
            event->executable_path
        );
    }

    if (event->type == EVENT_NETWORK &&
        is_script_interpreter(event->process_name) &&
        count < alert_capacity) {
        alert = &alerts[count++];
        initialize_alert(alert, event);
        alert->severity = ALERT_INFO;
        copy_wstring(
            alert->rule_name,
            ARRAYSIZE(alert->rule_name),
            L"Script interpreter owns TCP connection"
        );
        StringCchPrintfW(
            alert->details,
            ARRAYSIZE(alert->details),
            L"Script interpreter owns an established TCP connection; remote endpoint %s:%u",
            event->remote_address,
            (unsigned int)event->remote_port
        );
    }

    return count;
}
