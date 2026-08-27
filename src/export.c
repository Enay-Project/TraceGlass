#include "export.h"

#include <commdlg.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <strsafe.h>
#include <wchar.h>
#include "events.h"
#include "utils.h"

static void set_export_error(
    WCHAR *buffer,
    size_t buffer_count,
    const WCHAR *operation,
    DWORD error
) {
    WCHAR reason[256];
    if (buffer == NULL || buffer_count == 0) {
        return;
    }
    format_windows_error(error, reason, ARRAYSIZE(reason));
    StringCchPrintfW(buffer, buffer_count, L"%s\r\n\r\nReason:\r\n%s", operation, reason);
}

static BOOL write_all(HANDLE file, const void *data, size_t size) {
    const BYTE *cursor = (const BYTE *)data;
    while (size > 0) {
        DWORD chunk = size > MAXDWORD ? MAXDWORD : (DWORD)size;
        DWORD written = 0;
        if (!WriteFile(file, cursor, chunk, &written, NULL) || written == 0) {
            return FALSE;
        }
        cursor += written;
        size -= written;
    }
    return TRUE;
}

static BOOL write_utf8(HANDLE file, const WCHAR *text) {
    int source_length;
    int byte_count;
    CHAR *bytes;
    BOOL result;
    if (text == NULL) {
        text = L"";
    }
    if (wcslen(text) > INT_MAX) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    source_length = (int)wcslen(text);
    if (source_length == 0) {
        return TRUE;
    }
    byte_count = WideCharToMultiByte(
        CP_UTF8, 0, text, source_length, NULL, 0, NULL, NULL
    );
    if (byte_count <= 0) {
        return FALSE;
    }
    bytes = (CHAR *)malloc((size_t)byte_count);
    if (bytes == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    if (WideCharToMultiByte(
            CP_UTF8, 0, text, source_length, bytes, byte_count, NULL, NULL
        ) != byte_count) {
        free(bytes);
        return FALSE;
    }
    result = write_all(file, bytes, (size_t)byte_count);
    free(bytes);
    return result;
}

static void format_timestamp(
    const SYSTEMTIME *timestamp,
    WCHAR *buffer,
    size_t buffer_count
) {
    StringCchPrintfW(
        buffer,
        buffer_count,
        L"%04u-%02u-%02uT%02u:%02u:%02u.%03u",
        (unsigned int)timestamp->wYear,
        (unsigned int)timestamp->wMonth,
        (unsigned int)timestamp->wDay,
        (unsigned int)timestamp->wHour,
        (unsigned int)timestamp->wMinute,
        (unsigned int)timestamp->wSecond,
        (unsigned int)timestamp->wMilliseconds
    );
}

static BOOL write_json_string(HANDLE file, const WCHAR *value) {
    size_t length;
    size_t capacity;
    size_t source_index;
    size_t target_index = 0;
    WCHAR *escaped;
    BOOL result;
    if (value == NULL) {
        value = L"";
    }
    length = wcslen(value);
    if (length > ((SIZE_MAX / sizeof(WCHAR)) - 3) / 6) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    capacity = length * 6 + 3;
    escaped = (WCHAR *)malloc(capacity * sizeof(*escaped));
    if (escaped == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    escaped[target_index++] = L'"';
    for (source_index = 0; source_index < length; ++source_index) {
        WCHAR character = value[source_index];
        switch (character) {
            case L'"': escaped[target_index++] = L'\\'; escaped[target_index++] = L'"'; break;
            case L'\\': escaped[target_index++] = L'\\'; escaped[target_index++] = L'\\'; break;
            case L'\b': escaped[target_index++] = L'\\'; escaped[target_index++] = L'b'; break;
            case L'\f': escaped[target_index++] = L'\\'; escaped[target_index++] = L'f'; break;
            case L'\n': escaped[target_index++] = L'\\'; escaped[target_index++] = L'n'; break;
            case L'\r': escaped[target_index++] = L'\\'; escaped[target_index++] = L'r'; break;
            case L'\t': escaped[target_index++] = L'\\'; escaped[target_index++] = L't'; break;
            default:
                if (character < 0x20) {
                    StringCchPrintfW(
                        &escaped[target_index],
                        capacity - target_index,
                        L"\\u%04x",
                        (unsigned int)character
                    );
                    target_index += 6;
                } else {
                    escaped[target_index++] = character;
                }
                break;
        }
    }
    escaped[target_index++] = L'"';
    escaped[target_index] = L'\0';
    result = write_utf8(file, escaped);
    free(escaped);
    return result;
}

static BOOL write_json_field(
    HANDLE file,
    const WCHAR *name,
    const WCHAR *value,
    BOOL trailing_comma
) {
    return write_utf8(file, L"    \"") &&
        write_utf8(file, name) &&
        write_utf8(file, L"\": ") &&
        write_json_string(file, value) &&
        write_utf8(file, trailing_comma ? L",\r\n" : L"\r\n");
}

static BOOL write_json_export(
    HANDLE file,
    const EventStore *store,
    size_t first_event
) {
    size_t index;
    WCHAR timestamp[64];
    WCHAR number[64];
    if (!write_utf8(file, L"[\r\n")) {
        return FALSE;
    }
    for (index = first_event; index < store->event_count; ++index) {
        const TraceGlassEvent *event = &store->events[index];
        format_timestamp(&event->timestamp, timestamp, ARRAYSIZE(timestamp));
        if (!write_utf8(file, L"  {\r\n") ||
            !write_json_field(file, L"timestamp", timestamp, TRUE) ||
            !write_json_field(file, L"event_type", event_export_name(event->type), TRUE) ||
            !write_json_field(file, L"process_name", event->process_name, TRUE)) {
            return FALSE;
        }
        StringCchPrintfW(number, ARRAYSIZE(number), L"%lu", event->process.pid);
        if (!write_utf8(file, L"    \"pid\": ") || !write_utf8(file, number) ||
            !write_utf8(file, L",\r\n")) {
            return FALSE;
        }
        StringCchPrintfW(number, ARRAYSIZE(number), L"%lu", event->parent.pid);
        if (!write_utf8(file, L"    \"parent_pid\": ") || !write_utf8(file, number) ||
            !write_utf8(file, L",\r\n") ||
            !write_json_field(file, L"details", event->details, TRUE) ||
            !write_json_field(file, L"protocol", event->protocol, TRUE) ||
            !write_json_field(file, L"local_address", event->local_address, TRUE)) {
            return FALSE;
        }
        StringCchPrintfW(number, ARRAYSIZE(number), L"%u", (unsigned int)event->local_port);
        if (!write_utf8(file, L"    \"local_port\": ") || !write_utf8(file, number) ||
            !write_utf8(file, L",\r\n") ||
            !write_json_field(file, L"remote_address", event->remote_address, TRUE)) {
            return FALSE;
        }
        StringCchPrintfW(number, ARRAYSIZE(number), L"%u", (unsigned int)event->remote_port);
        if (!write_utf8(file, L"    \"remote_port\": ") || !write_utf8(file, number) ||
            !write_utf8(file, L",\r\n") ||
            !write_json_field(file, L"state", event->network_state, TRUE) ||
            !write_json_field(file, L"severity", alert_severity_name(event->severity), TRUE) ||
            !write_json_field(file, L"rule", event->rule_name, FALSE) ||
            !write_utf8(file, index + 1 < store->event_count ? L"  },\r\n" : L"  }\r\n")) {
            return FALSE;
        }
    }
    return write_utf8(file, L"]\r\n");
}

static BOOL write_csv_field(HANDLE file, const WCHAR *value) {
    size_t length;
    size_t capacity;
    size_t source_index;
    size_t target_index = 0;
    WCHAR *escaped;
    BOOL result;
    if (value == NULL) {
        value = L"";
    }
    length = wcslen(value);
    if (length > ((SIZE_MAX / sizeof(WCHAR)) - 3) / 2) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    capacity = length * 2 + 3;
    escaped = (WCHAR *)malloc(capacity * sizeof(*escaped));
    if (escaped == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    escaped[target_index++] = L'"';
    for (source_index = 0; source_index < length; ++source_index) {
        if (value[source_index] == L'"') {
            escaped[target_index++] = L'"';
        }
        escaped[target_index++] = value[source_index];
    }
    escaped[target_index++] = L'"';
    escaped[target_index] = L'\0';
    result = write_utf8(file, escaped);
    free(escaped);
    return result;
}

static BOOL write_csv_export(
    HANDLE file,
    const EventStore *store,
    size_t first_event
) {
    static const BYTE utf8_bom[] = {0xEF, 0xBB, 0xBF};
    size_t index;
    WCHAR timestamp[64];
    WCHAR number[64];
    if (!write_all(file, utf8_bom, sizeof(utf8_bom)) ||
        !write_utf8(file, L"timestamp,event_type,process_name,pid,parent_pid,details,protocol,local_address,local_port,remote_address,remote_port,state,severity,rule\r\n")) {
        return FALSE;
    }
#define CSV_TEXT(value) do { if (!write_csv_field(file, (value)) || !write_utf8(file, L",")) return FALSE; } while (0)
    for (index = first_event; index < store->event_count; ++index) {
        const TraceGlassEvent *event = &store->events[index];
        format_timestamp(&event->timestamp, timestamp, ARRAYSIZE(timestamp));
        CSV_TEXT(timestamp);
        CSV_TEXT(event_export_name(event->type));
        CSV_TEXT(event->process_name);
        StringCchPrintfW(number, ARRAYSIZE(number), L"%lu", event->process.pid);
        CSV_TEXT(number);
        StringCchPrintfW(number, ARRAYSIZE(number), L"%lu", event->parent.pid);
        CSV_TEXT(number);
        CSV_TEXT(event->details);
        CSV_TEXT(event->protocol);
        CSV_TEXT(event->local_address);
        StringCchPrintfW(number, ARRAYSIZE(number), L"%u", (unsigned int)event->local_port);
        CSV_TEXT(number);
        CSV_TEXT(event->remote_address);
        StringCchPrintfW(number, ARRAYSIZE(number), L"%u", (unsigned int)event->remote_port);
        CSV_TEXT(number);
        CSV_TEXT(event->network_state);
        CSV_TEXT(alert_severity_name(event->severity));
        if (!write_csv_field(file, event->rule_name) || !write_utf8(file, L"\r\n")) {
            return FALSE;
        }
    }
#undef CSV_TEXT
    return TRUE;
}

static BOOL write_text_export(
    HANDLE file,
    const EventStore *store,
    size_t first_event
) {
    static const BYTE utf8_bom[] = {0xEF, 0xBB, 0xBF};
    size_t index;
    WCHAR timestamp[64];
    WCHAR line[1536];
    if (!write_all(file, utf8_bom, sizeof(utf8_bom))) {
        return FALSE;
    }
    for (index = first_event; index < store->event_count; ++index) {
        const TraceGlassEvent *event = &store->events[index];
        format_timestamp(&event->timestamp, timestamp, ARRAYSIZE(timestamp));
        StringCchPrintfW(
            line,
            ARRAYSIZE(line),
            L"%s | %s | %s | PID %lu | Parent PID %lu | %s\r\n",
            timestamp,
            event_type_name(event->type),
            event->process_name,
            event->process.pid,
            event->parent.pid,
            event->details
        );
        if (!write_utf8(file, line)) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL export_events_to_file(
    const EventStore *store,
    size_t first_event,
    const WCHAR *path,
    ExportFormat format,
    WCHAR *error_message,
    size_t error_message_count
) {
    HANDLE file;
    BOOL result;
    DWORD error;
    if (error_message != NULL && error_message_count > 0) {
        error_message[0] = L'\0';
    }
    if (store == NULL || path == NULL || first_event > store->event_count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        set_export_error(error_message, error_message_count, L"Events could not be exported.", ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    file = CreateFileW(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        set_export_error(error_message, error_message_count, L"The export file could not be created.", GetLastError());
        return FALSE;
    }

    if (format == EXPORT_FORMAT_JSON) {
        result = write_json_export(file, store, first_event);
    } else if (format == EXPORT_FORMAT_CSV) {
        result = write_csv_export(file, store, first_event);
    } else {
        result = write_text_export(file, store, first_event);
    }
    error = result ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(file) && result) {
        result = FALSE;
        error = GetLastError();
    }
    if (!result) {
        set_export_error(error_message, error_message_count, L"The export could not be completed.", error);
    }
    return result;
}

static BOOL path_has_extension(const WCHAR *path) {
    const WCHAR *last_slash = wcsrchr(path, L'\\');
    const WCHAR *last_dot = wcsrchr(path, L'.');
    return last_dot != NULL && (last_slash == NULL || last_dot > last_slash);
}

static BOOL apply_selected_extension(
    WCHAR *path,
    size_t path_count,
    const WCHAR *extension
) {
    WCHAR *last_slash = wcsrchr(path, L'\\');
    WCHAR *last_dot = wcsrchr(path, L'.');
    if (last_dot != NULL && (last_slash == NULL || last_dot > last_slash)) {
        if (string_equals_insensitive(last_dot, L".json") ||
            string_equals_insensitive(last_dot, L".csv") ||
            string_equals_insensitive(last_dot, L".txt")) {
            *last_dot = L'\0';
            return SUCCEEDED(StringCchCatW(path, path_count, extension));
        }
        return TRUE;
    }
    return SUCCEEDED(StringCchCatW(path, path_count, extension));
}

ExportResult export_events_with_dialog(
    HWND owner,
    const EventStore *store,
    size_t first_event,
    WCHAR *saved_path,
    size_t saved_path_count,
    WCHAR *error_message,
    size_t error_message_count
) {
    static const WCHAR filters[] =
        L"JSON files (*.json)\0*.json\0"
        L"CSV files (*.csv)\0*.csv\0"
        L"Text files (*.txt)\0*.txt\0\0";
    OPENFILENAMEW dialog;
    WCHAR path[MAX_PATH] = L"traceglass-events.json";
    const WCHAR *extension;
    ExportFormat format;
    DWORD dialog_error;

    if (saved_path != NULL && saved_path_count > 0) {
        saved_path[0] = L'\0';
    }
    if (error_message != NULL && error_message_count > 0) {
        error_message[0] = L'\0';
    }

    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filters;
    dialog.nFilterIndex = 1;
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.lpstrTitle = L"Export all collected TraceGlass events";
    dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&dialog)) {
        dialog_error = CommDlgExtendedError();
        if (dialog_error == 0) {
            return EXPORT_RESULT_CANCELLED;
        }
        set_export_error(
            error_message,
            error_message_count,
            L"The Save As dialog could not be opened.",
            dialog_error
        );
        return EXPORT_RESULT_FAILED;
    }

    format = dialog.nFilterIndex == 2 ? EXPORT_FORMAT_CSV :
        dialog.nFilterIndex == 3 ? EXPORT_FORMAT_TEXT : EXPORT_FORMAT_JSON;
    extension = format == EXPORT_FORMAT_CSV ? L".csv" :
        format == EXPORT_FORMAT_TEXT ? L".txt" : L".json";
    if ((!path_has_extension(path) ||
         string_equals_insensitive(wcsrchr(path, L'.'), L".json") ||
         string_equals_insensitive(wcsrchr(path, L'.'), L".csv") ||
         string_equals_insensitive(wcsrchr(path, L'.'), L".txt")) &&
        !apply_selected_extension(path, ARRAYSIZE(path), extension)) {
        set_export_error(error_message, error_message_count, L"The export path is too long.", ERROR_BUFFER_OVERFLOW);
        return EXPORT_RESULT_FAILED;
    }

    if (!export_events_to_file(
            store,
            first_event,
            path,
            format,
            error_message,
            error_message_count)) {
        return EXPORT_RESULT_FAILED;
    }
    copy_wstring(saved_path, saved_path_count, path);
    return EXPORT_RESULT_SUCCEEDED;
}
