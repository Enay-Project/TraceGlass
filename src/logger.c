#include "logger.h"

#include <stdarg.h>
#include <strsafe.h>

static CRITICAL_SECTION g_logger_lock;
static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static BOOL g_logger_ready = FALSE;

BOOL logger_initialize(void) {
    BOOL directory_ready;
    InitializeCriticalSection(&g_logger_lock);
    g_logger_ready = TRUE;

    /* Application diagnostics are kept separate from collected telemetry. */
    directory_ready = CreateDirectoryW(L"logs", NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    if (!directory_ready) {
        return FALSE;
    }

    g_log_file = CreateFileW(
        L"logs\\traceglass.log",
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    return g_log_file != INVALID_HANDLE_VALUE;
}

void logger_shutdown(void) {
    if (!g_logger_ready) {
        return;
    }
    EnterCriticalSection(&g_logger_lock);
    if (g_log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_logger_lock);
    DeleteCriticalSection(&g_logger_lock);
    g_logger_ready = FALSE;
}

void logger_write(const WCHAR *level, const WCHAR *format, ...) {
    WCHAR message[1536];
    WCHAR line[1792];
    CHAR utf8_line[7168];
    SYSTEMTIME time_value;
    va_list arguments;
    int utf8_length;
    DWORD bytes_written;

    if (!g_logger_ready || format == NULL) {
        return;
    }

    va_start(arguments, format);
    if (FAILED(StringCchVPrintfW(message, ARRAYSIZE(message), format, arguments))) {
        message[ARRAYSIZE(message) - 1] = L'\0';
    }
    va_end(arguments);

    GetLocalTime(&time_value);
    StringCchPrintfW(
        line,
        ARRAYSIZE(line),
        L"[%02u:%02u:%02u] [%s] %s\r\n",
        (unsigned int)time_value.wHour,
        (unsigned int)time_value.wMinute,
        (unsigned int)time_value.wSecond,
        level != NULL ? level : L"INFO",
        message
    );

    OutputDebugStringW(line);

    if (g_log_file == INVALID_HANDLE_VALUE) {
        return;
    }

    utf8_length = WideCharToMultiByte(
        CP_UTF8,
        0,
        line,
        -1,
        utf8_line,
        (int)ARRAYSIZE(utf8_line),
        NULL,
        NULL
    );
    if (utf8_length <= 1) {
        return;
    }

    EnterCriticalSection(&g_logger_lock);
    WriteFile(g_log_file, utf8_line, (DWORD)(utf8_length - 1), &bytes_written, NULL);
    LeaveCriticalSection(&g_logger_lock);
}
