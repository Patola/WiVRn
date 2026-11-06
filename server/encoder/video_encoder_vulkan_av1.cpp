/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Philipp Schlegel <dev@phischle.xyz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "video_encoder_vulkan_av1.h"

#include "encoder/encoder_settings.h"
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace
{
uint8_t bits_for(uint32_t value)
{
	return std::max<uint8_t>(std::bit_width(value), 1);
}

} // namespace

wivrn::video_encoder_vulkan_av1::video_encoder_vulkan_av1(
        wivrn::vk_bundle & vk,
        const vk::VideoCapabilitiesKHR & video_caps,
        const vk::VideoEncodeCapabilitiesKHR & encode_caps,
        const vk::VideoEncodeAV1CapabilitiesKHR & encode_av1_caps,
        uint8_t stream_idx,
        const encoder_settings & settings) :
        video_encoder_vulkan(vk, video_caps, encode_caps, stream_idx, settings)
{
	if (not vk.has_device_ext(VK_KHR_VIDEO_ENCODE_AV1_EXTENSION_NAME) or
	    not std::get<vk::PhysicalDeviceVideoEncodeAV1FeaturesKHR>(vk.feat).videoEncodeAV1)
		throw std::runtime_error("Vulkan video encode AV1 extension not available");

	if (settings.bit_depth != 8 && settings.bit_depth != 10)
		throw std::runtime_error("av1 encoder supports 8-bit or 10-bit only");

	configure_from_caps(encode_av1_caps);

	max_level = encode_av1_caps.maxLevel;
	explicit_operating_point = encode_av1_caps.maxOperatingPoints > 0;
	auto selected = choose_av1_operating_point(aligned_extent.width, aligned_extent.height, settings.fps, rate_control ? rate_control_layer.maxBitrate : settings.bitrate, max_level);

	color_config = {
	        .flags = {
	                .mono_chrome = 0,
	                .color_range = 1,
	                .separate_uv_delta_q = 0,
	                .color_description_present_flag = 1,
	        },
	        .BitDepth = static_cast<uint8_t>(settings.bit_depth),
	        .subsampling_x = 1,
	        .subsampling_y = 1,
	        .reserved1 = 0,
	        .color_primaries = STD_VIDEO_AV1_COLOR_PRIMARIES_BT_709,
	        .transfer_characteristics = STD_VIDEO_AV1_TRANSFER_CHARACTERISTICS_BT_709,
	        .matrix_coefficients = STD_VIDEO_AV1_MATRIX_COEFFICIENTS_BT_709,
	        .chroma_sample_position = STD_VIDEO_AV1_CHROMA_SAMPLE_POSITION_COLOCATED,
	};

	seq_header = {
	        .flags = {
	                .still_picture = 0,
	                .reduced_still_picture_header = 0,
	                .use_128x128_superblock = superblock_size == 128 ? 1u : 0u,
	                .enable_filter_intra = 0,
	                .enable_intra_edge_filter = 0,
	                .enable_interintra_compound = 0,
	                .enable_masked_compound = 0,
	                .enable_warped_motion = 0,
	                .enable_dual_filter = 0,
	                .enable_order_hint = 1,
	                .enable_jnt_comp = 0,
	                .enable_ref_frame_mvs = 0,
	                .frame_id_numbers_present_flag = 0,
	                .enable_superres = 0,
	                .enable_cdef = 0,
	                .enable_restoration = 0,
	                .film_grain_params_present = 0,
	                .timing_info_present_flag = 0,
	                .initial_display_delay_present_flag = 0,
	        },
	        .seq_profile = STD_VIDEO_AV1_PROFILE_MAIN,
	        .frame_width_bits_minus_1 = static_cast<uint8_t>(bits_for(aligned_extent.width - 1) - 1),
	        .frame_height_bits_minus_1 = static_cast<uint8_t>(bits_for(aligned_extent.height - 1) - 1),
	        .max_frame_width_minus_1 = static_cast<uint16_t>(aligned_extent.width - 1),
	        .max_frame_height_minus_1 = static_cast<uint16_t>(aligned_extent.height - 1),
	        .delta_frame_id_length_minus_2 = 0,
	        .additional_frame_id_length_minus_1 = 0,
	        .order_hint_bits_minus_1 = static_cast<uint8_t>(order_hint_bits - 1),
	        .seq_force_integer_mv = STD_VIDEO_AV1_SELECT_INTEGER_MV,
	        .seq_force_screen_content_tools = STD_VIDEO_AV1_SELECT_SCREEN_CONTENT_TOOLS,
	        .reserved1 = {},
	        .pColorConfig = &color_config,
	        .pTimingInfo = nullptr,
	};

	operating_point = {
	        .flags = {
	                .decoder_model_present_for_this_op = 0,
	                .low_delay_mode_flag = 1,
	                .initial_display_delay_present_for_this_op = 0,
	        },
	        .operating_point_idc = 0,
	        .seq_level_idx = static_cast<uint8_t>(selected.level),
	        .seq_tier = selected.tier,
	        .decoder_buffer_delay = 0,
	        .encoder_buffer_delay = 0,
	        .initial_display_delay_minus_1 = 0,
	};

	quantization = {
	        .flags = {
	                .using_qmatrix = 0,
	                .diff_uv_delta = 0,
	        },
	        .base_q_idx = static_cast<uint8_t>(encode_av1_caps.maxQIndex),
	        .DeltaQYDc = 0,
	        .DeltaQUDc = 0,
	        .DeltaQUAc = 0,
	        .DeltaQVDc = 0,
	        .DeltaQVAc = 0,
	        .qm_y = 0,
	        .qm_u = 0,
	        .qm_v = 0,
	};

	loop_filter = {
	        .flags = {
	                .loop_filter_delta_enabled = 0,
	                .loop_filter_delta_update = 0,
	        },
	        .loop_filter_level = {},
	        .loop_filter_sharpness = 0,
	        .update_ref_delta = 0,
	        .loop_filter_ref_deltas = {},
	        .update_mode_delta = 0,
	        .loop_filter_mode_deltas = {},
	};

	cdef = {
	        .cdef_damping_minus_3 = 0,
	        .cdef_bits = 0,
	        .cdef_y_pri_strength = {},
	        .cdef_y_sec_strength = {},
	        .cdef_uv_pri_strength = {},
	        .cdef_uv_sec_strength = {},
	};

	loop_restoration = {
	        .FrameRestorationType = {STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_NONE,
	                                 STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_NONE,
	                                 STD_VIDEO_AV1_FRAME_RESTORATION_TYPE_NONE},
	        .LoopRestorationSize = {0, 0, 0},
	};

	global_motion = {
	        .GmType = {},
	        .gm_params = {},
	};

	segmentation = {};

	rate_control_layer.pNext = &rate_control_layer_av1;
}

void wivrn::video_encoder_vulkan_av1::configure_from_caps(const vk::VideoEncodeAV1CapabilitiesKHR & encode_av1_caps)
{
	U_LOG_D("AV1: capabilities: stdSyntaxFlags=0x%x, superblockSizes=0x%x, singleReferenceNameMask=0x%x, maxSingleReferenceCount=%u, maxUnidirectionalCompoundReferenceCount=%u, maxBidirectionalCompoundReferenceCount=%u, maxQIndex=%u, minQIndex=%u, requiresGopRemainingFrames=%u, maxOperatingPoints=%u",
	        static_cast<uint32_t>(encode_av1_caps.stdSyntaxFlags),
	        static_cast<uint32_t>(encode_av1_caps.superblockSizes),
	        encode_av1_caps.singleReferenceNameMask,
	        encode_av1_caps.maxSingleReferenceCount,
	        encode_av1_caps.maxUnidirectionalCompoundReferenceCount,
	        encode_av1_caps.maxBidirectionalCompoundReferenceCount,
	        encode_av1_caps.maxQIndex,
	        encode_av1_caps.minQIndex,
	        encode_av1_caps.requiresGopRemainingFrames,
	        encode_av1_caps.maxOperatingPoints);
	std_flags = encode_av1_caps.stdSyntaxFlags;
	if (encode_av1_caps.superblockSizes & vk::VideoEncodeAV1SuperblockSizeFlagBitsKHR::e64)
		superblock_size = 64;
	else if (encode_av1_caps.superblockSizes & vk::VideoEncodeAV1SuperblockSizeFlagBitsKHR::e128)
		superblock_size = 128;
	else
		throw std::runtime_error("AV1 encoder has no supported superblock size");

	single_reference_name_mask = encode_av1_caps.singleReferenceNameMask;
	max_single_reference_count = encode_av1_caps.maxSingleReferenceCount;

	// The single reference of an inter frame must use a reference name the encoder
	// advertises, bit i standing for LAST_FRAME + i.
	// VUID-vkCmdEncodeVideoKHR-predictionMode-10329
	ref_name_index = -1;
	for (size_t i = 0; i < reference_name_slot_indices.size(); ++i)
	{
		if (single_reference_name_mask & (1u << i))
		{
			ref_name_index = int32_t(i);
			break;
		}
	}
	// WiVRn streams key frame + inter frames referencing a single acknowledged frame, an
	// encoder that cannot do single reference prediction is of no use to us.
	if (ref_name_index < 0 or max_single_reference_count == 0)
		throw std::runtime_error("av1 encoder does not support single reference prediction");
	if (ref_name_index != 0)
		U_LOG_W("AV1: LAST_FRAME not supported (mask=0x%x), using reference name index %d", single_reference_name_mask, ref_name_index);

	rate_control_av1 = vk::VideoEncodeAV1RateControlInfoKHR{
	        .flags = vk::VideoEncodeAV1RateControlFlagBitsKHR::eRegularGop,
	        .gopFrameCount = std::numeric_limits<uint32_t>::max(),
	        .keyFramePeriod = std::numeric_limits<uint32_t>::max(),
	        .consecutiveBipredictiveFrameCount = 0,
	        // VUID-VkVideoEncodeAV1RateControlInfoKHR-temporalLayerCount-10299
	        .temporalLayerCount = std::min(1u, encode_av1_caps.maxTemporalLayerCount),
	};

	rate_control_layer_av1 = vk::VideoEncodeAV1RateControlLayerInfoKHR{
	        .useMinQIndex = VK_FALSE,
	        .minQIndex = {
	                .intraQIndex = encode_av1_caps.minQIndex,
	                .predictiveQIndex = encode_av1_caps.minQIndex,
	                .bipredictiveQIndex = encode_av1_caps.minQIndex,
	        },
	        .useMaxQIndex = VK_FALSE,
	        .maxQIndex = {
	                .intraQIndex = encode_av1_caps.maxQIndex,
	                .predictiveQIndex = encode_av1_caps.maxQIndex,
	                .bipredictiveQIndex = encode_av1_caps.maxQIndex,
	        },
	        .useMaxFrameSize = VK_FALSE,
	        .maxFrameSize = {},
	};
}

std::vector<void *> wivrn::video_encoder_vulkan_av1::setup_slot_info(size_t dpb_size)
{
	dpb_std_info.resize(dpb_size, {});
	dpb_std_slots.reserve(dpb_size);
	std::vector<void *> res;
	for (size_t i = 0; i < dpb_size; ++i)
	{
		dpb_std_slots.push_back({
		        .pStdReferenceInfo = &dpb_std_info[i],
		});
		res.push_back(&dpb_std_slots[i]);
	}

	return res;
}

static auto get_video_caps(vk::raii::PhysicalDevice & phys_dev, int bit_depth)
{
	if (!(bit_depth == 8 || bit_depth == 10))
		throw std::runtime_error("av1 encoder supports 8-bit or 10-bit only");

	vk::StructureChain video_profile_info{
	        vk::VideoProfileInfoKHR{
	                .videoCodecOperation = vk::VideoCodecOperationFlagBitsKHR::eEncodeAv1,
	                .chromaSubsampling = vk::VideoChromaSubsamplingFlagBitsKHR::e420,
	                .lumaBitDepth = bit_depth == 10 ? vk::VideoComponentBitDepthFlagBitsKHR::e10 : vk::VideoComponentBitDepthFlagBitsKHR::e8,
	                .chromaBitDepth = bit_depth == 10 ? vk::VideoComponentBitDepthFlagBitsKHR::e10 : vk::VideoComponentBitDepthFlagBitsKHR::e8,
	        },
	        vk::VideoEncodeAV1ProfileInfoKHR{
	                .stdProfile = STD_VIDEO_AV1_PROFILE_MAIN,
	        },
	        vk::VideoEncodeUsageInfoKHR{
	                .videoUsageHints = vk::VideoEncodeUsageFlagBitsKHR::eStreaming,
	                .videoContentHints = vk::VideoEncodeContentFlagBitsKHR::eRendered,
	                .tuningMode = vk::VideoEncodeTuningModeKHR::eUltraLowLatency,
	        }};

	try
	{
		auto [video_caps, encode_caps, encode_av1_caps] =
		        phys_dev.getVideoCapabilitiesKHR<
		                vk::VideoCapabilitiesKHR,
		                vk::VideoEncodeCapabilitiesKHR,
		                vk::VideoEncodeAV1CapabilitiesKHR>(video_profile_info.get());

		video_caps.maxDpbSlots = std::min(video_caps.maxDpbSlots, uint32_t(STD_VIDEO_AV1_NUM_REF_FRAMES));
		return std::make_tuple(video_caps, encode_caps, encode_av1_caps, video_profile_info);
	}
	catch (...)
	{}

	video_profile_info.unlink<vk::VideoEncodeUsageInfoKHR>();
	auto [video_caps, encode_caps, encode_av1_caps] =
	        phys_dev.getVideoCapabilitiesKHR<
	                vk::VideoCapabilitiesKHR,
	                vk::VideoEncodeCapabilitiesKHR,
	                vk::VideoEncodeAV1CapabilitiesKHR>(video_profile_info.get());
	video_caps.maxDpbSlots = std::min(video_caps.maxDpbSlots, uint32_t(STD_VIDEO_AV1_NUM_REF_FRAMES));
	return std::make_tuple(video_caps, encode_caps, encode_av1_caps, video_profile_info);
}

std::unique_ptr<wivrn::video_encoder_vulkan_av1> wivrn::video_encoder_vulkan_av1::create(
        wivrn::vk_bundle & vk,
        const encoder_settings & settings,
        uint8_t stream_idx)
{
	auto [video_caps, encode_caps, encode_av1_caps, video_profile_info] = get_video_caps(vk.physical_device, settings.bit_depth);

	std::unique_ptr<video_encoder_vulkan_av1> self(
	        new video_encoder_vulkan_av1(vk, video_caps, encode_caps, encode_av1_caps, stream_idx, settings));

	vk::VideoEncodeAV1SessionParametersCreateInfoKHR session_params_info{
	        .pStdSequenceHeader = &self->seq_header,
	        .pStdDecoderModelInfo = nullptr,
	        .stdOperatingPointCount = 0,
	        .pStdOperatingPoints = nullptr,
	};

	self->quality_level_info = vk::VideoEncodeQualityLevelInfoKHR{
	        .qualityLevel = 0, // Use default quality level
	};
	session_params_info.pNext = &self->quality_level_info;

	// Let the session be sized for whatever the encoder supports, the level that matters to the
	// decoder is the one written to the operating point.
	vk::VideoEncodeAV1SessionCreateInfoKHR session_create_info{
	        .useMaxLevel = false,
	        .maxLevel = encode_av1_caps.maxLevel,
	};

	// VkVideoEncodeAV1RateControlInfoKHR belongs to the same pNext chain as the generic
	// rate control info, in both vkCmdBeginVideoCodingKHR and vkCmdControlVideoCodingKHR.
	if (self->rate_control)
	{
		self->rate_control_enabled = true;
		self->rate_control->pNext = &self->rate_control_av1;
	}

	// VkVideoEncodeAV1GopRemainingFrameInfoKHR only extends VkVideoBeginCodingInfoKHR, so it
	// is added by begin_coding_next() rather than chained onto the rate control info, which is
	// also used for vkCmdControlVideoCodingKHR.
	// VUID-vkCmdBeginVideoCodingKHR-pBeginInfo-10282
	if (self->rate_control_enabled and (encode_av1_caps.requiresGopRemainingFrames or encode_av1_caps.prefersGopRemainingFrames))
	{
		self->gop_info = vk::VideoEncodeAV1GopRemainingFrameInfoKHR{
		        .useGopRemainingFrames = true,
		        .gopRemainingIntra = 0,
		        .gopRemainingPredictive = std::numeric_limits<uint32_t>::max(),
		        .gopRemainingBipredictive = 0,
		};
	}

	if (encode_av1_caps.maxOperatingPoints > 0)
	{
		session_params_info.stdOperatingPointCount = 1;
		session_params_info.pStdOperatingPoints = &self->operating_point;
	}

#ifdef VK_KHR_video_encode_intra_refresh
	vk::VideoEncodeIntraRefreshCapabilitiesKHR intra_caps{};
	if (std::get<vk::PhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR>(vk.feat).videoEncodeIntraRefresh)
		intra_caps = std::get<vk::VideoEncodeIntraRefreshCapabilitiesKHR>(
		        vk.physical_device.getVideoCapabilitiesKHR<vk::VideoCapabilitiesKHR, vk::VideoEncodeCapabilitiesKHR, vk::VideoEncodeAV1CapabilitiesKHR, vk::VideoEncodeIntraRefreshCapabilitiesKHR>(video_profile_info.get()));
#endif
	self->init(video_caps,
#ifdef VK_KHR_video_encode_intra_refresh
	           intra_caps,
#endif
	           video_profile_info.get(),
	           &session_create_info,
	           &session_params_info);
	return self;
}

std::vector<uint8_t> wivrn::video_encoder_vulkan_av1::get_idr_data()
{
	auto data = get_encoded_parameters(nullptr);
	if (data.empty())
		throw std::runtime_error("Vulkan AV1 encoder returned no sequence header");
	return data;
}

void wivrn::video_encoder_vulkan_av1::rate_control_updated()
{
	if (not explicit_operating_point)
		return;
	av1_operating_point current{StdVideoAV1Level(operating_point.seq_level_idx), operating_point.seq_tier};
	auto selected = choose_av1_operating_point(
	        aligned_extent.width, aligned_extent.height, double(rate_control_layer.frameRateNumerator) / rate_control_layer.frameRateDenominator, rate_control_layer.maxBitrate, max_level, current);
	if (selected == current)
		return;

	operating_point.seq_level_idx = selected.level;
	operating_point.seq_tier = selected.tier;
	vk::VideoEncodeAV1SessionParametersCreateInfoKHR params{
	        .pNext = &quality_level_info,
	        .pStdSequenceHeader = &seq_header,
	        .stdOperatingPointCount = 1,
	        .pStdOperatingPoints = &operating_point,
	};
	replace_session_parameters(&params);
	U_LOG_I("AV1: updated sequence level to %u.%u, tier %u", 2 + (selected.level >> 2), selected.level & 3, selected.tier);
}

void * wivrn::video_encoder_vulkan_av1::encode_info_next(uint32_t frame_num, size_t slot, std::optional<int32_t> ref_slot)
{
	const bool has_ref = ref_slot.has_value();
	const bool is_keyframe = not has_ref;

	// Initialize reference name slot indices to -1 (no reference)
	std::ranges::fill(reference_name_slot_indices, -1);

	if (has_ref)
		reference_name_slot_indices[ref_name_index] = *ref_slot;

	std_picture_info = {};
	std_picture_info.flags.error_resilient_mode = is_keyframe ? 1u : 0u;
	std_picture_info.flags.disable_cdf_update = 0;
	std_picture_info.flags.use_superres = 0;
	// The coded frame is always aligned_extent, which is what the sequence header declares as
	// max_frame_{width,height}, so the frame size is never overridden. Only the render size
	// differs, when the requested extent needed padding to satisfy the encoder granularity.
	// VUID-vkCmdEncodeVideoKHR-flags-10322
	std_picture_info.flags.frame_size_override_flag = 0;
	std_picture_info.flags.render_and_frame_size_different =
	        (aligned_extent.width != extent.width or aligned_extent.height != extent.height) ? 1u : 0u;
	std_picture_info.flags.allow_screen_content_tools = 0;
	std_picture_info.flags.is_filter_switchable = 1;
	// AV1 spec 5.9.2: both are inferred for intra frames, match what the decoder will derive
	std_picture_info.flags.force_integer_mv = is_keyframe ? 1u : 0u;
	std_picture_info.flags.buffer_removal_time_present_flag = 0;
	std_picture_info.flags.allow_intrabc = 0;
	std_picture_info.flags.frame_refs_short_signaling = 0;
	std_picture_info.flags.allow_high_precision_mv = is_keyframe ? 0u : 1u;
	std_picture_info.flags.is_motion_mode_switchable = 0;
	std_picture_info.flags.use_ref_frame_mvs = 0;
	std_picture_info.flags.disable_frame_end_update_cdf = 0;
	std_picture_info.flags.allow_warped_motion = 0;
	std_picture_info.flags.reduced_tx_set = 0;
	std_picture_info.flags.skip_mode_present = 0;
	std_picture_info.flags.delta_q_present = 0;
	std_picture_info.flags.delta_lf_present = 0;
	std_picture_info.flags.delta_lf_multi = 0;
	std_picture_info.flags.segmentation_enabled = 0;
	std_picture_info.flags.segmentation_update_map = 0;
	std_picture_info.flags.segmentation_temporal_update = 0;
	std_picture_info.flags.segmentation_update_data = 0;
	std_picture_info.flags.UsesLr = 0;
	std_picture_info.flags.usesChromaLr = 0;
	std_picture_info.flags.show_frame = 1;
	std_picture_info.flags.showable_frame = std_picture_info.flags.show_frame ? (is_keyframe ? 0u : 1u) : 1u;
	std_picture_info.frame_type = is_keyframe ? STD_VIDEO_AV1_FRAME_TYPE_KEY : STD_VIDEO_AV1_FRAME_TYPE_INTER;
	// Both are only coded when the decoder model / frame ids are signalled, which the sequence
	// header disables, and the decoder then infers 0 for them (AV1 spec 5.9.2).
	std_picture_info.frame_presentation_time = 0;
	std_picture_info.current_frame_id = 0;
	std_picture_info.order_hint = static_cast<uint8_t>(frame_num & ((1u << order_hint_bits) - 1));
	// primary_ref_frame indexes ref_frame_idx[], it selects the reference to load the CDFs and
	// the loop filter / segmentation state from. Only usable if the encoder supports values
	// other than PRIMARY_REF_NONE.
	if (has_ref and (std_flags & vk::VideoEncodeAV1StdFlagBitsKHR::ePrimaryRefFrame))
		std_picture_info.primary_ref_frame = uint8_t(ref_name_index);
	else
		std_picture_info.primary_ref_frame = STD_VIDEO_AV1_PRIMARY_REF_NONE;
	std_picture_info.refresh_frame_flags = static_cast<uint8_t>(is_keyframe ? 0xFF : (1u << slot));
	std_picture_info.coded_denom = 0;
	std_picture_info.render_width_minus_1 = static_cast<uint16_t>(extent.width - 1);
	std_picture_info.render_height_minus_1 = static_cast<uint16_t>(extent.height - 1);
	std_picture_info.interpolation_filter = STD_VIDEO_AV1_INTERPOLATION_FILTER_SWITCHABLE;
	std_picture_info.TxMode = STD_VIDEO_AV1_TX_MODE_SELECT;
	std_picture_info.delta_q_res = 0;
	std_picture_info.delta_lf_res = 0;
	std::fill(std_picture_info.ref_order_hint, std_picture_info.ref_order_hint + STD_VIDEO_AV1_NUM_REF_FRAMES, 0);
	// Even unused reference names need valid AV1 syntax. Map all of them to
	// the acknowledged frame; Vulkan still enables only ref_name_index for prediction.
	std::fill(std_picture_info.ref_frame_idx, std_picture_info.ref_frame_idx + STD_VIDEO_AV1_REFS_PER_FRAME, static_cast<int8_t>(ref_slot.value_or(0)));
	std::fill(std_picture_info.delta_frame_id_minus_1, std_picture_info.delta_frame_id_minus_1 + STD_VIDEO_AV1_REFS_PER_FRAME, 0);
	// Let the implementation select a legal tile layout for its capabilities
	// and the AV1 maximum tile width/area, including streams wider than 4096.
	std_picture_info.pTileInfo = nullptr;
	std_picture_info.pQuantization = &quantization;
	std_picture_info.pSegmentation = (std_picture_info.flags.segmentation_enabled) ? &segmentation : nullptr;
	std_picture_info.pLoopFilter = &loop_filter;
	std_picture_info.pCDEF = &cdef;
	std_picture_info.pLoopRestoration = &loop_restoration;
	std_picture_info.pGlobalMotion = &global_motion;
	std_picture_info.pExtensionHeader = nullptr;
	std_picture_info.pBufferRemovalTimes = nullptr;

	// ref_frame_idx[] maps a reference name to a DPB slot, and must be filled in for the same
	// reference name that referenceNameSlotIndices uses.
	if (has_ref)
		std_picture_info.ref_frame_idx[ref_name_index] = static_cast<int8_t>(*ref_slot);

	const size_t dpb_ref_count = std::min<size_t>(dpb_std_info.size(), STD_VIDEO_AV1_NUM_REF_FRAMES);
	for (size_t i = 0; i < dpb_ref_count; ++i)
		std_picture_info.ref_order_hint[i] = dpb_std_info[i].OrderHint;

	picture_info = vk::VideoEncodeAV1PictureInfoKHR{};
	// WiVRn always predicts from the primary reference, never uses it for CDFs only.
	// VUID-VkVideoEncodeAV1PictureInfoKHR-flags-10289
	picture_info.primaryReferenceCdfOnly = VK_FALSE;
	// Key frames are the only intra frames we produce, everything else predicts from the single
	// acknowledged reference frame. Encoders without single reference support are rejected in
	// configure_from_caps().
	// VUID-vkCmdEncodeVideoKHR-pStdPictureInfo-10327
	picture_info.predictionMode = is_keyframe ? vk::VideoEncodeAV1PredictionModeKHR::eIntraOnly
	                                          : vk::VideoEncodeAV1PredictionModeKHR::eSingleReference;

	// Set rate control group based on frame type
	// Note: WiVRn currently doesn't support B-frames, so only INTRA and PREDICTIVE groups
	picture_info.rateControlGroup = is_keyframe ? vk::VideoEncodeAV1RateControlGroupKHR::eIntra
	                                            : vk::VideoEncodeAV1RateControlGroupKHR::ePredictive;
	// The base encoder uses CBR, VBR, or DEFAULT, never DISABLED. All three
	// require zero here, including drivers without explicit rate control.
	// VUID-vkCmdEncodeVideoKHR-constantQIndex-10320
	picture_info.constantQIndex = 0;
	picture_info.pStdPictureInfo = &std_picture_info;
	picture_info.referenceNameSlotIndices = reference_name_slot_indices;
	picture_info.generateObuExtensionHeader = VK_FALSE;

	auto & i = dpb_std_info[slot];
	i = {};
	i.flags.disable_frame_end_update_cdf = 0;
	i.flags.segmentation_enabled = 0;
	// Frame ids are not signalled, see current_frame_id above
	i.RefFrameId = 0;
	i.frame_type = std_picture_info.frame_type;
	i.OrderHint = std_picture_info.order_hint;
	i.pExtensionHeader = nullptr;

	return &picture_info;
}

const void * wivrn::video_encoder_vulkan_av1::begin_coding_next(const void * next, bool keyframe)
{
	if (not gop_info)
		return next;

	gop_info->pNext = next;
	gop_info->gopRemainingIntra = keyframe ? 1 : 0;
	gop_info->gopRemainingPredictive = std::numeric_limits<uint32_t>::max() - gop_info->gopRemainingIntra;
	return &*gop_info;
}

vk::ExtensionProperties wivrn::video_encoder_vulkan_av1::std_header_version()
{
	vk::ExtensionProperties std_header_version{
	        .specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION,
	};
	strcpy(std_header_version.extensionName,
	       VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME);
	return std_header_version;
}
