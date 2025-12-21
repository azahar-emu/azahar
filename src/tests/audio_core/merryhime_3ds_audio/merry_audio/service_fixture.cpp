// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "audio_core/hle/hle.h"
#include "audio_core/lle/lle.h"
#include "common/settings.h"
#include "service_fixture.h"

// SVC
Result ServiceFixture::svcWaitSynchronization(Handle handle, s64 nanoseconds) {
    ASSERT(handle == PIPE2_IRQ_HANDLE);

    using AudioCore::DspPipe::Audio;
    using Service::DSP::InterruptType::Pipe;
    const bool& audio_pipe_interrupted =
        interrupts_fired[static_cast<u32>(Pipe)][static_cast<u32>(Audio)];

    while (!audio_pipe_interrupted) {
        core_timing.GetTimer(0)->AddTicks(core_timing.GetTimer(0)->GetDowncount());
        core_timing.GetTimer(0)->Advance();
        core_timing.GetTimer(0)->SetNextSlice();
    }

    return ResultSuccess;
}

Result ServiceFixture::svcClearEvent(Handle handle) {
    ASSERT(handle == PIPE2_IRQ_HANDLE);
    using AudioCore::DspPipe::Audio;
    using Service::DSP::InterruptType::Pipe;
    interrupts_fired[static_cast<u32>(Pipe)][static_cast<u32>(Audio)] = 0;

    return ResultSuccess;
}

Result ServiceFixture::svcSignalEvent(Handle handle) {
    ASSERT(handle == DSP_SEMAPHORE_HANDLE);
    dsp->SetSemaphore(0x2000);
    // TODO: Add relevent amount of ticks

    return ResultSuccess;
}

// dsp::DSP
Result ServiceFixture::DSP_LoadComponent(const u8* dspfirm_data, size_t size, u8 progmask,
                                         u8 datamask, bool* dspfirm_loaded) {
    dsp->LoadComponent({dspfirm_data, size});
    *dspfirm_loaded = true;

    return ResultSuccess;
}

Result ServiceFixture::DSP_UnloadComponent() {
    dsp->UnloadComponent();

    return ResultSuccess;
}

Result ServiceFixture::DSP_GetSemaphoreHandle(Handle* handle) {
    *handle = DSP_SEMAPHORE_HANDLE;

    return ResultSuccess;
};

Result ServiceFixture::DSP_SetSemaphore(u32 semaphore) {
    dsp->SetSemaphore(semaphore);

    return ResultSuccess;
}

Result ServiceFixture::DSP_ReadPipeIfPossible(u32 channel, u32 /*peer*/, void* out_buffer, u32 size,
                                              u16* out_size) {
    const AudioCore::DspPipe pipe = static_cast<AudioCore::DspPipe>(channel);
    const u16 pipe_readable_size = static_cast<u16>(dsp->GetPipeReadableSize(pipe));

    std::vector<u8> pipe_buffer;
    if (pipe_readable_size >= size) {
        pipe_buffer = dsp->PipeRead(pipe, size);
    }

    *out_size = static_cast<u16>(pipe_buffer.size());

    std::memcpy(out_buffer, pipe_buffer.data(), *out_size);

    return ResultSuccess;
}

Result ServiceFixture::ServiceFixture::DSP_ConvertProcessAddressFromDspDram(u32 dsp_address,
                                                                            u16** host_address) {
    *host_address = reinterpret_cast<u16*>(
        (dsp_address << 1) + (reinterpret_cast<uintptr_t>(memory.GetDspMemory(0)) + 0x40000u));
    return ResultSuccess;
}

Result ServiceFixture::DSP_WriteProcessPipe(u32 channel, const void* buffer, u32 length) {
    const AudioCore::DspPipe pipe = static_cast<AudioCore::DspPipe>(channel);
    dsp->PipeWrite(pipe, {reinterpret_cast<const u8*>(buffer), length});

    return ResultSuccess;
};

// libctru
u32 ServiceFixture::osConvertVirtToPhys(void* vaddr) {
    return static_cast<u32>(Memory::FCRAM_PADDR +
                            (reinterpret_cast<u8*>(vaddr) - memory.GetFCRAMPointer(0)));
}

void* ServiceFixture::linearAlloc(size_t size) {
    void* ret = memory.GetFCRAMPointer(0) + linear_alloc_offset;
    linear_alloc_offset += size;
    return ret;
}

Result ServiceFixture::dspInit() {
    if (!dsp) {
        dsp = std::make_unique<AudioCore::DspHle>(system, memory, core_timing);
        dsp->SetInterruptHandler([this](Service::DSP::InterruptType type, AudioCore::DspPipe pipe) {
            interrupts_fired[static_cast<u32>(type)][static_cast<u32>(pipe)] = 1;
        });
    }

    return ResultSuccess;
}

// Fixture
void ServiceFixture::InitDspCore(Settings::AudioEmulation dsp_core) {
    if (dsp_core == Settings::AudioEmulation::HLE) {
        dsp = std::make_unique<AudioCore::DspHle>(system, memory, core_timing);
    } else {
        dsp = std::make_unique<AudioCore::DspLle>(
            system, memory, core_timing, dsp_core == Settings::AudioEmulation::LLEMultithreaded);
    }
    dsp->SetInterruptHandler([this](Service::DSP::InterruptType type, AudioCore::DspPipe pipe) {
        interrupts_fired[static_cast<u32>(type)][static_cast<u32>(pipe)] = 1;
    });
}
