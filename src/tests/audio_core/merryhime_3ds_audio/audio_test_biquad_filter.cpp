// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdio>
#include <catch2/catch_template_test_macros.hpp>
#include "audio_core/hle/shared_memory.h"
#include "common/settings.h"
#include "merry_audio/merry_audio.h"

TEST_CASE_METHOD(MerryAudio::MerryAudioFixture, "AudioTest-BiquadFilter",
                 "[audio_core][merryhime_3ds_audio]") {
    // High frequency square wave, PCM16
    auto fillBuffer = [this](u32* audio_buffer, size_t size) {
        for (size_t i = 0; i < size; i++) {
            u32 data = (i % 2 == 0 ? 0x1000 : 0x2000);
            audio_buffer[i] = (data << 16) | (data & 0xFFFF);
        }
        DSP_FlushDataCache(audio_buffer, size);
    };

    constexpr size_t NUM_SAMPLES = 160 * 200;
    u32* audio_buffer = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    if (!audio_buffer) {
        FAIL("Memory allocation failed");
    }
    fillBuffer(audio_buffer, NUM_SAMPLES);

    MerryAudio::AudioState state;
    bool state_initialized = false;
    {
        std::vector<u8> dspfirm;
        SECTION("HLE") {
            // The test case assumes HLE AudioCore doesn't require a valid firmware
            dspfirm = {0};
            InitDspCore(Settings::AudioEmulation::HLE);
        }
        SECTION("LLE") {
            dspfirm = loadDspFirmFromFile();
            if (!dspfirm.size()) {
                SKIP("Couldn't load firmware\n");
            }
            InitDspCore(Settings::AudioEmulation::LLE);
        }
        auto ret = audioInit(dspfirm);
        if (!ret) {
            SKIP("Couldn't init audio\n");
        }
        state = *ret;
        state_initialized = true;
    }

    if (state_initialized) {
        {
            /*
            const s16 b0 = 0.057200221035302035 * (1 << 14);
            const s16 b1 = 0.11440044207060407 * (1 << 14);
            const s16 b2 = 0.0238274928983472 * (1 << 14);
            const s16 a1 = -1.2188761083637 * (1 << 14);
            const s16 a2 = 0.44767699250490806 * (1 << 14);
            */
            srand((u32)time(nullptr));
            const s16 b0 = rand();
            const s16 b1 = rand();
            const s16 b2 = rand();
            const s16 a1 = rand();
            const s16 a2 = rand();

            std::array<s32, 160> expected_output;
            {
                s32 x1 = 0;
                s32 x2 = 0;
                s32 y1 = 0;
                s32 y2 = 0;
                for (int i = 0; i < 160; i++) {
                    const s32 x0 = (i % 4 == 0 || i % 4 == 1 ? 0x1000 : 0x2000);
                    s32 y0 = ((s32)x0 * (s32)b0 + (s32)x1 * b1 + (s32)x2 * b2 + (s32)a1 * y1 +
                              (s32)a2 * y2) >>
                             14;

                    y0 = std::clamp(y0, -32768, 32767);
                    expected_output[i] = y2;

                    x2 = x1;
                    x1 = x0;
                    y2 = y1;
                    y1 = y0;
                }
            }

            state.waitForSync();
            initSharedMem(state);
            state.write().dsp_configuration->aux_bus_enable_0_dirty.Assign(true);
            state.write().dsp_configuration->aux_bus_enable[0] = true;
            state.write().source_configurations->config[0].gain[1][0] = 1.0;
            state.write().source_configurations->config[0].gain_1_dirty.Assign(true);
            state.notifyDsp();
            state.waitForSync();

            {
                u16 buffer_id = 0;

                state.write().source_configurations->config[0].play_position = 0;
                state.write().source_configurations->config[0].physical_address =
                    osConvertVirtToPhys(audio_buffer);
                state.write().source_configurations->config[0].length = NUM_SAMPLES;
                state.write().source_configurations->config[0].mono_or_stereo.Assign(
                    AudioCore::HLE::SourceConfiguration::Configuration::MonoOrStereo::Mono);
                state.write().source_configurations->config[0].format.Assign(
                    AudioCore::HLE::SourceConfiguration::Configuration::Format::PCM16);
                state.write().source_configurations->config[0].fade_in.Assign(false);
                state.write().source_configurations->config[0].adpcm_dirty.Assign(false);
                state.write().source_configurations->config[0].is_looping.Assign(false);
                state.write().source_configurations->config[0].buffer_id = ++buffer_id;
                state.write().source_configurations->config[0].partial_reset_flag.Assign(true);
                state.write().source_configurations->config[0].play_position_dirty.Assign(true);
                state.write().source_configurations->config[0].embedded_buffer_dirty.Assign(true);

                state.write().source_configurations->config[0].enable = true;
                state.write().source_configurations->config[0].enable_dirty.Assign(true);

                state.write().source_configurations->config[0].simple_filter.b0 = 0;
                state.write().source_configurations->config[0].simple_filter.a1 = 0;
                state.write().source_configurations->config[0].simple_filter_enabled.Assign(false);
                state.write().source_configurations->config[0].biquad_filter_enabled.Assign(true);
                state.write().source_configurations->config[0].biquad_filter.b0 = b0;
                state.write().source_configurations->config[0].biquad_filter.b1 = b1;
                state.write().source_configurations->config[0].biquad_filter.b2 = b2;
                state.write().source_configurations->config[0].biquad_filter.a1 = a1;
                state.write().source_configurations->config[0].biquad_filter.a2 = a2;
                state.write().source_configurations->config[0].filters_enabled_dirty.Assign(true);
                state.write().source_configurations->config[0].biquad_filter_dirty.Assign(true);
                state.write().source_configurations->config[0].simple_filter_dirty.Assign(true);
                state.notifyDsp();

                bool continue_reading = true;
                for (size_t frame_count = 0; continue_reading && frame_count < 10; frame_count++) {
                    state.waitForSync();

                    for (size_t i = 0; i < 160; i++) {
                        if (state.read().intermediate_mix_samples->mix1.pcm32[0][i]) {
                            for (size_t j = 0; j < 60; j++) {
                                REQUIRE(state.read().intermediate_mix_samples->mix1.pcm32[0][j] ==
                                        expected_output[j]);
                            }
                            continue_reading = false;
                            break;
                        }
                    }

                    state.notifyDsp();
                }
                REQUIRE(continue_reading == false);
            }
        }

        audioExit(state);
    }
}
