/*
Steam Controller Gamepad Viewer OBS source
Copyright (C) 2026 ramonchi5

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>

#include <windows.h>

class BackendManager {
public:
	static BackendManager &instance();

	void acquire();
	void release();
	void ensure_running();

private:
	BackendManager() = default;
	~BackendManager();
	BackendManager(const BackendManager &) = delete;
	BackendManager &operator=(const BackendManager &) = delete;

	bool health_check() const;
	void start_locked();
	void stop_locked();
	void clear_process_locked();

	std::mutex mutex_;
	size_t users_ = 0;
	HANDLE process_ = nullptr;
	HANDLE job_ = nullptr;
	bool using_external_backend_ = false;
	std::chrono::steady_clock::time_point next_external_check_{};
};
