#ifndef TRACEGLASS_LOGGER_H
#define TRACEGLASS_LOGGER_H

#include <windows.h>

BOOL logger_initialize(void);
void logger_shutdown(void);
void logger_write(const WCHAR *level, const WCHAR *format, ...);

#endif

