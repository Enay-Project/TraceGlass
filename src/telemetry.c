#include "telemetry.h"

#include "logger.h"
#include "traceglass.h"
#include "utils.h"

static DWORD WINAPI telemetry_worker(LPVOID parameter) {
    TelemetryEngine *engine = (TelemetryEngine *)parameter;
    logger_write(L"INFO", L"Telemetry polling worker started");

    while (WaitForSingleObject(engine->stop_event, 0) == WAIT_TIMEOUT) {
        process_monitor_poll(&engine->process_monitor, engine->queue);
        if (engine->network_available) {
            network_monitor_poll(&engine->network_monitor, engine->queue);
        }
        if (WaitForSingleObject(engine->stop_event, TRACEGLASS_POLL_INTERVAL_MS) != WAIT_TIMEOUT) {
            break;
        }
    }

    logger_write(L"INFO", L"Telemetry polling worker stopped");
    return 0;
}

BOOL initialize_telemetry(TelemetryEngine *engine, EventQueue *queue) {
    if (engine == NULL || queue == NULL) {
        return FALSE;
    }
    ZeroMemory(engine, sizeof(*engine));
    engine->queue = queue;
    process_monitor_initialize(&engine->process_monitor);
    engine->network_available = network_monitor_initialize(&engine->network_monitor);
    if (!engine->network_available) {
        logger_write(L"WARN", L"IPv4 TCP telemetry is unavailable; process monitoring will continue");
    }

    /* Manual-reset stop event lets the worker sleep without a busy loop. */
    engine->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (engine->stop_event == NULL) {
        DWORD error = GetLastError();
        if (engine->network_available) {
            network_monitor_destroy(&engine->network_monitor);
        }
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

BOOL start_telemetry(TelemetryEngine *engine) {
    if (engine == NULL || engine->stop_event == NULL || engine->running) {
        return FALSE;
    }

    ResetEvent(engine->stop_event);
    logger_write(L"INFO", L"ETW provider initialization started");
    if (!etw_process_monitor_start(&engine->etw_monitor, engine->queue)) {
        WCHAR reason[256];
        format_windows_error(engine->etw_monitor.last_error, reason, ARRAYSIZE(reason));
        logger_write(L"WARN", L"ETW unavailable: %s", reason);
        logger_write(L"INFO", L"Win32 process fallback enabled");
    }
    InterlockedExchange(&engine->running, TRUE);
    engine->worker_thread = CreateThread(NULL, 0, telemetry_worker, engine, 0, NULL);
    if (engine->worker_thread == NULL) {
        DWORD error = GetLastError();
        InterlockedExchange(&engine->running, FALSE);
        etw_process_monitor_stop(&engine->etw_monitor);
        logger_write(L"ERROR", L"Could not create telemetry worker: %lu", error);
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

void shutdown_telemetry(TelemetryEngine *engine) {
    if (engine == NULL) {
        return;
    }
    if (engine->running) {
        SetEvent(engine->stop_event);
        if (engine->worker_thread != NULL) {
            WaitForSingleObject(engine->worker_thread, INFINITE);
            CloseHandle(engine->worker_thread);
            engine->worker_thread = NULL;
        }
        InterlockedExchange(&engine->running, FALSE);
    }
    etw_process_monitor_stop(&engine->etw_monitor);
    process_monitor_destroy(&engine->process_monitor);
    if (engine->network_available) {
        network_monitor_destroy(&engine->network_monitor);
        engine->network_available = FALSE;
    }
    if (engine->stop_event != NULL) {
        CloseHandle(engine->stop_event);
        engine->stop_event = NULL;
    }
}

BOOL telemetry_etw_enabled(const TelemetryEngine *engine) {
    return engine != NULL && engine->etw_monitor.enabled;
}

DWORD telemetry_etw_error(const TelemetryEngine *engine) {
    return engine != NULL ? engine->etw_monitor.last_error : ERROR_INVALID_HANDLE;
}

const WCHAR *telemetry_mode_name(const TelemetryEngine *engine) {
    return telemetry_etw_enabled(engine) ? L"ETW" : L"Win32 Fallback";
}
