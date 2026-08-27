#include "process_monitor.h"

#include <stdlib.h>
#include <tlhelp32.h>
#include <strsafe.h>
#include "logger.h"
#include "utils.h"

static int compare_snapshot_entries(const void *left_value, const void *right_value) {
    const ProcessSnapshotEntry *left = (const ProcessSnapshotEntry *)left_value;
    const ProcessSnapshotEntry *right = (const ProcessSnapshotEntry *)right_value;
    if (left->key.pid < right->key.pid) {
        return -1;
    }
    if (left->key.pid > right->key.pid) {
        return 1;
    }
    if (left->key.creation_time < right->key.creation_time) {
        return -1;
    }
    return left->key.creation_time > right->key.creation_time ? 1 : 0;
}

static const ProcessSnapshotEntry *find_entry(
    const ProcessSnapshotEntry *entries,
    size_t count,
    ProcessKey key
) {
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (entries[middle].key.pid < key.pid) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    while (low < count && entries[low].key.pid == key.pid) {
        if (entries[low].key.creation_time == key.creation_time ||
            entries[low].key.creation_time == 0 || key.creation_time == 0) {
            return &entries[low];
        }
        ++low;
    }
    return NULL;
}

static const ProcessSnapshotEntry *find_entry_by_pid(
    const ProcessSnapshotEntry *entries,
    size_t count,
    DWORD pid
) {
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (entries[middle].key.pid < pid) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < count && entries[low].key.pid == pid ? &entries[low] : NULL;
}

static void timestamp_from_creation_time(ULONGLONG creation_time, SYSTEMTIME *timestamp) {
    if (!local_systemtime_from_filetime(creation_time, timestamp)) {
        get_local_system_time(timestamp);
    }
}

BOOL query_process_identity(DWORD pid, ProcessKey *key, WCHAR *path, size_t path_count) {
    HANDLE process_handle;
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    DWORD path_length;
    BOOL obtained = FALSE;

    if (key != NULL) {
        key->pid = pid;
        key->creation_time = 0;
    }
    if (path != NULL && path_count > 0) {
        path[0] = L'\0';
    }
    if (pid == 0) {
        return FALSE;
    }

    /*
     * PROCESS_QUERY_LIMITED_INFORMATION is sufficient for the image path and
     * creation time and avoids requesting invasive process access rights.
     */
    process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process_handle == NULL) {
        return FALSE;
    }

    if (key != NULL && GetProcessTimes(
            process_handle,
            &creation_time,
            &exit_time,
            &kernel_time,
            &user_time)) {
        key->creation_time = filetime_to_u64(creation_time);
        obtained = TRUE;
    }

    if (path != NULL && path_count > 0) {
        path_length = (DWORD)path_count;
        if (QueryFullProcessImageNameW(process_handle, 0, path, &path_length)) {
            obtained = TRUE;
        } else {
            path[0] = L'\0';
        }
    }
    CloseHandle(process_handle);
    return obtained;
}

BOOL query_process_name(DWORD pid, WCHAR *name, size_t name_count) {
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    BOOL found = FALSE;
    if (name == NULL || name_count == 0) {
        return FALSE;
    }
    name[0] = L'\0';

    /* Toolhelp provides a non-invasive process inventory for name fallback. */
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                copy_wstring(name, name_count, entry.szExeFile);
                found = TRUE;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

static BOOL capture_processes(ProcessSnapshotEntry **entries_out, size_t *count_out) {
    HANDLE snapshot;
    PROCESSENTRY32W process_entry;
    ProcessSnapshotEntry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    if (entries_out == NULL || count_out == NULL) {
        return FALSE;
    }
    *entries_out = NULL;
    *count_out = 0;

    /*
     * Creates a snapshot of all running processes. The snapshot is later
     * enumerated using Process32First/Process32Next.
     */
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        logger_write(L"ERROR", L"CreateToolhelp32Snapshot failed: %lu", GetLastError());
        return FALSE;
    }

    ZeroMemory(&process_entry, sizeof(process_entry));
    process_entry.dwSize = sizeof(process_entry);
    if (!Process32FirstW(snapshot, &process_entry)) {
        logger_write(L"ERROR", L"Process32FirstW failed: %lu", GetLastError());
        CloseHandle(snapshot);
        return FALSE;
    }

    do {
        ProcessSnapshotEntry *resized;
        ProcessSnapshotEntry *current;
        if (count == capacity) {
            capacity = capacity == 0 ? 128 : capacity * 2;
            resized = (ProcessSnapshotEntry *)realloc(entries, capacity * sizeof(*entries));
            if (resized == NULL) {
                free(entries);
                CloseHandle(snapshot);
                return FALSE;
            }
            entries = resized;
        }

        current = &entries[count++];
        ZeroMemory(current, sizeof(*current));
        current->key.pid = process_entry.th32ProcessID;
        current->parent_pid = process_entry.th32ParentProcessID;
        copy_wstring(current->name, ARRAYSIZE(current->name), process_entry.szExeFile);
        query_process_identity(
            current->key.pid,
            &current->key,
            current->path,
            ARRAYSIZE(current->path)
        );
    } while (Process32NextW(snapshot, &process_entry));

    CloseHandle(snapshot);
    qsort(entries, count, sizeof(*entries), compare_snapshot_entries);
    *entries_out = entries;
    *count_out = count;
    return TRUE;
}

static void emit_start_event(
    const ProcessSnapshotEntry *entry,
    const ProcessSnapshotEntry *entries,
    size_t entry_count,
    EventQueue *queue,
    BOOL baseline
) {
    TraceGlassEvent event;
    const ProcessSnapshotEntry *parent;

    ZeroMemory(&event, sizeof(event));
    event.type = EVENT_PROCESS_START;
    event.process = entry->key;
    event.parent.pid = entry->parent_pid;
    copy_wstring(event.process_name, ARRAYSIZE(event.process_name), entry->name);
    copy_wstring(event.executable_path, ARRAYSIZE(event.executable_path), entry->path);
    parent = find_entry_by_pid(entries, entry_count, entry->parent_pid);
    if (parent != NULL &&
        (parent->key.creation_time == 0 || entry->key.creation_time == 0 ||
         parent->key.creation_time <= entry->key.creation_time)) {
        event.parent = parent->key;
        copy_wstring(event.parent_name, ARRAYSIZE(event.parent_name), parent->name);
    }
    timestamp_from_creation_time(entry->key.creation_time, &event.timestamp);
    StringCchPrintfW(
        event.details,
        ARRAYSIZE(event.details),
        baseline
            ? L"Discovered at startup; parent %s (PID %lu); path %s"
            : L"Started; parent %s (PID %lu); path %s",
        event.parent_name[0] != L'\0' ? event.parent_name : L"<unknown>",
        event.parent.pid,
        event.executable_path[0] != L'\0' ? event.executable_path : L"<access denied or unavailable>"
    );
    push_event(queue, &event);
}

static void emit_stop_event(
    const ProcessSnapshotEntry *entry,
    const ProcessSnapshotEntry *entries,
    size_t entry_count,
    EventQueue *queue
) {
    TraceGlassEvent event;
    const ProcessSnapshotEntry *parent;

    ZeroMemory(&event, sizeof(event));
    event.type = EVENT_PROCESS_STOP;
    event.process = entry->key;
    event.parent.pid = entry->parent_pid;
    copy_wstring(event.process_name, ARRAYSIZE(event.process_name), entry->name);
    copy_wstring(event.executable_path, ARRAYSIZE(event.executable_path), entry->path);
    parent = find_entry_by_pid(entries, entry_count, entry->parent_pid);
    if (parent != NULL &&
        (parent->key.creation_time == 0 || entry->key.creation_time == 0 ||
         parent->key.creation_time <= entry->key.creation_time)) {
        event.parent = parent->key;
        copy_wstring(event.parent_name, ARRAYSIZE(event.parent_name), parent->name);
    }
    get_local_system_time(&event.timestamp);
    StringCchPrintfW(event.details, ARRAYSIZE(event.details), L"Process stopped (PID %lu)", event.process.pid);
    push_event(queue, &event);
}

void process_monitor_initialize(ProcessMonitor *monitor) {
    if (monitor != NULL) {
        ZeroMemory(monitor, sizeof(*monitor));
    }
}

void process_monitor_destroy(ProcessMonitor *monitor) {
    if (monitor == NULL) {
        return;
    }
    free(monitor->previous);
    ZeroMemory(monitor, sizeof(*monitor));
}

BOOL process_monitor_poll(ProcessMonitor *monitor, EventQueue *queue) {
    ProcessSnapshotEntry *current = NULL;
    size_t current_count = 0;
    size_t index;

    if (monitor == NULL || queue == NULL || !capture_processes(&current, &current_count)) {
        return FALSE;
    }

    for (index = 0; index < current_count; ++index) {
        if (find_entry(monitor->previous, monitor->previous_count, current[index].key) == NULL) {
            emit_start_event(&current[index], current, current_count, queue, !monitor->initialized);
        }
    }
    if (monitor->initialized) {
        for (index = 0; index < monitor->previous_count; ++index) {
            if (find_entry(current, current_count, monitor->previous[index].key) == NULL) {
                emit_stop_event(
                    &monitor->previous[index],
                    monitor->previous,
                    monitor->previous_count,
                    queue
                );
            }
        }
    }

    free(monitor->previous);
    monitor->previous = current;
    monitor->previous_count = current_count;
    monitor->initialized = TRUE;
    return TRUE;
}
