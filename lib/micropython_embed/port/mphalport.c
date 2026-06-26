#include "py/mphal.h"
#include "port/micropython_host_io.h"

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    micropython_host_stdout(str, len);
}
