#include "utils.h"

#include <strsafe.h>
#include <wchar.h>
#include <wctype.h>

ULONGLONG filetime_to_u64(FILETIME value) {
    ULARGE_INTEGER converted;
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

BOOL local_systemtime_from_filetime(ULONGLONG filetime_value, SYSTEMTIME *timestamp) {
    FILETIME utc_filetime;
    FILETIME local_filetime;
    if (filetime_value == 0 || timestamp == NULL) {
        return FALSE;
    }
    utc_filetime.dwLowDateTime = (DWORD)filetime_value;
    utc_filetime.dwHighDateTime = (DWORD)(filetime_value >> 32);
    return FileTimeToLocalFileTime(&utc_filetime, &local_filetime) &&
        FileTimeToSystemTime(&local_filetime, timestamp);
}

void get_local_system_time(SYSTEMTIME *time_value) {
    SYSTEMTIME utc_time;
    if (time_value == NULL) {
        return;
    }

    GetSystemTime(&utc_time);
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc_time, time_value)) {
        *time_value = utc_time;
    }
}

void copy_wstring(WCHAR *destination, size_t destination_count, const WCHAR *source) {
    size_t index;
    if (destination == NULL || destination_count == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = L'\0';
        return;
    }

    for (index = 0; index + 1 < destination_count && source[index] != L'\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = L'\0';
}

BOOL string_contains_insensitive(const WCHAR *value, const WCHAR *needle) {
    size_t value_length;
    size_t needle_length;
    size_t index;
    size_t offset;

    if (value == NULL || needle == NULL) {
        return FALSE;
    }
    value_length = wcslen(value);
    needle_length = wcslen(needle);
    if (needle_length == 0) {
        return TRUE;
    }
    if (needle_length > value_length) {
        return FALSE;
    }

    for (index = 0; index + needle_length <= value_length; ++index) {
        for (offset = 0; offset < needle_length; ++offset) {
            if (towlower(value[index + offset]) != towlower(needle[offset])) {
                break;
            }
        }
        if (offset == needle_length) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL string_equals_insensitive(const WCHAR *left, const WCHAR *right) {
    if (left == NULL || right == NULL) {
        return FALSE;
    }
    return CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

const WCHAR *path_basename(const WCHAR *path) {
    const WCHAR *last_separator;
    const WCHAR *alternate_separator;
    if (path == NULL) {
        return L"";
    }

    last_separator = wcsrchr(path, L'\\');
    alternate_separator = wcsrchr(path, L'/');
    if (alternate_separator != NULL &&
        (last_separator == NULL || alternate_separator > last_separator)) {
        last_separator = alternate_separator;
    }
    return last_separator == NULL ? path : last_separator + 1;
}

void format_windows_error(DWORD error, WCHAR *buffer, size_t buffer_count) {
    DWORD result;
    if (buffer == NULL || buffer_count == 0) {
        return;
    }

    result = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        0,
        buffer,
        (DWORD)buffer_count,
        NULL
    );
    if (result == 0) {
        StringCchPrintfW(buffer, buffer_count, L"Windows error %lu", error);
    } else {
        while (result > 0 && (buffer[result - 1] == L'\r' || buffer[result - 1] == L'\n')) {
            buffer[--result] = L'\0';
        }
    }
}
