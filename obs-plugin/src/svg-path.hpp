/*
Steam Controller Gamepad Viewer OBS source
Copyright (C) 2026 ramonchi5

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <memory>
#include <string_view>

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

std::unique_ptr<Gdiplus::GraphicsPath> parse_svg_path(
	std::string_view data, Gdiplus::FillMode fill_mode = Gdiplus::FillModeWinding);
