#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void micropython_host_stdout(const char *str, size_t len);

#ifdef __cplusplus
}
#endif
