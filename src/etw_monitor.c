#include "etw_monitor.h"

#include <limits.h>
#include <stdlib.h>
#include <tdh.h>
#include <strsafe.h>
#include "logger.h"
#include "process_monitor.h"
#include "utils.h"

static const GUID TRACEGLASS_KERNEL_PROCESS_PROVIDER = {
    0x22fb2cd6, 0x0e7b, 0x422b,
    {0xa0, 0xc7, 0x2f, 0xad, 0x1f, 0xd0, 0xe7, 0x16}
};

#define TRACEGLASS_PROCESS_KEYWORD 0x10ULL
#define TRACEGLASS_PROCESS_START_EVENT_ID 1U
#define TRACEGLASS_PROCESS_STOP_EVENT_ID 2U

static BOOL read_event_property(
    PEVENT_RECORD event_record,
    const WCHAR *property_name,
    BYTE *buffer,
    ULONG buffer_capacity,
    ULONG *property_size
) {
    PROPERTY_DATA_DESCRIPTOR descriptor;
    ULONG size = 0;
    ULONG status;
    ZeroMemory(&descriptor, sizeof(descriptor));
    descriptor.PropertyName = (ULONGLONG)(ULONG_PTR)property_name;
    descriptor.ArrayIndex = ULONG_MAX;

    status = TdhGetPropertySize(event_record, 0, NULL, 1, &descriptor, &size);
    if (status != ERROR_SUCCESS || size == 0 || size > buffer_capacity) {
        return FALSE;
    }
    status = TdhGetProperty(event_record, 0, NULL, 1, &descriptor, size, buffer);
    if (status != ERROR_SUCCESS) {
        return FALSE;
    }
    if (property_size != NULL) {
        *property_size = size;
    }
    return TRUE;
}

static DWORD read_event_dword(PEVENT_RECORD event_record, const WCHAR *first, const WCHAR *second) {
    DWORD value = 0;
    ULONG size = 0;
    if (read_event_property(event_record, first, (BYTE *)&value, (ULONG)sizeof(value), &size) &&
        size >= sizeof(value)) {
        return value;
    }
    value = 0;
    if (second != NULL &&
        read_event_property(event_record, second, (BYTE *)&value, (ULONG)sizeof(value), &size) &&
        size >= sizeof(value)) {
        return value;
    }
    return 0;
}

static ULONGLONG read_event_qword(
    PEVENT_RECORD event_record,
    const WCHAR *first,
    const WCHAR *second
) {
    ULONGLONG value = 0;
    ULONG size = 0;
    if (read_event_property(event_record, first, (BYTE *)&value, (ULONG)sizeof(value), &size) &&
        size >= sizeof(value)) {
        return value;
    }
    value = 0;
    if (second != NULL &&
        read_event_property(event_record, second, (BYTE *)&value, (ULONG)sizeof(value), &size) &&
        size >= sizeof(value)) {
        return value;
    }
    return 0;
}

static void read_event_string(
    PEVENT_RECORD event_record,
    const WCHAR *property_name,
    WCHAR *destination,
    size_t destination_count
) {
    BYTE property[2048];
    ULONG property_size = 0;
    destination[0] = L'\0';
    if (!read_event_property(
            event_record,
            property_name,
            property,
            (ULONG)sizeof(property),
            &property_size)) {
        return;
    }
    if (property_size >= sizeof(WCHAR)) {
        property[sizeof(property) - 2] = 0;
        property[sizeof(property) - 1] = 0;
        copy_wstring(destination, destination_count, (const WCHAR *)property);
    }
}

static VOID WINAPI etw_event_callback(PEVENT_RECORD event_record) {
    EtwProcessMonitor *monitor;
    TraceGlassEvent event;
    DWORD event_id;
    WCHAR image_name[MAX_PATH];
    WCHAR path[MAX_PATH];
    ProcessKey queried_key;

    if (event_record == NULL || event_record->UserContext == NULL) {
        return;
    }
    monitor = (EtwProcessMonitor *)event_record->UserContext;
    if (!monitor->running || !IsEqualGUID(&event_record->EventHeader.ProviderId,
                                          &TRACEGLASS_KERNEL_PROCESS_PROVIDER)) {
        return;
    }

    event_id = event_record->EventHeader.EventDescriptor.Id;
    if (event_id != TRACEGLASS_PROCESS_START_EVENT_ID &&
        event_id != TRACEGLASS_PROCESS_STOP_EVENT_ID) {
        return;
    }

    ZeroMemory(&event, sizeof(event));
    event.type = event_id == TRACEGLASS_PROCESS_START_EVENT_ID
        ? EVENT_PROCESS_START
        : EVENT_PROCESS_STOP;
    event.process.pid = read_event_dword(event_record, L"ProcessID", L"ProcessId");
    event.process.creation_time = read_event_qword(
        event_record,
        L"CreateTime",
        L"CreationTime"
    );
    event.parent.pid = read_event_dword(event_record, L"ParentProcessID", L"ParentProcessId");
    if (event.process.pid == 0) {
        event.process.pid = event_record->EventHeader.ProcessId;
    }

    read_event_string(event_record, L"ImageName", image_name, ARRAYSIZE(image_name));
    query_process_identity(event.process.pid, &queried_key, path, ARRAYSIZE(path));
    if (queried_key.creation_time != 0) {
        event.process.creation_time = queried_key.creation_time;
    }
    copy_wstring(event.executable_path, ARRAYSIZE(event.executable_path), path);
    if (image_name[0] != L'\0') {
        copy_wstring(event.process_name, ARRAYSIZE(event.process_name), path_basename(image_name));
    } else if (path[0] != L'\0') {
        copy_wstring(event.process_name, ARRAYSIZE(event.process_name), path_basename(path));
    } else {
        query_process_name(event.process.pid, event.process_name, ARRAYSIZE(event.process_name));
    }

    if (event.parent.pid != 0) {
        WCHAR parent_path[MAX_PATH];
        query_process_identity(
            event.parent.pid,
            &event.parent,
            parent_path,
            ARRAYSIZE(parent_path)
        );
        if (event.process.creation_time != 0 && event.parent.creation_time != 0 &&
            event.parent.creation_time > event.process.creation_time) {
            event.parent.creation_time = 0;
            parent_path[0] = L'\0';
        } else if (parent_path[0] != L'\0') {
            copy_wstring(event.parent_name, ARRAYSIZE(event.parent_name), path_basename(parent_path));
        } else {
            query_process_name(event.parent.pid, event.parent_name, ARRAYSIZE(event.parent_name));
        }
    }

    if (!local_systemtime_from_filetime(
            (ULONGLONG)event_record->EventHeader.TimeStamp.QuadPart,
            &event.timestamp)) {
        get_local_system_time(&event.timestamp);
    }
    if (event.type == EVENT_PROCESS_START) {
        StringCchPrintfW(
            event.details,
            ARRAYSIZE(event.details),
            L"Process start observed through ETW; parent %s (PID %lu); path %s",
            event.parent_name[0] != L'\0' ? event.parent_name : L"<unknown>",
            event.parent.pid,
            event.executable_path[0] != L'\0' ? event.executable_path : L"<unavailable>"
        );
    } else {
        StringCchPrintfW(
            event.details,
            ARRAYSIZE(event.details),
            L"Process stop observed through ETW (PID %lu)",
            event.process.pid
        );
    }
    push_event(monitor->queue, &event);
}

static DWORD WINAPI etw_consumer_thread(LPVOID parameter) {
    EtwProcessMonitor *monitor = (EtwProcessMonitor *)parameter;
    ULONG status = ProcessTrace(&monitor->trace_handle, 1, NULL, NULL);
    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) {
        logger_write(L"WARN", L"ETW ProcessTrace ended with status %lu", status);
    }
    return status;
}

static PEVENT_TRACE_PROPERTIES create_trace_properties(
    const WCHAR *session_name,
    size_t *allocation_size
) {
    size_t name_bytes = (wcslen(session_name) + 1) * sizeof(WCHAR);
    size_t total_size = sizeof(EVENT_TRACE_PROPERTIES) + name_bytes;
    PEVENT_TRACE_PROPERTIES properties = (PEVENT_TRACE_PROPERTIES)calloc(1, total_size);
    if (properties == NULL) {
        return NULL;
    }
    properties->Wnode.BufferSize = (ULONG)total_size;
    properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    properties->Wnode.ClientContext = 2;
    properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    copy_wstring(
        (WCHAR *)((BYTE *)properties + properties->LoggerNameOffset),
        name_bytes / sizeof(WCHAR),
        session_name
    );
    if (allocation_size != NULL) {
        *allocation_size = total_size;
    }
    return properties;
}

BOOL etw_process_monitor_start(EtwProcessMonitor *monitor, EventQueue *queue) {
    PEVENT_TRACE_PROPERTIES properties;
    EVENT_TRACE_LOGFILEW trace_log;
    ULONG status;
    size_t allocation_size;

    if (monitor == NULL || queue == NULL) {
        return FALSE;
    }
    ZeroMemory(monitor, sizeof(*monitor));
    monitor->session_handle = 0;
    monitor->trace_handle = INVALID_PROCESSTRACE_HANDLE;
    monitor->queue = queue;
    StringCchPrintfW(
        monitor->session_name,
        ARRAYSIZE(monitor->session_name),
        L"TraceGlass-Process-%lu",
        GetCurrentProcessId()
    );

    properties = create_trace_properties(monitor->session_name, &allocation_size);
    if (properties == NULL) {
        monitor->last_error = ERROR_OUTOFMEMORY;
        return FALSE;
    }
    (void)allocation_size;

    /* Starts a private real-time ETW session for kernel process events. */
    status = StartTraceW(&monitor->session_handle, monitor->session_name, properties);
    if (status != ERROR_SUCCESS) {
        monitor->last_error = status;
        logger_write(L"WARN", L"ETW process session could not be started (%lu)", status);
        free(properties);
        return FALSE;
    }

    status = EnableTraceEx2(
        monitor->session_handle,
        &TRACEGLASS_KERNEL_PROCESS_PROVIDER,
        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_INFORMATION,
        TRACEGLASS_PROCESS_KEYWORD,
        0,
        0,
        NULL
    );
    if (status != ERROR_SUCCESS) {
        monitor->last_error = status;
        logger_write(L"WARN", L"ETW process provider could not be enabled (%lu)", status);
        ControlTraceW(monitor->session_handle, monitor->session_name, properties, EVENT_TRACE_CONTROL_STOP);
        monitor->session_handle = 0;
        free(properties);
        return FALSE;
    }

    ZeroMemory(&trace_log, sizeof(trace_log));
    trace_log.LoggerName = monitor->session_name;
    trace_log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    trace_log.EventRecordCallback = etw_event_callback;
    trace_log.Context = monitor;
    monitor->trace_handle = OpenTraceW(&trace_log);
    if (monitor->trace_handle == INVALID_PROCESSTRACE_HANDLE) {
        monitor->last_error = GetLastError();
        logger_write(L"WARN", L"ETW process consumer could not be opened (%lu)", monitor->last_error);
        ControlTraceW(monitor->session_handle, monitor->session_name, properties, EVENT_TRACE_CONTROL_STOP);
        monitor->session_handle = 0;
        free(properties);
        return FALSE;
    }
    free(properties);

    InterlockedExchange(&monitor->running, TRUE);
    monitor->thread = CreateThread(NULL, 0, etw_consumer_thread, monitor, 0, NULL);
    if (monitor->thread == NULL) {
        monitor->last_error = GetLastError();
        InterlockedExchange(&monitor->running, FALSE);
        CloseTrace(monitor->trace_handle);
        monitor->trace_handle = INVALID_PROCESSTRACE_HANDLE;
        properties = create_trace_properties(monitor->session_name, NULL);
        if (properties != NULL) {
            ControlTraceW(monitor->session_handle, monitor->session_name, properties, EVENT_TRACE_CONTROL_STOP);
            free(properties);
        }
        monitor->session_handle = 0;
        return FALSE;
    }
    monitor->enabled = TRUE;
    monitor->last_error = ERROR_SUCCESS;
    logger_write(L"INFO", L"Microsoft-Windows-Kernel-Process ETW session enabled");
    return TRUE;
}

void etw_process_monitor_stop(EtwProcessMonitor *monitor) {
    PEVENT_TRACE_PROPERTIES properties;
    if (monitor == NULL || !monitor->enabled) {
        return;
    }

    InterlockedExchange(&monitor->running, FALSE);
    EnableTraceEx2(
        monitor->session_handle,
        &TRACEGLASS_KERNEL_PROCESS_PROVIDER,
        EVENT_CONTROL_CODE_DISABLE_PROVIDER,
        TRACE_LEVEL_NONE,
        0,
        0,
        0,
        NULL
    );
    properties = create_trace_properties(monitor->session_name, NULL);
    if (properties != NULL) {
        ControlTraceW(
            monitor->session_handle,
            monitor->session_name,
            properties,
            EVENT_TRACE_CONTROL_STOP
        );
        free(properties);
    }
    if (monitor->trace_handle != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(monitor->trace_handle);
    }
    if (monitor->thread != NULL) {
        WaitForSingleObject(monitor->thread, INFINITE);
        CloseHandle(monitor->thread);
    }
    ZeroMemory(monitor, sizeof(*monitor));
}
