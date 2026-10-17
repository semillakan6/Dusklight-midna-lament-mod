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
    lament::MainGainGuard guard;
    check(near(guard.select(false, false, 0.4f, 1), 0.4f), "ordinary native fade-in must be retained");
    check(near(guard.select(true, false, 0, 0), 1), "combat must not fade the base");
    check(near(guard.select(false, false, 0.133f, 1), 1), "cleared battle flags must not cut the base to 13 percent");
    for (float gain : {0.3f, 0.65f, 0.99f, 1.0f})
        check(near(guard.select(false, false, gain, 1), 1), "base gain must stay continuous through native recovery");
    check(near(guard.select(false, false, 0.7f, 0), 0.7f), "a genuine later fade-out must still work");
    guard.select(true, false, 0, 0);
    check(near(guard.select(false, true, 0.25f, 0), 0.25f), "other sub music must retain native attenuation");
    check(near(guard.select(false, false, 0.3f, 1), 0.3f), "sub music takeover must clear battle recovery");
    guard.select(true, false, 0, 0);guard.reset();
    check(near(guard.select(false, false, 0.5f, 1), 0.5f), "reload must clear battle recovery");
    std::cout << "Controller checks passed\n";
}
