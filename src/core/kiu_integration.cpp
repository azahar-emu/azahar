// Licensed under GPLv2 or any later version.
#include "core/kiu_integration.h"

#include <memory>
#include <mutex>
#include <vector>

#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/frontend/input.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "kiu_qol/kiu_controller.h"

namespace KiuIntegration {

namespace {

/// Input devices built from the config's param strings; rebuilt whenever the config reloads.
struct Devices {
    u32 generation = ~0u;
    std::vector<std::unique_ptr<Input::ButtonDevice>> macros;
    std::unique_ptr<Input::ButtonDevice> mode_cycle, aim_lock, reload;
    std::unique_ptr<Input::AnalogDevice> stick;
};
Devices g_devices; // only touched from the emulation thread (HID callback)

void RebuildDevices(const KiuQol::Config& cfg, u32 generation) {
    g_devices = {};
    g_devices.generation = generation;
    for (const auto& m : cfg.macros)
        g_devices.macros.push_back(Input::CreateDevice<Input::ButtonDevice>(m.button));
    g_devices.mode_cycle = Input::CreateDevice<Input::ButtonDevice>(cfg.mode_cycle_button);
    g_devices.aim_lock = Input::CreateDevice<Input::ButtonDevice>(cfg.aim_lock_button);
    g_devices.reload = Input::CreateDevice<Input::ButtonDevice>(cfg.reload_button);
    g_devices.stick = Input::CreateDevice<Input::AnalogDevice>(cfg.stylus.stick_device);
}

} // namespace

void OnGameLoaded(Core::System& system, Loader::AppLoader& loader, u64 title_id) {
    std::string title;
    loader.ReadTitle(title, true);
    auto& c = KiuQol::Controller::Get();
    c.SetLogger([](const std::string& msg) { LOG_INFO(Frontend, "{}", msg); });
    const std::string path = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "kiu_qol.ini";
    LOG_INFO(Frontend, "KIU QOL: title {:016X} '{}', config {}", title_id, title, path);
    c.OnGameStarted(title_id, title, path);
    g_devices.generation = ~0u; // force a rebuild on the first HID tick
}

void OnGameStopped() {
    KiuQol::Controller::Get().OnGameStopped();
}

std::optional<Touch> SampleTouch(Core::System& system, float dt) {
    auto& c = KiuQol::Controller::Get();
    // Tick even while inactive so that config edits (e.g. enabled = on) still get picked up.
    const u32 gen = c.ConfigGeneration();
    if (gen != g_devices.generation)
        RebuildDevices(*c.GetConfig(), gen);

    KiuQol::Controller::InputSnapshot in;
    in.macro_buttons.reserve(g_devices.macros.size());
    for (const auto& b : g_devices.macros)
        in.macro_buttons.push_back(b->GetStatus());
    in.mode_cycle = g_devices.mode_cycle->GetStatus();
    in.aim_lock = g_devices.aim_lock->GetStatus();
    in.reload = g_devices.reload->GetStatus();
    std::tie(in.stick_x, in.stick_y) = g_devices.stick->GetStatus();

    const auto process = system.Kernel().GetCurrentProcess();
    auto read_mem = [&](u32 addr, u8 size) -> std::optional<u32> {
        if (!process)
            return std::nullopt;
        auto& mem = system.Memory();
        if (!mem.IsValidVirtualAddress(*process, addr) ||
            !mem.IsValidVirtualAddress(*process, addr + size - 1)) {
            return std::nullopt;
        }
        switch (size) {
        case 1:
            return mem.Read8(*process, addr);
        case 2:
            return mem.Read16(*process, addr);
        default:
            return mem.Read32(*process, addr);
        }
    };

    const KiuQol::TouchOut t = c.Tick(dt, in, read_mem);
    if (!c.IsActive())
        return std::nullopt;
    return Touch{t.x, t.y, t.pressed};
}

} // namespace KiuIntegration
