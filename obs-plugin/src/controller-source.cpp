/*
Steam Controller Gamepad Viewer OBS source
Copyright (C) 2026 ramonchi5

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "controller-source.hpp"
#include "backend-manager.hpp"
#include "svg-path.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <winhttp.h>

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Gdiplus;

namespace {

constexpr const char *SOURCE_ID = "steam_controller_gamepad_viewer_source";
constexpr uint32_t DEFAULT_WIDTH = 1280;
constexpr uint32_t DEFAULT_HEIGHT = 900;
constexpr uint32_t DEFAULT_POLL_INTERVAL = 16;
constexpr float VIEW_X = 0.0f;
constexpr float VIEW_Y = -82.0f;
constexpr float VIEW_W = 456.0f;
constexpr float VIEW_H = 402.0f;

constexpr const char *SETTING_BACKEND_URL = "backend_url";
constexpr const char *SETTING_AUTO_BACKEND = "auto_backend";
constexpr const char *SETTING_POLL_INTERVAL = "poll_interval";
constexpr const char *SETTING_WIDTH = "output_width";
constexpr const char *SETTING_HEIGHT = "output_height";
constexpr const char *SETTING_BODY_LINES = "body_lines";
constexpr const char *SETTING_INNER_BODY_LINES = "inner_body_lines";
constexpr const char *SETTING_JOYSTICK_LINES = "joystick_lines";
constexpr const char *SETTING_BUTTON_LINES = "button_lines";
constexpr const char *SETTING_BACK_BUTTON_LINES = "back_button_lines";
constexpr const char *SETTING_LINES_COLOR = "lines_color";
constexpr const char *SETTING_LINES_OPACITY = "lines_opacity";
constexpr const char *SETTING_BODY_COLOR = "body_color";
constexpr const char *SETTING_BODY_OPACITY = "body_opacity";
constexpr const char *SETTING_BUTTON_IDLE_COLOR = "button_idle_color";
constexpr const char *SETTING_BUTTON_IDLE_OPACITY = "button_idle_opacity";
constexpr const char *SETTING_BUTTON_PRESSED_COLOR = "button_pressed_color";
constexpr const char *SETTING_BUTTON_PRESSED_OPACITY = "button_pressed_opacity";
constexpr const char *SETTING_TRIGGER_IDLE_OPACITY = "trigger_idle_opacity";
constexpr const char *SETTING_TRIGGER_IDLE_LINES_OPACITY = "trigger_idle_lines_opacity";
constexpr const char *SETTING_SHINE = "shine";
constexpr const char *SETTING_COLOR_ENCODING = "color_encoding";
constexpr const char *SCENE_ITEM_FILTER_INITIALIZED = "steam_controller_viewer_filter_initialized";

template<typename T> T clamp_value(T value, T low, T high)
{
	return std::min(std::max(value, low), high);
}

std::string trim(std::string value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return {};
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::string get_string(obs_data_t *settings, const char *name)
{
	const char *value = obs_data_get_string(settings, name);
	return value ? value : "";
}

std::wstring utf8_to_wide(const std::string &value)
{
	if (value.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
					 static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0)
		return std::wstring(value.begin(), value.end());
	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()),
			    result.data(), length);
	return result;
}

std::string windows_error(const char *operation)
{
	return std::string(operation) + " failed with Windows error " + std::to_string(GetLastError());
}

class WinHttpHandle {
public:
	WinHttpHandle() = default;
	explicit WinHttpHandle(HINTERNET value) : value_(value) {}
	~WinHttpHandle()
	{
		if (value_)
			WinHttpCloseHandle(value_);
	}

	WinHttpHandle(const WinHttpHandle &) = delete;
	WinHttpHandle &operator=(const WinHttpHandle &) = delete;

	WinHttpHandle(WinHttpHandle &&other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
	WinHttpHandle &operator=(WinHttpHandle &&other) noexcept
	{
		if (this != &other) {
			if (value_)
				WinHttpCloseHandle(value_);
			value_ = std::exchange(other.value_, nullptr);
		}
		return *this;
	}

	operator HINTERNET() const { return value_; }
	explicit operator bool() const { return value_ != nullptr; }

private:
	HINTERNET value_ = nullptr;
};

void throw_http_error(const char *operation)
{
	throw std::runtime_error(windows_error(operation));
}

std::string http_get_json(const std::string &url)
{
	const std::wstring wide_url = utf8_to_wide(url);
	URL_COMPONENTSW components{};
	components.dwStructSize = sizeof(components);
	components.dwSchemeLength = static_cast<DWORD>(-1);
	components.dwHostNameLength = static_cast<DWORD>(-1);
	components.dwUrlPathLength = static_cast<DWORD>(-1);
	components.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components))
		throw_http_error("WinHttpCrackUrl");

	const std::wstring host(components.lpszHostName, components.dwHostNameLength);
	std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
	if (components.dwExtraInfoLength > 0)
		path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
	if (path.empty())
		path = L"/";

	const std::wstring user_agent =
		utf8_to_wide(std::string("SteamControllerGamepadViewer-OBS/") + PLUGIN_VERSION);
	WinHttpHandle session(WinHttpOpen(user_agent.c_str(),
					  WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
					  WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session)
		throw_http_error("WinHttpOpen");
	WinHttpSetTimeouts(session, 400, 400, 600, 600);

	WinHttpHandle connection(WinHttpConnect(session, host.c_str(), components.nPort, 0));
	if (!connection)
		throw_http_error("WinHttpConnect");

	const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
	WinHttpHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
						 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
	if (!request)
		throw_http_error("WinHttpOpenRequest");

	const wchar_t *headers = L"Accept: application/json\r\nCache-Control: no-cache\r\n";
	if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
		throw_http_error("WinHttpSendRequest");
	if (!WinHttpReceiveResponse(request, nullptr))
		throw_http_error("WinHttpReceiveResponse");

	DWORD status_code = 0;
	DWORD status_size = sizeof(status_code);
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
				 &status_code, &status_size, nullptr))
		throw_http_error("WinHttpQueryHeaders");

	std::string response;
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available))
			throw_http_error("WinHttpQueryDataAvailable");
		if (available == 0)
			break;

		const size_t offset = response.size();
		response.resize(offset + available);
		DWORD read = 0;
		if (!WinHttpReadData(request, response.data() + offset, available, &read))
			throw_http_error("WinHttpReadData");
		response.resize(offset + read);
	}

	if (status_code < 200 || status_code >= 300)
		throw std::runtime_error("Local backend returned HTTP " + std::to_string(status_code));
	return response;
}

struct Buttons {
	bool a = false;
	bool b = false;
	bool x = false;
	bool y = false;
	bool view = false;
	bool menu = false;
	bool steam = false;
	bool quick_access = false;
	bool left_bumper = false;
	bool right_bumper = false;
	bool left_stick = false;
	bool right_stick = false;
	bool left_stick_touch = true;
	bool right_stick_touch = true;
	bool dpad_up = false;
	bool dpad_down = false;
	bool dpad_left = false;
	bool dpad_right = false;
	bool left_grip_upper = false;
	bool left_grip_lower = false;
	bool right_grip_upper = false;
	bool right_grip_lower = false;
	bool left_grip_touch = false;
	bool right_grip_touch = false;

	bool operator==(const Buttons &) const = default;
};

struct Axes {
	double left_stick_x = 0.0;
	double left_stick_y = 0.0;
	double right_stick_x = 0.0;
	double right_stick_y = 0.0;
	double left_trigger = 0.0;
	double right_trigger = 0.0;

	bool operator==(const Axes &) const = default;
};

struct Touchpad {
	bool touched = false;
	bool clicked = false;
	double x = 0.5;
	double y = 0.5;
	double pressure = 0.0;

	bool operator==(const Touchpad &) const = default;
};

struct ControllerState {
	Buttons buttons;
	Axes axes;
	Touchpad left_pad;
	Touchpad right_pad;

	bool operator==(const ControllerState &) const = default;
};

bool json_bool(obs_data_t *object, const char *name)
{
	return object && obs_data_get_bool(object, name);
}

double json_double(obs_data_t *object, const char *name)
{
	return object ? obs_data_get_double(object, name) : 0.0;
}

ControllerState parse_state_json(const std::string &json)
{
	ControllerState state;
	obs_data_t *root = obs_data_create_from_json(json.c_str());
	if (!root)
		return state;

	obs_data_t *buttons = obs_data_get_obj(root, "buttons");
	if (buttons) {
		state.buttons.a = json_bool(buttons, "a");
		state.buttons.b = json_bool(buttons, "b");
		state.buttons.x = json_bool(buttons, "x");
		state.buttons.y = json_bool(buttons, "y");
		state.buttons.view = json_bool(buttons, "view");
		state.buttons.menu = json_bool(buttons, "menu");
		state.buttons.steam = json_bool(buttons, "steam");
		state.buttons.quick_access = json_bool(buttons, "quickAccess");
		state.buttons.left_bumper = json_bool(buttons, "leftBumper");
		state.buttons.right_bumper = json_bool(buttons, "rightBumper");
		state.buttons.left_stick = json_bool(buttons, "leftStick");
		state.buttons.right_stick = json_bool(buttons, "rightStick");
		state.buttons.left_stick_touch = json_bool(buttons, "leftStickTouch");
		state.buttons.right_stick_touch = json_bool(buttons, "rightStickTouch");
		state.buttons.dpad_up = json_bool(buttons, "dpadUp");
		state.buttons.dpad_down = json_bool(buttons, "dpadDown");
		state.buttons.dpad_left = json_bool(buttons, "dpadLeft");
		state.buttons.dpad_right = json_bool(buttons, "dpadRight");
		state.buttons.left_grip_upper = json_bool(buttons, "leftGripUpper");
		state.buttons.left_grip_lower = json_bool(buttons, "leftGripLower");
		state.buttons.right_grip_upper = json_bool(buttons, "rightGripUpper");
		state.buttons.right_grip_lower = json_bool(buttons, "rightGripLower");
		state.buttons.left_grip_touch = json_bool(buttons, "leftGripTouch");
		state.buttons.right_grip_touch = json_bool(buttons, "rightGripTouch");
		obs_data_release(buttons);
	}

	obs_data_t *axes = obs_data_get_obj(root, "axes");
	if (axes) {
		state.axes.left_stick_x = json_double(axes, "leftStickX");
		state.axes.left_stick_y = json_double(axes, "leftStickY");
		state.axes.right_stick_x = json_double(axes, "rightStickX");
		state.axes.right_stick_y = json_double(axes, "rightStickY");
		state.axes.left_trigger = json_double(axes, "leftTrigger");
		state.axes.right_trigger = json_double(axes, "rightTrigger");
		obs_data_release(axes);
	}

	obs_data_t *left_pad = obs_data_get_obj(root, "leftTouchpad");
	if (left_pad) {
		state.left_pad.touched = json_bool(left_pad, "touched");
		state.left_pad.clicked = json_bool(left_pad, "clicked");
		state.left_pad.x = json_double(left_pad, "x");
		state.left_pad.y = json_double(left_pad, "y");
		state.left_pad.pressure = json_double(left_pad, "pressure");
		obs_data_release(left_pad);
	}

	obs_data_t *right_pad = obs_data_get_obj(root, "rightTouchpad");
	if (right_pad) {
		state.right_pad.touched = json_bool(right_pad, "touched");
		state.right_pad.clicked = json_bool(right_pad, "clicked");
		state.right_pad.x = json_double(right_pad, "x");
		state.right_pad.y = json_double(right_pad, "y");
		state.right_pad.pressure = json_double(right_pad, "pressure");
		obs_data_release(right_pad);
	}

	obs_data_release(root);
	return state;
}

struct SourceSettings {
	std::string backend_url = "http://127.0.0.1:31337";
	bool auto_backend = true;
	uint32_t poll_interval = DEFAULT_POLL_INTERVAL;
	uint32_t width = DEFAULT_WIDTH;
	uint32_t height = DEFAULT_HEIGHT;
	int body_lines = 10;
	int inner_body_lines = 10;
	int joystick_lines = 10;
	int button_lines = 10;
	int back_button_lines = 10;
	uint32_t lines_color = 0xffffffff;
	int lines_opacity = 55;
	uint32_t body_color = 0xff000000;
	int body_opacity = 30;
	uint32_t button_idle_color = 0xffffa725;
	int button_idle_opacity = 0;
	uint32_t button_pressed_color = 0xffffa725;
	int button_pressed_opacity = 100;
	int trigger_idle_opacity = 0;
	int trigger_idle_lines_opacity = 0;
	int shine = 100;
};

bool uses_managed_backend(const SourceSettings &settings)
{
	if (!settings.auto_backend)
		return false;
	std::string url = settings.backend_url;
	std::transform(url.begin(), url.end(), url.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	while (!url.empty() && url.back() == '/')
		url.pop_back();
	return url == "http://127.0.0.1:31337" || url == "http://localhost:31337";
}

uint32_t swap_red_blue(uint32_t color)
{
	return (color & 0xff00ff00) | ((color & 0x000000ff) << 16) | ((color & 0x00ff0000) >> 16);
}

void migrate_color_encoding(obs_data_t *settings)
{
	if (obs_data_has_user_value(settings, SETTING_COLOR_ENCODING))
		return;

	for (const char *name : {SETTING_LINES_COLOR, SETTING_BODY_COLOR, SETTING_BUTTON_IDLE_COLOR,
				 SETTING_BUTTON_PRESSED_COLOR}) {
		if (obs_data_has_user_value(settings, name)) {
			const auto old_color = static_cast<uint32_t>(obs_data_get_int(settings, name));
			obs_data_set_int(settings, name, swap_red_blue(old_color));
		}
	}
	obs_data_set_int(settings, SETTING_COLOR_ENCODING, 2);
}

Color color_from_obs(uint32_t color, int opacity_percent)
{
	const auto alpha = static_cast<BYTE>(std::lround(255.0 * clamp_value(opacity_percent, 0, 100) / 100.0));
	const auto red = static_cast<BYTE>(color & 0xff);
	const auto green = static_cast<BYTE>((color >> 8) & 0xff);
	const auto blue = static_cast<BYTE>((color >> 16) & 0xff);
	return Color(alpha, red, green, blue);
}

Color with_alpha(Color color, int opacity_percent)
{
	const auto alpha = static_cast<BYTE>(std::lround(255.0 * clamp_value(opacity_percent, 0, 100) / 100.0));
	return Color(alpha, color.GetR(), color.GetG(), color.GetB());
}

float line_size(int value, float base)
{
	if (value <= 0)
		return 0.0f;
	return base * static_cast<float>(value) / 10.0f;
}

struct RenderFrame {
	uint32_t texture_width = 0;
	uint32_t texture_height = 0;
	uint32_t stride = 0;
	std::vector<uint8_t> pixels;
};

using PathPtr = std::unique_ptr<GraphicsPath>;

PathPtr rounded_rect_path(float x, float y, float w, float h, float r)
{
	auto path = std::make_unique<GraphicsPath>();
	const float d = r * 2.0f;
	path->AddArc(x, y, d, d, 180.0f, 90.0f);
	path->AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
	path->AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
	path->AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
	path->CloseFigure();
	return path;
}

PathPtr controller_body_path()
{
	return parse_svg_path(
		R"(M384.95 316.346C387.534 317.113 389.109 317.041 391.801 317.178C406.386 317.919 421.837 312.708 432.331 302.458C457.84 277.541 454.505 233.68 450.551 201.518C445.672 161.824 434.279 122.749 423.101 84.5278C419.828 73.3342 417.674 61.6952 412.621 51.0978C408.414 42.2728 401.94 34.0273 394.341 27.8778C388.861 23.4426 382.132 20.8841 375.461 18.8478C356.926 13.1892 337.122 12.5528 317.901 11.6378C287.84 10.2066 257.723 10.0237 227.634 10.0078C197.545 10.0237 167.426 10.2066 137.364 11.6378C118.143 12.5528 98.3394 13.1892 79.804 18.8478C73.1335 20.8841 66.4044 23.4426 60.924 27.8778C53.3251 34.0273 46.8514 42.2728 42.644 51.0978C37.5915 61.6952 35.4374 73.3342 32.164 84.5278C20.9865 122.749 9.59354 161.824 4.71402 201.518C0.760376 233.68 -2.5752 277.541 22.934 302.458C33.4278 312.708 48.8787 317.919 63.464 317.178C66.1557 317.041 67.7314 317.113 70.315 316.346C76.0378 314.646 80.7996 310.482 84.454 305.908C88.6574 300.646 90.6228 294.089 93.854 288.258C98.4137 280.029 103.732 271.366 111.854 266.258C117.01 263.015 122.674 261.614 128.714 261.528C161.965 261.053 194.3 260.476 227.633 260.309C260.965 260.476 293.3 261.053 326.551 261.528C332.592 261.614 338.255 263.015 343.411 266.258C351.533 271.366 356.851 280.029 361.411 288.258C364.642 294.089 366.608 300.646 370.811 305.908C374.466 310.482 379.227 314.646 384.95 316.346Z)");
}

PathPtr left_bumper_path()
{
	return parse_svg_path(
		R"(M60.9238 27.8778C66.4042 23.4426 73.1334 20.8841 79.8038 18.8478C95.749 13.9799 112.633 12.8287 129.277 12.0195C129.207 11.5995 129.07 10.8379 129 10.4179C128.659 8.77624 127.856 7.19377 126.71 5.96782C125.565 4.72122 124.075 3.80654 122.47 3.27782C120.225 2.49815 117.772 2.32237 115.42 2.16782C106.735 1.64 97.8756 2.38451 89.3481 4.0178C81.7091 5.19303 73.5981 8.00852 67.3601 12.7579C63.9041 15.4075 60.8911 18.7736 58.9601 22.6979C58.0401 24.5479 57.3601 26.5179 56.9101 28.5379L56.2801 32.0308C57.7709 30.5628 59.3224 29.1738 60.9238 27.8778Z)");
}

PathPtr right_bumper_path()
{
	return parse_svg_path(
		R"(M394.341 27.8778C388.861 23.4426 382.132 20.8841 375.461 18.8478C359.516 13.9799 342.632 12.8287 325.988 12.0195C326.058 11.5995 326.196 10.8379 326.266 10.4179C326.606 8.77624 327.409 7.19377 328.556 5.96782C329.7 4.72122 331.19 3.80654 332.796 3.27782C335.04 2.49815 337.493 2.32237 339.846 2.16782C348.531 1.64 357.39 2.38451 365.917 4.0178C373.556 5.19303 381.667 8.00852 387.905 12.7579C391.361 15.4075 394.374 18.7736 396.305 22.6979C397.225 24.5479 397.905 26.5179 398.355 28.5379L398.985 32.0308C397.494 30.5628 395.943 29.1738 394.341 27.8778Z)");
}

PathPtr trigger_path(bool right)
{
	auto p = std::make_unique<GraphicsPath>();
	const auto x = [right](float v) {
		return right ? v : 456.0f - v;
	};
	p->StartFigure();
	p->AddBezier(x(312.4f), 38.0f, x(314.6f), 10.0f, x(321.2f), -21.0f, x(336.6f), -34.0f);
	p->AddBezier(x(336.6f), -34.0f, x(345.4f), -41.0f, x(358.6f), -41.0f, x(367.4f), -34.0f);
	p->AddBezier(x(367.4f), -34.0f, x(382.8f), -21.0f, x(389.4f), 10.0f, x(391.6f), 38.0f);
	p->AddBezier(x(391.6f), 38.0f, x(379.5f), 35.0f, x(366.3f), 34.0f, x(352.0f), 34.0f);
	p->AddBezier(x(352.0f), 34.0f, x(336.6f), 34.0f, x(324.5f), 35.0f, x(312.4f), 38.0f);
	p->CloseFigure();
	return p;
}

PathPtr dpad_path()
{
	return parse_svg_path(
		R"(M74.0048 64.0078C76.0048 64.0078 77.0049 63.0078 77.0048 61.0078L77.0049 47.0078C77.0049 45.2084 77.0049 44.0078 80.0048 43.0078C83.0048 42.0078 85.0048 42.0078 88.0048 42.0078L89.0037 42.0078C92.0037 42.0078 94.0037 42.0078 97.0037 43.0078C100.004 44.0078 100.004 45.2084 100.004 47.0078L100.004 61.0078C100.004 63.0078 101.004 64.0078 103.004 64.0078L117.004 64.0078C118.803 64.0078 120.004 64.0078 121.004 67.0078C122.004 70.0078 122.004 72.0078 122.004 75.0078L122.004 76.0078C122.004 79.0078 122.004 81.0078 121.004 84.0078C120.004 87.0078 118.803 87.0078 117.004 87.0078L103.004 87.0078C101.004 87.0078 100.004 88.0078 100.004 90.0078L100.004 104.008C100.004 105.807 100.004 107.008 97.0037 108.008C94.0037 109.008 92.0037 109.008 89.0037 109.008H88.0037C85.0037 109.008 83.0037 109.008 80.0037 108.008C77.0037 107.008 77.0037 105.807 77.0037 104.008L77.0037 90.0078C77.0037 88.0078 76.0037 87.0078 74.0037 87.0078L60.0037 87.0078C58.2042 87.0078 57.0037 87.0078 56.0037 84.0078C55.0037 81.0078 55.0037 79.0078 55.0037 76.0078L55.0037 75.0078C55.0037 72.0078 55.0037 70.0078 56.0037 67.0078C57.0037 64.0078 58.2042 64.0078 60.0037 64.0078L74.0037 64.0078C76.0037 64.0078 77.0037 63.0078 77.0037 61.0078Z)");
}

PathPtr dpad_part_path(const char *direction)
{
	auto p = std::make_unique<GraphicsPath>();
	if (std::strcmp(direction, "up") == 0) {
		PointF pts[] = {{77.0f, 42.0f}, {100.0f, 42.0f}, {100.0f, 64.0f},
				{88.5f, 75.5f}, {77.0f, 64.0f}};
		p->AddPolygon(pts, 5);
	} else if (std::strcmp(direction, "right") == 0) {
		PointF pts[] = {{122.0f, 64.0f}, {122.0f, 87.0f}, {100.0f, 87.0f},
				{88.5f, 75.5f}, {100.0f, 64.0f}};
		p->AddPolygon(pts, 5);
	} else if (std::strcmp(direction, "down") == 0) {
		PointF pts[] = {{77.0f, 109.0f}, {77.0f, 87.0f}, {88.5f, 75.5f},
				{100.0f, 87.0f}, {100.0f, 109.0f}};
		p->AddPolygon(pts, 5);
	} else {
		PointF pts[] = {{55.0f, 64.0f}, {77.0f, 64.0f}, {88.5f, 75.5f},
				{77.0f, 87.0f}, {55.0f, 87.0f}};
		p->AddPolygon(pts, 5);
	}
	return p;
}

void transform(GraphicsPath &path, const Matrix &world);
PointF transform_point(PointF point, const Matrix &world);

void draw_dpad_part(Graphics &graphics, const Matrix &world, const char *direction, Color color)
{
	auto part = dpad_part_path(direction);
	auto boundary = dpad_path();
	transform(*part, world);
	transform(*boundary, world);

	PointF start;
	PointF end;
	if (std::strcmp(direction, "up") == 0) {
		start = PointF(88.5f, 42.0f);
		end = PointF(88.5f, 83.0f);
	} else if (std::strcmp(direction, "right") == 0) {
		start = PointF(122.0f, 75.5f);
		end = PointF(81.0f, 75.5f);
	} else if (std::strcmp(direction, "down") == 0) {
		start = PointF(88.5f, 109.0f);
		end = PointF(88.5f, 68.0f);
	} else {
		start = PointF(55.0f, 75.5f);
		end = PointF(96.0f, 75.5f);
	}
	start = transform_point(start, world);
	end = transform_point(end, world);

	const auto alpha = [&](double factor) {
		return Color(static_cast<BYTE>(std::lround(color.GetA() * factor)), color.GetR(), color.GetG(), color.GetB());
	};
	Color colors[] = {alpha(1.0), alpha(1.0), alpha(0.78), alpha(0.31), alpha(0.0)};
	REAL positions[] = {0.0f, 0.03f, 0.24f, 0.58f, 1.0f};
	LinearGradientBrush brush(start, end, colors[0], colors[4]);
	brush.SetInterpolationColors(colors, positions, 5);
	brush.SetWrapMode(WrapModeClamp);

	Region old_clip;
	graphics.GetClip(&old_clip);
	Region dpad_region(boundary.get());
	graphics.SetClip(&dpad_region, CombineModeIntersect);
	graphics.FillPath(&brush, part.get());
	graphics.SetClip(&old_clip, CombineModeReplace);
}

std::unique_ptr<Matrix> world_matrix(uint32_t width, uint32_t height)
{
	const float scale = std::min(static_cast<float>(width) / VIEW_W, static_cast<float>(height) / VIEW_H);
	const float x = (static_cast<float>(width) - VIEW_W * scale) * 0.5f - VIEW_X * scale;
	const float y = (static_cast<float>(height) - VIEW_H * scale) * 0.5f - VIEW_Y * scale;
	return std::make_unique<Matrix>(scale, 0.0f, 0.0f, scale, x, y);
}

void transform(GraphicsPath &path, const Matrix &world)
{
	path.Transform(const_cast<Matrix *>(&world));
}

PointF transform_point(PointF point, const Matrix &world)
{
	PointF points[] = {point};
	const_cast<Matrix &>(world).TransformPoints(points, 1);
	return points[0];
}

float scaled_line(float line, const Matrix &world)
{
	REAL elements[6]{};
	const_cast<Matrix &>(world).GetElements(elements);
	return line * elements[0];
}

void draw_glow_path(Graphics &graphics, GraphicsPath &path, Color color, float line_width, int shine)
{
	if (shine <= 0 || line_width <= 0.0f)
		return;
	const int alpha = static_cast<int>(95.0 * clamp_value(shine, 0, 100) / 100.0);
	for (int i = 4; i >= 1; --i) {
		Pen glow(Color(static_cast<BYTE>(alpha / i), color.GetR(), color.GetG(), color.GetB()),
			 line_width + static_cast<float>(i) * 6.0f);
		glow.SetLineJoin(LineJoinRound);
		glow.SetStartCap(LineCapRound);
		glow.SetEndCap(LineCapRound);
		graphics.DrawPath(&glow, &path);
	}
}

void draw_filled_path(Graphics &graphics, const GraphicsPath &source, const Matrix &world, Color fill, Color line,
		      float line_width, bool active, int shine)
{
	std::unique_ptr<GraphicsPath> path(source.Clone());
	if (!path)
		return;
	transform(*path, world);
	if (active)
		draw_glow_path(graphics, *path, line, scaled_line(line_width, world), shine);
	SolidBrush brush(fill);
	graphics.FillPath(&brush, path.get());
	if (line_width > 0.0f && line.GetA() > 0) {
		Pen pen(line, scaled_line(line_width, world));
		pen.SetLineJoin(LineJoinRound);
		pen.SetStartCap(LineCapRound);
		pen.SetEndCap(LineCapRound);
		graphics.DrawPath(&pen, path.get());
	}
}

void draw_outline_path(Graphics &graphics, const GraphicsPath &source, const Matrix &world, Color line, float line_width,
		       bool active, int shine)
{
	if (line_width <= 0.0f || line.GetA() == 0)
		return;
	std::unique_ptr<GraphicsPath> path(source.Clone());
	if (!path)
		return;
	transform(*path, world);
	const float width = scaled_line(line_width, world);
	if (active)
		draw_glow_path(graphics, *path, line, width, shine);
	Pen pen(line, width);
	pen.SetLineJoin(LineJoinRound);
	pen.SetStartCap(LineCapRound);
	pen.SetEndCap(LineCapRound);
	graphics.DrawPath(&pen, path.get());
}

void draw_glow_ellipse(Graphics &graphics, const RectF &rect, Color color, int shine)
{
	if (shine <= 0)
		return;
	const int alpha = static_cast<int>(95.0 * clamp_value(shine, 0, 100) / 100.0);
	for (int i = 4; i >= 1; --i) {
		Pen glow(Color(static_cast<BYTE>(alpha / i), color.GetR(), color.GetG(), color.GetB()),
			 static_cast<REAL>(i * 6));
		graphics.DrawEllipse(&glow, rect);
	}
}

void draw_circle(Graphics &graphics, PointF center, float radius, Color fill, Color line, float line_width,
		 bool active, int shine)
{
	RectF rect(center.X - radius, center.Y - radius, radius * 2.0f, radius * 2.0f);
	if (active)
		draw_glow_ellipse(graphics, rect, line, shine);
	SolidBrush brush(fill);
	graphics.FillEllipse(&brush, rect);
	if (line_width > 0.0f && line.GetA() > 0) {
		Pen pen(line, line_width);
		graphics.DrawEllipse(&pen, rect);
	}
}

void draw_vector_icon(Graphics &graphics, const Matrix &world, std::string_view path_data, Color color,
		      bool active, int shine, FillMode fill_mode = FillModeWinding)
{
	auto path = parse_svg_path(path_data, fill_mode);
	draw_filled_path(graphics, *path, world, color, color, active ? 0.35f : 0.0f, active, shine);
}

PathPtr rotated_rounded_rect(float x, float y, float w, float h, float r, float angle, PointF center)
{
	auto path = rounded_rect_path(x, y, w, h, r);
	Matrix rotation;
	rotation.RotateAt(angle, center);
	path->Transform(&rotation);
	return path;
}

PathPtr transformed_rounded_rect(float x, float y, float w, float h, float r, Matrix &transform_matrix)
{
	auto path = rounded_rect_path(x, y, w, h, r);
	path->Transform(&transform_matrix);
	return path;
}

PathPtr trackpad_path(bool left)
{
	if (left) {
		return rotated_rounded_rect(103.296f, 139.723f, 93.0f, 93.0f, 16.0f, 9.87378f,
					    PointF(103.296f, 139.723f));
	}
	Matrix pad_matrix(-0.985188f, 0.171478f, 0.171478f, 0.985188f, 350.965f, 138.723f);
	return transformed_rounded_rect(-0.81371f, 1.15667f, 93.0f, 93.0f, 16.0f, pad_matrix);
}

PathPtr trackpad_inner_path(bool left)
{
	if (left) {
		return rotated_rounded_rect(107.296f, 143.723f, 85.0f, 85.0f, 13.0f, 9.87378f,
					    PointF(103.296f, 139.723f));
	}
	Matrix pad_matrix(-0.985188f, 0.171478f, 0.171478f, 0.985188f, 350.965f, 138.723f);
	return transformed_rounded_rect(3.18629f, 5.15667f, 85.0f, 85.0f, 13.0f, pad_matrix);
}

void draw_trackpad(Graphics &graphics, const Matrix &world, const Touchpad &pad, bool left,
		   const SourceSettings &settings, Color idle_fill, Color active_fill, Color idle_line)
{
	PathPtr pad_path = trackpad_path(left);
	PathPtr inner_path = trackpad_inner_path(left);
	const float line = line_size(settings.button_lines, 0.9f);
	draw_filled_path(graphics, *pad_path, world, idle_fill, idle_line, line, false, 0);
	const double pressure = clamp_value(pad.pressure, 0.0, 1.0);
	const bool touched = pad.touched || pressure > 0.02;
	const bool clicked = touched && (pad.clicked || pressure >= 0.05);
	if (touched) {
		draw_outline_path(graphics, *pad_path, world, with_alpha(active_fill, 90), 1.2f,
				  true, settings.shine);
		draw_outline_path(graphics, *inner_path, world, with_alpha(active_fill, 65), 0.9f,
				  false, 0);
	}

	std::unique_ptr<Matrix> local_to_controller;
	if (left) {
		local_to_controller = std::make_unique<Matrix>();
		local_to_controller->RotateAt(9.87378f, PointF(103.296f, 139.723f));
	} else {
		local_to_controller =
			std::make_unique<Matrix>(-0.985188f, 0.171478f, 0.171478f, 0.985188f, 350.965f, 138.723f);
	}

	const float px = left ? 103.296f + static_cast<float>(pad.x) * 93.0f
			      : -0.81371f + static_cast<float>(1.0 - pad.x) * 93.0f;
	const float py = left ? 139.723f + static_cast<float>(pad.y) * 93.0f
			      : 1.15667f + static_cast<float>(pad.y) * 93.0f;
	PointF points[] = {PointF(px, py)};
	local_to_controller->TransformPoints(points, 1);
	const PointF marker = transform_point(points[0], world);
	if (touched) {
		Region old_clip;
		graphics.GetClip(&old_clip);
		auto marker_clip = trackpad_path(left);
		transform(*marker_clip, world);
		Region marker_region(marker_clip.get());
		graphics.SetClip(&marker_region, CombineModeIntersect);

		const float scale = scaled_line(1.0f, world);
		if (clicked) {
			const double click_strength = std::max(pressure, pad.clicked ? 0.5 : 0.0);
			const float ring_radius = static_cast<float>(12.0 + click_strength * 8.0) * scale;
			draw_circle(graphics, marker, ring_radius,
				    with_alpha(active_fill, static_cast<int>(82 * settings.button_pressed_opacity / 100.0)),
				    Color(250, 242, 248, 255), 1.25f * scale, true, settings.shine);
			draw_circle(graphics, marker, 7.0f * scale,
				    with_alpha(active_fill, static_cast<int>(96 * settings.button_pressed_opacity / 100.0)),
				    with_alpha(active_fill, 100), 0.0f, true, settings.shine);
		} else {
			draw_circle(graphics, marker, 7.0f * scale, Color(255, 255, 255, 255),
				    Color(0, 255, 255, 255), 0.0f, false, 0);
		}
		graphics.SetClip(&old_clip, CombineModeReplace);
	}
}

void draw_stick(Graphics &graphics, const Matrix &world, PointF center, double x, double y, bool pressed,
		bool touched, const SourceSettings &settings, Color idle_fill, Color active_fill, Color line)
{
	const PointF screen_center = transform_point(center, world);
	const float scale = scaled_line(1.0f, world);
	const float area_radius = 34.5f * scale;
	const float input_radius = 14.49f * scale;
	const float threshold = 0.08f;
	const double magnitude = std::sqrt(x * x + y * y);
	const bool moving = magnitude > threshold;
	const bool show_input = touched || moving;
	const float idle_area_line = scaled_line(line_size(settings.joystick_lines, 0.9f), world);

	draw_circle(graphics, screen_center, area_radius, idle_fill, line, idle_area_line, false, 0);
	if (pressed) {
		draw_circle(graphics, screen_center, area_radius, with_alpha(active_fill, 20),
			    Color(250, 242, 248, 255),
			    scaled_line(line_size(settings.joystick_lines, 2.0f), world), true, settings.shine);
	}
	draw_circle(graphics, screen_center, 4.5f * scale, Color(224, 255, 255, 255), Color(0, 0, 0, 0), 0.0f,
		    false, 0);
	if (!show_input)
		return;

	const float travel = 23.0f * scale;
	PointF input_center(screen_center.X + static_cast<float>(x) * travel,
			    screen_center.Y + static_cast<float>(y) * travel);
	const Color fill = moving ? with_alpha(active_fill, settings.button_pressed_opacity)
				  : Color(230, 205, 209, 213);
	const Color outline = moving || pressed ? Color(250, 242, 248, 255) : Color(140, 240, 244, 248);
	const float input_line = scaled_line(
		line_size(settings.button_lines, pressed ? 1.6f : (moving ? 1.35f : 0.8f)), world);
	draw_circle(graphics, input_center, input_radius, fill, outline, input_line,
		    moving || pressed, settings.shine);
}

void draw_grip_sensor(Graphics &graphics, const Matrix &world, bool right, bool active,
		      const SourceSettings &settings)
{
	if (!active)
		return;

	const std::string_view left_paths[] = {
		R"(M27.6 100.1C17.9 134.6 8.7 168.8 4.71402 201.518C0.760376 233.68 -2.5752 277.541 22.934 302.458)",
		R"(M30.9 110.5C22.2 143.7 15.1 176.9 11.8 206.8C8.4 237.7 7.4 270.8 27.6 293.6)",
		R"(M34.5 119.9C26.9 151.5 21.2 183.2 18.8 211.9C16.3 239.6 17.2 265.9 31.7 284.1)",
	};
	const std::string_view right_paths[] = {
		R"(M427.7 100.1C437.4 134.6 446.6 168.8 450.551 201.518C454.505 233.68 457.84 277.541 432.331 302.458)",
		R"(M424.4 110.5C433.1 143.7 440.2 176.9 443.5 206.8C446.9 237.7 447.9 270.8 427.7 293.6)",
		R"(M420.8 119.9C428.4 151.5 434.1 183.2 436.5 211.9C439 239.6 438.1 265.9 423.6 284.1)",
	};
	const std::string_view *paths = right ? right_paths : left_paths;
	for (int i = 0; i < 3; ++i) {
		auto path = parse_svg_path(paths[i]);
		const Color line = i == 0 ? Color(145, 230, 233, 236) : Color(125, 230, 233, 236);
		const float width = line_size(settings.body_lines, i == 0 ? 1.9f : 1.25f);
		draw_outline_path(graphics, *path, world, line, width, false, 0);
	}
}

void render_controller_shapes(Graphics &graphics, const ControllerState &state, const SourceSettings &settings,
			      uint32_t width, uint32_t height)
{
	auto world_ptr = world_matrix(width, height);
	const Matrix &world = *world_ptr;
	const Color lines = color_from_obs(settings.lines_color, settings.lines_opacity);
	const Color body = color_from_obs(settings.body_color, settings.body_opacity);
	const Color idle = color_from_obs(settings.button_idle_color, settings.button_idle_opacity);
	const Color pressed = color_from_obs(settings.button_pressed_color, settings.button_pressed_opacity);
	const Color pressed_line = Color(255, 255, 255, 255);
	const float body_line = line_size(settings.body_lines, 1.3f);
	const float inner_line = line_size(settings.inner_body_lines, 0.9f);
	const float button_line = line_size(settings.button_lines, 0.9f);
	const float back_line = line_size(settings.back_button_lines, 0.8f);

	draw_filled_path(graphics, *left_bumper_path(), world, body, lines, body_line, false, 0);
	draw_filled_path(graphics, *right_bumper_path(), world, body, lines, body_line, false, 0);
	draw_filled_path(graphics, *controller_body_path(), world, body, lines, body_line, false, 0);

	auto center_line = parse_svg_path(R"(M260.328 121.388C249.068 121.368 205.408 121.388 194.148 121.458)");
	auto left_inner = parse_svg_path(
		R"(M129.908 122.978C126.218 123.138 123.508 123.318 119.828 123.508C117.828 123.618 115.837 123.768 113.847 123.968C111.377 124.228 108.897 124.558 106.447 125.018C104.247 125.428 102.057 125.928 99.8962 126.548C96.0362 127.658 92.2762 129.158 88.8362 131.208C85.7662 133.028 82.9562 135.288 80.5662 137.928C78.1162 140.658 76.1162 143.798 74.5662 147.118M51.4373 293.418C52.5273 277.648 53.9673 261.918 55.7773 246.218C57.1773 234.248 58.8071 222.328 60.6471 210.428M74.5663 147.108C73.6463 149.098 72.8662 151.128 72.2263 153.218C71.9963 153.948 71.7857 154.688 71.5857 155.428M60.6482 210.428C63.5382 191.928 66.7282 173.558 71.5882 155.428)");
	auto right_inner = parse_svg_path(
		R"(M380.707 147.108C381.627 149.098 382.397 151.128 383.047 153.218C383.277 153.948 383.487 154.688 383.687 155.428C384.167 157.238 384.637 159.048 385.077 160.858C385.557 162.818 386.017 164.778 386.467 166.748C387.467 171.148 388.397 175.558 389.277 179.988C391.287 190.098 393.037 200.248 394.627 210.428M335.445 123.508C337.445 123.618 339.435 123.768 341.425 123.968C343.895 124.228 346.376 124.558 348.826 125.018C351.026 125.428 353.216 125.928 355.376 126.548C359.236 127.658 362.996 129.158 366.446 131.208C369.506 133.028 372.326 135.288 374.706 137.928C377.166 140.658 379.156 143.798 380.706 147.118M399.496 246.218C401.306 261.918 402.746 277.648 403.826 293.418M394.617 210.428C396.467 222.328 398.087 234.258 399.497 246.218M324.398 122.968C328.078 123.138 331.768 123.308 335.448 123.498)");
	draw_outline_path(graphics, *center_line, world, lines, inner_line, false, 0);
	draw_outline_path(graphics, *left_inner, world, lines, inner_line, false, 0);
	draw_outline_path(graphics, *right_inner, world, lines, inner_line, false, 0);

	auto draw_trigger = [&](bool right, double amount) {
		auto fill_path = trigger_path(right);
		RectF bounds;
		fill_path->GetBounds(&bounds);
		const float fill_top = bounds.Y + bounds.Height * (1.0f - static_cast<float>(clamp_value(amount, 0.0, 1.0)));
		Region old_clip;
		graphics.GetClip(&old_clip);
		RectF clip_rect(bounds.X - 20.0f, fill_top - 10.0f, bounds.Width + 40.0f,
				bounds.Y + bounds.Height - fill_top + 20.0f);
		clip_rect = RectF(transform_point(PointF(clip_rect.X, clip_rect.Y), world).X,
				  transform_point(PointF(clip_rect.X, clip_rect.Y), world).Y,
				  clip_rect.Width * scaled_line(1.0f, world),
				  clip_rect.Height * scaled_line(1.0f, world));
		graphics.SetClip(clip_rect, CombineModeIntersect);
		draw_filled_path(graphics, *fill_path, world, pressed, pressed_line, body_line,
				 amount > 0.01, settings.shine);
		graphics.SetClip(&old_clip, CombineModeReplace);
	};

	Region trigger_clip;
	graphics.GetClip(&trigger_clip);
	auto occluding_body = controller_body_path();
	auto occluding_left_bumper = left_bumper_path();
	auto occluding_right_bumper = right_bumper_path();
	transform(*occluding_body, world);
	transform(*occluding_left_bumper, world);
	transform(*occluding_right_bumper, world);
	Region body_region(occluding_body.get());
	Region left_bumper_region(occluding_left_bumper.get());
	Region right_bumper_region(occluding_right_bumper.get());
	graphics.ExcludeClip(&body_region);
	graphics.ExcludeClip(&left_bumper_region);
	graphics.ExcludeClip(&right_bumper_region);

	if (settings.trigger_idle_opacity > 0 || settings.trigger_idle_lines_opacity > 0) {
		draw_filled_path(graphics, *trigger_path(false), world,
				 color_from_obs(settings.button_idle_color, settings.trigger_idle_opacity),
				 color_from_obs(settings.lines_color, settings.trigger_idle_lines_opacity), body_line, false,
				 0);
		draw_filled_path(graphics, *trigger_path(true), world,
				 color_from_obs(settings.button_idle_color, settings.trigger_idle_opacity),
				 color_from_obs(settings.lines_color, settings.trigger_idle_lines_opacity), body_line, false,
				 0);
	}
	draw_trigger(false, state.axes.left_trigger);
	draw_trigger(true, state.axes.right_trigger);
	graphics.SetClip(&trigger_clip, CombineModeReplace);

	const auto draw_back = [&](float cx, float cy, bool right, bool pressed_state) {
		GraphicsPath path;
		path.AddEllipse(cx - 14.4f, cy - 20.8f, 28.8f, 41.6f);
		Matrix rot;
		rot.RotateAt(right ? -11.0f : 11.0f, PointF(cx, cy));
		path.Transform(&rot);
		const Color fill = pressed_state ? pressed : Color(57, 205, 209, 213);
		const Color line = pressed_state ? pressed_line : Color(35, 240, 244, 248);
		draw_filled_path(graphics, path, world, fill, line, back_line, pressed_state, settings.shine);
	};
	Region back_clip;
	graphics.GetClip(&back_clip);
	auto left_pad_mask = trackpad_path(true);
	auto right_pad_mask = trackpad_path(false);
	transform(*left_pad_mask, world);
	transform(*right_pad_mask, world);
	Region left_pad_region(left_pad_mask.get());
	Region right_pad_region(right_pad_mask.get());
	graphics.ExcludeClip(&left_pad_region);
	graphics.ExcludeClip(&right_pad_region);
	draw_back(88.0f, 180.5f, false, state.buttons.left_grip_upper);
	draw_back(80.0f, 234.5f, false, state.buttons.left_grip_lower);
	draw_back(367.0f, 180.5f, true, state.buttons.right_grip_upper);
	draw_back(375.0f, 234.5f, true, state.buttons.right_grip_lower);
	graphics.SetClip(&back_clip, CombineModeReplace);

	draw_trackpad(graphics, world, state.left_pad, true, settings, idle, pressed, lines);
	draw_trackpad(graphics, world, state.right_pad, false, settings, idle, pressed, lines);

	const bool dpad_active = state.buttons.dpad_up || state.buttons.dpad_down || state.buttons.dpad_left ||
				 state.buttons.dpad_right;
	draw_filled_path(graphics, *dpad_path(), world, idle, lines, button_line, false, 0);
	if (state.buttons.dpad_up)
		draw_dpad_part(graphics, world, "up", pressed);
	if (state.buttons.dpad_right)
		draw_dpad_part(graphics, world, "right", pressed);
	if (state.buttons.dpad_down)
		draw_dpad_part(graphics, world, "down", pressed);
	if (state.buttons.dpad_left)
		draw_dpad_part(graphics, world, "left", pressed);
	if (dpad_active)
		draw_outline_path(graphics, *dpad_path(), world, pressed_line, button_line, true,
				  settings.shine * 75 / 100);

	auto button_circle = [&](float x, float y, float r, bool active) {
		PointF c = transform_point(PointF(x, y), world);
		const float s = scaled_line(1.0f, world);
		draw_circle(graphics, c, r * s, active ? pressed : idle, active ? pressed_line : lines,
			    scaled_line(button_line, world), active, settings.shine);
	};
	button_circle(367.133f, 101.508f, 13.5f, state.buttons.a);
	button_circle(393.133f, 76.0078f, 13.5f, state.buttons.b);
	button_circle(341.133f, 76.0078f, 13.5f, state.buttons.x);
	button_circle(367.133f, 49.5078f, 13.5f, state.buttons.y);
	draw_vector_icon(graphics, world,
		R"(M370.343 107.838L369.583 105.568H365.023L364.243 107.838H361.633L366.303 95.0078H368.263L372.953 107.838H370.343ZM367.353 98.7878L365.733 103.458H368.923L367.353 98.7878Z)",
		state.buttons.a ? pressed_line : lines, state.buttons.a, settings.shine);
	draw_vector_icon(graphics, world,
		R"(M394.503 82.8378H389.133V70.0078H394.283C396.783 70.0078 398.243 71.4178 398.243 73.6178C398.243 75.0378 397.303 75.9578 396.663 76.2678C397.433 76.6278 398.423 77.4378 398.423 79.1478C398.423 81.5478 396.783 82.8477 394.493 82.8477L394.503 82.8378ZM394.083 72.2377H391.633V75.1978H394.083C395.143 75.1978 395.743 74.5978 395.743 73.7178C395.743 72.8378 395.143 72.2377 394.083 72.2377ZM394.253 77.4478H391.643V80.6077H394.253C395.383 80.6077 395.923 79.8878 395.923 79.0178C395.923 78.1478 395.383 77.4478 394.253 77.4478Z)",
		state.buttons.b ? pressed_line : lines, state.buttons.b, settings.shine);
	draw_vector_icon(graphics, world,
		R"(M343.453 82.8378L340.963 78.3678L338.493 82.8378H335.633L339.613 76.2578L335.883 70.0078H338.733L340.973 74.1478L343.223 70.0078H346.053L342.323 76.2578L346.323 82.8378H343.463H343.453Z)",
		state.buttons.x ? pressed_line : lines, state.buttons.x, settings.shine);
	draw_vector_icon(graphics, world,
		R"(M368.483 51.0778V56.3377H365.993V51.0778L362.133 43.5078H364.853L367.253 48.6777L369.613 43.5078H372.333L368.473 51.0778H368.483Z)",
		state.buttons.y ? pressed_line : lines, state.buttons.y, settings.shine);

	auto path_button = [&](std::string_view path_data, bool active) {
		auto path = parse_svg_path(path_data);
		draw_filled_path(graphics, *path, world, active ? pressed : idle, active ? pressed_line : lines,
				 button_line, active, settings.shine);
	};
	path_button(
		R"(M139.217 37.5878H155.525C158.835 37.6078 161.556 40.3077 161.556 43.6177C161.556 46.9377 158.886 49.6277 155.566 49.6477L139.217 49.6478C135.887 49.6478 133.187 46.9478 133.187 43.6178C133.187 40.2878 135.887 37.5878 139.217 37.5878Z)",
		state.buttons.view);
	path_button(
		R"(M299.741 37.5878H316.049C319.359 37.6078 322.08 40.3077 322.08 43.6177C322.08 46.9377 319.41 49.6277 316.09 49.6477L299.741 49.6478C296.411 49.6478 293.711 46.9478 293.711 43.6178C293.711 40.2878 296.411 37.5878 299.741 37.5878Z)",
		state.buttons.menu);
	button_circle(227.5f, 76.0f, 14.0f, state.buttons.steam);
	path_button(
		R"(M245.444 194.708C245.444 198.258 242.564 201.138 239.014 201.138H216.254C212.704 201.138 209.824 198.258 209.824 194.708C209.824 191.158 212.704 188.278 216.254 188.278H239.014C242.564 188.278 245.444 191.158 245.444 194.708Z)",
		state.buttons.quick_access);

	const Color view_icon = state.buttons.view ? pressed_line : lines;
	draw_vector_icon(graphics, world,
		R"(M152.068 42.8677H146.058C145.718 42.8677 145.578 43.0077 145.578 43.3477V45.8677C145.578 46.2077 145.718 46.3477 146.058 46.3477H152.078C152.418 46.3477 152.558 46.2077 152.558 45.8677V43.3477C152.558 43.0077 152.418 42.8677 152.068 42.8677Z)",
		view_icon, state.buttons.view, settings.shine);
	draw_vector_icon(graphics, world,
		R"(M149.216 41.2977V41.7877C147.206 41.7877 145.216 41.7877 143.206 41.7877V43.3377H144.236V44.3077H142.726C142.386 44.3077 142.246 44.1677 142.246 43.8277V41.3077C142.246 40.9577 142.386 40.8177 142.726 40.8177H148.746C149.086 40.8177 149.226 40.9677 149.226 41.3077Z)",
		view_icon, state.buttons.view, settings.shine);

	const Color menu_icon = state.buttons.menu ? pressed_line : lines;
	draw_vector_icon(graphics, world,
		R"(M312.686 41.5978H303.286C302.936 41.5978 302.656 41.3178 302.656 40.9678C302.656 40.6178 302.936 40.3378 303.286 40.3378H312.686C313.036 40.3378 313.316 40.6178 313.316 40.9678C313.316 41.3178 313.036 41.5978 312.686 41.5978Z)",
		menu_icon, state.buttons.menu, settings.shine);
	draw_vector_icon(graphics, world,
		R"(M312.686 44.1478H303.286C302.936 44.1478 302.656 43.8678 302.656 43.5278C302.656 43.1878 302.936 42.8978 303.286 42.8978H312.686C313.036 42.8978 313.316 43.1778 313.316 43.5278C313.316 43.8778 313.036 44.1478 312.686 44.1478Z)",
		menu_icon, state.buttons.menu, settings.shine);
	draw_vector_icon(graphics, world,
		R"(M312.686 46.7078H303.286C302.936 46.7078 302.656 46.4278 302.656 46.0778C302.656 45.7278 302.936 45.4578 303.286 45.4578H312.686C313.036 45.4578 313.316 45.7378 313.316 46.0778C313.316 46.4178 313.036 46.7078 312.686 46.7078Z)",
		menu_icon, state.buttons.menu, settings.shine);

	const Color steam_icon = state.buttons.steam ? pressed_line : lines;
	draw_vector_icon(graphics, world,
		R"(M231.917 70.1406C233.279 70.1406 234.383 71.245 234.383 72.6074C234.383 73.9699 233.279 75.0751 231.917 75.0751C230.554 75.0751 229.45 73.9699 229.45 72.6074C229.45 71.245 230.554 70.1406 231.917 70.1406Z)",
		steam_icon, state.buttons.steam, settings.shine);
	draw_vector_icon(graphics, world,
		R"(M231.914 67.6972C234.625 67.6972 236.824 69.8958 236.824 72.6074C236.824 75.3191 234.625 77.5175 231.914 77.5175C231.873 77.5175 231.833 77.5156 231.793 77.5146L231.796 77.5175L227.379 80.6679C227.383 80.7387 227.386 80.81 227.386 80.8818C227.386 82.9248 225.73 84.581 223.687 84.581C221.882 84.5809 220.38 83.2887 220.054 81.579L214.888 79.5195C214.583 78.4056 214.419 77.233 214.419 76.0224C214.419 75.6188 214.439 75.2195 214.474 74.8251L221.62 77.8134C222.21 77.415 222.921 77.1826 223.687 77.1826C223.762 77.1826 223.837 77.1849 223.912 77.1894L223.894 77.1718L227 72.6601L227.004 72.665C227.004 72.6459 227.003 72.6265 227.003 72.6074C227.004 69.8959 229.202 67.6975 231.914 67.6972ZM223.651 78.1513C223.345 78.1514 223.051 78.2038 222.776 78.2968L224.422 78.9853C225.449 79.4122 225.936 80.5912 225.509 81.6181C225.082 82.6449 223.903 83.1308 222.876 82.704L221.174 82.0253C221.607 82.9614 222.552 83.6121 223.651 83.6122C225.159 83.6122 226.381 82.3896 226.381 80.8818C226.381 79.3739 225.159 78.1513 223.651 78.1513ZM231.912 69.3359C230.105 69.336 228.64 70.8008 228.64 72.6074C228.64 74.4142 230.105 75.8788 231.912 75.8788C233.718 75.8788 235.183 74.4142 235.183 72.6074C235.183 70.8007 233.718 69.3359 231.912 69.3359Z)",
		steam_icon, state.buttons.steam, settings.shine, FillModeAlternate);

	const Color quick_icon = state.buttons.quick_access ? pressed_line : lines;
	for (float x : {221.376f, 227.606f, 233.817f}) {
		const PointF dot = transform_point(PointF(x, 194.708f), world);
		const float radius = 1.45f * scaled_line(1.0f, world);
		draw_circle(graphics, dot, radius, quick_icon, quick_icon, 0.0f,
			    state.buttons.quick_access, settings.shine);
	}

	draw_stick(graphics, world, PointF(162.133f, 108.758f), state.axes.left_stick_x, state.axes.left_stick_y,
		   state.buttons.left_stick, state.buttons.left_stick_touch, settings, idle, pressed, lines);
	draw_stick(graphics, world, PointF(293.133f, 108.758f), state.axes.right_stick_x, state.axes.right_stick_y,
		   state.buttons.right_stick, state.buttons.right_stick_touch, settings, idle, pressed, lines);

	if (state.buttons.left_bumper)
		draw_filled_path(graphics, *left_bumper_path(), world, pressed, pressed_line, body_line, true,
				 settings.shine);
	if (state.buttons.right_bumper)
		draw_filled_path(graphics, *right_bumper_path(), world, pressed, pressed_line, body_line, true,
				 settings.shine);

	draw_grip_sensor(graphics, world, false, state.buttons.left_grip_touch, settings);
	draw_grip_sensor(graphics, world, true, state.buttons.right_grip_touch, settings);
}

RenderFrame copy_bitmap(Bitmap &bitmap, uint32_t width, uint32_t height)
{
	RenderFrame frame;
	frame.texture_width = width;
	frame.texture_height = height;
	frame.stride = width * 4;
	frame.pixels.resize(static_cast<size_t>(frame.stride) * height);

	BitmapData data{};
	Rect rectangle(0, 0, static_cast<INT>(width), static_cast<INT>(height));
	if (bitmap.LockBits(&rectangle, ImageLockModeRead, PixelFormat32bppPARGB, &data) != Ok)
		throw std::runtime_error("GDI+ could not read the rendered controller");

	const auto *scan = static_cast<const uint8_t *>(data.Scan0);
	const INT source_stride = data.Stride;
	for (uint32_t row = 0; row < height; ++row) {
		const uint32_t source_row = source_stride < 0 ? height - row - 1 : row;
		const auto *source = scan + static_cast<ptrdiff_t>(source_row) * std::abs(source_stride);
		auto *destination = frame.pixels.data() + static_cast<size_t>(row) * frame.stride;
		std::memcpy(destination, source, frame.stride);
	}
	bitmap.UnlockBits(&data);
	return frame;
}

RenderFrame render_controller(const ControllerState &state, const SourceSettings &settings)
{
	Bitmap bitmap(static_cast<INT>(settings.width), static_cast<INT>(settings.height), PixelFormat32bppPARGB);
	Graphics graphics(&bitmap);
	graphics.SetSmoothingMode(SmoothingModeAntiAlias);
	graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
	graphics.SetCompositingMode(CompositingModeSourceOver);
	graphics.Clear(Color(0, 0, 0, 0));
	render_controller_shapes(graphics, state, settings, settings.width, settings.height);
	return copy_bitmap(bitmap, settings.width, settings.height);
}

SourceSettings read_settings(obs_data_t *settings)
{
	SourceSettings result;
	result.backend_url = trim(get_string(settings, SETTING_BACKEND_URL));
	if (result.backend_url.empty())
		result.backend_url = "http://127.0.0.1:31337";
	while (!result.backend_url.empty() && result.backend_url.back() == '/')
		result.backend_url.pop_back();
	result.auto_backend = obs_data_get_bool(settings, SETTING_AUTO_BACKEND);
	result.poll_interval = static_cast<uint32_t>(
		clamp_value<int64_t>(obs_data_get_int(settings, SETTING_POLL_INTERVAL), 8, 250));
	result.width = static_cast<uint32_t>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_WIDTH), 320, 3840));
	result.height = static_cast<uint32_t>(
		clamp_value<int64_t>(obs_data_get_int(settings, SETTING_HEIGHT), 240, 2160));
	result.body_lines = static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_BODY_LINES), 0, 50));
	result.inner_body_lines =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_INNER_BODY_LINES), 0, 50));
	result.joystick_lines =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_JOYSTICK_LINES), 0, 50));
	result.button_lines =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_BUTTON_LINES), 0, 50));
	result.back_button_lines =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_BACK_BUTTON_LINES), 0, 50));
	result.lines_color = static_cast<uint32_t>(obs_data_get_int(settings, SETTING_LINES_COLOR));
	result.lines_opacity =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_LINES_OPACITY), 0, 100));
	result.body_color = static_cast<uint32_t>(obs_data_get_int(settings, SETTING_BODY_COLOR));
	result.body_opacity =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_BODY_OPACITY), 0, 100));
	result.button_idle_color = static_cast<uint32_t>(obs_data_get_int(settings, SETTING_BUTTON_IDLE_COLOR));
	result.button_idle_opacity =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_BUTTON_IDLE_OPACITY), 0, 100));
	result.button_pressed_color = static_cast<uint32_t>(obs_data_get_int(settings, SETTING_BUTTON_PRESSED_COLOR));
	result.button_pressed_opacity =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_BUTTON_PRESSED_OPACITY), 0, 100));
	result.trigger_idle_opacity =
		static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_TRIGGER_IDLE_OPACITY), 0, 100));
	result.trigger_idle_lines_opacity = static_cast<int>(
		clamp_value<int64_t>(obs_data_get_int(settings, SETTING_TRIGGER_IDLE_LINES_OPACITY), 0, 100));
	result.shine = static_cast<int>(clamp_value<int64_t>(obs_data_get_int(settings, SETTING_SHINE), 0, 200));
	return result;
}

struct ScaleFilterContext {
	obs_source_t *target = nullptr;
	size_t applied = 0;
};

bool initialize_scale_filter_in_scene(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto &context = *static_cast<ScaleFilterContext *>(data);
	if (obs_sceneitem_get_source(item) == context.target) {
		obs_data_t *private_settings = obs_sceneitem_get_private_settings(item);
		if (!obs_data_get_bool(private_settings, SCENE_ITEM_FILTER_INITIALIZED)) {
			if (obs_sceneitem_get_scale_filter(item) == OBS_SCALE_DISABLE) {
				obs_sceneitem_set_scale_filter(item, OBS_SCALE_BILINEAR);
				++context.applied;
			}
			obs_data_set_bool(private_settings, SCENE_ITEM_FILTER_INITIALIZED, true);
		}
		obs_data_release(private_settings);
	}

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, initialize_scale_filter_in_scene, &context);
	return true;
}

bool initialize_scale_filter_in_source(void *data, obs_source_t *scene_source)
{
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (scene)
		obs_scene_enum_items(scene, initialize_scale_filter_in_scene, data);
	return true;
}

void initialize_scale_filter(void *data)
{
	auto *target = static_cast<obs_source_t *>(data);
	ScaleFilterContext context{target};
	obs_enum_scenes(initialize_scale_filter_in_source, &context);
	if (context.applied > 0) {
		obs_log(LOG_INFO, "enabled Bilinear scale filtering for %zu scene item%s", context.applied,
			context.applied == 1 ? "" : "s");
	}
	obs_source_release(target);
}

void queue_scale_filter_initialization(obs_source_t *source)
{
	obs_source_t *reference = source ? obs_source_get_ref(source) : nullptr;
	if (reference)
		obs_queue_task(OBS_TASK_UI, initialize_scale_filter, reference, false);
}

class ControllerSource {
public:
	explicit ControllerSource(obs_data_t *settings, obs_source_t *source) : source_(source)
	{
		update(settings);
		worker_ = std::thread([this] { worker_loop(); });
	}

	~ControllerSource()
	{
		sync_backend_registration(false);
		stop_.store(true);
		condition_.notify_all();
		if (worker_.joinable())
			worker_.join();

		obs_enter_graphics();
		gs_texture_destroy(texture_);
		texture_ = nullptr;
		obs_leave_graphics();
	}

	void update(obs_data_t *settings)
	{
		migrate_color_encoding(settings);
		{
			std::lock_guard lock(settings_mutex_);
			settings_ = read_settings(settings);
			output_width_.store(settings_.width);
			output_height_.store(settings_.height);
			settings_revision_.fetch_add(1);
		}
		sync_backend_registration(active_.load());
		condition_.notify_all();
	}

	void set_active(bool active)
	{
		if (active_.exchange(active) == active)
			return;
		if (active)
			queue_scale_filter_initialization(source_);
		sync_backend_registration(active);
		condition_.notify_all();
	}

	uint32_t width() const { return output_width_.load(); }
	uint32_t height() const { return output_height_.load(); }

	void video_render()
	{
		RenderFrame frame;
		uint64_t generation = 0;
		{
			std::lock_guard lock(frame_mutex_);
			if (pending_generation_ > uploaded_generation_) {
				frame = std::move(pending_frame_);
				generation = pending_generation_;
			}
		}

		if (!frame.pixels.empty()) {
			if (!texture_ || texture_width_ != frame.texture_width ||
			    texture_height_ != frame.texture_height) {
				gs_texture_destroy(texture_);
				const uint8_t *levels[] = {frame.pixels.data()};
				texture_ = gs_texture_create(frame.texture_width, frame.texture_height, GS_BGRA, 1,
							     levels, GS_DYNAMIC);
				texture_width_ = frame.texture_width;
				texture_height_ = frame.texture_height;
			} else {
				gs_texture_set_image(texture_, frame.pixels.data(), frame.stride, false);
			}
			uploaded_generation_ = generation;
		}

		if (texture_) {
			gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);
			gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
			gs_effect_set_texture(image, texture_);
			while (gs_effect_loop(effect, "Draw"))
				obs_source_draw(texture_, 0, 0, output_width_.load(), output_height_.load(), false);
		}
	}

private:
	void worker_loop()
	{
		std::string previous_error;
		ControllerState previous_state;
		bool has_previous_state = false;
		uint64_t rendered_revision = 0;
		while (!stop_.load()) {
			if (!active_.load()) {
				std::unique_lock lock(wait_mutex_);
				condition_.wait(lock, [this] { return stop_.load() || active_.load(); });
				continue;
			}

			SourceSettings settings;
			uint64_t revision = 0;
			{
				std::lock_guard lock(settings_mutex_);
				settings = settings_;
				revision = settings_revision_.load();
			}

			try {
				if (uses_managed_backend(settings))
					BackendManager::instance().ensure_running();

				const ControllerState state =
					parse_state_json(http_get_json(settings.backend_url + "/api/state"));
				if (!has_previous_state || state != previous_state || revision != rendered_revision) {
					RenderFrame frame = render_controller(state, settings);
					{
						std::lock_guard lock(frame_mutex_);
						pending_frame_ = std::move(frame);
						++pending_generation_;
					}
					previous_state = state;
					has_previous_state = true;
					rendered_revision = revision;
				}
				previous_error.clear();
			} catch (const std::exception &error) {
				if (previous_error != error.what()) {
					obs_log(LOG_WARNING, "controller source update failed: %s", error.what());
					previous_error = error.what();
				}
			}

			std::unique_lock lock(wait_mutex_);
			condition_.wait_for(lock, std::chrono::milliseconds(settings.poll_interval), [this, revision] {
				return stop_.load() || !active_.load() || settings_revision_.load() != revision;
			});
		}
	}

	void sync_backend_registration(bool active)
	{
		SourceSettings settings;
		{
			std::lock_guard lock(settings_mutex_);
			settings = settings_;
		}
		const bool should_acquire = active && uses_managed_backend(settings);
		std::lock_guard lock(backend_mutex_);
		if (should_acquire == backend_acquired_)
			return;
		if (should_acquire)
			BackendManager::instance().acquire();
		else
			BackendManager::instance().release();
		backend_acquired_ = should_acquire;
	}

	std::atomic<bool> stop_{false};
	std::atomic<bool> active_{false};
	obs_source_t *source_ = nullptr;
	std::atomic<uint32_t> output_width_{DEFAULT_WIDTH};
	std::atomic<uint32_t> output_height_{DEFAULT_HEIGHT};
	std::thread worker_;
	std::condition_variable condition_;
	std::mutex wait_mutex_;

	std::mutex settings_mutex_;
	SourceSettings settings_;
	std::atomic<uint64_t> settings_revision_{0};
	std::mutex backend_mutex_;
	bool backend_acquired_ = false;

	std::mutex frame_mutex_;
	RenderFrame pending_frame_;
	uint64_t pending_generation_ = 0;
	uint64_t uploaded_generation_ = 0;

	gs_texture_t *texture_ = nullptr;
	uint32_t texture_width_ = 0;
	uint32_t texture_height_ = 0;
};

void *source_create(obs_data_t *settings, obs_source_t *source)
{
	return new ControllerSource(settings, source);
}

void source_destroy(void *data)
{
	delete static_cast<ControllerSource *>(data);
}

void source_update(void *data, obs_data_t *settings)
{
	if (data)
		static_cast<ControllerSource *>(data)->update(settings);
}

void source_activate(void *data)
{
	if (data)
		static_cast<ControllerSource *>(data)->set_active(true);
}

void source_deactivate(void *data)
{
	if (data)
		static_cast<ControllerSource *>(data)->set_active(false);
}

uint32_t source_width(void *data)
{
	return data ? static_cast<ControllerSource *>(data)->width() : DEFAULT_WIDTH;
}

uint32_t source_height(void *data)
{
	return data ? static_cast<ControllerSource *>(data)->height() : DEFAULT_HEIGHT;
}

void source_render(void *data, gs_effect_t *)
{
	if (data)
		static_cast<ControllerSource *>(data)->video_render();
}

void source_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_BACKEND_URL, "http://127.0.0.1:31337");
	obs_data_set_default_bool(settings, SETTING_AUTO_BACKEND, true);
	obs_data_set_default_int(settings, SETTING_POLL_INTERVAL, DEFAULT_POLL_INTERVAL);
	obs_data_set_default_int(settings, SETTING_WIDTH, DEFAULT_WIDTH);
	obs_data_set_default_int(settings, SETTING_HEIGHT, DEFAULT_HEIGHT);
	obs_data_set_default_int(settings, SETTING_BODY_LINES, 10);
	obs_data_set_default_int(settings, SETTING_INNER_BODY_LINES, 10);
	obs_data_set_default_int(settings, SETTING_JOYSTICK_LINES, 10);
	obs_data_set_default_int(settings, SETTING_BUTTON_LINES, 10);
	obs_data_set_default_int(settings, SETTING_BACK_BUTTON_LINES, 10);
	obs_data_set_default_int(settings, SETTING_LINES_COLOR, 0xffffffff);
	obs_data_set_default_int(settings, SETTING_LINES_OPACITY, 55);
	obs_data_set_default_int(settings, SETTING_BODY_COLOR, 0xff000000);
	obs_data_set_default_int(settings, SETTING_BODY_OPACITY, 30);
	obs_data_set_default_int(settings, SETTING_BUTTON_IDLE_COLOR, 0xffffa725);
	obs_data_set_default_int(settings, SETTING_BUTTON_IDLE_OPACITY, 0);
	obs_data_set_default_int(settings, SETTING_BUTTON_PRESSED_COLOR, 0xffffa725);
	obs_data_set_default_int(settings, SETTING_BUTTON_PRESSED_OPACITY, 100);
	obs_data_set_default_int(settings, SETTING_TRIGGER_IDLE_OPACITY, 0);
	obs_data_set_default_int(settings, SETTING_TRIGGER_IDLE_LINES_OPACITY, 0);
	obs_data_set_default_int(settings, SETTING_SHINE, 100);
}

obs_properties_t *source_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_text(properties, SETTING_BACKEND_URL, obs_module_text("BackendUrl"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(properties, SETTING_AUTO_BACKEND, obs_module_text("AutoBackend"));
	obs_properties_add_int_slider(properties, SETTING_POLL_INTERVAL, obs_module_text("PollInterval"), 8, 250, 1);

	obs_properties_t *layout = obs_properties_create();
	obs_properties_add_int_slider(layout, SETTING_WIDTH, obs_module_text("OutputWidth"), 320, 3840, 10);
	obs_properties_add_int_slider(layout, SETTING_HEIGHT, obs_module_text("OutputHeight"), 240, 2160, 10);
	obs_properties_add_group(properties, "layout_group", "Layout", OBS_GROUP_NORMAL, layout);

	obs_properties_t *lines = obs_properties_create();
	obs_properties_add_int_slider(lines, SETTING_BODY_LINES, obs_module_text("BodyLines"), 0, 50, 1);
	obs_properties_add_int_slider(lines, SETTING_INNER_BODY_LINES, obs_module_text("InnerBodyLines"), 0, 50, 1);
	obs_properties_add_int_slider(lines, SETTING_JOYSTICK_LINES, obs_module_text("JoystickLines"), 0, 50, 1);
	obs_properties_add_int_slider(lines, SETTING_BUTTON_LINES, obs_module_text("ButtonLines"), 0, 50, 1);
	obs_properties_add_int_slider(lines, SETTING_BACK_BUTTON_LINES, obs_module_text("BackButtonLines"), 0, 50, 1);
	obs_properties_add_color(lines, SETTING_LINES_COLOR, obs_module_text("LinesColor"));
	obs_properties_add_int_slider(lines, SETTING_LINES_OPACITY, obs_module_text("LinesOpacity"), 0, 100, 1);
	obs_properties_add_group(properties, "lines_group", "Lines", OBS_GROUP_NORMAL, lines);

	obs_properties_t *colors = obs_properties_create();
	obs_properties_add_color(colors, SETTING_BODY_COLOR, obs_module_text("BodyColor"));
	obs_properties_add_int_slider(colors, SETTING_BODY_OPACITY, obs_module_text("BodyOpacity"), 0, 100, 1);
	obs_properties_add_color(colors, SETTING_BUTTON_IDLE_COLOR, obs_module_text("ButtonIdleColor"));
	obs_properties_add_int_slider(colors, SETTING_BUTTON_IDLE_OPACITY, obs_module_text("ButtonIdleOpacity"), 0, 100, 1);
	obs_properties_add_color(colors, SETTING_BUTTON_PRESSED_COLOR, obs_module_text("ButtonPressedColor"));
	obs_properties_add_int_slider(colors, SETTING_BUTTON_PRESSED_OPACITY, obs_module_text("ButtonPressedOpacity"), 0, 100, 1);
	obs_properties_add_int_slider(colors, SETTING_TRIGGER_IDLE_OPACITY, obs_module_text("TriggerIdleOpacity"), 0, 100, 1);
	obs_properties_add_int_slider(colors, SETTING_TRIGGER_IDLE_LINES_OPACITY, obs_module_text("TriggerIdleLinesOpacity"), 0, 100, 1);
	obs_properties_add_int_slider(colors, SETTING_SHINE, obs_module_text("Shine"), 0, 200, 1);
	obs_properties_add_group(properties, "colors_group", "Colors", OBS_GROUP_NORMAL, colors);

	return properties;
}

const char *source_name(void *)
{
	return obs_module_text("SteamControllerSource");
}

} // namespace

void register_steam_controller_source()
{
	static obs_source_info info{};
	info.id = SOURCE_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = source_name;
	info.create = source_create;
	info.destroy = source_destroy;
	info.update = source_update;
	info.activate = source_activate;
	info.deactivate = source_deactivate;
	info.get_defaults = source_defaults;
	info.get_properties = source_properties;
	info.video_render = source_render;
	info.get_width = source_width;
	info.get_height = source_height;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}
