// Licensed under GPLv2 or any later version.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "kiu_qol/kiu_hud.h"
#include "kiu_qol/kiu_stylus.h"
#include "kiu_qol/kiu_types.h"

namespace KiuQol {

/// Ties the QOL pieces together. Called from three threads:
///   * frontend/UI thread: pointer events, window layout
///   * emulation thread:   Tick() once per HID update, produces the touch the game sees
///   * render thread:      GetDrawList() once per presented frame
class Controller {
public:
    static Controller& Get();

    using LogFn = std::function<void(const std::string&)>;
    void SetLogger(LogFn fn);

    // ---- lifecycle (emulation thread) ----
    void OnGameStarted(std::uint64_t title_id, const std::string& title_name,
                       const std::filesystem::path& config_path);
    void OnGameStopped();
    bool IsActive() const { return active_.load(std::memory_order_acquire); }

    /// Bumped every time the config is (re)loaded; the input glue uses it to rebuild devices.
    std::uint32_t ConfigGeneration() const { return generation_.load(); }
    std::shared_ptr<const Config> GetConfig() const;

    // ---- frontend thread ----
    void SetTopScreenRect(const RectF& fb_rect);
    void OnPointerPress(float fb_x, float fb_y);
    void OnPointerMove(float fb_x, float fb_y);
    void OnPointerRelease();
    /// Raw motion while the pointer is locked (cursor warped back to center by the frontend).
    void OnPointerDelta(float dx, float dy);
    /// True when the frontend should hide + recenter the cursor and send OnPointerDelta.
    bool WantsPointerLock() const;

    // ---- emulation thread ----
    struct InputSnapshot {
        std::vector<bool> macro_buttons; ///< parallel to GetConfig()->macros
        bool mode_cycle = false;
        bool aim_lock = false;
        bool reload = false;
        float stick_x = 0, stick_y = 0;
    };
    /// Reads guest memory for mode probes; nullopt if the address isn't mapped.
    using MemReader = std::function<std::optional<std::uint32_t>(std::uint32_t, std::uint8_t)>;

    TouchOut Tick(float dt, const InputSnapshot& input, const MemReader& read_mem);

    // ---- render thread ----
    struct DrawWidget {
        RectI src;
        RectF dst;
        float opacity;
    };
    std::vector<DrawWidget> GetDrawList() const;
    GameMode CurrentMode() const { return static_cast<GameMode>(mode_.load()); }

private:
    Controller() = default;

    void LoadConfigLocked();
    void Log(const std::string& msg) const;
    GameMode DetectMode(const MemReader& read_mem) const;

    mutable std::mutex mutex_;
    LogFn log_;
    std::shared_ptr<const Config> config_ = std::make_shared<Config>();
    std::filesystem::path config_path_;
    std::filesystem::file_time_type config_mtime_{};
    float reload_poll_timer_ = 0;

    std::atomic<bool> active_{false};
    std::atomic<std::uint32_t> generation_{0};
    std::atomic<std::uint8_t> mode_{0};
    std::uint64_t title_id_ = 0;
    std::string title_name_;

    // Manual override: nullopt = follow probes / default.
    std::optional<GameMode> manual_mode_;
    std::atomic<bool> pointer_lock_{false};

    RectF top_rect_{0, 0, kTopW, kTopH};

    // Pointer state (guarded by mutex_).
    bool mouse_aim_ = false;
    float last_x_ = 0, last_y_ = 0;
    std::optional<PlacedWidget> drag_widget_;
    std::optional<TouchOut> direct_touch_;
    std::shared_ptr<const Config> drag_config_; ///< keeps drag_widget_->cfg alive across reloads

    // Emulation-thread state.
    VirtualStylus stylus_;
    MacroPlayer macro_;
    InputSnapshot prev_input_;
    enum class Source { None, Direct, Macro, Stylus } last_source_ = Source::None;
    float switch_lift_ = 0;
    TouchOut last_out_;
};

} // namespace KiuQol
