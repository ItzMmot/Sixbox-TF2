// tests/auto_rocket_jump_test.cpp — RocketJump::ReloadGate and trajectory helpers
#include "../Unibox/src/Features/Aimbot/AutoRocketJump/JumpPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>

int main()
{
    using namespace RocketJump;
    // Nearby walls must never override a valid floor, including a floor plan
    // that is still waiting for the second grounded command before firing.
    int floorCalls = 0, wallCalls = 0;
    bool floorValid = true, wallValid = true;
    const auto choose = [&] {
        return SelectSurface([&] { ++floorCalls; return floorValid; }, [&] { ++wallCalls; return wallValid; });
    };
    assert(choose() == Surface::Floor && floorCalls == 1 && wallCalls == 0);
    floorValid = false;
    assert(choose() == Surface::Wall && floorCalls == 2 && wallCalls == 1);
    wallValid = false;
    assert(choose() == Surface::None && floorCalls == 3 && wallCalls == 2);
    floorValid = wallValid = true;
    assert(choose() == Surface::Floor && wallCalls == 2); // new landing: floor wins again
    assert(FloorNormal(0.f, 0.f, 1.f));
    assert(FloorNormal(.6f, 0.f, .8f)); // walkable slope
    assert(!FloorNormal(1.f, 0.f, 0.f));
    assert(!FloorNormal(0.f, 0.f, -1.f));
    assert(GetMode(-1) == Mode::Diagonal && GetMode(3) == Mode::Diagonal);
    assert(GetMode(1) == Mode::Vertical && GetMode(2) == Mode::Horizontal);
    const auto vertical = [](float x, float z, float distance) { return BoostScore(Mode::Vertical, x, 0.f, z, distance, 109.f, 1.f, 0.f); };
    const auto horizontal = [](float x, float z, float distance) { return BoostScore(Mode::Horizontal, x, 0.f, z, distance, 109.f, 1.f, 0.f); };
    assert(vertical(0.f, 41.f, 0.f) > vertical(17.f, 41.f, 0.f));
    assert(vertical(17.f, 41.f, 0.f) > vertical(64.f, 41.f, 40.f));
    assert(horizontal(64.f, 41.f, 40.f) > horizontal(17.f, 41.f, 0.f));
    assert(horizontal(64.f, 41.f, 40.f) > horizontal(112.f, 41.f, 88.f)); // distance falloff matters
    assert(horizontal(0.f, 41.f, 0.f) < 0.f); // a vertical launch gives no forward impulse
    assert(horizontal(-64.f, 41.f, 40.f) < 0.f); // reject backwards boosts
    assert(BoostScore(Mode::Horizontal, 0.f, 64.f, 41.f, 40.f, 109.f, 0.f, 1.f) > 0.f); // strafe/right
    assert(vertical(0.f, -10.f, 0.f) < 0.f); // no downward push for Vertical
    assert(vertical(0.f, 41.f, 110.f) < 0.f);
    assert(vertical(std::numeric_limits<float>::quiet_NaN(), 41.f, 0.f) < 0.f);
    ReloadGate reload;
    // A loaded launcher interrupts reload, respects the restored fire cooldown,
    // and only releases the jump sequence when an attack can actually shoot.
    assert(reload.Update(10.f, false, true, true, true) == ReloadAction::Interrupt);
    assert(reload.Update(10.015f, false, false, true, true) == ReloadAction::Interrupt);
    assert(reload.Update(10.03f, true, false, true, true) == ReloadAction::Ready);
    // No interruption while empty: allow the first shell to enter the chamber.
    assert(reload.Update(20.f, false, true, false, true) == ReloadAction::WaitingForShell);
    assert(reload.Update(20.6f, false, true, false, true) == ReloadAction::WaitingForShell);
    assert(reload.Update(20.8f, false, true, true, true) == ReloadAction::Interrupt);
    assert(reload.Update(20.815f, true, false, true, true) == ReloadAction::Ready);
    // A reload animation alone does not forbid a shot once native timers allow it.
    assert(reload.Update(25.f, true, true, true, false) == ReloadAction::Ready);
    assert(reload.Update(26.f, false, false, true, true) == ReloadAction::Idle);
    assert(reload.Update(27.f, false, true, false, false) == ReloadAction::Idle);
    // Bound an interrupted/stalled sequence; an actual ready weapon recovers it.
    assert(reload.Update(30.f, false, true, true, true) == ReloadAction::Interrupt);
    assert(reload.Update(35.1f, false, true, true, true) == ReloadAction::TimedOut);
    assert(reload.Update(35.2f, false, true, true, true) == ReloadAction::TimedOut);
    assert(reload.Update(36.f, true, false, true, true) == ReloadAction::Ready);
    assert(reload.Update(37.f, false, true, false, true) == ReloadAction::WaitingForShell);
    reload.Reset(); // disabling, death, map change, or weapon switch
    assert(reload.Update(0.f, false, false, true, true) == ReloadAction::Idle);
    assert(reload.Update(1.f, false, true, true, true) == ReloadAction::Interrupt);
    assert(reload.Update(.5f, false, true, true, true) == ReloadAction::TimedOut);
    assert(reload.Update(std::numeric_limits<float>::quiet_NaN(), false, true, true, true) == ReloadAction::Idle);

    float root = 0.f;
    assert(PositiveRoot(0.f, -100.f, root) && root == 10.f);
    assert(PositiveRoot(-5.f, 6.f, root) && root == 3.f); // far forward intersection
    assert(!PositiveRoot(5.f, 6.f, root)); // target behind muzzle
    assert(!PositiveRoot(0.f, 1.f, root)); // no real intersection
    assert(!PositiveRoot(std::numeric_limits<float>::infinity(), 0.f, root));
    assert(WallNormal(1.f, 0.f, 0.f));
    assert(WallNormal(.707107f, .707107f, 0.f));
    assert(!WallNormal(0.f, 0.f, 1.f)); // floor/ceiling
    assert(!WallNormal(.866025f, 0.f, .5f)); // ramp
    assert(!WallNormal(0.f, 0.f, 0.f));
    // 66.67-Hz server; stock and Direct Hit rocket speeds (HU/s).
    assert(ImpactTicks(100.f, 1100.f, .015f) == 7);
    assert(ImpactTicks(100.f, 1980.f, .015f) == 4);
    assert(ImpactTicks(0.f, 1100.f, .015f) == 1);
    assert(ImpactTicks(165.f, 1100.f, .015f) == 10);
    assert(ImpactTicks(166.f, 1100.f, .015f) == 0);
    assert(!ImpactTicks(10.f, 0.f, .015f));
    assert(!ImpactTicks(10.f, 1100.f, 0.f));
    assert(!ImpactTicks(-1.f, 1100.f, .015f));
    std::cout << "Auto rocket jump: floor priority, boost modes, reload and trajectory tests passed\n";
}
