/*
Steam Controller Gamepad Viewer OBS source
Copyright (C) 2026 ramonchi5

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char *PLUGIN_NAME;
extern const char *PLUGIN_VERSION;

void obs_log(int log_level, const char *format, ...);

#ifdef __cplusplus
}
#endif
