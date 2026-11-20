// Copyright 2022-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// Copyright 2022 The Pixellizer Group
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <array>
#include <span>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/archive_backend.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/plgldr/plgldr.h"

namespace Loader {
enum class ResultStatus;
}

namespace FileUtil {
class IOFile;
}

namespace Kernel {
class ClientSession;
}

namespace FileSys {

class FileBackend;

class Plugin3GXLoader {
public:
    Loader::ResultStatus Load(Service::PLGLDR::PLG_LDR::PluginLoaderContext& plg_context,
                              Kernel::Process& process, Kernel::KernelSystem& kernel,
                              Service::PLGLDR::PLG_LDR& plg_ldr);

    struct PluginHeader {
        u32_le magic;
        u32_le version;
        u32_le heap_VA;
        u32_le heap_size;
        u32_le exe_size; // Include sizeof(PluginHeader) + .text + .rodata + .data + .bss (0x1000
                         // aligned too)
        u32_le is_default_plugin;
        u32_le plgldr_event;  ///< Used for synchronization, unused in citra
        u32_le plgldr_reply;  ///< Used for synchronization, unused in citra
        u8 notify_home_event; ///< Unused in Azahar
        u8 padding[7];
        u64_le wait_for_reply_timeout;   ///< Unused in Azahar
        u32_le builtin_data_file_handle; ///< FS file handle of the built-in data, or 0
        u32_le reserved[19];
        u32_le config[32];
    };

    static_assert(sizeof(PluginHeader) == 0x100, "Invalid plugin header size");

    static constexpr const char* _3GX_magic = "3GX$0003";
    static constexpr const char* _3GX_magic_v2 = "3GX$0002";
    static constexpr u32 _3GX_exe_load_addr = 0x07000000;
    static constexpr u32 _3GX_heap_load_addr = 0x06000000;
    static constexpr u32 _3GX_fb_size = 0xA9000;

private:
    Loader::ResultStatus Map(Service::PLGLDR::PLG_LDR::PluginLoaderContext& plg_context,
                             Kernel::Process& process, Kernel::KernelSystem& kernel,
                             Service::PLGLDR::PLG_LDR& plg_ldr);

    static constexpr std::size_t bootloader_memory_size = 0x1000;
    static void MapBootloader(Kernel::Process& process, Kernel::KernelSystem& kernel,
                              u32 memory_offset, std::span<const u32> exe_load_func,
                              const u32_le* exe_load_args, u32 checksum_size, u32 exe_checksum,
                              bool no_flash);

    struct _3gx_Infos {
        enum class Compatibility { CONSOLE = 0, CITRA = 1, CONSOLE_CITRA = 2 };
        u32_le author_len;
        u32_le author_msg_offset;
        u32_le title_len;
        u32_le title_msg_offset;
        u32_le summary_len;
        u32_le summary_msg_offset;
        u32_le description_len;
        u32_le description_msg_offset;
        union {
            u32_le raw;
            BitField<0, 1, u32_le> embedded_exe_func;
            BitField<1, 1, u32_le> embedded_swap_func;
            BitField<2, 2, u32_le> memory_region_size;
            BitField<4, 2, u32_le> compatibility;
            BitField<6, 1, u32_le> events_self_managed;
            BitField<7, 1, u32_le> swap_not_needed;
            BitField<8, 1, u32_le> use_private_memory;
            BitField<9, 1, u32_le> allow_homebrew_load;
        } flags;
        u32_le exe_load_checksum;
        u32_le builtin_load_exe_args[4];
        u32_le builtin_swap_load_args[4];
    };

    struct _3gx_Targets {
        u32_le count;
        u32_le title_offsets;
    };

    struct _3gx_Symtable {
        u32_le nb_symbols;
        u32_le symbols_offset;
        u32_le name_table_offset;
    };

    struct _3gx_Executable {
        u32_le code_offset;
        u32_le rodata_offset;
        u32_le data_offset;
        u32_le code_size;
        u32_le rodata_size;
        u32_le data_size;
        u32_le bss_size;
        u32_le exe_load_func_offset;  // NOP terminated
        u32_le swap_save_func_offset; // NOP terminated
        u32_le swap_load_func_offset; // NOP terminated
    };

    struct _3gx_BuiltInData {
        u32_le offset;
        u32_le size;
    };

    struct _3gx_Header {
        std::array<char, 8> magic;
        u32_le version;
        u16_le header_size; ///< Version 3 and later only
        u16_le reserved;
        _3gx_Infos infos;
        _3gx_Executable executable;
        _3gx_Targets targets;
        _3gx_Symtable symtable;
        _3gx_BuiltInData builtin_data; ///< Version 3 and later only
    };

    // Version 2 headers have no header size, and end before the built-in data
    static constexpr std::size_t _3gx_v2_header_size = 0x94;
    static_assert(sizeof(_3gx_Header) == _3gx_v2_header_size + sizeof(_3gx_BuiltInData),
                  "Invalid 3GX header size");

    _3gx_Header header;

    std::string author;
    std::string title;
    std::string summary;
    std::string description;

    std::vector<u32> compatible_TID;
    std::vector<u8> text_section;
    std::vector<u8> data_section;
    std::vector<u8> rodata_section;

    std::vector<u32> exe_load_func;
    u32_le exe_load_args[4];

    std::shared_ptr<Kernel::ClientSession> builtin_data_session;
};
} // namespace FileSys
