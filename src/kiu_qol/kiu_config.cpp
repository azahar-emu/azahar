// Licensed under GPLv2 or any later version.
#include "kiu_qol/kiu_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace KiuQol {

const char* ModeName(GameMode m) {
    switch (m) {
    case GameMode::Gameplay:
        return "gameplay";
    case GameMode::Dialogue:
        return "dialogue";
    case GameMode::Menu:
        return "menu";
    }
    return "?";
}

namespace {

std::string Trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return std::string(s.substr(b, e - b));
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> Split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            std::string part = Trim(s.substr(start, i - start));
            if (!part.empty())
                out.push_back(std::move(part));
            start = i + 1;
        }
    }
    return out;
}

bool ParseLong(const std::string& s, long long& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 0); // accepts 0x prefix
    return end && *end == '\0';
}

bool ParseU64Hex(const std::string& s, std::uint64_t& out) {
    std::string t = s;
    t.erase(std::remove(t.begin(), t.end(), '-'), t.end()); // allow 00040000-00012345
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        t = t.substr(2);
    if (t.empty())
        return false;
    char* end = nullptr;
    out = std::strtoull(t.c_str(), &end, 16);
    return end && *end == '\0';
}

bool ParseFloat(const std::string& s, float& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0';
}

bool ParseBool(const std::string& s, bool& out) {
    const std::string l = Lower(s);
    if (l == "1" || l == "true" || l == "yes" || l == "on") {
        out = true;
        return true;
    }
    if (l == "0" || l == "false" || l == "no" || l == "off") {
        out = false;
        return true;
    }
    return false;
}

bool ParseInts(const std::string& s, std::vector<int>& out, size_t min_n, size_t max_n) {
    out.clear();
    for (const auto& p : Split(s, ',')) {
        long long v;
        if (!ParseLong(p, v))
            return false;
        out.push_back(static_cast<int>(v));
    }
    return out.size() >= min_n && out.size() <= max_n;
}

bool ParseMode(const std::string& s, GameMode& out) {
    const std::string l = Lower(s);
    if (l == "gameplay" || l == "battle") {
        out = GameMode::Gameplay;
        return true;
    }
    if (l == "dialogue" || l == "dialog") {
        out = GameMode::Dialogue;
        return true;
    }
    if (l == "menu") {
        out = GameMode::Menu;
        return true;
    }
    return false;
}

bool ParseModes(const std::string& s, ModeMask& out) {
    out = 0;
    for (const auto& p : Split(s, ',')) {
        if (Lower(p) == "all") {
            out = kAllModes;
            continue;
        }
        GameMode m;
        if (!ParseMode(p, m))
            return false;
        out |= ModeBit(m);
    }
    return out != 0;
}

bool ParseAnchor(const std::string& s, Anchor& out) {
    static const std::pair<const char*, Anchor> table[] = {
        {"top_left", Anchor::TopLeft},         {"top_center", Anchor::TopCenter},
        {"top_right", Anchor::TopRight},       {"center_left", Anchor::CenterLeft},
        {"center", Anchor::Center},            {"center_right", Anchor::CenterRight},
        {"bottom_left", Anchor::BottomLeft},   {"bottom_center", Anchor::BottomCenter},
        {"bottom_right", Anchor::BottomRight},
    };
    const std::string l = Lower(s);
    for (const auto& [name, a] : table) {
        if (l == name) {
            out = a;
            return true;
        }
    }
    return false;
}

} // namespace

IniFile IniFile::Parse(std::string_view text) {
    IniFile ini;
    ini.sections.push_back({"", {}}); // keys before any [section]
    std::istringstream in{std::string(text)};
    std::string raw;
    while (std::getline(in, raw)) {
        // Strip comments, but only when ';' / '#' starts the line or follows whitespace,
        // so values like "engine:keyboard,code:59" survive.
        std::string line;
        for (size_t i = 0; i < raw.size(); ++i) {
            const char c = raw[i];
            if ((c == ';' || c == '#') &&
                (i == 0 || std::isspace(static_cast<unsigned char>(raw[i - 1])))) {
                break;
            }
            line.push_back(c);
        }
        line = Trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            ini.sections.push_back({Lower(Trim(line.substr(1, line.size() - 2))), {}});
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        ini.sections.back().values[Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
    }
    return ini;
}

const IniFile::Section* IniFile::Find(std::string_view name) const {
    for (const auto& s : sections) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

Config ParseConfig(std::string_view text) {
    Config cfg;
    const IniFile ini = IniFile::Parse(text);

    auto warn = [&](const std::string& section, const std::string& key, const std::string& val) {
        cfg.warnings.push_back("[" + section + "] " + key + " = '" + val + "' is invalid");
    };

    if (const auto* g = ini.Find("general")) {
        for (const auto& [k, v] : g->values) {
            if (k == "enabled") {
                const std::string l = Lower(v);
                if (l == "auto")
                    cfg.enabled = EnableMode::Auto;
                else if (l == "on" || l == "true")
                    cfg.enabled = EnableMode::On;
                else if (l == "off" || l == "false")
                    cfg.enabled = EnableMode::Off;
                else
                    warn("general", k, v);
            } else if (k == "title_ids") {
                for (const auto& p : Split(v, ',')) {
                    std::uint64_t id;
                    if (ParseU64Hex(p, id))
                        cfg.title_ids.push_back(id);
                    else
                        warn("general", k, p);
                }
            } else if (k == "title_name_contains") {
                cfg.title_name_contains = Split(v, ',');
            } else if (k == "default_mode") {
                if (!ParseMode(v, cfg.default_mode))
                    warn("general", k, v);
            } else if (k == "mode_cycle_button") {
                cfg.mode_cycle_button = v;
            } else if (k == "aim_lock_button") {
                cfg.aim_lock_button = v;
            } else if (k == "reload_button") {
                cfg.reload_button = v;
            }
        }
    }

    if (const auto* s = ini.Find("stylus")) {
        auto& st = cfg.stylus;
        for (const auto& [k, v] : s->values) {
            long long iv;
            bool ok = true;
            if (k == "mouse_sensitivity")
                ok = ParseFloat(v, st.mouse_sensitivity);
            else if (k == "stick_speed")
                ok = ParseFloat(v, st.stick_speed);
            else if (k == "stick_deadzone")
                ok = ParseFloat(v, st.stick_deadzone);
            else if (k == "stick_curve")
                ok = ParseFloat(v, st.stick_curve);
            else if (k == "invert_y")
                ok = ParseBool(v, st.invert_y);
            else if (k == "edge_margin")
                ok = ParseFloat(v, st.edge_margin);
            else if (k == "settle_ms")
                ok = ParseLong(v, iv) && ((st.settle_ms = static_cast<int>(iv)), true);
            else if (k == "lift_ms")
                ok = ParseLong(v, iv) && ((st.lift_ms = static_cast<int>(iv)), true);
            else if (k == "stick_release_ms")
                ok = ParseLong(v, iv) && ((st.stick_release_ms = static_cast<int>(iv)), true);
            else if (k == "stick_device")
                st.stick_device = v;
            if (!ok)
                warn("stylus", k, v);
        }
        st.edge_margin = std::clamp(st.edge_margin, 0.0f, 100.0f);
    }

    for (const auto& sec : ini.sections) {
        const std::string& n = sec.name;
        if (n.rfind("widget.", 0) == 0) {
            WidgetConfig w;
            w.name = n.substr(7);
            for (const auto& [k, v] : sec.values) {
                std::vector<int> nums;
                bool ok = true;
                if (k == "src") {
                    ok = ParseInts(v, nums, 4, 4);
                    if (ok)
                        w.src = {nums[0], nums[1], nums[2], nums[3]};
                } else if (k == "anchor") {
                    ok = ParseAnchor(v, w.anchor);
                } else if (k == "offset") {
                    const auto parts = Split(v, ',');
                    ok = parts.size() == 2 && ParseFloat(parts[0], w.offset_x) &&
                         ParseFloat(parts[1], w.offset_y);
                } else if (k == "scale") {
                    ok = ParseFloat(v, w.scale) && w.scale > 0;
                } else if (k == "opacity") {
                    ok = ParseFloat(v, w.opacity);
                } else if (k == "modes") {
                    ok = ParseModes(v, w.modes);
                } else if (k == "touchable") {
                    ok = ParseBool(v, w.touchable);
                }
                if (!ok)
                    warn(n, k, v);
            }
            // Clip the source rect to the bottom screen so texcoords stay in range.
            w.src.x = std::clamp(w.src.x, 0, kBottomW - 1);
            w.src.y = std::clamp(w.src.y, 0, kBottomH - 1);
            w.src.w = std::clamp(w.src.w, 1, kBottomW - w.src.x);
            w.src.h = std::clamp(w.src.h, 1, kBottomH - w.src.y);
            w.opacity = std::clamp(w.opacity, 0.0f, 1.0f);
            cfg.widgets.push_back(std::move(w));
        } else if (n.rfind("probe.", 0) == 0) {
            ProbeConfig p;
            bool have_addr = false;
            for (const auto& [k, v] : sec.values) {
                long long iv;
                bool ok = true;
                if (k == "addr") {
                    ok = have_addr = ParseLong(v, iv);
                    p.addr = static_cast<std::uint32_t>(iv);
                } else if (k == "size") {
                    ok = ParseLong(v, iv) && (iv == 1 || iv == 2 || iv == 4);
                    p.size = static_cast<std::uint8_t>(iv);
                } else if (k == "mask") {
                    ok = ParseLong(v, iv);
                    p.mask = static_cast<std::uint32_t>(iv);
                } else if (k == "value") {
                    ok = ParseLong(v, iv);
                    p.value = static_cast<std::uint32_t>(iv);
                } else if (k == "op") {
                    ok = v == "==" || v == "!=";
                    p.not_equal = v == "!=";
                } else if (k == "mode") {
                    ok = ParseMode(v, p.mode);
                } else if (k == "priority") {
                    ok = ParseLong(v, iv);
                    p.priority = static_cast<int>(iv);
                }
                if (!ok)
                    warn(n, k, v);
            }
            if (have_addr)
                cfg.probes.push_back(p);
            else
                cfg.warnings.push_back("[" + n + "] has no valid addr; ignored");
        } else if (n.rfind("macro.", 0) == 0) {
            MacroConfig m;
            m.name = n.substr(6);
            for (const auto& [k, v] : sec.values) {
                bool ok = true;
                if (k == "button") {
                    m.button = v;
                } else if (k == "modes") {
                    ok = ParseModes(v, m.modes);
                } else if (k == "taps") {
                    for (const auto& step : Split(v, '|')) {
                        std::vector<int> nums;
                        if (!ParseInts(step, nums, 2, 4)) {
                            ok = false;
                            break;
                        }
                        TapStep t;
                        t.x = std::clamp(nums[0], 0, kBottomW - 1);
                        t.y = std::clamp(nums[1], 0, kBottomH - 1);
                        if (nums.size() > 2)
                            t.hold_ms = std::max(1, nums[2]);
                        if (nums.size() > 3)
                            t.gap_ms = std::max(0, nums[3]);
                        m.steps.push_back(t);
                    }
                }
                if (!ok)
                    warn(n, k, v);
            }
            if (m.steps.empty() || m.button.empty())
                cfg.warnings.push_back("[" + n + "] needs both button and taps; ignored");
            else
                cfg.macros.push_back(std::move(m));
        }
    }
    return cfg;
}

} // namespace KiuQol
