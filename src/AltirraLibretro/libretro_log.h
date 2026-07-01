#ifndef ALTIRRA_LIBRETRO_LOG_H
#define ALTIRRA_LIBRETRO_LOG_H

#include "libretro/libretro.h"

void ATLibretroSetLogCallback(retro_log_printf_t cb);
void ATLibretroLog(enum retro_log_level level, const char *fmt, ...);

#endif
