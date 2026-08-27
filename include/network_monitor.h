#ifndef TRACEGLASS_NETWORK_MONITOR_H
#define TRACEGLASS_NETWORK_MONITOR_H

#include <windows.h>
#include <stddef.h>
#include "event_queue.h"

typedef struct NetworkConnectionKey {
    DWORD pid;
    DWORD local_address;
    DWORD remote_address;
    USHORT local_port;
    USHORT remote_port;
} NetworkConnectionKey;

typedef struct NetworkMonitor {
    NetworkConnectionKey *previous;
    size_t previous_count;
    BOOL winsock_initialized;
} NetworkMonitor;

BOOL network_monitor_initialize(NetworkMonitor *monitor);
void network_monitor_destroy(NetworkMonitor *monitor);
BOOL network_monitor_poll(NetworkMonitor *monitor, EventQueue *queue);

#endif
