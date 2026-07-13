/*
Steam Controller Gamepad Viewer OBS source
Copyright (C) 2026 ramonchi5

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "svg-path.hpp"

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

using namespace Gdiplus;

namespace {

class SvgPathParser {
public:
	SvgPathParser(std::string_view data, FillMode fill_mode)
		: storage_(data), cursor_(storage_.c_str()), end_(cursor_ + storage_.size()),
		  path_(std::make_unique<GraphicsPath>(fill_mode))
	{
	}

	std::unique_ptr<GraphicsPath> parse()
	{
		char command = 0;
		while (true) {
			skip_separators();
			if (cursor_ >= end_)
				break;

			if (std::isalpha(static_cast<unsigned char>(*cursor_)))
				command = *cursor_++;
			else if (command == 0)
				fail("missing command");

			const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
			switch (static_cast<char>(std::toupper(static_cast<unsigned char>(command)))) {
			case 'M':
				parse_move(relative);
				command = relative ? 'l' : 'L';
				break;
			case 'L':
				parse_lines(relative);
				break;
			case 'H':
				parse_horizontal(relative);
				break;
			case 'V':
				parse_vertical(relative);
				break;
			case 'C':
				parse_curves(relative);
				break;
			case 'Z':
				path_->CloseFigure();
				current_ = figure_start_;
				command = 0;
				break;
			default:
				fail("unsupported command");
			}
		}
		return std::move(path_);
	}

private:
	void skip_separators()
	{
		while (cursor_ < end_ &&
		       (std::isspace(static_cast<unsigned char>(*cursor_)) || *cursor_ == ','))
			++cursor_;
	}

	bool has_number()
	{
		skip_separators();
		return cursor_ < end_ && !std::isalpha(static_cast<unsigned char>(*cursor_));
	}

	float number()
	{
		skip_separators();
		if (cursor_ >= end_)
			fail("missing number");
		char *next = nullptr;
		const float value = std::strtof(cursor_, &next);
		if (next == cursor_ || next > end_)
			fail("invalid number");
		cursor_ = next;
		return value;
	}

	PointF point(bool relative)
	{
		const float x = number();
		const float y = number();
		PointF result(x, y);
		if (relative) {
			result.X += current_.X;
			result.Y += current_.Y;
		}
		return result;
	}

	void parse_move(bool relative)
	{
		current_ = point(relative);
		figure_start_ = current_;
		path_->StartFigure();
		while (has_number()) {
			const PointF next = point(relative);
			path_->AddLine(current_, next);
			current_ = next;
		}
	}

	void parse_lines(bool relative)
	{
		while (has_number()) {
			const PointF next = point(relative);
			path_->AddLine(current_, next);
			current_ = next;
		}
	}

	void parse_horizontal(bool relative)
	{
		while (has_number()) {
			float x = number();
			if (relative)
				x += current_.X;
			const PointF next(x, current_.Y);
			path_->AddLine(current_, next);
			current_ = next;
		}
	}

	void parse_vertical(bool relative)
	{
		while (has_number()) {
			float y = number();
			if (relative)
				y += current_.Y;
			const PointF next(current_.X, y);
			path_->AddLine(current_, next);
			current_ = next;
		}
	}

	void parse_curves(bool relative)
	{
		while (has_number()) {
			PointF control1 = point(relative);
			PointF control2 = point(relative);
			PointF next = point(relative);
			path_->AddBezier(current_, control1, control2, next);
			current_ = next;
		}
	}

	[[noreturn]] void fail(const char *message) const
	{
		throw std::runtime_error(std::string("Invalid SVG path: ") + message);
	}

	std::string storage_;
	const char *cursor_;
	const char *end_;
	PointF current_{};
	PointF figure_start_{};
	std::unique_ptr<GraphicsPath> path_;
};

} // namespace

std::unique_ptr<GraphicsPath> parse_svg_path(std::string_view data, FillMode fill_mode)
{
	return SvgPathParser(data, fill_mode).parse();
}
