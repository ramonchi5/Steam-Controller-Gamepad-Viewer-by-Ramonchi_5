/*
Steam Controller Gamepad Viewer OBS source
Copyright (C) 2026 ramonchi5

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "backend-manager.hpp"

#include <winhttp.h>

#include <obs-module.h>
#include <plugin-support.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {
constexpr INTERNET_PORT BACKEND_PORT = 31337;

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

std::wstring module_file(const char *relative_path)
{
	char *path = obs_module_file(relative_path);
	if (!path)
		return {};
	const std::wstring result = utf8_to_wide(path);
	bfree(path);
	return result;
}

std::string windows_error(const char *operation)
{
	return std::string(operation) + " failed with Windows error " + std::to_string(GetLastError());
}
} // namespace

BackendManager &BackendManager::instance()
{
	static BackendManager manager;
	return manager;
}

BackendManager::~BackendManager()
{
	std::lock_guard lock(mutex_);
	stop_locked();
}

void BackendManager::acquire()
{
	std::lock_guard lock(mutex_);
	++users_;
}

void BackendManager::release()
{
	std::lock_guard lock(mutex_);
	if (users_ == 0)
		return;
	--users_;
	if (users_ == 0)
		stop_locked();
}

void BackendManager::ensure_running()
{
	std::lock_guard lock(mutex_);
	if (users_ == 0)
		return;

	if (process_) {
		DWORD exit_code = 0;
		if (GetExitCodeProcess(process_, &exit_code) && exit_code == STILL_ACTIVE)
			return;
		obs_log(LOG_WARNING, "bundled backend exited; restarting it");
		clear_process_locked();
	}

	if (using_external_backend_) {
		const auto now = std::chrono::steady_clock::now();
		if (now < next_external_check_)
			return;
		if (health_check()) {
			next_external_check_ = now + std::chrono::seconds(1);
			return;
		}
		using_external_backend_ = false;
		obs_log(LOG_INFO, "existing local backend stopped; starting the bundled backend");
	}

	start_locked();
}

bool BackendManager::health_check() const
{
	const std::wstring user_agent =
		utf8_to_wide(std::string("SteamControllerGamepadViewer-Backend/") + PLUGIN_VERSION);
	HINTERNET session = WinHttpOpen(user_agent.c_str(),
					WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return false;
	WinHttpSetTimeouts(session, 300, 300, 300, 300);

	HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", BACKEND_PORT, 0);
	HINTERNET request = connection
				? WinHttpOpenRequest(connection, L"GET", L"/health", nullptr,
						     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
				: nullptr;
	bool healthy = false;
	if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
					  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
	    WinHttpReceiveResponse(request, nullptr)) {
		DWORD status = 0;
		DWORD length = sizeof(status);
		healthy = WinHttpQueryHeaders(request,
					      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					      WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
					      WINHTTP_NO_HEADER_INDEX) &&
			  status == 200;
	}

	if (request)
		WinHttpCloseHandle(request);
	if (connection)
		WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return healthy;
}

void BackendManager::start_locked()
{
	if (users_ == 0 || process_)
		return;
	if (health_check()) {
		using_external_backend_ = true;
		next_external_check_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		obs_log(LOG_INFO, "using existing local backend on 127.0.0.1:31337");
		return;
	}

	const std::wstring backend = module_file("backend/SteamControllerGamepadViewer.exe");
	std::error_code backend_error;
	const bool backend_exists = !backend.empty() && std::filesystem::is_regular_file(backend, backend_error);
	if (!backend_exists) {
		obs_log(LOG_ERROR,
			"The bundled Steam Controller backend is missing. Reinstall the complete plugin folder.");
		return;
	}

	const std::wstring command = L"\"" + backend + L"\" --urls http://127.0.0.1:31337";
	std::vector<wchar_t> command_buffer(command.begin(), command.end());
	command_buffer.push_back(L'\0');

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESHOWWINDOW;
	startup.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION process_info{};
	const std::wstring working_directory = std::filesystem::path(backend).parent_path().wstring();
	if (!CreateProcessW(backend.c_str(), command_buffer.data(), nullptr, nullptr, FALSE,
			    CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, working_directory.c_str(),
			    &startup, &process_info)) {
		obs_log(LOG_ERROR, "%s", windows_error("Starting the bundled backend").c_str());
		return;
	}

	job_ = CreateJobObjectW(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits,
					       sizeof(limits)) ||
	    !AssignProcessToJobObject(job_, process_info.hProcess)) {
		const std::string error = windows_error("Preparing the bundled backend process");
		TerminateProcess(process_info.hProcess, 1);
		CloseHandle(process_info.hThread);
		CloseHandle(process_info.hProcess);
		if (job_)
			CloseHandle(job_);
		job_ = nullptr;
		obs_log(LOG_ERROR, "%s", error.c_str());
		return;
	}

	process_ = process_info.hProcess;
	if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1)) {
		const std::string error = windows_error("Resuming the bundled backend process");
		TerminateJobObject(job_, 1);
		CloseHandle(process_info.hThread);
		clear_process_locked();
		obs_log(LOG_ERROR, "%s", error.c_str());
		return;
	}
	CloseHandle(process_info.hThread);
	using_external_backend_ = false;
	obs_log(LOG_INFO, "started bundled backend for visible Steam Controller sources");
}

void BackendManager::stop_locked()
{
	if (process_) {
		if (job_)
			TerminateJobObject(job_, 0);
		else
			TerminateProcess(process_, 0);
		WaitForSingleObject(process_, 100);
		obs_log(LOG_INFO, "stopped bundled backend because no Steam Controller sources are visible");
	}
	clear_process_locked();
	using_external_backend_ = false;
	next_external_check_ = {};
}

void BackendManager::clear_process_locked()
{
	if (process_)
		CloseHandle(process_);
	if (job_)
		CloseHandle(job_);
	process_ = nullptr;
	job_ = nullptr;
}
