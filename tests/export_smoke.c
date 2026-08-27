#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <strsafe.h>
#include <string.h>
#include "event_store.h"
#include "export.h"
#include "utils.h"

static BOOL file_contains(const WCHAR *path, const CHAR *needle) {
    HANDLE file;
    LARGE_INTEGER size;
    CHAR *contents;
    DWORD bytes_read;
    BOOL found = FALSE;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > (ULONGLONG)(SIZE_MAX - 1) ||
        size.QuadPart > MAXDWORD) {
        CloseHandle(file);
        return FALSE;
    }
    contents = (CHAR *)malloc((size_t)size.QuadPart + 1);
    if (contents != NULL && ReadFile(file, contents, (DWORD)size.QuadPart, &bytes_read, NULL)) {
        contents[bytes_read] = '\0';
        found = strstr(contents, needle) != NULL;
    }
    free(contents);
    CloseHandle(file);
    return found;
}

static BOOL make_test_path(WCHAR *path, size_t path_count, const WCHAR *extension) {
    WCHAR directory[MAX_PATH];
    DWORD length = GetTempPathW(ARRAYSIZE(directory), directory);
    if (length == 0 || length >= ARRAYSIZE(directory)) return FALSE;
    return SUCCEEDED(StringCchPrintfW(path, path_count,
        L"%straceglass-export-smoke-%lu.%s", directory,
        GetCurrentProcessId(), extension));
}

static BOOL run_export_check(
    const EventStore *store,
    ExportFormat format,
    const WCHAR *extension,
    const CHAR *expected
) {
    WCHAR path[MAX_PATH];
    WCHAR error[512];
    BOOL result;
    if (!make_test_path(path, ARRAYSIZE(path), extension)) return FALSE;
    result = export_events_to_file(store, 0, path, format,
        error, ARRAYSIZE(error)) && file_contains(path, expected);
    DeleteFileW(path);
    return result;
}

int main(void) {
    EventStore store;
    TraceGlassEvent event;
    BOOL accepted = FALSE;
    BOOL passed;
    if (!event_store_initialize(&store)) return 1;
    ZeroMemory(&event, sizeof(event));
    event.type = EVENT_NETWORK;
    event.process.pid = 4820;
    event.parent.pid = 1204;
    get_local_system_time(&event.timestamp);
    copy_wstring(event.process_name, ARRAYSIZE(event.process_name), L"powershell.exe");
    copy_wstring(event.protocol, ARRAYSIZE(event.protocol), L"TCP");
    copy_wstring(event.local_address, ARRAYSIZE(event.local_address), L"192.168.1.20");
    copy_wstring(event.remote_address, ARRAYSIZE(event.remote_address), L"104.21.10.10");
    copy_wstring(event.network_state, ARRAYSIZE(event.network_state), L"ESTABLISHED");
    copy_wstring(event.details, ARRAYSIZE(event.details),
        L"Established TCP connection; quoted value \"preserved\"");
    event.local_port = 52144;
    event.remote_port = 443;
    if (!event_store_add(&store, &event, &accepted) || !accepted) {
        event_store_destroy(&store);
        return 1;
    }

    passed = run_export_check(&store, EXPORT_FORMAT_JSON, L"json",
        "\"event_type\": \"network_connection\"") &&
        run_export_check(&store, EXPORT_FORMAT_CSV, L"csv",
            "timestamp,event_type,process_name,pid,parent_pid") &&
        run_export_check(&store, EXPORT_FORMAT_TEXT, L"txt",
            "PID 4820 | Parent PID 1204");
    event_store_destroy(&store);
    if (!passed) {
        fputs("TraceGlass export smoke test failed.\n", stderr);
        return 1;
    }
    puts("TraceGlass JSON, CSV and TXT export smoke tests passed.");
    return 0;
}
