#ifndef TRACEGLASS_EVENT_QUEUE_H
#define TRACEGLASS_EVENT_QUEUE_H

#include <windows.h>
#include "events.h"

typedef struct EventNode {
    TraceGlassEvent event;
    struct EventNode *next;
} EventNode;

typedef struct EventQueue {
    CRITICAL_SECTION lock;
    EventNode *head;
    EventNode *tail;
    HWND notification_window;
    UINT notification_message;
    BOOL notification_pending;
    volatile LONG initialized;
} EventQueue;

BOOL event_queue_initialize(EventQueue *queue, HWND window, UINT message);
void event_queue_set_window(EventQueue *queue, HWND window);
BOOL push_event(EventQueue *queue, const TraceGlassEvent *event);
BOOL pop_event(EventQueue *queue, TraceGlassEvent *event);
void event_queue_destroy(EventQueue *queue);

#endif
