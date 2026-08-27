#ifndef TRACEGLASS_PROCESS_MONITOR_H
#define TRACEGLASS_PROCESS_MONITOR_H

#include <windows.h>
#include <stddef.h>
#include "event_queue.h"

typedef struct ProcessSnapshotEntry {
    ProcessKey key;
    DWORD parent_pid;
    WCHAR name[MAX_PATH];
    WCHAR path[MAX_PATH];
} ProcessSnapshotEntry;

typedef struct ProcessMonitor {
    ProcessSnapshotEntry *previous;
    size_t previous_count;
    BOOL initialized;
} ProcessMonitor;

void process_monitor_initialize(ProcessMonitor *monitor);
void process_monitor_destroy(ProcessMonitor *monitor);
BOOL process_monitor_poll(ProcessMonitor *monitor, EventQueue *queue);
BOOL query_process_identity(DWORD pid, ProcessKey *key, WCHAR *path, size_t path_count);
BOOL query_process_name(DWORD pid, WCHAR *name, size_t name_count);

#endif
