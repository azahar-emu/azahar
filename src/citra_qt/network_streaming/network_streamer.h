// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "core/dumping/backend.h"
#include "core/frontend/framebuffer_layout.h"

extern "C" {
typedef struct AVCodecContext AVCodecContext;
typedef struct AVFilterGraph AVFilterGraph;
typedef struct AVFilterContext AVFilterContext;
}

namespace Frontend {
class EmuWindow;
}

namespace VideoCore {
class RendererBase;
}

namespace NetworkStreaming {

class AvahiPublisher;

/**
 * Streams the emulated bottom screen to a network viewer over TCP.
 *
 * This is the desktop counterpart to the Android app's NetworkStreamer.kt: it speaks the exact
 * same wire protocol (4-byte big-endian payload size + 4-byte MediaCodec-style buffer flags +
 * raw H.264 Annex-B payload, plus a small bidirectional touch-event channel on the same socket)
 * so the existing, unmodified Azahar Viewer Android app can be used as the receiver.
 *
 * Capture reuses the emulator's existing video dumping pipeline (VideoDumper::Backend), which
 * already does GPU->CPU readback of a chosen FramebufferLayout on its own thread - only the
 * encode+network step below is new.
 */
class NetworkStreamer : public VideoDumper::Backend {
public:
    static constexpr u32 Width = 320;
    static constexpr u32 Height = 240;
    static constexpr u16 Port = 27500;

    NetworkStreamer(VideoCore::RendererBase& renderer, Frontend::EmuWindow& render_window);
    ~NetworkStreamer() override;

    bool StartDumping(const std::string& path, const Layout::FramebufferLayout& layout) override;
    void AddVideoFrame(VideoDumper::VideoFrame frame) override;
    void AddAudioFrame(AudioCore::StereoFrame16 frame) override {}
    void AddAudioSample(const std::array<s16, 2>& sample) override {}
    void StopDumping() override;
    bool IsDumping() const override;
    Layout::FramebufferLayout GetLayout() const override;

private:
    bool InitEncoder();
    void FreeEncoder();
    void AcceptLoop();
    void TouchLoop(int fd);
    void SendChunk(int fd, const u8* data, int size, u32 flags);
    void EncodeAndSend(const VideoDumper::VideoFrame& frame);
    void InjectTouch(u8 action, float norm_x, float norm_y);

    VideoCore::RendererBase& renderer;
    Frontend::EmuWindow& render_window;
    Layout::FramebufferLayout layout{};

    std::atomic_bool running{false};
    int server_fd = -1;
    std::atomic<int> client_fd{-1};
    std::thread accept_thread;
    std::thread touch_thread;
    std::mutex send_mutex;

    AVCodecContext* codec_context = nullptr;
    AVFilterGraph* filter_graph = nullptr;
    AVFilterContext* buffersrc_ctx = nullptr;
    AVFilterContext* buffersink_ctx = nullptr;
    s64 next_pts = 0;

    std::mutex codec_config_mutex;
    std::vector<u8> codec_config; ///< Cached SPS/PPS, replayed to every newly connected client.

    std::unique_ptr<AvahiPublisher> avahi;
    bool previous_skip_duplicate_frames = false;
};

} // namespace NetworkStreaming
