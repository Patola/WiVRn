/*
 * WiVRn VR streaming
 * Copyright (C) 2026 Philipp Schlegel <dev@phischle.xyz>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vk_video/vulkan_video_codec_av1std.h>

namespace wivrn
{
struct av1_operating_point
{
	StdVideoAV1Level level;
	uint8_t tier;
	bool operator==(const av1_operating_point &) const = default;
};

// AV1 specification, Annex A.3. WiVRn shows every encoded frame, so the
// display rate (which is lower than the decode rate) is the limiting rate.
// Main/High Mbps are the limits for the Main profile, including 10-bit.
constexpr av1_operating_point choose_av1_operating_point(
        uint32_t width,
        uint32_t height,
        double fps,
        uint64_t bitrate,
        StdVideoAV1Level max_level,
        std::optional<av1_operating_point> current = {})
{
	struct limits
	{
		StdVideoAV1Level level;
		uint32_t max_pic_size, max_h_size, max_v_size;
		uint64_t max_display_rate, main_bitrate, high_bitrate;
	};
	constexpr limits levels[] = {
	        {STD_VIDEO_AV1_LEVEL_2_0, 147456, 2048, 1152, 4423680, 1'500'000, 0},
	        {STD_VIDEO_AV1_LEVEL_2_1, 278784, 2816, 1584, 8363520, 3'000'000, 0},
	        {STD_VIDEO_AV1_LEVEL_3_0, 665856, 4352, 2448, 19975680, 6'000'000, 0},
	        {STD_VIDEO_AV1_LEVEL_3_1, 1065024, 5504, 3096, 31950720, 10'000'000, 0},
	        {STD_VIDEO_AV1_LEVEL_4_0, 2359296, 6144, 3456, 70778880, 12'000'000, 30'000'000},
	        {STD_VIDEO_AV1_LEVEL_4_1, 2359296, 6144, 3456, 141557760, 20'000'000, 50'000'000},
	        {STD_VIDEO_AV1_LEVEL_5_0, 8912896, 8192, 4352, 267386880, 30'000'000, 100'000'000},
	        {STD_VIDEO_AV1_LEVEL_5_1, 8912896, 8192, 4352, 534773760, 40'000'000, 160'000'000},
	        {STD_VIDEO_AV1_LEVEL_5_2, 8912896, 8192, 4352, 1069547520, 60'000'000, 240'000'000},
	        {STD_VIDEO_AV1_LEVEL_5_3, 8912896, 8192, 4352, 1069547520, 60'000'000, 240'000'000},
	        {STD_VIDEO_AV1_LEVEL_6_0, 35651584, 16384, 8704, 1069547520, 60'000'000, 240'000'000},
	        {STD_VIDEO_AV1_LEVEL_6_1, 35651584, 16384, 8704, 2139095040, 100'000'000, 480'000'000},
	        {STD_VIDEO_AV1_LEVEL_6_2, 35651584, 16384, 8704, 4278190080, 160'000'000, 800'000'000},
	        {STD_VIDEO_AV1_LEVEL_6_3, 35651584, 16384, 8704, 4278190080, 160'000'000, 800'000'000},
	};
	const uint64_t pic_size = uint64_t(width) * height;
	if (not width or not height or not(fps > 0))
		throw std::runtime_error("Invalid AV1 stream dimensions or frame rate");

	for (const auto & l: levels)
	{
		if (l.level > max_level)
			break;
		if (current and l.level < current->level)
			continue;
		if (fps > (l.level < STD_VIDEO_AV1_LEVEL_4_0 ? 150 : 300))
			continue;
		if (pic_size > l.max_pic_size or width > l.max_h_size or height > l.max_v_size or double(pic_size) * fps > l.max_display_rate)
			continue;
		// Keep an existing tier on decreases, avoiding unnecessary sequence
		// headers and decoder resets when the user lowers the bitrate.
		if (current and l.level == current->level and bitrate <= (current->tier ? l.high_bitrate : l.main_bitrate))
			return *current;
		if (bitrate <= l.main_bitrate)
			return {l.level, 0};
		if (l.high_bitrate and bitrate <= l.high_bitrate)
			return {l.level, 1};
	}
	throw std::runtime_error("AV1 stream exceeds the encoder's supported level");
}
} // namespace wivrn
