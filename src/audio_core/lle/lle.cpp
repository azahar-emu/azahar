// Copyright 2018-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <teakra/teakra.h>
#include "audio_core/lle/lle.h"
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/swap.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/dsp/dsp_dsp.h"

namespace AudioCore {

enum class SegmentType : u8 {
    ProgramA = 0,
    ProgramB = 1,
    Data = 2,
};

class Dsp1 {
public:
    explicit Dsp1(std::span<const u8> raw);

    struct Header {
        std::array<u8, 0x100> signature;
        std::array<u8, 0x4> magic;
        u32_le binary_size;
        u16_le memory_layout;
        INSERT_PADDING_BYTES(3);
        SegmentType special_segment_type;
        u8 num_segments;
        union {
            BitField<0, 1, u8> recv_data_on_start;
            BitField<1, 1, u8> load_special_segment;
        };
        u32_le special_segment_address;
        u32_le special_segment_size;
        u64_le zero;
        struct Segment {
            u32_le offset;
            u32_le address;
            u32_le size;
            INSERT_PADDING_BYTES(3);
            SegmentType memory_type;
            std::array<u8, 0x20> sha256;
        };
        std::array<Segment, 10> segments;
    };
    static_assert(sizeof(Header) == 0x300);

    struct Segment {
        std::vector<u8> data;
        SegmentType memory_type;
        u32 target;
    };

    std::vector<Segment> segments;
    bool recv_data_on_start;
};

Dsp1::Dsp1(std::span<const u8> raw) {
    Header header;
    std::memcpy(&header, raw.data(), sizeof(header));
    recv_data_on_start = header.recv_data_on_start != 0;
    segments.reserve(header.num_segments);
    for (u32 i = 0; i < header.num_segments; ++i) {
        Segment segment;
        segment.data =
            std::vector<u8>(raw.begin() + header.segments[i].offset,
                            raw.begin() + header.segments[i].offset + header.segments[i].size);
        segment.memory_type = header.segments[i].memory_type;
        segment.target = header.segments[i].address;
        segments.push_back(std::move(segment));
    }
}

struct PipeStatus {
    u16_le waddress;
    u16_le bsize;
    u16_le read_bptr;
    u16_le write_bptr;
    u8 slot_index;
    u8 flags;

    static constexpr u16 WrapBit = 0x8000;
    static constexpr u16 PtrMask = 0x7FFF;

    bool IsFull() const {
        return (read_bptr ^ write_bptr) == WrapBit;
    }

    bool IsEmpty() const {
        return (read_bptr ^ write_bptr) == 0;
    }

    /*
     * IsWrapped: Are read and write pointers not in the same pass.
     * false:  ----[xxxx]----
     * true:   xxxx]----[xxxx (data is wrapping around the end)
     */
    bool IsWrapped() const {
        return (read_bptr ^ write_bptr) >= WrapBit;
    }
};

static_assert(sizeof(PipeStatus) == 10);

enum class PipeDirection : u8 {
    DSPtoCPU = 0,
    CPUtoDSP = 1,
};

static u8 PipeIndexToSlotIndex(u8 pipe_index, PipeDirection direction) {
    return (pipe_index << 1) + static_cast<u8>(direction);
}

struct DspLle::Impl final {
    Impl(Core::Timing& timing, Memory::MemorySystem& memory, bool multithread, bool use_jit)
        : teakra(Teakra::UserConfig{.dsp_memory = memory.GetDspMemory(0), .use_jit = use_jit}),
          core_timing(timing), multithread(multithread) {
        teakra_slice_event = core_timing.RegisterEvent(
            "DSP slice", [this](u64, int late) { TeakraSliceEvent(static_cast<u64>(late)); });
    }

    ~Impl() {
        StopTeakraThread();
    }

    Teakra::Teakra teakra;
    u16 pipe_base_waddr = 0;

    bool semaphore_signaled = false;
    bool data_signaled = false;

    Core::Timing& core_timing;
    Core::TimingEventType* teakra_slice_event;
    std::atomic<bool> loaded = false;

    const bool multithread;
    std::thread teakra_thread;
    Common::Barrier teakra_slice_barrier{2};
    std::atomic<bool> stop_signal = false;
    std::size_t stop_generation;

    static constexpr u32 DspDataOffset = 0x40000;
    static constexpr u32 TeakraSlice = 16384;

    // Simulates the delay between the guest application writing commands and the DSP hardware
    // receiving them. Without this delay, the events would arrive too quickly which causes issues
    // inside the DSP firmware. Measured on a real console, and takes about 2650 cycles on average.
    static constexpr u64 HostWriteLatency = 2650;

    struct HostWrite {
        u64 cycles_remaining;
        std::function<void()> apply;
    };
    std::mutex host_mutex;
    std::deque<HostWrite> host_writes;
    std::array<bool, 3> index_data_pending{};

    void QueueHostWrite(std::function<void()> apply) {
        std::lock_guard lock(host_mutex);
        host_writes.push_back({HostWriteLatency, std::move(apply)});
    }

    void ClearHostWrites() {
        std::lock_guard lock(host_mutex);
        host_writes.clear();
        index_data_pending = {};
    }

    void SetSemaphore(u16 value) {
        QueueHostWrite([this, value] { teakra.SetSemaphore(value); });
    }

    void SendData(u8 index, u16 value) {
        {
            std::lock_guard lock(host_mutex);
            index_data_pending[index] = true;
        }
        QueueHostWrite([this, index, value] {
            {
                std::lock_guard lock(host_mutex);
                index_data_pending[index] = false;
            }
            teakra.SendData(index, value);
        });
    }

    bool SendDataIsEmpty(u8 index) {
        std::lock_guard lock(host_mutex);
        return !index_data_pending[index] && teakra.SendDataIsEmpty(index);
    }

    // Runs the DSP, applying queued host writes when appropriate.
    void RunTeakra(u64 cycles) {
        while (cycles > 0) {
            std::size_t pending_host_writes;
            u64 step = cycles;
            {
                std::lock_guard lock(host_mutex);
                pending_host_writes = host_writes.size();
                for (const auto& write : host_writes) {
                    step = std::min(step, write.cycles_remaining);
                }
            }
            teakra.Run(static_cast<unsigned>(step));
            cycles -= step;

            std::vector<std::function<void()>> ready;
            {
                std::lock_guard lock(host_mutex);
                for (std::size_t i = 0; i < pending_host_writes; ++i) {
                    host_writes[i].cycles_remaining -= step;
                }
                while (!host_writes.empty() && host_writes.front().cycles_remaining == 0) {
                    ready.push_back(std::move(host_writes.front().apply));
                    host_writes.pop_front();
                }
            }
            for (auto& apply : ready) {
                // Run the apply callback
                apply();
            }
        }
    }

    void TeakraThread() {
        while (true) {
            RunTeakra(TeakraSlice);
            teakra_slice_barrier.Sync();
            if (stop_signal) {
                if (stop_generation == teakra_slice_barrier.Generation())
                    break;
            }
        }
        stop_signal = false;
    }

    void StopTeakraThread() {
        if (teakra_thread.joinable()) {
            stop_generation = teakra_slice_barrier.Generation() + 1;
            stop_signal = true;
            teakra_slice_barrier.Sync();
            teakra_thread.join();
        }
    }

    void RunTeakraSlice() {
        if (multithread) {
            teakra_slice_barrier.Sync();
        } else {
            RunTeakra(TeakraSlice);
        }
    }

    void TeakraSliceEvent(u64 late) {
        RunTeakraSlice();
        u64 next = TeakraSlice * 2; // DSP runs at clock rate half of the CPU rate
        if (next < late)
            next = 0;
        else
            next -= late;
        core_timing.ScheduleEvent(next, teakra_slice_event, 0);
    }

    u8* GetDspDataPointer(u32 baddr) {
        uint8_t* memory = teakra.GetDspMemory();
        return &memory[DspDataOffset + baddr];
    }

    const u8* GetDspDataPointer(u32 baddr) const {
        const uint8_t* memory = teakra.GetDspMemory();
        return &memory[DspDataOffset + baddr];
    }

    PipeStatus GetPipeStatus(u8 pipe_index, PipeDirection direction) const {
        u8 slot_index = PipeIndexToSlotIndex(pipe_index, direction);
        PipeStatus pipe_status;
        std::memcpy(&pipe_status,
                    GetDspDataPointer(pipe_base_waddr * 2 + slot_index * sizeof(PipeStatus)),
                    sizeof(PipeStatus));
        ASSERT(pipe_status.slot_index == slot_index);
        return pipe_status;
    }

    void UpdatePipeStatus(const PipeStatus& pipe_status) {
        u8 slot_index = pipe_status.slot_index;
        u8* status_address =
            GetDspDataPointer(pipe_base_waddr * 2 + slot_index * sizeof(PipeStatus));
        if (slot_index % 2 == 0) {
            std::memcpy(status_address + 4, &pipe_status.read_bptr, sizeof(u16));
        } else {
            std::memcpy(status_address + 6, &pipe_status.write_bptr, sizeof(u16));
        }
    }

    void WritePipe(u8 pipe_index, std::span<const u8> data) {
        PipeStatus pipe_status = GetPipeStatus(pipe_index, PipeDirection::CPUtoDSP);
        bool need_update = false;
        const u8* buffer_ptr = data.data();
        u16 bsize = static_cast<u16>(data.size());
        while (bsize != 0) {
            ASSERT_MSG(!pipe_status.IsFull(), "Pipe is Full");
            u16 write_bend;
            if (pipe_status.IsWrapped())
                write_bend = pipe_status.read_bptr & PipeStatus::PtrMask;
            else
                write_bend = pipe_status.bsize;
            u16 write_bbegin = pipe_status.write_bptr & PipeStatus::PtrMask;
            ASSERT_MSG(write_bend > write_bbegin,
                       "Pipe is in inconsistent state: end {:04X} <= begin {:04X}, size {:04X}",
                       write_bend, write_bbegin, pipe_status.bsize);
            u16 write_bsize = std::min<u16>(bsize, write_bend - write_bbegin);
            std::memcpy(GetDspDataPointer(pipe_status.waddress * 2 + write_bbegin), buffer_ptr,
                        write_bsize);
            buffer_ptr += write_bsize;
            pipe_status.write_bptr += write_bsize;
            bsize -= write_bsize;
            ASSERT_MSG((pipe_status.write_bptr & PipeStatus::PtrMask) <= pipe_status.bsize,
                       "Pipe is in inconsistent state: write > size");
            if ((pipe_status.write_bptr & PipeStatus::PtrMask) == pipe_status.bsize) {
                pipe_status.write_bptr &= PipeStatus::WrapBit;
                pipe_status.write_bptr ^= PipeStatus::WrapBit;
            }
            need_update = true;
        }
        if (need_update) {
            UpdatePipeStatus(pipe_status);
            while (!SendDataIsEmpty(2))
                RunTeakraSlice();
            SendData(2, pipe_status.slot_index);
        }
    }

    std::vector<u8> ReadPipe(u8 pipe_index, u16 bsize) {
        PipeStatus pipe_status = GetPipeStatus(pipe_index, PipeDirection::DSPtoCPU);
        bool need_update = false;
        std::vector<u8> data(bsize);
        u8* buffer_ptr = data.data();
        while (bsize != 0) {
            ASSERT_MSG(!pipe_status.IsEmpty(), "Pipe is empty");
            u16 read_bend;
            if (pipe_status.IsWrapped()) {
                read_bend = pipe_status.bsize;
            } else {
                read_bend = pipe_status.write_bptr & PipeStatus::PtrMask;
            }
            u16 read_bbegin = pipe_status.read_bptr & PipeStatus::PtrMask;
            ASSERT(read_bend > read_bbegin);
            u16 read_bsize = std::min<u16>(bsize, read_bend - read_bbegin);
            std::memcpy(buffer_ptr, GetDspDataPointer(pipe_status.waddress * 2 + read_bbegin),
                        read_bsize);
            buffer_ptr += read_bsize;
            pipe_status.read_bptr += read_bsize;
            bsize -= read_bsize;
            ASSERT_MSG((pipe_status.read_bptr & PipeStatus::PtrMask) <= pipe_status.bsize,
                       "Pipe is in inconsistent state: read > size");
            if ((pipe_status.read_bptr & PipeStatus::PtrMask) == pipe_status.bsize) {
                pipe_status.read_bptr &= PipeStatus::WrapBit;
                pipe_status.read_bptr ^= PipeStatus::WrapBit;
            }
            need_update = true;
        }
        if (need_update) {
            UpdatePipeStatus(pipe_status);
            while (!SendDataIsEmpty(2))
                RunTeakraSlice();
            SendData(2, pipe_status.slot_index);
        }
        return data;
    }
    u16 GetPipeReadableSize(u8 pipe_index) const {
        PipeStatus pipe_status = GetPipeStatus(pipe_index, PipeDirection::DSPtoCPU);
        u16 size = pipe_status.write_bptr - pipe_status.read_bptr;
        if (pipe_status.IsWrapped()) {
            size += pipe_status.bsize;
        }
        return size & PipeStatus::PtrMask;
    }

    void LoadComponent(std::span<const u8> buffer) {
        if (loaded) {
            LOG_ERROR(Audio_DSP, "Component already loaded!");
            return;
        }

        teakra.Reset();
        ClearHostWrites();

        Dsp1 dsp(buffer);
        auto dsp_memory = teakra.GetDspMemory();
        u8* program = dsp_memory;
        u8* data = dsp_memory + DspDataOffset;
        for (const auto& segment : dsp.segments) {
            if (segment.memory_type == SegmentType::ProgramA ||
                segment.memory_type == SegmentType::ProgramB) {
                std::memcpy(program + segment.target * 2, segment.data.data(), segment.data.size());
            } else if (segment.memory_type == SegmentType::Data) {
                std::memcpy(data + segment.target * 2, segment.data.data(), segment.data.size());
            }
        }

        // TODO: load special segment

        core_timing.ScheduleEvent(TeakraSlice, teakra_slice_event, 0);

        if (multithread) {
            teakra_thread = std::thread(&Impl::TeakraThread, this);
        }

        // Wait for initialization
        if (dsp.recv_data_on_start) {
            for (u8 i = 0; i < 3; ++i) {
                do {
                    while (!teakra.RecvDataIsReady(i))
                        RunTeakraSlice();
                } while (teakra.RecvData(i) != 1);
            }
        }

        // Get pipe base address
        while (!teakra.RecvDataIsReady(2))
            RunTeakraSlice();
        pipe_base_waddr = teakra.RecvData(2);

        loaded = true;
    }

    void UnloadComponent() {
        if (!loaded) {
            LOG_ERROR(Audio_DSP, "Component not loaded!");
            return;
        }

        loaded = false;

        // Send finalization signal via command/reply register 2
        constexpr u16 FinalizeSignal = 0x8000;
        while (!SendDataIsEmpty(2))
            RunTeakraSlice();

        SendData(2, FinalizeSignal);

        // Wait for completion
        while (!teakra.RecvDataIsReady(2))
            RunTeakraSlice();

        teakra.RecvData(2); // discard the value

        core_timing.UnscheduleEvent(teakra_slice_event, 0);
        StopTeakraThread();
    }
};

u16 DspLle::RecvData(u32 register_number) {
    while (!impl->teakra.RecvDataIsReady(register_number)) {
        impl->RunTeakraSlice();
    }
    return impl->teakra.RecvData(static_cast<u8>(register_number));
}

bool DspLle::RecvDataIsReady(u32 register_number) const {
    return impl->teakra.RecvDataIsReady(register_number);
}

void DspLle::SetSemaphore(u16 semaphore_value) {
    impl->SetSemaphore(semaphore_value);
}

std::vector<u8> DspLle::PipeRead(DspPipe pipe_number, std::size_t length) {
    return impl->ReadPipe(static_cast<u8>(pipe_number), static_cast<u16>(length));
}

std::size_t DspLle::GetPipeReadableSize(DspPipe pipe_number) const {
    return impl->GetPipeReadableSize(static_cast<u8>(pipe_number));
}

void DspLle::PipeWrite(DspPipe pipe_number, std::span<const u8> buffer) {
    impl->WritePipe(static_cast<u8>(pipe_number), buffer);
}

void DspLle::SetInterruptHandler(
    std::function<void(Service::DSP::InterruptType type, DspPipe pipe)> handler) {
    impl->teakra.SetRecvDataHandler(0, [this, handler]() {
        if (!impl->loaded) {
            return;
        }
        handler(Service::DSP::InterruptType::Zero, static_cast<DspPipe>(0));
    });
    impl->teakra.SetRecvDataHandler(1, [this, handler]() {
        if (!impl->loaded) {
            return;
        }
        handler(Service::DSP::InterruptType::One, static_cast<DspPipe>(0));
    });

    auto ProcessPipeEvent = [this, handler](bool event_from_data) {
        if (!impl->loaded)
            return;

        auto& teakra = impl->teakra;
        if (event_from_data) {
            impl->data_signaled = true;
        } else {
            if ((teakra.GetSemaphore() & 0x8000) == 0)
                return;
            impl->semaphore_signaled = true;
        }
        if (impl->semaphore_signaled && impl->data_signaled) {
            impl->semaphore_signaled = impl->data_signaled = false;
            u16 slot = teakra.RecvData(2);
            u16 side = slot % 2;
            u16 pipe = slot / 2;
            ASSERT(pipe < 16);
            if (side != static_cast<u16>(PipeDirection::DSPtoCPU))
                return;
            if (pipe == 0) {
                // pipe 0 is for debug. 3DS automatically drains this pipe and discards the data
                impl->ReadPipe(static_cast<u8>(pipe),
                               impl->GetPipeReadableSize(static_cast<u8>(pipe)));
            } else {
                handler(Service::DSP::InterruptType::Pipe, static_cast<DspPipe>(pipe));
            }
        }
    };

    impl->teakra.SetRecvDataHandler(2, [ProcessPipeEvent]() { ProcessPipeEvent(true); });
    impl->teakra.SetSemaphoreHandler([ProcessPipeEvent]() { ProcessPipeEvent(false); });
}

void DspLle::LoadComponent(std::span<const u8> buffer) {
    impl->LoadComponent(buffer);
}

void DspLle::UnloadComponent() {
    impl->UnloadComponent();
}

DspLle::DspLle(Core::System& system, bool multithread, bool use_jit)
    : DspLle(system, system.Memory(), system.CoreTiming(), multithread, use_jit) {}

DspLle::DspLle(Core::System& system, Memory::MemorySystem& memory, Core::Timing& timing,
               bool multithread, bool use_jit)
    : DspInterface(system), impl(std::make_unique<Impl>(timing, memory, multithread, use_jit)) {
    if (use_jit && !IsJitSupported()) {
        LOG_WARNING(Audio_DSP, "The DSP JIT is not supported on this host, using the interpreter");
    }
    Teakra::AHBMCallback ahbm;
    ahbm.read8 = [&memory](u32 address) -> u8 {
        return *memory.GetFCRAMPointer(address - Memory::FCRAM_PADDR);
    };
    ahbm.write8 = [&memory](u32 address, u8 value) {
        *memory.GetFCRAMPointer(address - Memory::FCRAM_PADDR) = value;
    };
    ahbm.read16 = [&memory](u32 address) -> u16 {
        u16 value;
        std::memcpy(&value, memory.GetFCRAMPointer(address - Memory::FCRAM_PADDR), sizeof(u16));
        return value;
    };
    ahbm.write16 = [&memory](u32 address, u16 value) {
        std::memcpy(memory.GetFCRAMPointer(address - Memory::FCRAM_PADDR), &value, sizeof(u16));
    };
    ahbm.read32 = [&memory](u32 address) -> u32 {
        u32 value;
        std::memcpy(&value, memory.GetFCRAMPointer(address - Memory::FCRAM_PADDR), sizeof(u32));
        return value;
    };
    ahbm.write32 = [&memory](u32 address, u32 value) {
        std::memcpy(memory.GetFCRAMPointer(address - Memory::FCRAM_PADDR), &value, sizeof(u32));
    };
    impl->teakra.SetAHBMCallback(ahbm);
    impl->teakra.SetAudioCallback(
        [this](std::array<s16, 2> sample) { OutputSample(std::move(sample)); });
}
DspLle::~DspLle() = default;

bool DspLle::IsJitSupported() {
    return Teakra::Teakra::IsJitSupported();
}

} // namespace AudioCore
