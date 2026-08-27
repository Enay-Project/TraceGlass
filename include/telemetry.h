#ifndef TRACEGLASS_TELEMETRY_H
#define TRACEGLASS_TELEMETRY_H

#include <windows.h>
#include "event_queue.h"
#include "etw_monitor.h"
#include "network_monitor.h"
#include "process_monitor.h"

typedef struct TelemetryEngine {
    HANDLE worker_thread;
    HANDLE stop_event;
    EventQueue *queue;
    ProcessMonitor process_monitor;
    NetworkMonitor network_monitor;
    EtwProcessMonitor etw_monitor;
    BOOL network_available;
    volatile LONG running;
} TelemetryEngine;

BOOL initialize_telemetry(TelemetryEngine *engine, EventQueue *queue);
BOOL start_telemetry(TelemetryEngine *engine);
void shutdown_telemetry(TelemetryEngine *engine);
BOOL telemetry_etw_enabled(const TelemetryEngine *engine);
DWORD telemetry_etw_error(const TelemetryEngine *engine);
const WCHAR *telemetry_mode_name(const TelemetryEngine *engine);

#endif
