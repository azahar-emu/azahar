// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <string>
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_metal/mtl_common.h"
#include "video_core/shader/generator/profile.h"

namespace VideoCore {
enum class CustomPixelFormat : u32;
}

namespace Metal {

struct FormatTraits {
    MTLPixelFormat native = MTLPixelFormatInvalid;
    bool transfer_support = false;   ///< The format can be uploaded/downloaded.
    bool attachment_support = false; ///< The format can be used as a render target.
    bool needs_conversion = false;   ///< Texels are converted by the texture codec.
    bool is_depth = false;
    bool has_stencil = false;
    u32 bytes_per_pixel = 0; ///< Bytes per pixel of the staging data.
};

/// Owns the Metal device and command queue and describes the device capabilities.
class Instance {
public:
    explicit Instance();
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    id<MTLDevice> GetDevice() const {
        return device;
    }

    id<MTLCommandQueue> GetCommandQueue() const {
        return queue;
    }

    const FormatTraits& GetTraits(VideoCore::PixelFormat pixel_format) const;
    const FormatTraits& GetTraits(VideoCore::CustomPixelFormat pixel_format) const;

    /// Returns the shader generator profile describing this device.
    const Pica::Shader::Profile& GetProfile() const {
        return profile;
    }

    std::string GetDeviceName() const;

    /// True when fragment shaders can export a stencil value ([[stencil]]).
    bool IsStencilExportSupported() const {
        return stencil_export;
    }

    /// True when BCn compressed textures are supported (Apple silicon iPads/Macs).
    bool IsBCSupported() const {
        return bc_support;
    }

    /// True when ASTC compressed textures are supported (all Apple GPUs).
    bool IsASTCSupported() const {
        return astc_support;
    }

    /**
     * The MSAA sample count to render with for the requested one: the largest the device supports
     * that is not above it, or 1. MSAA needs stencil export (the multisampled copy of a depth
     * buffer is refreshed from the depth buffer with a shader that writes the stencil too).
     */
    u32 MsaaSamples(u32 requested) const {
        for (u32 samples = 8; samples >= 2; samples /= 2) {
            if (samples <= requested && samples <= max_msaa_samples) {
                return samples;
            }
        }
        return 1;
    }

private:
    void CreateFormatTable();

    u32 max_msaa_samples{1};

private:
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    Pica::Shader::Profile profile{};
    std::array<FormatTraits, VideoCore::PIXEL_FORMAT_COUNT> format_table{};
    std::array<FormatTraits, 8> custom_format_table{};
    FormatTraits null_traits{};
    bool stencil_export{};
    bool bc_support{};
    bool astc_support{};
};

} // namespace Metal
