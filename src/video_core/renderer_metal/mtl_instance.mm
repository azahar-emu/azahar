// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "video_core/custom_textures/custom_format.h"
#include "video_core/renderer_metal/mtl_instance.h"

namespace Metal {

Instance::Instance() {
    device = MTLCreateSystemDefaultDevice();
    ASSERT_MSG(device, "Metal is not supported on this device");

    queue = [device newCommandQueueWithMaxCommandBufferCount:64];
    queue.label = @"Azahar Metal queue";

    stencil_export =
        [device supportsFamily:MTLGPUFamilyApple5] || [device supportsFamily:MTLGPUFamilyMac2];
    astc_support = [device supportsFamily:MTLGPUFamilyApple2];
    if (stencil_export) {
        for (u32 samples = 8; samples >= 2; samples /= 2) {
            if ([device supportsTextureSampleCount:samples]) {
                max_msaa_samples = samples;
                break;
            }
        }
    }
#if TARGET_OS_OSX
    bc_support = true;
#else
    bc_support = false;
    if (@available(iOS 16.4, *)) {
        bc_support = device.supportsBCTextureCompression;
    }
#endif

    // Describe the device to the shader generators.
    profile.has_separable_shaders = true;
    profile.has_clip_planes = true;
    profile.has_geometry_shader = false;
    profile.has_custom_border_color = false;
    profile.has_fragment_shader_interlock = true; // Raster order groups
    profile.has_fragment_shader_barycentric = false;
    profile.has_blend_minmax_factor = false;
    profile.has_minus_one_to_one_range = false;
    profile.has_logic_op = false;
    profile.enable_accurate_mul = Settings::values.shaders_accurate_mul.GetValue();
    profile.is_metal = true;

    CreateFormatTable();

    LOG_INFO(Render, "Metal device: {}", GetDeviceName());
}

Instance::~Instance() = default;

std::string Instance::GetDeviceName() const {
    return std::string{[device.name UTF8String]};
}

const FormatTraits& Instance::GetTraits(VideoCore::PixelFormat pixel_format) const {
    if (pixel_format == VideoCore::PixelFormat::Invalid) [[unlikely]] {
        return null_traits;
    }
    return format_table[static_cast<u32>(pixel_format)];
}

const FormatTraits& Instance::GetTraits(VideoCore::CustomPixelFormat pixel_format) const {
    const auto index = static_cast<u32>(pixel_format);
    if (index >= custom_format_table.size()) [[unlikely]] {
        return null_traits;
    }
    return custom_format_table[index];
}

void Instance::CreateFormatTable() {
    using VideoCore::PixelFormat;

    // Color formats are always converted to RGBA8 by the texture codec. Metal on Apple GPUs does
    // support some packed 16-bit formats but with different component orders, and using a single
    // format makes reinterpretation between color formats a plain copy.
    const FormatTraits rgba8_converted = {
        .native = MTLPixelFormatRGBA8Unorm,
        .transfer_support = true,
        .attachment_support = true,
        .needs_conversion = true,
        .bytes_per_pixel = 4,
    };
    for (const auto format : {PixelFormat::RGBA8, PixelFormat::RGB8, PixelFormat::RGB5A1,
                              PixelFormat::RGB565, PixelFormat::RGBA4}) {
        format_table[static_cast<u32>(format)] = rgba8_converted;
    }

    // Texture-only formats are implicitly decoded to RGBA8.
    FormatTraits rgba8_texture = rgba8_converted;
    rgba8_texture.needs_conversion = false;
    for (const auto format :
         {PixelFormat::IA8, PixelFormat::RG8, PixelFormat::I8, PixelFormat::A8, PixelFormat::IA4,
          PixelFormat::I4, PixelFormat::A4, PixelFormat::ETC1, PixelFormat::ETC1A4}) {
        format_table[static_cast<u32>(format)] = rgba8_texture;
    }

    format_table[static_cast<u32>(PixelFormat::D16)] = {
        .native = MTLPixelFormatDepth16Unorm,
        .transfer_support = true,
        .attachment_support = true,
        .needs_conversion = false,
        .is_depth = true,
        .bytes_per_pixel = 2,
    };
    // iOS GPUs have no 24-bit depth formats; use 32-bit float depth. The codec converts D24 to
    // normalized floats when conversion is requested.
    format_table[static_cast<u32>(PixelFormat::D24)] = {
        .native = MTLPixelFormatDepth32Float,
        .transfer_support = true,
        .attachment_support = true,
        .needs_conversion = true,
        .is_depth = true,
        .bytes_per_pixel = 4,
    };
    // D24S8 is de-interleaved by the runtime into a float depth plane followed by a stencil
    // plane, so it needs 5 bytes per pixel of staging memory.
    format_table[static_cast<u32>(PixelFormat::D24S8)] = {
        .native = MTLPixelFormatDepth32Float_Stencil8,
        .transfer_support = true,
        .attachment_support = true,
        .needs_conversion = false,
        .is_depth = true,
        .has_stencil = true,
        .bytes_per_pixel = 5,
    };

    // Custom texture formats.
    using VideoCore::CustomPixelFormat;
    const auto set_custom = [&](CustomPixelFormat format, MTLPixelFormat native, bool supported) {
        custom_format_table[static_cast<u32>(format)] = {
            .native = native,
            .transfer_support = supported,
            .attachment_support = false,
            .bytes_per_pixel = 4,
        };
    };
    set_custom(CustomPixelFormat::RGBA8, MTLPixelFormatRGBA8Unorm, true);
    if (@available(iOS 16.4, macOS 11.0, *)) {
        set_custom(CustomPixelFormat::BC1, MTLPixelFormatBC1_RGBA, bc_support);
        set_custom(CustomPixelFormat::BC3, MTLPixelFormatBC3_RGBA, bc_support);
        set_custom(CustomPixelFormat::BC5, MTLPixelFormatBC5_RGUnorm, bc_support);
        set_custom(CustomPixelFormat::BC7, MTLPixelFormatBC7_RGBAUnorm, bc_support);
    }
    set_custom(CustomPixelFormat::ASTC4, MTLPixelFormatASTC_4x4_LDR, astc_support);
    set_custom(CustomPixelFormat::ASTC6, MTLPixelFormatASTC_6x6_LDR, astc_support);
    set_custom(CustomPixelFormat::ASTC8, MTLPixelFormatASTC_8x6_LDR, astc_support);
}

} // namespace Metal
