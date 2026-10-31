// Licensed under GPLv2 or any later version.
#include "kiu_qol/kiu_controller.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "kiu_qol/kiu_config.h"

namespace KiuQol {

namespace {

// Lift the stylus this long when touch ownership changes hands (e.g. stick aim -> tap on the
// power widget), so the game sees a new press instead of a teleporting drag.
constexpr float kSourceSwitchLift = 0.034f;

std::string LowerAscii(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Rising(bool now, bool before) {
    return now && !before;
}

} // namespace

Controller& Controller::Get() {
    static Controller instance;
    return instance;
}

void Controller::SetLogger(LogFn fn) {
    std::scoped_lock lock(mutex_);
    log_ = std::move(fn);
}

void Controller::Log(const std::string& msg) const {
    if (log_)
        log_(msg);
}

std::shared_ptr<const Config> Controller::GetConfig() const {
    std::scoped_lock lock(mutex_);
    return config_;
}

void Controller::LoadConfigLocked() {
    std::error_code ec;
    if (!std::filesystem::exists(config_path_, ec)) {
        std::filesystem::create_directories(config_path_.parent_path(), ec);
        std::ofstream(config_path_) << kDefaultConfigText;
        Log("KIU QOL: wrote default config to " + config_path_.string());
    }
    std::ifstream in(config_path_);
    std::stringstream ss;
    ss << in.rdbuf();
    auto cfg = std::make_shared<Config>(ParseConfig(ss.str()));
    for (const auto& w : cfg->warnings)
        Log("KIU QOL config: " + w);
    config_mtime_ = std::filesystem::last_write_time(config_path_, ec);

    bool active = false;
    switch (cfg->enabled) {
    case EnableMode::On:
        active = true;
        break;
    case EnableMode::Off:
        active = false;
        break;
    case EnableMode::Auto: {
        active = std::find(cfg->title_ids.begin(), cfg->title_ids.end(), title_id_) !=
                 cfg->title_ids.end();
        const std::string name = LowerAscii(title_name_);
        for (const auto& needle : cfg->title_name_contains) {
            if (!needle.empty() && name.find(LowerAscii(needle)) != std::string::npos)
                active = true;
        }
        break;
    }
    }

    stylus_.Configure(cfg->stylus);
    stylus_.Reset();
    macro_.Cancel();
    prev_input_ = {};
    config_ = std::move(cfg);
    active_.store(active, std::memory_order_release);
    generation_.fetch_add(1);
    Log(std::string("KIU QOL: ") + (active ? "active" : "inactive") + " (" +
        std::to_string(config_->widgets.size()) + " widgets, " +
        std::to_string(config_->probes.size()) + " probes, " +
        std::to_string(config_->macros.size()) + " macros)");
}

void Controller::OnGameStarted(std::uint64_t title_id, const std::string& title_name,
                               const std::filesystem::path& config_path) {
    std::scoped_lock lock(mutex_);
    title_id_ = title_id;
    title_name_ = title_name;
    config_path_ = config_path;
    manual_mode_.reset();
    pointer_lock_ = false;
    LoadConfigLocked();
}

void Controller::OnGameStopped() {
    std::scoped_lock lock(mutex_);
    active_ = false;
    pointer_lock_ = false;
    mouse_aim_ = false;
    direct_touch_.reset();
    drag_widget_.reset();
    stylus_.Reset();
    macro_.Cancel();
}

void Controller::SetTopScreenRect(const RectF& r) {
    std::scoped_lock lock(mutex_);
    top_rect_ = r;
}

bool Controller::WantsPointerLock() const {
    return IsActive() && pointer_lock_.load() && CurrentMode() == GameMode::Gameplay;
}

void Controller::OnPointerPress(float x, float y) {
    std::scoped_lock lock(mutex_);
    const auto placed = PlaceWidgets(config_->widgets, CurrentMode(), top_rect_);
    for (auto it = placed.rbegin(); it != placed.rend(); ++it) {
        if (!it->dst.Contains(x, y))
            continue;
        if (it->cfg->touchable) {
            drag_widget_ = *it;
            drag_config_ = config_;
            direct_touch_ = MapIntoWidget(*it, x, y);
        }
        return; // topmost widget swallows the click either way
    }
    if (CurrentMode() == GameMode::Gameplay) {
        mouse_aim_ = true;
        last_x_ = x;
        last_y_ = y;
    }
}

void Controller::OnPointerMove(float x, float y) {
    std::scoped_lock lock(mutex_);
    if (drag_widget_) {
        direct_touch_ = MapIntoWidget(*drag_widget_, x, y);
    } else if (mouse_aim_) {
        stylus_.AddMouseDelta(x - last_x_, y - last_y_);
        last_x_ = x;
        last_y_ = y;
    }
}

void Controller::OnPointerRelease() {
    std::scoped_lock lock(mutex_);
    mouse_aim_ = false;
    drag_widget_.reset();
    drag_config_.reset();
    direct_touch_.reset();
}

void Controller::OnPointerDelta(float dx, float dy) {
    std::scoped_lock lock(mutex_);
    stylus_.AddMouseDelta(dx, dy);
}

GameMode Controller::DetectMode(const MemReader& read_mem) const {
    if (manual_mode_)
        return *manual_mode_;
    const ProbeConfig* best = nullptr;
    for (const auto& p : config_->probes) {
        const auto v = read_mem ? read_mem(p.addr, p.size) : std::nullopt;
        if (!v)
            continue;
        const bool eq = (*v & p.mask) == (p.value & p.mask);
        if (eq != p.not_equal && (!best || p.priority > best->priority))
            best = &p;
    }
    return best ? best->mode : config_->default_mode;
}

TouchOut Controller::Tick(float dt, const InputSnapshot& in, const MemReader& read_mem) {
    std::scoped_lock lock(mutex_);

    // Live-reload the config when the file changes, so HUD calibration is edit-and-look.
    reload_poll_timer_ -= dt;
    bool reload = Rising(in.reload, prev_input_.reload);
    if (reload_poll_timer_ <= 0) {
        reload_poll_timer_ = 1.0f;
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(config_path_, ec);
        if (!ec && mtime != config_mtime_)
            reload = true;
    }
    if (reload) {
        LoadConfigLocked();
        prev_input_ = in; // a held reload key must not retrigger every tick
        return last_out_ = {};
    }
    if (!IsActive())
        return last_out_ = {};

    // Hotkeys.
    if (Rising(in.mode_cycle, prev_input_.mode_cycle)) {
        if (!manual_mode_)
            manual_mode_ = GameMode::Gameplay;
        else if (*manual_mode_ == GameMode::Menu)
            manual_mode_.reset();
        else
            manual_mode_ = static_cast<GameMode>(static_cast<int>(*manual_mode_) + 1);
        Log(std::string("KIU QOL: mode override -> ") +
            (manual_mode_ ? ModeName(*manual_mode_) : "auto"));
    }
    if (Rising(in.aim_lock, prev_input_.aim_lock)) {
        pointer_lock_ = !pointer_lock_.load();
        Log(std::string("KIU QOL: pointer lock ") + (pointer_lock_ ? "on" : "off"));
    }

    const GameMode prev_mode = CurrentMode();
    const GameMode mode = DetectMode(read_mem);
    mode_.store(static_cast<std::uint8_t>(mode));
    if (mode != prev_mode) {
        stylus_.Reset();
        mouse_aim_ = false;
    }

    // Macros: rising edge on a bound button starts it (one at a time).
    const auto& macros = config_->macros;
    if (!macro_.Active()) {
        for (size_t i = 0; i < macros.size() && i < in.macro_buttons.size(); ++i) {
            const bool before = i < prev_input_.macro_buttons.size() && prev_input_.macro_buttons[i];
            if (Rising(in.macro_buttons[i], before) && (macros[i].modes & ModeBit(mode))) {
                macro_.Start(macros[i], last_out_.pressed);
                stylus_.Reset();
                break;
            }
        }
    }
    prev_input_ = in;

    // Pick this tick's touch: direct widget touch > macro > aim stylus.
    TouchOut out{};
    Source src = Source::None;
    if (direct_touch_) {
        out = *direct_touch_;
        src = Source::Direct;
        stylus_.Reset();
    } else if (auto m = macro_.Tick(dt)) {
        out = *m;
        src = Source::Macro;
    } else if (mode == GameMode::Gameplay) {
        stylus_.SetMouseEngaged(mouse_aim_ || WantsPointerLock());
        out = stylus_.Tick(dt, in.stick_x, in.stick_y);
        src = out.pressed ? Source::Stylus : Source::None;
    }

    // Force a short release when a held touch changes owner.
    if (out.pressed && last_out_.pressed && src != last_source_ && src != Source::None &&
        last_source_ != Source::None) {
        switch_lift_ = kSourceSwitchLift;
    }
    if (switch_lift_ > 0) {
        switch_lift_ -= dt;
        out.pressed = false;
    }
    if (out.pressed || src == Source::None)
        last_source_ = src;
    return last_out_ = out;
}

std::vector<Controller::DrawWidget> Controller::GetDrawList() const {
    std::vector<DrawWidget> out;
    if (!IsActive())
        return out;
    std::scoped_lock lock(mutex_);
    for (const auto& p : PlaceWidgets(config_->widgets, CurrentMode(), top_rect_))
        out.push_back({p.cfg->src, p.dst, p.cfg->opacity});
    return out;
}

} // namespace KiuQol
