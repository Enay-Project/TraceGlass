#include "event_queue.h"

#include <stdlib.h>

BOOL event_queue_initialize(EventQueue *queue, HWND window, UINT message) {
    if (queue == NULL) {
        return FALSE;
    }
    ZeroMemory(queue, sizeof(*queue));
    InitializeCriticalSection(&queue->lock);
    queue->notification_window = window;
    queue->notification_message = message;
    InterlockedExchange(&queue->initialized, TRUE);
    return TRUE;
}

void event_queue_set_window(EventQueue *queue, HWND window) {
    if (queue == NULL || !queue->initialized) {
        return;
    }
    EnterCriticalSection(&queue->lock);
    queue->notification_window = window;
    LeaveCriticalSection(&queue->lock);
}

BOOL push_event(EventQueue *queue, const TraceGlassEvent *event) {
    EventNode *node;
    HWND notification_window;
    UINT notification_message;
    BOOL should_notify = FALSE;

    if (queue == NULL || event == NULL || !queue->initialized) {
        return FALSE;
    }
    node = (EventNode *)malloc(sizeof(*node));
    if (node == NULL) {
        return FALSE;
    }
    node->event = *event;
    node->next = NULL;

    EnterCriticalSection(&queue->lock);
    if (queue->tail == NULL) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    notification_window = queue->notification_window;
    notification_message = queue->notification_message;
    if (!queue->notification_pending) {
        queue->notification_pending = TRUE;
        should_notify = TRUE;
    }
    LeaveCriticalSection(&queue->lock);

    if (should_notify && notification_window != NULL) {
        PostMessageW(notification_window, notification_message, 0, 0);
    }
    return TRUE;
}

BOOL pop_event(EventQueue *queue, TraceGlassEvent *event) {
    EventNode *node;
    if (queue == NULL || event == NULL || !queue->initialized) {
        return FALSE;
    }

    EnterCriticalSection(&queue->lock);
    node = queue->head;
    if (node != NULL) {
        queue->head = node->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
    } else {
        /* The next producer owns posting a fresh notification. */
        queue->notification_pending = FALSE;
    }
    LeaveCriticalSection(&queue->lock);

    if (node == NULL) {
        return FALSE;
    }
    *event = node->event;
    free(node);
    return TRUE;
}

void event_queue_destroy(EventQueue *queue) {
    EventNode *node;
    EventNode *next;
    if (queue == NULL || !queue->initialized) {
        return;
    }

    InterlockedExchange(&queue->initialized, FALSE);
    EnterCriticalSection(&queue->lock);
    node = queue->head;
    queue->head = NULL;
    queue->tail = NULL;
    queue->notification_window = NULL;
    LeaveCriticalSection(&queue->lock);

    while (node != NULL) {
        next = node->next;
        free(node);
        node = next;
    }
    DeleteCriticalSection(&queue->lock);
}
