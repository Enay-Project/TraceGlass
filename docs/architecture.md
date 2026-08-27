# TraceGlass architecture

TraceGlass keeps telemetry collection separate from Win32 rendering. The
collectors never call GUI APIs, and the GUI thread is the sole owner of the
event store and common controls.

```text
                 Windows
                    |
        +-----------+-----------+
        |                       |
       ETW                  Win32 APIs
        |                       |
        +-----------+-----------+
                    |
              Telemetry Layer
                    |
               Event Queue
                    |
        +-----------+-----------+
        |                       |
  Event Store              Detection
        |                       |
        +-----------+-----------+
                    |
                  GUI
```

## Components

| Component | Responsibility |
| --- | --- |
| `etw_monitor.c` | Optional real-time process start/stop consumer for `Microsoft-Windows-Kernel-Process` |
| `process_monitor.c` | Toolhelp inventory and process start/stop reconciliation |
| `network_monitor.c` | Newly observed, established IPv4 TCP rows with owning PID |
| `event_queue.c` | Critical-section-protected value queue and coalesced window notification |
| `event_store.c` | Event history, process records, deduplication, and session counters |
| `detection.c` | Small, local, observation-only alert rules |
| `export.c` | Native Save As flow and UTF-8 JSON/CSV/TXT serializers |
| `gui.c` | Overview, process tree, tables, filtering, sorting, dialogs, and status |

## Threading

The polling worker takes a Toolhelp process snapshot and an IP Helper TCP
snapshot once per second. When available, a separate ETW consumer thread reads
the real-time process session. Both producers copy complete `TraceGlassEvent`
values into the same queue.

```text
ETW consumer --------+
                     |
Polling worker ------+----> EventQueue ----WM_APP----> GUI thread
                                                       |
                                                       +--> EventStore
                                                       +--> Detection
                                                       +--> Win32 controls
```

The queue posts at most one outstanding window notification. The GUI drains
all queued values per notification, which avoids a window-message flood during
the startup inventory. Pause affects rendering only: producers and the GUI-side
store continue to accept events, then the controls are rebuilt on Resume.

## Process identity and correlation

Windows can reuse a PID. TraceGlass therefore correlates a process with
`PID + creation FILETIME` whenever `GetProcessTimes` succeeds. PID-only matching
is a fallback for protected or already-exited processes whose creation time
cannot be queried. Parent records carry the same identity shape.

ETW and Toolhelp can report the same start/stop transition. The store coalesces
duplicate process identities while retaining Toolhelp reconciliation. Network
events are associated with the owning PID and the creation time visible at the
time of polling. Network rows are deliberately not inserted into the process
tree.

## Telemetry modes

TraceGlass attempts a private ETW process session without requesting automatic
elevation. If the session or provider cannot be opened, it records the Windows
error and continues in **Win32 Fallback** mode. The Overview and status bar
always display the active mode. Toolhelp reconciliation remains enabled when
ETW is active.

## Shutdown

Window shutdown follows one ownership order:

1. signal the polling worker and stop the ETW session;
2. wait for both worker threads and close their thread/session handles;
3. detach and destroy the event queue;
4. release process/network snapshots and the event store;
5. destroy GUI fonts and controls;
6. close the diagnostic log in `wWinMain`.

No collector survives the main window, and queued heap nodes are released even
if they were not rendered.

## Performance characteristics

Process and connection snapshots are sorted, then checked with binary search
instead of repeated full scans. Event lists append new rows and only re-sort
continuously after a user selects a sort column. Search rebuilds are debounced
by 250 ms. The process tree is rebuilt only for accepted process start/stop
events, never for network activity.

The event store remains session-memory-backed in v0.1.0. A retention policy is
intentionally deferred so that the release does not silently discard evidence.
