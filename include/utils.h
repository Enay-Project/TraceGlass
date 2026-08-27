#ifndef TRACEGLASS_UTILS_H
#define TRACEGLASS_UTILS_H

#include <windows.h>
#include <stddef.h>

ULONGLONG filetime_to_u64(FILETIME value);
BOOL local_systemtime_from_filetime(ULONGLONG filetime_value, SYSTEMTIME *timestamp);
void get_local_system_time(SYSTEMTIME *time_value);
void copy_wstring(WCHAR *destination, size_t destination_count, const WCHAR *source);
BOOL string_contains_insensitive(const WCHAR *value, const WCHAR *needle);
BOOL string_equals_insensitive(const WCHAR *left, const WCHAR *right);
const WCHAR *path_basename(const WCHAR *path);
void format_windows_error(DWORD error, WCHAR *buffer, size_t buffer_count);

#endif
