#ifndef TRACEGLASS_EVENTS_H
#define TRACEGLASS_EVENTS_H

#include <windows.h>

#define TRACEGLASS_DETAIL_LENGTH 1024
#define TRACEGLASS_PROTOCOL_LENGTH 16
#define TRACEGLASS_ADDRESS_LENGTH 64
#define TRACEGLASS_STATE_LENGTH 32
#define TRACEGLASS_RULE_LENGTH 96

typedef enum EventType {
    EVENT_PROCESS_START = 0,
    EVENT_PROCESS_STOP,
    EVENT_NETWORK,
    EVENT_FILE,
    EVENT_REGISTRY,
    EVENT_ALERT
} EventType;

typedef enum AlertSeverity {
    ALERT_NONE = 0,
    ALERT_INFO,
    ALERT_LOW,
    ALERT_MEDIUM,
    ALERT_HIGH
} AlertSeverity;

/*
 * PID alone is not stable because Windows may reuse it.  The process creation
 * time is therefore carried with every event whenever the OS permits it.
 */
typedef struct ProcessKey {
    DWORD pid;
    ULONGLONG creation_time;
} ProcessKey;

typedef struct TraceGlassEvent {
    EventType type;
    AlertSeverity severity;
    ULONGLONG sequence;
    ProcessKey process;
    ProcessKey parent;
    SYSTEMTIME timestamp;
    WCHAR process_name[MAX_PATH];
    WCHAR parent_name[MAX_PATH];
    WCHAR executable_path[MAX_PATH];
    WCHAR protocol[TRACEGLASS_PROTOCOL_LENGTH];
    WCHAR local_address[TRACEGLASS_ADDRESS_LENGTH];
    WCHAR remote_address[TRACEGLASS_ADDRESS_LENGTH];
    WCHAR network_state[TRACEGLASS_STATE_LENGTH];
    WCHAR rule_name[TRACEGLASS_RULE_LENGTH];
    USHORT local_port;
    USHORT remote_port;
    WCHAR details[TRACEGLASS_DETAIL_LENGTH];
} TraceGlassEvent;

const WCHAR *event_type_name(EventType type);
const WCHAR *event_category_name(EventType type);
const WCHAR *event_export_name(EventType type);
const WCHAR *alert_severity_name(AlertSeverity severity);

#endif
