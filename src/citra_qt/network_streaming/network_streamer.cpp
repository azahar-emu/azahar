// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Must precede any other include in this translation unit - see net_compat.h's comment on why
// winsock2.h needs to come first on Windows.
#include "citra_qt/network_streaming/net_compat.h"

#include <cstring>

#include "citra_qt/network_streaming/network_streamer.h"
#include "common/dynamic_library/ffmpeg.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/frontend/emu_window.h"
#include "network/socket_manager.h"
#include "video_core/renderer_base.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}

namespace NetworkStreaming {

namespace {
// MediaCodec.BUFFER_FLAG_* constants (Android), reused verbatim so the existing, unmodified
// Azahar Viewer Android app can decode this stream without any protocol negotiation.
constexpr u32 BUFFER_FLAG_KEY_FRAME = 1;
constexpr u32 BUFFER_FLAG_CODEC_CONFIG = 2;

// Writes all `size` bytes or throws away the connection - mirrors the Kotlin sender's
// "single-client, best-effort delivery, no retry/backpressure handling" approach.
bool WriteAll(SocketHandle fd, const void* data, std::size_t size) {
    const u8* p = static_cast<const u8*>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const auto n = send(NativeSocket(fd), reinterpret_cast<const char*>(p + sent),
                            static_cast<int>(size - sent), kNoSignal);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool ReadAll(SocketHandle fd, void* data, std::size_t size) {
    u8* p = static_cast<u8*>(data);
    std::size_t got = 0;
    while (got < size) {
        const auto n = recv(NativeSocket(fd), reinterpret_cast<char*>(p + got),
                            static_cast<int>(size - got), 0);
        if (n <= 0) {
            return false;
        }
        got += static_cast<std::size_t>(n);
    }
    return true;
}
} // namespace

NetworkStreamer::NetworkStreamer(VideoCore::RendererBase& renderer_,
                                 Frontend::EmuWindow& render_window_)
    : renderer(renderer_), render_window(render_window_) {}

NetworkStreamer::~NetworkStreamer() {
    StopDumping();
}

Layout::FramebufferLayout NetworkStreamer::GetLayout() const {
    return layout;
}

bool NetworkStreamer::IsDumping() const {
    return running;
}

bool NetworkStreamer::InitEncoder() {
    if (!DynamicLibrary::FFmpeg::LoadFFmpeg()) {
        LOG_ERROR(Frontend, "NetworkStreamer: could not load FFmpeg");
        return false;
    }

    const AVCodec* codec = DynamicLibrary::FFmpeg::avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        LOG_ERROR(Frontend, "NetworkStreamer: libx264 encoder not available");
        return false;
    }

    codec_context = DynamicLibrary::FFmpeg::avcodec_alloc_context3(codec);
    codec_context->width = Width;
    codec_context->height = Height;
    codec_context->time_base = {1, 60};
    codec_context->framerate = {60, 1};
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_context->bit_rate = 4'000'000;
    codec_context->gop_size = 60;
    codec_context->max_b_frames = 0;
    // Ask the encoder to hand SPS/PPS back via extradata (once) instead of repeating them
    // in-band before every keyframe - this is what lets us mirror MediaCodec's separate
    // "codec config" chunk instead of having to hand-parse NAL units out of the bitstream.
    codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* options = nullptr;
    DynamicLibrary::FFmpeg::av_dict_set(&options, "preset", "ultrafast", 0);
    DynamicLibrary::FFmpeg::av_dict_set(&options, "tune", "zerolatency", 0);

    if (DynamicLibrary::FFmpeg::avcodec_open2(codec_context, codec, &options) < 0) {
        LOG_ERROR(Frontend, "NetworkStreamer: could not open video encoder");
        return false;
    }

    {
        std::scoped_lock lock(codec_config_mutex);
        codec_config.assign(codec_context->extradata,
                            codec_context->extradata + codec_context->extradata_size);
    }

    // Filter graph: converts the raw BGRA frames the dumping pipeline hands us into the
    // yuv420p the encoder wants, mirroring FFmpegVideoStream::InitFilters in ffmpeg_backend.cpp.
    filter_graph = DynamicLibrary::FFmpeg::avfilter_graph_alloc();
    const AVFilter* source = DynamicLibrary::FFmpeg::avfilter_get_by_name("buffer");
    const AVFilter* sink = DynamicLibrary::FFmpeg::avfilter_get_by_name("buffersink");

    const std::string in_args = fmt::format("video_size={}x{}:pix_fmt={}:time_base=1/60:"
                                            "pixel_aspect=1",
                                            Width, Height, static_cast<int>(AV_PIX_FMT_BGRA));
    if (DynamicLibrary::FFmpeg::avfilter_graph_create_filter(
            &buffersrc_ctx, source, "in", in_args.c_str(), nullptr, filter_graph) < 0) {
        LOG_ERROR(Frontend, "NetworkStreamer: could not create buffer source");
        return false;
    }
    if (DynamicLibrary::FFmpeg::avfilter_graph_create_filter(&buffersink_ctx, sink, "out", nullptr,
                                                             nullptr, filter_graph) < 0) {
        LOG_ERROR(Frontend, "NetworkStreamer: could not create buffer sink");
        return false;
    }

    AVFilterInOut* outputs = DynamicLibrary::FFmpeg::avfilter_inout_alloc();
    outputs->name = DynamicLibrary::FFmpeg::av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    AVFilterInOut* inputs = DynamicLibrary::FFmpeg::avfilter_inout_alloc();
    inputs->name = DynamicLibrary::FFmpeg::av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const bool parsed =
        DynamicLibrary::FFmpeg::avfilter_graph_parse_ptr(filter_graph, "format=yuv420p", &inputs,
                                                         &outputs, nullptr) >= 0 &&
        DynamicLibrary::FFmpeg::avfilter_graph_config(filter_graph, nullptr) >= 0;
    DynamicLibrary::FFmpeg::avfilter_inout_free(&outputs);
    DynamicLibrary::FFmpeg::avfilter_inout_free(&inputs);
    if (!parsed) {
        LOG_ERROR(Frontend, "NetworkStreamer: could not build filter graph");
        return false;
    }

    return true;
}

void NetworkStreamer::FreeEncoder() {
    if (filter_graph) {
        DynamicLibrary::FFmpeg::avfilter_graph_free(&filter_graph);
    }
    buffersrc_ctx = nullptr;
    buffersink_ctx = nullptr;
    if (codec_context) {
        DynamicLibrary::FFmpeg::avcodec_free_context(&codec_context);
    }
}

bool NetworkStreamer::StartDumping(const std::string& /*path*/,
                                   const Layout::FramebufferLayout& layout_) {
    if (running) {
        return false;
    }
    layout = layout_;
    next_pts = 0;

    if (!InitEncoder()) {
        FreeEncoder();
        return false;
    }

    Network::SocketManager::EnableSockets();

    const SocketHandle new_server_fd =
        static_cast<SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (new_server_fd == InvalidSocketHandle) {
        LOG_ERROR(Frontend, "NetworkStreamer: could not create socket");
        Network::SocketManager::DisableSockets();
        FreeEncoder();
        return false;
    }
    server_fd = new_server_fd;
    const int reuse = 1;
    setsockopt(NativeSocket(server_fd), SOL_SOCKET, SO_REUSEADDR,
              reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(Port);
    if (bind(NativeSocket(server_fd), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(NativeSocket(server_fd), 1) < 0) {
        LOG_ERROR(Frontend, "NetworkStreamer: could not bind/listen on port {}", Port);
        CloseSocketHandle(server_fd);
        server_fd = InvalidSocketHandle;
        Network::SocketManager::DisableSockets();
        FreeEncoder();
        return false;
    }

    running = true;
    avahi = std::make_unique<ServicePublisher>();
    avahi->Start("_azahar._tcp", Port);
    accept_thread = std::thread(&NetworkStreamer::AcceptLoop, this);

    // RendererOpenGL::RenderToMailbox() only actually draws a frame when
    // "!use_skip_duplicate_frames || game_frames_updated" - and the main window's own
    // RenderToMailbox call (earlier in the same SwapBuffers) always clears game_frames_updated
    // before the frame dumper's call gets to check it, so with the (default-on) skip-duplicates
    // optimization enabled the dumper's mailbox would never receive a single frame. Force it off
    // while streaming, exactly like the Android sender forces ASPECT_RATIO to Default.
    previous_skip_duplicate_frames = Settings::values.use_skip_duplicate_frames.GetValue();
    Settings::values.use_skip_duplicate_frames.SetValue(false);

    // Registering this Backend with Core::System only tells the renderer where encoded frames
    // should go - it does not by itself start the GPU->CPU readback thread that feeds
    // AddVideoFrame(). That thread only starts once PrepareVideoDumping() is called, mirroring
    // FFmpegBackend::StartDumping() in core/dumping/ffmpeg_backend.cpp.
    renderer.PrepareVideoDumping();

    LOG_INFO(Frontend, "NetworkStreamer: listening on port {}", Port);
    return true;
}

void NetworkStreamer::StopDumping() {
    if (!running) {
        return;
    }
    running = false;
    renderer.CleanupVideoDumping();
    Settings::values.use_skip_duplicate_frames.SetValue(previous_skip_duplicate_frames);

    if (avahi) {
        avahi->Stop();
        avahi.reset();
    }

    if (server_fd != InvalidSocketHandle) {
        shutdown(NativeSocket(server_fd), kShutdownBoth);
        CloseSocketHandle(server_fd);
        server_fd = InvalidSocketHandle;
    }
    const SocketHandle fd = client_fd.exchange(InvalidSocketHandle);
    if (fd != InvalidSocketHandle) {
        shutdown(NativeSocket(fd), kShutdownBoth);
        CloseSocketHandle(fd);
    }

    if (accept_thread.joinable()) {
        accept_thread.join();
    }
    if (touch_thread.joinable()) {
        touch_thread.join();
    }

    FreeEncoder();
    Network::SocketManager::DisableSockets();
    LOG_INFO(Frontend, "NetworkStreamer: stopped");
}

void NetworkStreamer::AcceptLoop() {
    // Polls with a short timeout instead of just blocking in accept() - on Windows, closing a
    // socket from another thread does not reliably unblock a thread parked inside accept() the
    // way it does on POSIX, so StopDumping() closing server_fd alone can't be relied on to wake
    // this loop. A bounded select() lets us both accept promptly and recheck `running` promptly
    // on either platform.
    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(NativeSocket(server_fd), &read_fds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200'000;
#if defined(_WIN32)
        const int ready = select(0, &read_fds, nullptr, nullptr, &tv);
#else
        const int ready =
            select(NativeSocket(server_fd) + 1, &read_fds, nullptr, nullptr, &tv);
#endif
        if (ready <= 0) {
            continue; // Timeout (expected) or a transient error - loop back and recheck running.
        }

        sockaddr_in client_addr{};
        SockLenT client_len = sizeof(client_addr);
        const SocketHandle fd = static_cast<SocketHandle>(
            accept(NativeSocket(server_fd), reinterpret_cast<sockaddr*>(&client_addr),
                  &client_len));
        if (fd == InvalidSocketHandle) {
            continue; // Expected once StopDumping() closes server_fd concurrently.
        }
        const int nodelay = 1;
        setsockopt(NativeSocket(fd), IPPROTO_TCP, TCP_NODELAY,
                  reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

        char addr_str[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
        LOG_INFO(Frontend, "NetworkStreamer: viewer connected from {}", addr_str);

        const SocketHandle old_fd = client_fd.exchange(fd);
        if (old_fd != InvalidSocketHandle) {
            shutdown(NativeSocket(old_fd), kShutdownBoth);
            CloseSocketHandle(old_fd);
        }
        if (touch_thread.joinable()) {
            touch_thread.join();
        }

        {
            std::scoped_lock lock(codec_config_mutex);
            if (!codec_config.empty()) {
                SendChunk(fd, codec_config.data(), static_cast<int>(codec_config.size()),
                         BUFFER_FLAG_CODEC_CONFIG);
            }
        }
        touch_thread = std::thread(&NetworkStreamer::TouchLoop, this, fd);
    }
}

void NetworkStreamer::TouchLoop(SocketHandle fd) {
    while (running && client_fd == fd) {
        u8 action;
        if (!ReadAll(fd, &action, 1)) {
            break;
        }
        u32 raw_x, raw_y;
        if (!ReadAll(fd, &raw_x, 4) || !ReadAll(fd, &raw_y, 4)) {
            break;
        }
        float norm_x, norm_y;
        const u32 host_x = ntohl(raw_x);
        const u32 host_y = ntohl(raw_y);
        std::memcpy(&norm_x, &host_x, 4);
        std::memcpy(&norm_y, &host_y, 4);
        InjectTouch(action, norm_x, norm_y);
    }
}

void NetworkStreamer::InjectTouch(u8 action, float norm_x, float norm_y) {
    const auto& current_layout = render_window.GetFramebufferLayout();
    const auto& bottom = current_layout.bottom_screen;
    const unsigned x =
        bottom.left + static_cast<unsigned>(norm_x * static_cast<float>(bottom.GetWidth()));
    const unsigned y =
        bottom.top + static_cast<unsigned>(norm_y * static_cast<float>(bottom.GetHeight()));
    switch (action) {
    case 0:
        render_window.TouchPressed(x, y);
        break;
    case 1:
        render_window.TouchMoved(x, y);
        break;
    case 2:
        render_window.TouchReleased();
        break;
    }
}

void NetworkStreamer::SendChunk(SocketHandle fd, const u8* data, int size, u32 flags) {
    std::scoped_lock lock(send_mutex);
    const u32 size_be = htonl(static_cast<u32>(size));
    const u32 flags_be = htonl(flags);
    if (!WriteAll(fd, &size_be, 4) || !WriteAll(fd, &flags_be, 4) || !WriteAll(fd, data, size)) {
        const SocketHandle cur = client_fd.load();
        if (cur == fd) {
            client_fd = InvalidSocketHandle;
        }
    }
}

void NetworkStreamer::AddVideoFrame(VideoDumper::VideoFrame frame) {
    if (!running) {
        return;
    }
    if (frame.width != Width || frame.height != Height) {
        LOG_ERROR(Frontend, "NetworkStreamer: frame size {}x{} does not match expected {}x{}",
                 frame.width, frame.height, Width, Height);
        return;
    }
    EncodeAndSend(frame);
}

void NetworkStreamer::EncodeAndSend(const VideoDumper::VideoFrame& frame) {
    const SocketHandle fd = client_fd.load();
    if (fd == InvalidSocketHandle) {
        return; // No viewer connected - skip encoding entirely.
    }

    AVFrame* src_frame = DynamicLibrary::FFmpeg::av_frame_alloc();
    src_frame->format = AV_PIX_FMT_BGRA;
    src_frame->width = Width;
    src_frame->height = Height;
    src_frame->data[0] = const_cast<u8*>(frame.data.data());
    src_frame->linesize[0] = static_cast<int>(frame.stride);
    src_frame->pts = next_pts++;

    if (DynamicLibrary::FFmpeg::av_buffersrc_add_frame(buffersrc_ctx, src_frame) >= 0) {
        AVFrame* filtered = DynamicLibrary::FFmpeg::av_frame_alloc();
        while (DynamicLibrary::FFmpeg::av_buffersink_get_frame(buffersink_ctx, filtered) >= 0) {
            if (DynamicLibrary::FFmpeg::avcodec_send_frame(codec_context, filtered) >= 0) {
                AVPacket* pkt = DynamicLibrary::FFmpeg::av_packet_alloc();
                while (DynamicLibrary::FFmpeg::avcodec_receive_packet(codec_context, pkt) >= 0) {
                    const SocketHandle active_fd = client_fd.load();
                    if (active_fd != InvalidSocketHandle) {
                        const u32 flags = (pkt->flags & AV_PKT_FLAG_KEY) ? BUFFER_FLAG_KEY_FRAME : 0;
                        static u64 frames_sent = 0;
                        if (frames_sent < 5) {
                            LOG_INFO(Frontend, "NetworkStreamer: sending frame #{} ({} bytes, flags={})",
                                    frames_sent, pkt->size, flags);
                        }
                        frames_sent++;
                        SendChunk(active_fd, pkt->data, pkt->size, flags);
                    }
                    DynamicLibrary::FFmpeg::av_packet_free(&pkt);
                    pkt = DynamicLibrary::FFmpeg::av_packet_alloc();
                }
                DynamicLibrary::FFmpeg::av_packet_free(&pkt);
            }
            DynamicLibrary::FFmpeg::av_frame_unref(filtered);
        }
        DynamicLibrary::FFmpeg::av_frame_free(&filtered);
    }
    // src_frame's data[] points at caller-owned memory (frame.data) - do not let av_frame_free
    // touch it; av_frame_free() only frees buffers allocated via the ref-counted AVBufferRef
    // API (none were created here), so this is safe to free directly.
    DynamicLibrary::FFmpeg::av_frame_free(&src_frame);
}

} // namespace NetworkStreaming
