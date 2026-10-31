// tests/auto_trolldier_test.cpp — portable regression tests for Trolldier::Solver.
// g++ -std=c++20 -O2 -Wall -Wextra -pedantic tests/auto_trolldier_test.cpp -o /tmp/auto-trolldier-test
#include "../Unibox/src/Features/Aimbot/AutoTrolldier/Intercept.h"
#include "../Unibox/src/Features/Aimbot/AutoTrolldier/Rotation.h"
#include "../Unibox/src/Features/Aimbot/AutoTrolldier/Automation.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

static size_t allocations = 0;
void* operator new(size_t size) { ++allocations; if (void* p = std::malloc(size)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

using namespace Trolldier;
struct World { int calls = 0; bool wall = false, ceiling = false, invalidTrace = false, blockPositiveY = false; };
static Trace TraceWorld(void* ctx, Vector from, Vector to, Vector, Vector, int actor)
{
    auto& world = *static_cast<World*>(ctx); ++world.calls;
    assert(from.Finite() && to.Finite());
    if (world.invalidTrace)
        return {std::numeric_limits<float>::quiet_NaN(), {}, false};
    if (world.ceiling && actor == -1)
        return {0.f, {0,0,-1}, false};
    if (world.wall && actor == -1 && to.x > 40.f)
        return { std::clamp((40.f - from.x) / (to.x - from.x), 0.f, 1.f), {-1,0,0}, false };
    if (world.blockPositiveY && actor == -1 && to.y > 0.f)
        return {std::clamp((0.f - from.y) / (to.y - from.y), 0.f, 1.f), {0,-1,0}, false};
    if (to.z < 0.f)
        return { std::clamp(from.z / (from.z - to.z), 0.f, 1.f), {0,0,1}, false };
    return {};
}
static Actor Body(Vector origin, Vector velocity = {}, bool grounded = false)
{
    return {origin, velocity, {-24,-24,0}, {24,24,82}, 800.f, grounded};
}
// Drive the real planner one command at a time through a full MG windup.
// This catches errors hidden by a single optimistic, open-loop trajectory.
static void GardenPass(Actor local, Actor target)
{
    Solver solver; Settings s; Plan plan; World world;
    s.traceBudget = 4096; s.ticks = 90; s.reach = 58.f;
    std::array<Actor, MaxTargets> targets{};
    float deadline = -1.f;
    int previous = -1;
    Vector side;
    for (int command = 0; command < 100; ++command)
    {
        const int base = 1000 + command;
        const float now = float(base) * s.interval;
        const int impact = ImpactSample(base, s.interval, deadline > 0.f ? deadline : now + .2f);
        assert(impact);
        s.strikeTick = std::max(1, impact - 1);
        targets[0] = target;
        const auto before = allocations;
        solver.Solve(local, targets, 1, s, TraceWorld, &world, previous, plan, side, deadline > 0.f ? 1 : 3);
        assert(allocations == before);
        const Vector wish = plan.target >= 0 ? plan.wish : Vector{};
        if (deadline < 0.f && StableStrikeWindow(plan, impact)) deadline = now + .2f;
        previous = plan.target; side = plan.side;
        local.velocity.z -= local.gravity * s.interval * .5f;
        local.velocity = Accelerate(local.velocity, wish, s);
        local.origin = local.origin + local.velocity * s.interval;
        local.velocity.z -= local.gravity * s.interval * .5f;
        target.origin = target.origin + target.velocity * s.interval;
        if (deadline > 0.f && now > deadline)
        {
            assert(local.origin.z > 0.f); // must still be airborne at impact
            const Vector eye = local.origin + s.eyeOffset;
            const Vector point = InteriorPoint(eye, target.origin + target.mins, target.origin + target.maxs);
            Vector delta = point - eye;
            assert(delta.Squared() > 0.f);
            const Vector ray = delta * (48.f / std::sqrt(delta.Squared()));
            const Vector lo = target.origin + target.mins - Vector{18,18,18};
            const Vector hi = target.origin + target.maxs + Vector{18,18,18};
            float enter = 0.f, leave = 1.f;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float start = axis == 0 ? eye.x : axis == 1 ? eye.y : eye.z;
                const float dir = axis == 0 ? ray.x : axis == 1 ? ray.y : ray.z;
                const float min = axis == 0 ? lo.x : axis == 1 ? lo.y : lo.z;
                const float max = axis == 0 ? hi.x : axis == 1 ? hi.y : hi.z;
                if (std::abs(dir) < .0001f) { assert(start >= min && start <= max); continue; }
                float first = (min-start)/dir, last = (max-start)/dir;
                if (first > last) std::swap(first,last);
                enter = std::max(enter,first); leave = std::min(leave,last);
            }
            assert(enter <= leave); // finite native 48 HU ray + 18 HU hull reaches the victim
            return;
        }
        if (local.origin.z <= 0.f) break;
    }
    assert(false && "reachable garden pass never produced a stable launch window");
}

int main()
{
    GardenPass(Body({0,0,130}, {300,0,-100}), Body({150,0,0}, {}, true));
    GardenPass(Body({0,0,130}, {300,0,-100}), Body({150,0,0}, {0,60,0}, true));
    GardenPass(Body({0,0,150}, {800,0,-100}), Body({350,0,0}, {}, true));
    // Native smack time is strict, not rounded to the nearest Tick. The
    // returned path index includes the current command's movement sample.
    assert(ImpactSample(100, .015f, 1.5f + .2f) == 15);
    assert(ImpactSample(100, .015f, float(114) * .015f) == 16);
    assert(ImpactSample(100, .015f, 1.5f + .25f) == 18);
    assert(ImpactSample(100, .015f, 1.4f) == 1);
    assert(!ImpactSample(100, 0.f, 1.7f));
    assert(!ImpactSample(100, .015f, std::numeric_limits<float>::quiet_NaN()));
    Plan window; window.target = 0; window.hitTick = 14; window.windowTicks = 5; window.endTick = 18;
    assert(!StableStrikeWindow(window, 14) && StableStrikeWindow(window, 15));
    assert(StableStrikeWindow(window, 17) && !StableStrikeWindow(window, 18));
    window.windowTicks = 1; assert(!StableStrikeWindow(window, 15));
    const auto interior = InteriorPoint({100,100,100}, {-24,-24,0}, {24,24,82});
    assert(interior.x == 21.f && interior.y == 21.f && interior.z == 79.f);
    const auto tiny = InteriorPoint({100,0,0}, {-1,-1,0}, {1,1,2});
    assert(tiny.x == 0.f && tiny.y == 0.f && tiny.z == 1.f);
    assert(HullFallback(1.f, false, false));
    assert(!HullFallback(.5f, false, false)); // a ray hitting a wall/player already owns the strike
    assert(!HullFallback(1.f, true, false));
    // A hit confirmation never cancels the windup; a miss needs an observed
    // completed swing and an acknowledgment window. A denied request cancels.
    SwingTracker swing;
    swing.Request(10.f, .2f);
    assert(swing.Observe(10.05f, 10.2f, .015f, .2f) == SwingResult::Waiting);
    swing.ConfirmHit();
    assert(swing.Observe(10.21f, 10.2f, .015f, .2f) == SwingResult::Waiting);
    assert(swing.Observe(10.23f, -1.f, .015f, .2f) == SwingResult::Hit);
    swing.Reset(); swing.Request(20.f, .2f);
    swing.Observe(20.05f, 20.2f, .015f, .2f);
    assert(swing.Observe(20.23f, -1.f, .015f, .2f) == SwingResult::Waiting);
    assert(swing.Observe(20.41f, -1.f, .015f, .2f) == SwingResult::Unconfirmed);
    swing.Reset(); swing.Request(30.f, .2f);
    assert(swing.Observe(30.6f, -1.f, .015f, .2f) == SwingResult::Cancelled);
    swing.Reset(); swing.Request(40.f, .2f); swing.Observe(40.05f, 40.2f, .015f, .2f);
    assert(swing.Observe(42.f, 40.2f, .015f, .2f) == SwingResult::Cancelled);
    swing.Reset(); swing.Request(50.f, .2f); swing.ConfirmHit();
    assert(swing.Observe(50.1f, -1.f, .015f, .2f) == SwingResult::Waiting);
    assert(swing.Observe(50.23f, -1.f, .015f, .2f) == SwingResult::Hit);
    swing.Reset(); swing.Request(60.f, .2f); swing.ConfirmHit();
    assert(swing.Observe(60.23f, 60.3f, .015f, .2f) == SwingResult::Waiting);
    assert(swing.Observe(60.33f, -1.f, .015f, .2f) == SwingResult::Hit);
    swing.Reset(); swing.Request(70.f, .2f);
    assert(swing.Observe(69.f, -1.f, .015f, .2f) == SwingResult::Cancelled);
    int aim = 0, flags = 0; bool shoot = false, restrictAngle = true;
    {
        AimSettingsGuard override(aim, shoot, flags, restrictAngle, true, 3, 1);
        assert(aim == 3 && shoot && flags == 1 && !restrictAngle);
    }
    assert(aim == 0 && !shoot && flags == 0 && restrictAngle);
    // Rotation uses the short path through +/-180, caps acceleration/speed,
    // settles without overshoot and clears invalid input safely.
    float angularVelocity = 0.f;
    float angle = SmoothRotation(179.f, -179.f, .015f, 25.f, false, angularVelocity);
    assert(WrapAngle(angle - 179.f) > 0.f && WrapAngle(angle - 179.f) < 2.f);
    assert(angularVelocity <= 720.f * .015f + .001f);
    angularVelocity = 0.f; angle = 0.f;
    for (int tick = 0; tick < 100; ++tick)
    {
        angle = SmoothRotation(angle, 90.f, .015f, 25.f, false, angularVelocity);
        assert(angle >= 0.f && angle <= 90.f && std::abs(angularVelocity) <= 180.f);
    }
    assert(std::abs(angle - 90.f) < .001f && angularVelocity == 0.f);
    angle = SmoothRotation(angle, 0.f, 0.f, 25.f, false, angularVelocity);
    assert(angle == 90.f && angularVelocity == 0.f);
    assert(SmoothRotation(0.f, 120.f, .015f, 25.f, true, angularVelocity) <= 89.f);
    assert(SmoothRotation(0.f, std::numeric_limits<float>::quiet_NaN(), .015f, 25.f, false, angularVelocity) == 0.f);
    Solver solver; Settings settings; settings.traceBudget = 2048;
    Plan plan; World world; std::array<Actor, MaxTargets> targets{};
    const Actor local = Body({0,0,130}, {300,0,-100});
    targets[0] = Body({150,0,0}, {}, true);
    auto solve = [&](int count = 1, int previous = -1) {
        world.calls = 0;
        const auto before = allocations;
        solver.Solve(local, targets, count, settings, TraceWorld, &world, previous, plan);
        assert(allocations == before);
        assert(world.calls == plan.traces && plan.traces <= settings.traceBudget);
    };
    // The projected speed is capped at 30 HU/s, while gain uses uncapped wishspeed.
    auto speed = Accelerate({}, {1,0,0}, settings);
    assert(std::abs(speed.x - 30.f) < .001f);
    speed = Accelerate({300,0,0}, {-1,0,0}, settings);
    assert(std::abs(speed.x - 264.f) < .001f);
    assert(Accelerate({300,0,0}, {1,0,0}, settings).x == 300.f);
    solve();
    assert(plan.target == 0 && plan.hitTick >= settings.strikeTick);
    Plan insufficientWindow;
    solver.Solve(local, targets, 1, settings, TraceWorld, &world, -1, insufficientWindow, {}, MaxTicks);
    assert(insufficientWindow.target == -1); // an unusable graze must not reserve the nearest victim
    const auto hit = plan.path[plan.hitTick];
    assert(plan.windowTicks > 0 && plan.endTick == plan.hitTick + plan.windowTicks - 1);
    assert(SeparatedXY(Body(hit.local), Body(hit.target)));
    for (int tick = plan.hitTick; tick <= plan.endTick; ++tick)
        assert(SeparatedXY(Body(plan.path[tick].local), Body(plan.path[tick].target)));
    const Vector eye = hit.local + settings.eyeOffset;
    assert((eye - Nearest(eye, hit.target + targets[0].mins, hit.target + targets[0].maxs)).Squared() <= settings.reach * settings.reach);
    // Half gravity integration must give the exact ballistic Z at the recorded Tick.
    const float time = plan.hitTick * settings.interval;
    assert(std::abs(hit.local.z - (local.origin.z + local.velocity.z*time - .5f*local.gravity*time*time)) < .01f);
    assert(hit.local.z > 2.f);
    // A small numerical/trajectory difference must not flip the chosen face.
    world.calls = 0;
    solver.Solve(local, targets, 1, settings, TraceWorld, &world, 0, plan, {0,-58,0});
    assert(plan.target == 0 && plan.side.y < 0.f);
    const Vector retainedSide = plan.side;
    Actor perturbed = local; perturbed.origin.y = .01f;
    world.calls = 0;
    solver.Solve(perturbed, targets, 1, settings, TraceWorld, &world, 0, plan, retainedSide);
    assert(plan.target == 0 && plan.side.Dot(retainedSide) > 0.f);
    // Continuity never forces a blocked face: the opposite pass stays usable.
    world.blockPositiveY = true; world.calls = 0;
    solver.Solve(local, targets, 1, settings, TraceWorld, &world, 0, plan, {0,58,0});
    assert(plan.target == 0 && plan.side.y < 0.f);
    assert(world.calls == plan.traces && plan.traces <= settings.traceBudget);
    world.blockPositiveY = true; solve();
    assert(plan.target == 0 && plan.path[plan.hitTick].local.y < 0.f);
    world.blockPositiveY = false;
    settings.airAcceleration = 0.f;
    const Actor overhead = Body({150,0,130}, {0,0,-100});
    solver.Solve(overhead, targets, 1, settings, TraceWorld, &world, -1, plan);
    assert(plan.target == -1); // Landing on the head is not a side intercept.
    settings.airAcceleration = 10.f;
    targets[0] = Body({150,0,1000}, {}, true); solve(); assert(plan.target == -1);
    targets[1] = Body({160,0,0}, {}, true); solve(2); assert(plan.target == 1);
    // A reachable previous target gets a modest retention bias.
    targets[0] = Body({150,0,0}, {}, true); targets[1] = Body({155,0,0}, {}, true);
    solve(2); assert(plan.target == 0);
    solve(2, 1); assert(plan.target == 1);
    world.wall = true; solve(); assert(plan.target == -1); world.wall = false;
    world.ceiling = true; solve(); assert(plan.target == -1); world.ceiling = false;
    world.invalidTrace = true; solve(); assert(plan.target == -1); world.invalidTrace = false;
    targets[0] = Body({2000,0,0}, {}, true); solve(); assert(plan.target == -1);
    targets[0] = Body({150,0,0}, {0,60,0}, true); solve(); assert(plan.target == 0);
    settings.strikeTick = 60; solve(); assert(plan.target == -1); settings.strikeTick = 14;
    settings.traceBudget = 16; solve(4); assert(world.calls <= 16);
    settings.traceBudget = 128; solve(); assert(plan.target == 0);
    settings.traceBudget = 2048;
    settings.interval = std::numeric_limits<float>::quiet_NaN(); solve(); assert(plan.target == -1 && plan.traces == 0);
    settings.interval = .015f;
    targets[0].gravity = std::numeric_limits<float>::quiet_NaN(); solve(); assert(plan.target == -1 && !world.calls);
    targets[0] = Body({150,0,0}, {}, true); targets[0].mins.x = 50.f; solve(); assert(plan.target == -1 && !world.calls);
    solve(0); assert(plan.target == -1 && !world.calls);
    for (int i = 0; i < MaxTargets; ++i)
        targets[i] = Body({150.f + i * 5.f, 0, 0}, {}, true);
    settings.ticks = MaxTicks;
    solve(MaxTargets); assert(plan.target == 0);
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 256; ++i) solve(MaxTargets);
    const double micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() / 256.;
    std::printf("Four-target/%d-Tick snapshot benchmark: %.1f us/solve, %d traces (synthetic world)\n", MaxTicks, micros, plan.traces);
    // A high blast jump must allow a real intercept later than three seconds,
    // rather than only exposing a larger slider while keeping the old buffers.
    const Actor longJump = Body({0,0,2000}, {0,0,600});
    targets[0] = Body({0,0,0}, {}, true);
    settings.ticks = int(5.f / settings.interval);
    settings.traceBudget = 8192;
    world.calls = 0;
    const auto before = allocations;
    solver.Solve(longJump, targets, 1, settings, TraceWorld, &world, -1, plan);
    assert(allocations == before);
    assert(plan.target == 0 && plan.hitTick * settings.interval > 3.f);
    assert(world.calls == plan.traces && plan.traces <= settings.traceBudget);
    std::puts("Auto Trolldier planner: all regression tests passed (zero allocations per solve)");
}
