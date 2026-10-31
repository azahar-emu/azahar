// Standalone tests for kiu_qol (no emulator needed).
//   g++ -std=c++17 -I src src/kiu_qol/*.cpp src/kiu_qol/tests/kiu_qol_tests.cpp -o kiu_tests
#include <cmath>
#include <fstream>
#include <cstdio>
#include <string>

#include "kiu_qol/kiu_config.h"
#include "kiu_qol/kiu_controller.h"
#include "kiu_qol/kiu_hud.h"
#include "kiu_qol/kiu_stylus.h"

using namespace KiuQol;

static int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                        \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
static bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

// Reproduces Azahar's Landscape vertex setup to check that a cropped texcoord rect samples
// exactly the requested bottom-screen pixels. Returns the bottom px sampled at a corner.
struct UV {
    float u, v;
};
static UV CornerUV(const TexRect& t, bool right, bool lower) {
    // (x,y)->(bottom,left) (x+w,y)->(bottom,right) (x,y+h)->(top,left) (x+w,y+h)->(top,right)
    return {lower ? t.top : t.bottom, right ? t.right : t.left};
}

static void TestCrop() {
    // Software renderer base rect is (0,0,1,1); hardware renderer uses scaled sub-rects.
    const TexRect bases[] = {{0, 0, 1, 1}, {0.1f, 0.2f, 0.6f, 0.9f}};
    for (const auto& base : bases) {
        const RectI src{40, 30, 120, 60};
        const TexRect c = CropTexcoords(base, src);
        const UV full_tl = CornerUV(base, false, false);
        const UV full_br = CornerUV(base, true, true);
        auto expect = [&](float px, float py) {
            const float fx = px / kBottomW, fy = py / kBottomH;
            return UV{full_tl.u + fy * (full_br.u - full_tl.u),
                      full_tl.v + fx * (full_br.v - full_tl.v)};
        };
        const UV tl = CornerUV(c, false, false), br = CornerUV(c, true, true);
        const UV etl = expect(40, 30), ebr = expect(160, 90);
        CHECK(Near(tl.u, etl.u) && Near(tl.v, etl.v));
        CHECK(Near(br.u, ebr.u) && Near(br.v, ebr.v));
    }
    // Identity crop is a no-op.
    const TexRect b{0.1f, 0.2f, 0.6f, 0.9f};
    const TexRect id = CropTexcoords(b, {0, 0, kBottomW, kBottomH});
    CHECK(Near(id.left, b.left) && Near(id.right, b.right) && Near(id.top, b.top) &&
          Near(id.bottom, b.bottom));
}

static void TestPlacementAndHitTest() {
    WidgetConfig w;
    w.src = {0, 0, 100, 50};
    w.anchor = Anchor::TopRight;
    w.offset_x = -10;
    w.offset_y = 5;
    w.scale = 2.0f;
    w.touchable = true;
    const RectF l = WidgetLogicalRect(w);
    CHECK(Near(l.x, 400 - 200 - 10) && Near(l.y, 5) && Near(l.w, 200) && Near(l.h, 100));

    // Top screen drawn at 2x, offset in the window.
    const RectF top{100, 50, 800, 480};
    const RectF fb = WidgetFramebufferRect(w, top);
    CHECK(Near(fb.x, 100 + 190 * 2) && Near(fb.y, 50 + 10) && Near(fb.w, 400) && Near(fb.h, 200));

    std::vector<WidgetConfig> ws{w};
    auto placed = PlaceWidgets(ws, GameMode::Gameplay, top);
    CHECK(placed.size() == 1);
    // Center of the widget maps to center of its src rect.
    auto hit = HitTestTouch(placed, fb.x + fb.w / 2, fb.y + fb.h / 2);
    CHECK(hit && hit->pressed && Near(hit->x, 50) && Near(hit->y, 25));
    CHECK(!HitTestTouch(placed, 10, 10)); // miss

    // Mode filtering.
    CHECK(PlaceWidgets(ws, GameMode::Menu, top).empty());

    // Non-touchable widget swallows the click.
    ws[0].touchable = false;
    placed = PlaceWidgets(ws, GameMode::Gameplay, top);
    hit = HitTestTouch(placed, fb.x + 1, fb.y + 1);
    CHECK(hit && !hit->pressed);
}

static void TestConfigParse() {
    const char* text = R"(
[general]
enabled = on
title_ids = 0004000000012345, 00040000-000ABCDE
mode_cycle_button = engine:keyboard,code:16777268   ; F5
[stylus]
mouse_sensitivity = 0.75
settle_ms = 80
[widget.radar]
src = 10, 20, 400, 999     ; clipped to the screen
anchor = top_right
modes = gameplay, dialogue
opacity = 2
[widget.bad]
anchor = sideways
[probe.menu]
addr = 0x1000
size = 1
value = 3
mode = menu
priority = 5
[probe.noaddr]
value = 1
[macro.p1]
button = engine:keyboard,code:49
taps = 30,200,60 | 40, 210
)";
    const Config c = ParseConfig(text);
    CHECK(c.enabled == EnableMode::On);
    CHECK(c.title_ids.size() == 2 && c.title_ids[0] == 0x0004000000012345ull &&
          c.title_ids[1] == 0x00040000000ABCDEull);
    CHECK(c.mode_cycle_button == "engine:keyboard,code:16777268");
    CHECK(Near(c.stylus.mouse_sensitivity, 0.75f) && c.stylus.settle_ms == 80);
    CHECK(c.widgets.size() == 2);
    CHECK(c.widgets[0].name == "radar" && c.widgets[0].src.w == 310 && c.widgets[0].src.h == 220);
    CHECK(c.widgets[0].modes == (ModeBit(GameMode::Gameplay) | ModeBit(GameMode::Dialogue)));
    CHECK(Near(c.widgets[0].opacity, 1.0f));
    CHECK(c.probes.size() == 1 && c.probes[0].addr == 0x1000 && c.probes[0].mode == GameMode::Menu);
    CHECK(c.macros.size() == 1 && c.macros[0].steps.size() == 2 &&
          c.macros[0].steps[0].hold_ms == 60 && c.macros[0].steps[1].hold_ms == 50);
    CHECK(c.warnings.size() == 2); // bad anchor, probe without addr

    // The shipped default must parse cleanly.
    const Config d = ParseConfig(kDefaultConfigText);
    for (const auto& w : d.warnings)
        std::printf("default config warning: %s\n", w.c_str());
    CHECK(d.warnings.empty());
    CHECK(d.widgets.size() == 2);
    CHECK(d.aim_lock_button == "engine:keyboard,code:16777269");
}

static void TestStylusRecenter() {
    StylusConfig sc;
    sc.mouse_sensitivity = 1.0f;
    sc.edge_margin = 20;
    sc.settle_ms = 40;
    sc.lift_ms = 30;
    VirtualStylus s;
    s.Configure(sc);
    const float dt = 0.004f;

    CHECK(!s.Tick(dt, 0, 0).pressed); // nothing engaged
    s.SetMouseEngaged(true);
    TouchOut t = s.Tick(dt, 0, 0);
    CHECK(t.pressed && Near(t.x, 160) && Near(t.y, 120)); // press at center first

    s.AddMouseDelta(30, -10);
    t = s.Tick(dt, 0, 0);
    CHECK(t.pressed && Near(t.x, 190) && Near(t.y, 110));

    // Drive into the right edge: should settle (held still), then lift, then re-press center.
    s.AddMouseDelta(200, 0);
    t = s.Tick(dt, 0, 0);
    CHECK(t.pressed && Near(t.x, 190 + 128)); // pending clamp is 40% of 320 = 128 -> 318
    const float held_x = t.x;
    s.AddMouseDelta(15, 0); // arrives during settle; must be buffered, not applied
    int settle_ticks = 0, lift_ticks = 0;
    bool moved_while_settling = false;
    for (int i = 0; i < 100; ++i) {
        t = s.Tick(dt, 0, 0);
        if (t.pressed && !Near(t.x, held_x) && lift_ticks == 0)
            moved_while_settling = true;
        if (t.pressed && lift_ticks == 0)
            ++settle_ticks;
        else if (!t.pressed)
            ++lift_ticks;
        else
            break; // re-pressed
    }
    CHECK(!moved_while_settling);
    CHECK(settle_ticks >= 9 && settle_ticks <= 11); // ~40ms at 4ms
    CHECK(lift_ticks >= 7 && lift_ticks <= 9);      // ~30ms
    CHECK(t.pressed && Near(t.x, 160) && Near(t.y, 120));
    t = s.Tick(dt, 0, 0);
    CHECK(Near(t.x, 175)); // buffered 15px applied after the re-press

    s.SetMouseEngaged(false);
    CHECK(!s.Tick(dt, 0, 0).pressed);
}

static void TestStylusStick() {
    StylusConfig sc;
    sc.stick_speed = 100;
    sc.stick_deadzone = 0.1f;
    sc.stick_curve = 1.0f;
    sc.stick_release_ms = 20;
    VirtualStylus s;
    s.Configure(sc);
    const float dt = 0.01f;
    CHECK(!s.Tick(dt, 0.05f, 0).pressed); // inside deadzone
    TouchOut t = s.Tick(dt, 1.0f, 0);     // press at center
    CHECK(t.pressed && Near(t.x, 160));
    t = s.Tick(dt, 1.0f, 1.0f); // applies two ticks of motion
    CHECK(t.x > 161.9f && t.x < 162.1f);
    CHECK(t.y < 120); // stick up = stylus up
    // Release after stick_release_ms of idle.
    int ticks = 0;
    while (s.Tick(dt, 0, 0).pressed && ticks < 50)
        ++ticks;
    CHECK(ticks >= 1 && ticks <= 3);
}

static void TestMacro() {
    MacroConfig m;
    m.steps = {{10, 20, 20, 10}, {30, 40, 20, 0}};
    MacroPlayer p;
    p.Start(m, /*stylus_was_down=*/true);
    const float dt = 0.005f;
    int pre = 0, down1 = 0, gap1 = 0, down2 = 0, gap2 = 0;
    int stage = 0;
    for (int i = 0; i < 200; ++i) {
        auto t = p.Tick(dt);
        if (!t)
            break;
        if (stage == 0 && t->pressed)
            stage = 1;
        if (stage == 1 && !t->pressed)
            stage = 2;
        if (stage == 2 && t->pressed)
            stage = 3;
        if (stage == 3 && !t->pressed)
            stage = 4;
        switch (stage) {
        case 0:
            ++pre;
            break;
        case 1:
            ++down1;
            CHECK(Near(t->x, 10) && Near(t->y, 20));
            break;
        case 2:
            ++gap1;
            break;
        case 3:
            ++down2;
            CHECK(Near(t->x, 30) && Near(t->y, 40));
            break;
        case 4:
            ++gap2;
            break;
        }
    }
    CHECK(!p.Active());
    CHECK(pre >= 6 && pre <= 8);     // ~34ms pre-lift
    CHECK(down1 >= 4 && down1 <= 5); // ~20ms
    CHECK(gap1 >= 2 && gap1 <= 3);   // ~10ms
    CHECK(down2 >= 4 && down2 <= 5);
    CHECK(gap2 == 1); // zero gap still yields one released tick
}

static void TestControllerFlow() {
    auto& c = Controller::Get();
    const auto path =
        std::filesystem::temp_directory_path() / ("kiu_qol_test_" + std::to_string(::rand()) + ".ini");
    {
        std::ofstream(path) << R"(
[general]
enabled = auto
title_name_contains = Kid Icarus
default_mode = gameplay
[widget.inset]
src = 0, 0, 320, 240
anchor = top_left
scale = 0.5
modes = gameplay
touchable = true
[probe.menu]
addr = 0x2000
size = 1
value = 1
mode = menu
[macro.p]
button = x
taps = 50, 60, 20, 10
modes = gameplay
)";
    }
    c.OnGameStarted(0x1234, "KID ICARUS: UPRISING", path);
    CHECK(c.IsActive());
    c.SetTopScreenRect({0, 0, 400, 240});

    std::uint8_t menu_flag = 0;
    Controller::MemReader mem = [&](std::uint32_t a, std::uint8_t) -> std::optional<std::uint32_t> {
        if (a == 0x2000)
            return menu_flag;
        return std::nullopt;
    };
    Controller::InputSnapshot in;
    in.macro_buttons = {false};
    const float dt = 0.004f;

    // Click on the inset -> direct touch at mapped coords (inset is 160x120 at top-left).
    c.OnPointerPress(80, 60);
    TouchOut t = c.Tick(dt, in, mem);
    CHECK(t.pressed && Near(t.x, 160) && Near(t.y, 120));
    c.OnPointerRelease();
    CHECK(!c.Tick(dt, in, mem).pressed);

    // Click outside the inset in gameplay -> mouse aim; drag moves the stylus.
    c.OnPointerPress(300, 200);
    t = c.Tick(dt, in, mem);
    CHECK(t.pressed && Near(t.x, 160));
    c.OnPointerMove(320, 200); // +20 fb px * default sensitivity 0.5 = +10
    t = c.Tick(dt, in, mem);
    CHECK(t.pressed && Near(t.x, 170));
    c.OnPointerRelease();
    CHECK(!c.Tick(dt, in, mem).pressed);

    // Probe flips to menu: inset (gameplay-only) disappears, no aiming.
    menu_flag = 1;
    c.Tick(dt, in, mem);
    CHECK(c.CurrentMode() == GameMode::Menu);
    CHECK(c.GetDrawList().empty());
    c.OnPointerPress(300, 200);
    CHECK(!c.Tick(dt, in, mem).pressed);
    c.OnPointerRelease();

    // Macro only fires in its modes.
    in.macro_buttons = {true};
    CHECK(!c.Tick(dt, in, mem).pressed);
    in.macro_buttons = {false};
    menu_flag = 0;
    c.Tick(dt, in, mem);
    in.macro_buttons = {true};
    t = c.Tick(dt, in, mem);
    CHECK(t.pressed && Near(t.x, 50) && Near(t.y, 60));

    // Mode override hotkey wins over probes.
    in.macro_buttons = {false};
    in.mode_cycle = true; // auto -> gameplay
    c.Tick(dt, in, mem);
    in.mode_cycle = false;
    c.Tick(dt, in, mem);
    in.mode_cycle = true; // -> dialogue
    c.Tick(dt, in, mem);
    CHECK(c.CurrentMode() == GameMode::Dialogue);

    // Name mismatch + no title IDs -> inactive.
    c.OnGameStarted(0x9999, "Some Other Game", path);
    CHECK(!c.IsActive());
    c.OnGameStopped();
    std::filesystem::remove(path);
}

int main() {
    TestCrop();
    TestPlacementAndHitTest();
    TestConfigParse();
    TestStylusRecenter();
    TestStylusStick();
    TestMacro();
    TestControllerFlow();
    if (g_failures == 0)
        std::printf("all kiu_qol tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
