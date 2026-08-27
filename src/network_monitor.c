#include "network_monitor.h"

#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <strsafe.h>
#include "logger.h"
#include "process_monitor.h"
#include "utils.h"

static int compare_connection_keys(const void *left_value, const void *right_value) {
    const NetworkConnectionKey *left = (const NetworkConnectionKey *)left_value;
    const NetworkConnectionKey *right = (const NetworkConnectionKey *)right_value;
#define COMPARE_FIELD(field) \
    do { \
        if (left->field < right->field) return -1; \
        if (left->field > right->field) return 1; \
    } while (0)
    COMPARE_FIELD(pid);
    COMPARE_FIELD(local_address);
    COMPARE_FIELD(local_port);
    COMPARE_FIELD(remote_address);
    COMPARE_FIELD(remote_port);
#undef COMPARE_FIELD
    return 0;
}

static BOOL connection_exists(
    const NetworkConnectionKey *entries,
    size_t count,
    const NetworkConnectionKey *key
) {
    return count > 0 && bsearch(
        key,
        entries,
        count,
        sizeof(*entries),
        compare_connection_keys
    ) != NULL;
}

static void address_to_string(DWORD address, WCHAR *buffer, size_t buffer_count) {
    IN_ADDR ipv4_address;
    ipv4_address.S_un.S_addr = address;
    if (InetNtopW(AF_INET, &ipv4_address, buffer, (DWORD)buffer_count) == NULL) {
        copy_wstring(buffer, buffer_count, L"0.0.0.0");
    }
}

static void emit_network_event(const NetworkConnectionKey *key, EventQueue *queue) {
    TraceGlassEvent event;
    WCHAR path[MAX_PATH];

    ZeroMemory(&event, sizeof(event));
    event.type = EVENT_NETWORK;
    event.process.pid = key->pid;
    query_process_identity(key->pid, &event.process, path, ARRAYSIZE(path));
    copy_wstring(event.executable_path, ARRAYSIZE(event.executable_path), path);
    if (path[0] != L'\0') {
        copy_wstring(event.process_name, ARRAYSIZE(event.process_name), path_basename(path));
    } else if (!query_process_name(key->pid, event.process_name, ARRAYSIZE(event.process_name))) {
        copy_wstring(event.process_name, ARRAYSIZE(event.process_name), L"<unknown>");
    }
    copy_wstring(event.protocol, ARRAYSIZE(event.protocol), L"TCP");
    copy_wstring(event.network_state, ARRAYSIZE(event.network_state), L"ESTABLISHED");
    address_to_string(key->local_address, event.local_address, ARRAYSIZE(event.local_address));
    address_to_string(key->remote_address, event.remote_address, ARRAYSIZE(event.remote_address));
    event.local_port = key->local_port;
    event.remote_port = key->remote_port;
    get_local_system_time(&event.timestamp);
    StringCchPrintfW(
        event.details,
        ARRAYSIZE(event.details),
        L"Established TCP connection; local %s:%u; remote %s:%u",
        event.local_address,
        (unsigned int)event.local_port,
        event.remote_address,
        (unsigned int)event.remote_port
    );
    push_event(queue, &event);
}

BOOL network_monitor_initialize(NetworkMonitor *monitor) {
    WSADATA winsock_data;
    int status;
    if (monitor == NULL) {
        return FALSE;
    }
    ZeroMemory(monitor, sizeof(*monitor));
    status = WSAStartup(MAKEWORD(2, 2), &winsock_data);
    if (status != 0) {
        logger_write(L"ERROR", L"WSAStartup failed: %d", status);
        return FALSE;
    }
    monitor->winsock_initialized = TRUE;
    return TRUE;
}

void network_monitor_destroy(NetworkMonitor *monitor) {
    if (monitor == NULL) {
        return;
    }
    free(monitor->previous);
    if (monitor->winsock_initialized) {
        WSACleanup();
    }
    ZeroMemory(monitor, sizeof(*monitor));
}

BOOL network_monitor_poll(NetworkMonitor *monitor, EventQueue *queue) {
    PMIB_TCPTABLE_OWNER_PID table = NULL;
    DWORD table_size = 0;
    DWORD status;
    NetworkConnectionKey *current = NULL;
    size_t current_count = 0;
    DWORD row_index;

    if (monitor == NULL || queue == NULL) {
        return FALSE;
    }

    /*
     * GetExtendedTcpTable associates each IPv4 TCP row with its owning PID.
     * This MVP reports newly observed established connections.
     */
    status = GetExtendedTcpTable(
        NULL,
        &table_size,
        FALSE,
        AF_INET,
        TCP_TABLE_OWNER_PID_ALL,
        0
    );
    if (status != ERROR_INSUFFICIENT_BUFFER || table_size == 0) {
        logger_write(L"WARN", L"TCP table size query failed: %lu", status);
        return FALSE;
    }

    table = (PMIB_TCPTABLE_OWNER_PID)malloc(table_size);
    if (table == NULL) {
        return FALSE;
    }
    status = GetExtendedTcpTable(
        table,
        &table_size,
        FALSE,
        AF_INET,
        TCP_TABLE_OWNER_PID_ALL,
        0
    );
    if (status != NO_ERROR) {
        logger_write(L"WARN", L"GetExtendedTcpTable failed: %lu", status);
        free(table);
        return FALSE;
    }

    if (table->dwNumEntries > 0) {
        current = (NetworkConnectionKey *)calloc(table->dwNumEntries, sizeof(*current));
        if (current == NULL) {
            free(table);
            return FALSE;
        }
    }

    for (row_index = 0; row_index < table->dwNumEntries; ++row_index) {
        const MIB_TCPROW_OWNER_PID *row = &table->table[row_index];
        NetworkConnectionKey key;
        if (row->dwState != MIB_TCP_STATE_ESTAB) {
            continue;
        }
        key.pid = row->dwOwningPid;
        key.local_address = row->dwLocalAddr;
        key.remote_address = row->dwRemoteAddr;
        key.local_port = ntohs((u_short)row->dwLocalPort);
        key.remote_port = ntohs((u_short)row->dwRemotePort);
        current[current_count++] = key;

        if (!connection_exists(monitor->previous, monitor->previous_count, &key)) {
            emit_network_event(&key, queue);
        }
    }

    if (current_count > 1) {
        qsort(current, current_count, sizeof(*current), compare_connection_keys);
    }

    free(table);
    free(monitor->previous);
    monitor->previous = current;
    monitor->previous_count = current_count;
    return TRUE;
}
