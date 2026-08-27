#ifndef TRACEGLASS_ETW_MONITOR_H
#define TRACEGLASS_ETW_MONITOR_H

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include "event_queue.h"

typedef struct EtwProcessMonitor {
    TRACEHANDLE session_handle;
    TRACEHANDLE trace_handle;
    HANDLE thread;
    EventQueue *queue;
    WCHAR session_name[64];
    volatile LONG running;
    BOOL enabled;
    DWORD last_error;
} EtwProcessMonitor;

BOOL etw_process_monitor_start(EtwProcessMonitor *monitor, EventQueue *queue);
void etw_process_monitor_stop(EtwProcessMonitor *monitor);

#endif
