// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdio>
#include <catch2/catch_template_test_macros.hpp>
#include "audio_core/hle/shared_memory.h"
#include "common/settings.h"
#include "tests/audio_core/merryhime_3ds_audio/merry_audio/merry_audio.h"

TEST_CASE_METHOD(MerryAudio::MerryAudioFixture, "Verify SourceStatus::Status::last_buffer_id 1",
                 "[audio_core][hle]") {
    // World's worst triangle wave generator. Generates PCM16.
    auto fillBuffer = [this](u32* audio_buffer, size_t size, unsigned freq) {
        for (size_t i = 0; i < size; i++) {
            u32 data = (i % freq) * 256;
            audio_buffer[i] = (data << 16) | (data & 0xFFFF);
        }
        DSP_FlushDataCache(audio_buffer, size);
    };

    constexpr size_t NUM_SAMPLES = 160 * 1;
    u32* audio_buffer = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    if (!audio_buffer) {
        FAIL("Memory allocation failed for audio_buffer");
    }
    fillBuffer(audio_buffer, NUM_SAMPLES, 160);
    u32* audio_buffer2 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    if (!audio_buffer2) {
        FAIL("Memory allocation failed for audio_buffer2");
    }
    fillBuffer(audio_buffer2, NUM_SAMPLES, 80);
    u32* audio_buffer3 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    if (!audio_buffer3) {
        FAIL("Memory allocation failed for audio_buffer3");
    }
    fillBuffer(audio_buffer3, NUM_SAMPLES, 40);

    MerryAudio::AudioState state;
    bool initialized = false; // Track initialization
    {
        std::vector<u8> dspfirm;
        SECTION("HLE") {
            // The test case assumes HLE AudioCore doesn't require a valid firmware
            dspfirm = {0};
            InitDspCore(Settings::AudioEmulation::HLE);
        }
        SECTION("LLE Sanity") {
            dspfirm = loadDspFirmFromFile(); // Load firmware for LLE
            if (dspfirm.empty()) {
                SKIP("Couldn't load firmware\n");
            }
            InitDspCore(Settings::AudioEmulation::LLE);
        }
        auto ret = audioInit(dspfirm);
        if (!ret) {
            SKIP("Couldn't init audio\n");
        }
        state = *ret;
        initialized = true; // Mark as initialized only on success
    }

    if (initialized) {
        state.waitForSync();
        initSharedMem(state);
        state.notifyDsp();

        state.waitForSync();
        state.notifyDsp();
        state.waitForSync();
        state.notifyDsp();
        state.waitForSync();
        state.notifyDsp();
        state.waitForSync();
        state.notifyDsp();

        {
            u16 buffer_id = 0;
            size_t next_queue_position = 0;

            state.write().source_configurations->config[0].play_position = 0;
            state.write().source_configurations->config[0].physical_address =
                osConvertVirtToPhys(audio_buffer3);
            state.write().source_configurations->config[0].length = NUM_SAMPLES;
            state.write().source_configurations->config[0].mono_or_stereo.Assign(
                AudioCore::HLE::SourceConfiguration::Configuration::MonoOrStereo::Stereo);
            state.write().source_configurations->config[0].format.Assign(
                AudioCore::HLE::SourceConfiguration::Configuration::Format::PCM16);
            state.write().source_configurations->config[0].fade_in.Assign(false);
            state.write().source_configurations->config[0].adpcm_dirty.Assign(false);
            state.write().source_configurations->config[0].is_looping.Assign(false);
            state.write().source_configurations->config[0].buffer_id = ++buffer_id;
            state.write().source_configurations->config[0].partial_reset_flag.Assign(true);
            state.write().source_configurations->config[0].play_position_dirty.Assign(true);
            state.write().source_configurations->config[0].embedded_buffer_dirty.Assign(true);

            state.write()
                .source_configurations->config[0]
                .buffers[next_queue_position]
                .physical_address =
                osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
            state.write().source_configurations->config[0].buffers[next_queue_position].length =
                NUM_SAMPLES;
            state.write()
                .source_configurations->config[0]
                .buffers[next_queue_position]
                .adpcm_dirty = false;
            state.write().source_configurations->config[0].buffers[next_queue_position].is_looping =
                false;
            state.write().source_configurations->config[0].buffers[next_queue_position].buffer_id =
                ++buffer_id;
            state.write().source_configurations->config[0].buffers_dirty |= 1
                                                                            << next_queue_position;
            next_queue_position = (next_queue_position + 1) % 4;
            state.write().source_configurations->config[0].buffer_queue_dirty.Assign(true);
            state.write().source_configurations->config[0].enable = true;
            state.write().source_configurations->config[0].enable_dirty.Assign(true);

            state.notifyDsp();

            for (size_t frame_count = 0; frame_count < 10; frame_count++) {
                state.waitForSync();
                if (!state.read().source_statuses->status[0].is_enabled) {
                    state.write().source_configurations->config[0].enable = true;
                    state.write().source_configurations->config[0].enable_dirty.Assign(true);
                }

                if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                    if (state.read().source_statuses->status[0].current_buffer_id == buffer_id ||
                        state.read().source_statuses->status[0].current_buffer_id == 0) {
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .physical_address =
                            osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .length = NUM_SAMPLES;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .adpcm_dirty = false;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .is_looping = false;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .buffer_id = ++buffer_id;
                        state.write().source_configurations->config[0].buffers_dirty |=
                            1 << next_queue_position;
                        next_queue_position = (next_queue_position + 1) % 4;
                        state.write().source_configurations->config[0].buffer_queue_dirty.Assign(
                            true);
                    }
                }

                state.notifyDsp();
            }

            // current_buffer_id should be 0 if the queue is not empty
            REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 0);

            // Let the queue finish playing
            for (size_t frame_count = 0; frame_count < 10; frame_count++) {
                state.waitForSync();
                state.notifyDsp();
            }

            // TODO: There seems to be some nuances with how the LLE firmware runs the buffer queue,
            // that differs from the HLE implementation
            // REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 5);

            // current_buffer_id should be equal to buffer_id once the queue is empty
            REQUIRE(state.read().source_statuses->status[0].last_buffer_id == buffer_id);
        }

        audioExit(state); // Only called if initialization succeeded
    }
}

TEST_CASE_METHOD(MerryAudio::MerryAudioFixture, "Verify SourceStatus::Status::last_buffer_id 2",
                 "[audio_core][hle]") {
    //  World's worst triangle wave generator. Generates PCM16.
    auto fillBuffer = [this](u32* audio_buffer, size_t size, unsigned freq) {
        for (size_t i = 0; i < size; i++) {
            u32 data = (i % freq) * 256;
            audio_buffer[i] = (data << 16) | (data & 0xFFFF);
        }
        DSP_FlushDataCache(audio_buffer, size);
    };

    constexpr size_t NUM_SAMPLES = 160 * 1;
    u32* audio_buffer = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    if (!audio_buffer) {
        FAIL("Memory allocation failed for audio_buffer");
    }
    fillBuffer(audio_buffer, NUM_SAMPLES, 160);
    u32* audio_buffer2 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    if (!audio_buffer2) {
        FAIL("Memory allocation failed for audio_buffer2");
    }
    fillBuffer(audio_buffer2, NUM_SAMPLES, 80);
    u32* audio_buffer3 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    if (!audio_buffer3) {
        FAIL("Memory allocation failed for audio_buffer3");
    }
    fillBuffer(audio_buffer3, NUM_SAMPLES, 40);

    MerryAudio::AudioState state;
    bool initialized = false; // Track initialization
    {
        std::vector<u8> dspfirm;
        SECTION("HLE") {
            // The test case assumes HLE AudioCore doesn't require a valid firmware
            dspfirm = {0};
            InitDspCore(Settings::AudioEmulation::HLE);
        }
        SECTION("LLE Sanity") {
            dspfirm = loadDspFirmFromFile();
            if (dspfirm.empty()) {
                SKIP("Couldn't load firmware\n");
            }
            InitDspCore(Settings::AudioEmulation::LLE);
        }
        auto ret = audioInit(dspfirm);
        if (!ret) {
            SKIP("Couldn't init audio\n");
        }
        state = *ret;
        initialized = true; // Mark as initialized only on success
    }

    if (initialized) {
        state.waitForSync();
        initSharedMem(state);
        state.notifyDsp();

        state.waitForSync();
        state.notifyDsp();
        state.waitForSync();
        state.notifyDsp();
        state.waitForSync();
        state.notifyDsp();
        state.waitForSync();
        state.notifyDsp();

        {
            u16 buffer_id = 0;
            size_t next_queue_position = 0;

            state.write().source_configurations->config[0].play_position = 0;
            state.write().source_configurations->config[0].physical_address =
                osConvertVirtToPhys(audio_buffer3);
            state.write().source_configurations->config[0].length = NUM_SAMPLES;
            state.write().source_configurations->config[0].mono_or_stereo.Assign(
                AudioCore::HLE::SourceConfiguration::Configuration::MonoOrStereo::Stereo);
            state.write().source_configurations->config[0].format.Assign(
                AudioCore::HLE::SourceConfiguration::Configuration::Format::PCM16);
            state.write().source_configurations->config[0].fade_in.Assign(false);
            state.write().source_configurations->config[0].adpcm_dirty.Assign(false);
            state.write().source_configurations->config[0].is_looping.Assign(false);
            state.write().source_configurations->config[0].buffer_id = ++buffer_id;
            state.write().source_configurations->config[0].partial_reset_flag.Assign(true);
            state.write().source_configurations->config[0].play_position_dirty.Assign(true);
            state.write().source_configurations->config[0].embedded_buffer_dirty.Assign(true);

            state.write()
                .source_configurations->config[0]
                .buffers[next_queue_position]
                .physical_address =
                osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
            state.write().source_configurations->config[0].buffers[next_queue_position].length =
                NUM_SAMPLES;
            state.write()
                .source_configurations->config[0]
                .buffers[next_queue_position]
                .adpcm_dirty = false;
            state.write().source_configurations->config[0].buffers[next_queue_position].is_looping =
                false;
            state.write().source_configurations->config[0].buffers[next_queue_position].buffer_id =
                ++buffer_id;
            state.write().source_configurations->config[0].buffers_dirty |= 1
                                                                            << next_queue_position;
            next_queue_position = (next_queue_position + 1) % 4;
            state.write().source_configurations->config[0].buffer_queue_dirty.Assign(true);
            state.write().source_configurations->config[0].enable = true;
            state.write().source_configurations->config[0].enable_dirty.Assign(true);

            state.notifyDsp();

            for (size_t frame_count = 0; frame_count < 10; frame_count++) {
                state.waitForSync();
                if (!state.read().source_statuses->status[0].is_enabled) {
                    state.write().source_configurations->config[0].enable = true;
                    state.write().source_configurations->config[0].enable_dirty.Assign(true);
                }

                if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                    if (state.read().source_statuses->status[0].current_buffer_id == buffer_id ||
                        state.read().source_statuses->status[0].current_buffer_id == 0) {
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .physical_address =
                            osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .length = NUM_SAMPLES;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .adpcm_dirty = false;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .is_looping = false;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .buffer_id = ++buffer_id;
                        state.write().source_configurations->config[0].buffers_dirty |=
                            1 << next_queue_position;
                        next_queue_position = (next_queue_position + 1) % 4;
                        state.write().source_configurations->config[0].buffer_queue_dirty.Assign(
                            true);
                    }
                }

                state.notifyDsp();
            }

            // current_buffer_id should be 0 if the queue is not empty
            REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 0);

            // Let the queue finish playing
            for (size_t frame_count = 0; frame_count < 10; frame_count++) {
                state.waitForSync();
                state.notifyDsp();
            }

            // TODO: There seems to be some nuances with how the LLE firmware runs the buffer queue,
            // that differs from the HLE implementation
            // REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 5);

            // current_buffer_id should be equal to buffer_id once the queue is empty
            REQUIRE(state.read().source_statuses->status[0].last_buffer_id == buffer_id);

            // Restart Playing
            for (size_t frame_count = 0; frame_count < 10; frame_count++) {
                state.waitForSync();
                if (!state.read().source_statuses->status[0].is_enabled) {
                    state.write().source_configurations->config[0].enable = true;
                    state.write().source_configurations->config[0].enable_dirty.Assign(true);
                }

                if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                    if (state.read().source_statuses->status[0].current_buffer_id == buffer_id ||
                        state.read().source_statuses->status[0].current_buffer_id == 0) {
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .physical_address =
                            osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .length = NUM_SAMPLES;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .adpcm_dirty = false;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .is_looping = false;
                        state.write()
                            .source_configurations->config[0]
                            .buffers[next_queue_position]
                            .buffer_id = ++buffer_id;
                        state.write().source_configurations->config[0].buffers_dirty |=
                            1 << next_queue_position;
                        next_queue_position = (next_queue_position + 1) % 4;
                        state.write().source_configurations->config[0].buffer_queue_dirty.Assign(
                            true);
                    }
                }

                state.notifyDsp();
            }

            // current_buffer_id should be 0 if the queue is not empty
            REQUIRE(state.read().source_statuses->status[0].last_buffer_id == 0);

            // Let the queue finish playing
            for (size_t frame_count = 0; frame_count < 10; frame_count++) {
                state.waitForSync();
                state.notifyDsp();
            }

            // current_buffer_id should be equal to buffer_id once the queue is empty
            REQUIRE(state.read().source_statuses->status[0].last_buffer_id == buffer_id);

            audioExit(state);
        }
    }
}
