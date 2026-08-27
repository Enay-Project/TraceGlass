#ifndef TRACEGLASS_EXPORT_H
#define TRACEGLASS_EXPORT_H

#include <windows.h>
#include <stddef.h>
#include "event_store.h"

typedef enum ExportFormat {
    EXPORT_FORMAT_JSON = 1,
    EXPORT_FORMAT_CSV,
    EXPORT_FORMAT_TEXT
} ExportFormat;

typedef enum ExportResult {
    EXPORT_RESULT_CANCELLED = 0,
    EXPORT_RESULT_SUCCEEDED,
    EXPORT_RESULT_FAILED
} ExportResult;

ExportResult export_events_with_dialog(
    HWND owner,
    const EventStore *store,
    size_t first_event,
    WCHAR *saved_path,
    size_t saved_path_count,
    WCHAR *error_message,
    size_t error_message_count
);

BOOL export_events_to_file(
    const EventStore *store,
    size_t first_event,
    const WCHAR *path,
    ExportFormat format,
    WCHAR *error_message,
    size_t error_message_count
);

#endif
