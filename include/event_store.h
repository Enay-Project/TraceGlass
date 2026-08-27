#ifndef TRACEGLASS_EVENT_STORE_H
#define TRACEGLASS_EVENT_STORE_H

#include <windows.h>
#include <stddef.h>
#include "events.h"

typedef struct ProcessRecord {
    ProcessKey key;
    ProcessKey parent;
    BOOL active;
    WCHAR name[MAX_PATH];
    WCHAR parent_name[MAX_PATH];
    WCHAR path[MAX_PATH];
    SYSTEMTIME start_time;
    SYSTEMTIME stop_time;
} ProcessRecord;

typedef struct EventStore {
    TraceGlassEvent *events;
    size_t event_count;
    size_t event_capacity;
    ProcessRecord *processes;
    size_t process_count;
    size_t process_capacity;
    size_t active_process_count;
    size_t network_count;
    size_t alert_count;
    ULONGLONG next_sequence;
} EventStore;

BOOL event_store_initialize(EventStore *store);
void event_store_destroy(EventStore *store);
BOOL event_store_add(EventStore *store, TraceGlassEvent *event, BOOL *accepted);
ProcessRecord *event_store_find_process(EventStore *store, ProcessKey key);
const ProcessRecord *event_store_find_process_const(const EventStore *store, ProcessKey key);

#endif
