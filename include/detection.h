#ifndef TRACEGLASS_DETECTION_H
#define TRACEGLASS_DETECTION_H

#include <stddef.h>
#include "events.h"

#define TRACEGLASS_MAX_ALERTS_PER_EVENT 3

size_t detection_evaluate(
    const TraceGlassEvent *event,
    TraceGlassEvent *alerts,
    size_t alert_capacity
);

#endif
