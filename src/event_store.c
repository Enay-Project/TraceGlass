#include "event_store.h"

#include <stdlib.h>
#include <stdint.h>
#include "utils.h"

static BOOL process_keys_match(ProcessKey left, ProcessKey right) {
    if (left.pid != right.pid) {
        return FALSE;
    }
    return left.creation_time == right.creation_time ||
        left.creation_time == 0 ||
        right.creation_time == 0;
}

static BOOL reserve_events(EventStore *store, size_t required) {
    TraceGlassEvent *resized;
    size_t capacity;
    if (required <= store->event_capacity) {
        return TRUE;
    }
    if (store->event_capacity == 0) {
        capacity = 256;
    } else {
        if (store->event_capacity > SIZE_MAX / 2) {
            return FALSE;
        }
        capacity = store->event_capacity * 2;
    }
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            return FALSE;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*resized)) {
        return FALSE;
    }
    resized = (TraceGlassEvent *)realloc(store->events, capacity * sizeof(*resized));
    if (resized == NULL) {
        return FALSE;
    }
    store->events = resized;
    store->event_capacity = capacity;
    return TRUE;
}

static BOOL reserve_processes(EventStore *store, size_t required) {
    ProcessRecord *resized;
    size_t capacity;
    if (required <= store->process_capacity) {
        return TRUE;
    }
    if (store->process_capacity == 0) {
        capacity = 128;
    } else {
        if (store->process_capacity > SIZE_MAX / 2) {
            return FALSE;
        }
        capacity = store->process_capacity * 2;
    }
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            return FALSE;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*resized)) {
        return FALSE;
    }
    resized = (ProcessRecord *)realloc(store->processes, capacity * sizeof(*resized));
    if (resized == NULL) {
        return FALSE;
    }
    store->processes = resized;
    store->process_capacity = capacity;
    return TRUE;
}

BOOL event_store_initialize(EventStore *store) {
    if (store == NULL) {
        return FALSE;
    }
    ZeroMemory(store, sizeof(*store));
    store->next_sequence = 1;
    return TRUE;
}

void event_store_destroy(EventStore *store) {
    if (store == NULL) {
        return;
    }
    free(store->events);
    free(store->processes);
    ZeroMemory(store, sizeof(*store));
}

ProcessRecord *event_store_find_process(EventStore *store, ProcessKey key) {
    size_t index;
    ProcessRecord *active_match = NULL;
    if (store == NULL || key.pid == 0) {
        return NULL;
    }
    for (index = store->process_count; index > 0; --index) {
        ProcessRecord *record = &store->processes[index - 1];
        if (process_keys_match(record->key, key)) {
            if (record->key.creation_time == key.creation_time && key.creation_time != 0) {
                return record;
            }
            if (record->active && active_match == NULL) {
                active_match = record;
            }
        }
    }
    return active_match;
}

const ProcessRecord *event_store_find_process_const(const EventStore *store, ProcessKey key) {
    return event_store_find_process((EventStore *)store, key);
}

static void update_record_from_event(ProcessRecord *record, const TraceGlassEvent *event) {
    if (event->process.creation_time != 0) {
        record->key.creation_time = event->process.creation_time;
    }
    if (event->parent.pid != 0) {
        record->parent = event->parent;
    }
    if (event->process_name[0] != L'\0') {
        copy_wstring(record->name, ARRAYSIZE(record->name), event->process_name);
    }
    if (event->parent_name[0] != L'\0') {
        copy_wstring(record->parent_name, ARRAYSIZE(record->parent_name), event->parent_name);
    }
    if (event->executable_path[0] != L'\0') {
        copy_wstring(record->path, ARRAYSIZE(record->path), event->executable_path);
    }
}

BOOL event_store_add(EventStore *store, TraceGlassEvent *event, BOOL *accepted) {
    ProcessRecord *record;
    if (accepted != NULL) {
        *accepted = FALSE;
    }
    if (store == NULL || event == NULL) {
        return FALSE;
    }

    record = NULL;
    if (event->type == EVENT_PROCESS_START) {
        record = event_store_find_process(store, event->process);
        if (record != NULL) {
            update_record_from_event(record, event);
            /* ETW and the reconciliation poller may report the same identity. */
            return TRUE;
        } else {
            if (!reserve_processes(store, store->process_count + 1)) {
                return FALSE;
            }
            record = &store->processes[store->process_count++];
            ZeroMemory(record, sizeof(*record));
            record->key = event->process;
            record->parent = event->parent;
            update_record_from_event(record, event);
        }
        record->active = TRUE;
        record->start_time = event->timestamp;
        ++store->active_process_count;
    } else if (event->type == EVENT_PROCESS_STOP) {
        record = event_store_find_process(store, event->process);
        if (record != NULL && !record->active) {
            return TRUE;
        }
        if (record == NULL) {
            if (!reserve_processes(store, store->process_count + 1)) {
                return FALSE;
            }
            record = &store->processes[store->process_count++];
            ZeroMemory(record, sizeof(*record));
            record->key = event->process;
            record->parent = event->parent;
            update_record_from_event(record, event);
        } else {
            update_record_from_event(record, event);
        }
        if (record->active && store->active_process_count > 0) {
            --store->active_process_count;
        }
        record->active = FALSE;
        record->stop_time = event->timestamp;
    } else if (event->type == EVENT_NETWORK) {
        ++store->network_count;
    } else if (event->type == EVENT_ALERT) {
        ++store->alert_count;
    }

    if (!reserve_events(store, store->event_count + 1)) {
        return FALSE;
    }
    event->sequence = store->next_sequence++;
    store->events[store->event_count++] = *event;
    if (accepted != NULL) {
        *accepted = TRUE;
    }
    return TRUE;
}
