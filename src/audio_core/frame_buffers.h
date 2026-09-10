// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include "common/common_types.h"

namespace AudioCore {

/// Interleaved stereo frames held contiguously, so a reader can address them at offsets from
/// the front: refilled at the back, consumed at the front, compacted when the back runs out
/// of room. Sized for the largest callback the pipeline serves plus what a stretcher flush
/// can hand over behind it, plus one splice period. Audio thread only.
class FrameStash {
public:
    static constexpr std::size_t kCapacity = 12288;

    std::size_t Size() const {
        return tail - head;
    }
    std::size_t Room() const {
        return kCapacity - Size();
    }
    /// The frames from the front, contiguous, Size() of them.
    const s16* Data() const {
        return &data[head * 2];
    }
    void Clear() {
        head = 0;
        tail = 0;
    }
    void Consume(std::size_t num_frames) {
        head += std::min(num_frames, Size());
        if (head == tail) {
            head = 0;
            tail = 0;
        }
    }

    /// Pops from `fifo` until this holds `want` frames or the fifo is empty. Returns the frames
    /// pulled; they are the last ones behind Data() + Size().
    template <typename Fifo>
    std::size_t PullFrom(Fifo& fifo, std::size_t want) {
        if (Size() >= want) {
            return 0;
        }
        const std::size_t need = std::min(want - Size(), Room());
        if (need == 0) {
            return 0;
        }
        MakeRoom(need);
        const std::size_t got = fifo.Pop(&data[tail * 2], need);
        tail += got;
        return got;
    }

    /// Appends up to `num_frames`. Returns the frames accepted.
    std::size_t Append(const s16* frames, std::size_t num_frames) {
        const std::size_t n = std::min(num_frames, Room());
        if (n == 0) {
            return 0;
        }
        MakeRoom(n);
        std::memcpy(&data[tail * 2], frames, n * 2 * sizeof(s16));
        tail += n;
        return n;
    }

private:
    void MakeRoom(std::size_t num_frames) {
        if (tail + num_frames <= kCapacity) {
            return;
        }
        std::memmove(data.data(), &data[head * 2], Size() * 2 * sizeof(s16));
        tail -= head;
        head = 0;
    }

    std::array<s16, kCapacity * 2> data{};
    std::size_t head = 0;
    std::size_t tail = 0;
};

/// The most recent frames that passed a point, as a ring, for reading the last few hundred
/// milliseconds back out. Audio thread only.
class FrameHistory {
public:
    static constexpr std::size_t kCapacity = 8192;

    std::size_t Size() const {
        return fill;
    }
    void Clear() {
        pos = 0;
        fill = 0;
    }

    void Record(const s16* frames, std::size_t num_frames) {
        std::size_t first = 0;
        std::size_t n = num_frames;
        if (n > kCapacity) {
            first = n - kCapacity;
            n = kCapacity;
        }
        if (n == 0) {
            return;
        }
        const std::size_t a = std::min(n, kCapacity - pos);
        std::memcpy(&data[pos * 2], frames + (first * 2), a * 2 * sizeof(s16));
        if (n > a) {
            std::memcpy(data.data(), frames + ((first + a) * 2), (n - a) * 2 * sizeof(s16));
        }
        pos = (pos + n) % kCapacity;
        fill = std::min(fill + n, kCapacity);
    }

    /// Copies the most recent `num_frames`, at most Size(), into `out`, oldest first. Returns
    /// the frames copied.
    std::size_t Last(std::size_t num_frames, s16* out) const {
        const std::size_t n = std::min(num_frames, fill);
        if (n == 0) {
            return 0;
        }
        const std::size_t start = (pos + kCapacity - n) % kCapacity;
        const std::size_t a = std::min(n, kCapacity - start);
        std::memcpy(out, &data[start * 2], a * 2 * sizeof(s16));
        if (n > a) {
            std::memcpy(out + (a * 2), data.data(), (n - a) * 2 * sizeof(s16));
        }
        return n;
    }

private:
    std::array<s16, kCapacity * 2> data{};
    std::size_t pos = 0;
    std::size_t fill = 0;
};

} // namespace AudioCore
