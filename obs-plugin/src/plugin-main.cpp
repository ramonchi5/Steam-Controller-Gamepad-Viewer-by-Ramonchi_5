/*
Steam Controller Gamepad Viewer OBS source
Copyright (C) 2026 ramonchi5

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "controller-source.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
ULONG_PTR gdiplus_token = 0;
}

bool obs_module_load()
{
	Gdiplus::GdiplusStartupInput gdiplus_input;
	if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok) {
		obs_log(LOG_ERROR, "failed to start GDI+");
		return false;
	}

	register_steam_controller_source();
	obs_log(LOG_INFO, "loaded version %s", PLUGIN_VERSION);
	return true;
}

void obs_module_unload()
{
	if (gdiplus_token) {
		Gdiplus::GdiplusShutdown(gdiplus_token);
		gdiplus_token = 0;
	}
}
