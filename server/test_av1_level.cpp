// SPDX-License-Identifier: GPL-3.0-or-later
#include "encoder/av1_level.h"

#include <iostream>
#include <limits>

using namespace wivrn;

constexpr auto max_level = STD_VIDEO_AV1_LEVEL_6_3;

// These cases distinguish display rate from decode rate, exercise fractional
// refresh rates and bitrate tiers, and cover the 50 -> 140 Mbit/s transition
// (approximately half the total bitrate goes to each eye).
static_assert(choose_av1_operating_point(1280, 1344, 80, 24'600'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_4_1, 1});
static_assert(choose_av1_operating_point(1280, 1344, 90, 24'600'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_5_0, 0});
static_assert(choose_av1_operating_point(1280, 1344, 90, 69'000'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_5_0, 1});
static_assert(choose_av1_operating_point(1280, 1344, 80, 69'000'000, max_level, av1_operating_point{STD_VIDEO_AV1_LEVEL_4_1, 1}) == av1_operating_point{STD_VIDEO_AV1_LEVEL_5_0, 1});
static_assert(choose_av1_operating_point(1280, 1344, 80, 24'600'000, max_level, av1_operating_point{STD_VIDEO_AV1_LEVEL_5_0, 1}) == av1_operating_point{STD_VIDEO_AV1_LEVEL_5_0, 1});
static_assert(choose_av1_operating_point(1280, 720, 30, 10'000'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_3_1, 0});
static_assert(choose_av1_operating_point(1280, 720, 30, 10'000'001, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_4_0, 0});
static_assert(choose_av1_operating_point(1920, 1080, 60, 20'000'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_4_1, 0});
static_assert(choose_av1_operating_point(2048, 1152, 60, 20'000'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_4_1, 0});
static_assert(choose_av1_operating_point(2048, 1152, 60.001, 20'000'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_5_0, 0});
static_assert(choose_av1_operating_point(1920, 1080, 59.94, 50'000'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_4_1, 1});
static_assert(choose_av1_operating_point(4096, 2160, 120, 100'000'000, max_level) == av1_operating_point{STD_VIDEO_AV1_LEVEL_5_2, 1});

int main()
{
	auto rejects = [](uint32_t width, uint32_t height, double fps, uint64_t bitrate, StdVideoAV1Level max) {
		try
		{
			choose_av1_operating_point(width, height, fps, bitrate, max);
			return false;
		}
		catch (const std::runtime_error &)
		{
			return true;
		}
	};
	if (not rejects(0, 720, 90, 10'000'000, max_level) or
	    not rejects(1280, 720, 0, 10'000'000, max_level) or
	    not rejects(1280, 720, std::numeric_limits<double>::quiet_NaN(), 10'000'000, max_level) or
	    not rejects(1280, 720, 301, 10'000'000, max_level) or
	    not rejects(1280, 1344, 90, 24'600'000, STD_VIDEO_AV1_LEVEL_4_1) or
	    not rejects(1280, 1344, 80, 69'000'000, STD_VIDEO_AV1_LEVEL_4_1) or
	    not rejects(16384, 8704, 120, 100'000'000, max_level) or
	    not rejects(1280, 720, 90, 800'000'001, max_level))
	{
		std::cerr << "Invalid AV1 configuration accepted\n";
		return 1;
	}
	std::cout << "AV1 level and tier checks passed\n";
}
