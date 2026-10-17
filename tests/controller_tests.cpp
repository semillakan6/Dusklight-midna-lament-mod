#include "../src/controller.hpp"
#include <cstdlib>
#include <iostream>
static void check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
static bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }
int main() {
    for (int fps : {30, 60, 120, 144}) {
        lament::CombatFade fade;
        fade.setCombat(true);
        for (int i = 0; i < fps / 2; ++i) fade.advance(1.0f / fps);
        check(near(fade.value(), 1), "entry duration depends on FPS");
        fade.setCombat(false);
        for (int i = 0; i < fps * 3 / 2; ++i) fade.advance(1.0f / fps);
        check(near(fade.value(), 0), "exit duration depends on FPS");
    }
    lament::CombatFade fade;
    fade.setCombat(true);
    check(near(fade.advance(0.25f), 0.5f), "entry midpoint");
    fade.setCombat(false);
    check(near(fade.advance(0), 0.5f), "reversal jumps");
    check(near(fade.advance(0.75f), 0.25f), "exit midpoint");
    fade.setCombat(true);
    check(near(fade.advance(0.5f), 1), "re-entry");
    check(near(fade.advance(0), 1), "pause changes envelope");
    fade.reset();
    check(near(fade.value(), 0), "reload retains combat gain");
    check(lament::combatRequested(11, 11, 12, 0, false, true), "normal combat");
    check(lament::combatRequested(12, 11, 12, 0, false, true), "twilight combat");
    check(!lament::combatRequested(11, 11, 12, 5, false, true), "retreat countdown");
    check(!lament::combatRequested(11, 11, 12, 0, true, true), "battle disabled");
    check(!lament::combatRequested(11, 11, 12, 0, false, false), "scene absent");
    check(!lament::combatRequested(0xffffffff, 11, 12, 0, false, true), "non-battle");
    std::cout << "Controller checks passed\n";
}
