#include "events.h"

const WCHAR *event_type_name(EventType type) {
    switch (type) {
        case EVENT_PROCESS_START:
            return L"PROCESS START";
        case EVENT_PROCESS_STOP:
            return L"PROCESS STOP";
        case EVENT_NETWORK:
            return L"NETWORK";
        case EVENT_FILE:
            return L"FILE";
        case EVENT_REGISTRY:
            return L"REGISTRY";
        case EVENT_ALERT:
            return L"ALERT";
        default:
            return L"UNKNOWN";
    }
}

const WCHAR *event_category_name(EventType type) {
    switch (type) {
        case EVENT_PROCESS_START:
        case EVENT_PROCESS_STOP:
            return L"Process";
        case EVENT_NETWORK:
            return L"Network";
        case EVENT_FILE:
            return L"File";
        case EVENT_REGISTRY:
            return L"Registry";
        case EVENT_ALERT:
            return L"Alert";
        default:
            return L"Unknown";
    }
}

const WCHAR *event_export_name(EventType type) {
    switch (type) {
        case EVENT_PROCESS_START:
            return L"process_start";
        case EVENT_PROCESS_STOP:
            return L"process_stop";
        case EVENT_NETWORK:
            return L"network_connection";
        case EVENT_FILE:
            return L"file";
        case EVENT_REGISTRY:
            return L"registry";
        case EVENT_ALERT:
            return L"alert";
        default:
            return L"unknown";
    }
}

const WCHAR *alert_severity_name(AlertSeverity severity) {
    switch (severity) {
        case ALERT_INFO:
            return L"INFO";
        case ALERT_LOW:
            return L"LOW";
        case ALERT_MEDIUM:
            return L"MEDIUM";
        case ALERT_HIGH:
            return L"HIGH";
        case ALERT_NONE:
        default:
            return L"NONE";
    }
}
