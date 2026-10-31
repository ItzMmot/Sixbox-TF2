#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// TF2 / Source units: position in HU, velocity in HU/s, time in seconds.
// This bounded planner operates on snapshots and never changes engine entities.
namespace Trolldier
{
	// Five seconds fit even on a 100 Hz server; buffers remain fixed-size.
	constexpr int MaxTargets = 4, MaxTicks = 512;
	struct Vector
	{
		float x = 0.f, y = 0.f, z = 0.f;
		Vector operator+(Vector b) const { return { x + b.x, y + b.y, z + b.z }; }
		Vector operator-(Vector b) const { return { x - b.x, y - b.y, z - b.z }; }
		Vector operator*(float n) const { return { x * n, y * n, z * n }; }
		float Dot(Vector b) const { return x * b.x + y * b.y + z * b.z; }
		float Squared() const { return Dot(*this); }
		bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
	};
	struct Actor
	{
		Vector origin, velocity, mins, maxs;
		float gravity = 800.f;
		bool grounded = false;
		bool Valid() const
		{
			return origin.Finite() && velocity.Finite() && mins.Finite() && maxs.Finite()
				&& mins.x <= maxs.x && mins.y <= maxs.y && mins.z <= maxs.z
				&& std::isfinite(gravity) && gravity >= 0.f;
		}
	};
	struct Trace
	{
		float fraction = 1.f;
		Vector normal;
		bool solid = false;
	};
	using TraceFn = Trace(*)(void*, Vector, Vector, Vector, Vector, int);
	struct Settings
	{
		float interval = 0.015f, airAcceleration = 10.f, wishSpeed = 240.f;
		float friction = 1.f, airCap = 30.f, reach = 66.f;
		Vector eyeOffset = { 0, 0, 68 };
		int ticks = 64, strikeTick = 14, traceBudget = 1024;
	};
	struct Sample { Vector local, target; };
	struct Plan
	{
		int target = -1, hitTick = 0, windowTicks = 0, endTick = 0, traces = 0;
		Vector wish, side;
		std::array<Sample, MaxTicks + 1> path = {};
	};
	inline Vector Nearest(Vector point, Vector lo, Vector hi)
	{
		return { std::clamp(point.x, lo.x, hi.x), std::clamp(point.y, lo.y, hi.y), std::clamp(point.z, lo.z, hi.z) };
	}
	inline bool SeparatedXY(const Actor& a, const Actor& b, float gap = 6.f)
	{
		return a.origin.x + a.maxs.x + gap <= b.origin.x + b.mins.x
			|| b.origin.x + b.maxs.x + gap <= a.origin.x + a.mins.x
			|| a.origin.y + a.maxs.y + gap <= b.origin.y + b.mins.y
			|| b.origin.y + b.maxs.y + gap <= a.origin.y + a.mins.y;
	}
	inline Vector SideOffset(const Actor& local, const Actor& target, Vector side)
	{
		// Pick a face alongside the approach, accounting for both collision
		// hulls. The local player's feet must pass beside, not over, the victim.
		constexpr float gap = 10.f;
		if (std::abs(side.x) > std::abs(side.y))
			return { side.x >= 0.f ? target.maxs.x - local.mins.x + gap : target.mins.x - local.maxs.x - gap, 0, 0 };
		return { 0, side.y >= 0.f ? target.maxs.y - local.mins.y + gap : target.mins.y - local.maxs.y - gap, 0 };
	}
	inline Vector Accelerate(Vector velocity, Vector direction, const Settings& s)
	{
		// Source caps the projected speed, but uses uncapped wishspeed in the
		// acceleration term: HU/s gained this Tick, not a per-frame speed limit.
		const float available = s.airCap - velocity.Dot(direction);
		const float gain = std::max(0.f, std::min(available, s.airAcceleration * s.wishSpeed * s.interval * s.friction));
		return velocity + direction * gain;
	}
	inline Vector ChooseWish(Vector velocity, Vector desired, const Settings& s)
	{
		static constexpr std::array<Vector, 16> directions = {{
			{1,0,0},{0.9238795f,0.3826834f,0},{0.7071068f,0.7071068f,0},{0.3826834f,0.9238795f,0},
			{0,1,0},{-0.3826834f,0.9238795f,0},{-0.7071068f,0.7071068f,0},{-0.9238795f,0.3826834f,0},
			{-1,0,0},{-0.9238795f,-0.3826834f,0},{-0.7071068f,-0.7071068f,0},{-0.3826834f,-0.9238795f,0},
			{0,-1,0},{0.3826834f,-0.9238795f,0},{0.7071068f,-0.7071068f,0},{0.9238795f,-0.3826834f,0}
		}};
		velocity.z = desired.z = 0.f;
		Vector best;
		float error = (velocity - desired).Squared(); // Coasting is a valid control.
		auto evaluate = [&](Vector direction)
		{
			const float candidate = (Accelerate(velocity, direction, s) - desired).Squared();
			if (candidate < error) { error = candidate; best = direction; }
		};
		for (Vector direction : directions)
			evaluate(direction);
		// Include the optimal strafe directions relative to current velocity;
		// a fixed angle grid alone loses control at rocket-jump speeds.
		const float speed = std::sqrt(velocity.Squared());
		if (speed > 1.f)
		{
			const float projected = std::clamp((s.airCap - s.airAcceleration * s.wishSpeed * s.interval * s.friction) / speed, -1.f, 1.f);
			const float lateral = std::sqrt(std::max(0.f, 1.f - projected * projected));
			const Vector forward = velocity * (1.f / speed), side = { -forward.y, forward.x, 0 };
			evaluate(forward * projected + side * lateral);
			evaluate(forward * projected - side * lateral);
		}
		return best;
	}

	class Solver
	{
		std::array<Actor, MaxTicks + 1> m_target = {};
		std::array<Sample, MaxTicks + 1> m_path = {};
		int m_traces = 0, m_limit = 0;

		bool Cast(TraceFn fn, void* context, const Actor& a, Vector end, int actor, Trace& trace)
		{
			if (m_traces >= m_limit || !end.Finite())
				return false;
			++m_traces;
			trace = fn(context, a.origin, end, a.mins, a.maxs, actor);
			return std::isfinite(trace.fraction) && trace.fraction >= 0.f && trace.fraction <= 1.f && trace.normal.Finite() && !trace.solid;
		}
		bool Step(Actor& a, Vector wish, bool local, int actor, const Settings& s, TraceFn fn, void* context)
		{
			if (!a.grounded)
			{
				a.velocity.z -= a.gravity * s.interval * 0.5f;
				if (local)
					a.velocity = Accelerate(a.velocity, wish, s);
			}
			const Vector end = a.origin + a.velocity * s.interval;
			Trace trace;
			if (!Cast(fn, context, a, end, actor, trace))
				return false;
			a.origin = a.origin + (end - a.origin) * trace.fraction;
			if (trace.fraction < 1.f)
			{
				// Reject local wall/ceiling/landing paths instead of claiming a
				// hit based on an approximate slide or a lost blast-jump state.
				if (local)
					return false;
				if (trace.normal.z >= 0.7f && a.velocity.z <= 0.f)
					a.grounded = true;
				a.velocity = a.velocity - trace.normal * std::min(0.f, a.velocity.Dot(trace.normal));
			}
			if (!a.grounded)
				a.velocity.z -= a.gravity * s.interval * 0.5f;
			if (a.velocity.z <= 140.f)
			{
				const float depth = a.grounded ? 18.f : 2.f;
				if (!Cast(fn, context, a, a.origin + Vector{ 0, 0, -depth }, actor, trace))
					return false;
				const bool ground = trace.fraction < 1.f && trace.normal.z >= 0.7f;
				if (local && ground)
					return false;
				a.grounded = ground;
				if (ground) { a.origin.z -= depth * trace.fraction; a.velocity.z = 0.f; }
			}
			return a.origin.Finite() && a.velocity.Finite();
		}

	public:
		void Solve(const Actor& local, const std::array<Actor, MaxTargets>& targets, int count,
			const Settings& s, TraceFn fn, void* context, int previous, Plan& result, Vector previousSide = {}, int minimumWindow = 1)
		{
			result.target = -1; result.hitTick = result.windowTicks = result.endTick = 0; result.traces = 0; result.wish = {}; m_traces = 0;
			result.side = {};
			if (!fn || !local.Valid() || local.grounded
				|| !std::isfinite(s.interval) || s.interval <= 0.f || s.interval > 0.1f
				|| !std::isfinite(s.reach) || s.reach <= 0.f || s.traceBudget <= 0
				|| !s.eyeOffset.Finite() || !std::isfinite(s.airAcceleration) || s.airAcceleration < 0.f
				|| !std::isfinite(s.wishSpeed) || s.wishSpeed <= 0.f || !std::isfinite(s.friction) || s.friction < 0.f
				|| !std::isfinite(s.airCap) || s.airCap <= 0.f)
				return;
			const int ticks = std::clamp(s.ticks, 1, MaxTicks), strike = std::max(s.strikeTick, 1);
			float bestScore = std::numeric_limits<float>::max();
			for (int target = 0; target < std::clamp(count, 0, MaxTargets); ++target)
			{
				if (!targets[target].Valid())
					continue;
				float score = (targets[target].origin - local.origin).Squared();
				if (target == previous) score *= 0.8f; // Retain a still-reachable target.
				// Distance ranks candidates independently of their eventual hit
				// Tick. Once a nearer path succeeds, farther candidates cannot win.
				if (!std::isfinite(score) || score > bestScore)
					continue;
				// Reserve a fair share for later candidates; an unreachable first
				// target must not consume the entire command's collision budget.
				m_limit = m_traces + (s.traceBudget - m_traces) / (std::clamp(count, 0, MaxTargets) - target);
				// Reserve enough work for at least one local rollout rather than
				// spending a small budget entirely on the enemy trajectory.
				const int candidateTicks = std::min(ticks, (m_limit - m_traces) / 4);
				if (candidateTicks < strike)
					continue;
				m_target[0] = targets[target];
				int validTicks = 0;
				for (int tick = 1; tick <= candidateTicks; ++tick)
				{
					m_target[tick] = m_target[tick - 1];
					if (!Step(m_target[tick], {}, false, target, s, fn, context))
						break;
					validTicks = tick;
				}
				int first = 0, last = 0;
				for (int tick = strike; tick <= validTicks; ++tick)
				{
					const float time = tick * s.interval;
					const float eyeZ = local.origin.z + s.eyeOffset.z + local.velocity.z * time - 0.5f * local.gravity * time * time;
					const auto& a = m_target[tick];
					if (eyeZ >= a.origin.z + a.mins.z - s.reach && eyeZ <= a.origin.z + a.maxs.z + s.reach)
					{
						if (!first) first = tick;
						last = tick;
					}
				}
				if (!first)
					continue;
				const std::array<int, 3> goals = { first, (first + last) / 2, last };
				Vector travel = local.velocity - targets[target].velocity;
				travel.z = 0.f;
				if (travel.Squared() < 1.f) { travel = targets[target].origin - local.origin; travel.z = 0.f; }
				if (travel.Squared() < 1.f) travel = {1,0,0};
				travel = travel * (1.f / std::sqrt(travel.Squared()));
				Vector side = {-travel.y, travel.x, 0};
				const Vector coast = local.origin + local.velocity * (goals[1] * s.interval) - m_target[goals[1]].origin;
				if (coast.Dot(side) < 0.f) side = side * -1.f;
				const bool retainSide = target == previous && previousSide.Finite() && previousSide.Squared() > 1.f;
				if (retainSide) side = previousSide;
				for (int policy = 0; policy < 6 && m_traces < m_limit; ++policy)
				{
					const int goalTick = goals[policy / 2];
					if (policy >= 2 && goalTick == goals[policy / 2 - 1]) continue;
					Actor a = local;
					Vector firstWish;
					const Vector offset = SideOffset(local, targets[target], side * (policy % 2 ? -1.f : 1.f));
					int runStart = 0, runLength = 0, bestStart = 0, bestLength = 0;
					m_path[0] = { a.origin, m_target[0].origin };
					for (int tick = 1; tick <= validTicks; ++tick)
					{
						const int goal = std::max(goalTick, tick);
						Vector delta = m_target[goal].origin + offset - a.origin;
						delta.z = 0.f;
						const float remaining = (goal - tick + 1) * s.interval;
						Vector targetVelocity = m_target[goal].velocity; targetVelocity.z = 0.f;
						// Keep following the victim when the final approach is damped.
						// Dividing the entire future-position delta by 80 ms also
						// damped its velocity, causing late braking on moving targets.
						const Vector desired = targetVelocity
							+ (delta - targetVelocity * remaining) * (1.f / std::max(remaining, 0.08f));
						const Vector wish = ChooseWish(a.velocity, desired, s);
						if (tick == 1) firstWish = wish;
						if (!Step(a, wish, true, -1, s, fn, context)) break;
						const auto& victim = m_target[tick];
						// World-only traces don't see players. Reject a predicted
						// hull collision explicitly, especially landing on a head.
						if (!SeparatedXY(a, victim, 0.f)
							&& a.origin.z + a.mins.z < victim.origin.z + victim.maxs.z + 2.f
							&& a.origin.z + a.maxs.z > victim.origin.z + victim.mins.z) break;
						m_path[tick] = { a.origin, victim.origin };
						const Vector eye = a.origin + s.eyeOffset;
						const Vector point = Nearest(eye, victim.origin + victim.mins, victim.origin + victim.maxs);
						const bool contact = tick >= strike && SeparatedXY(a, victim)
							&& (eye - point).Squared() <= s.reach * s.reach;
						if (!contact) { runLength = 0; continue; }
						if (!runLength) runStart = tick;
						if (++runLength > bestLength) { bestStart = runStart; bestLength = runLength; }
						if (bestLength >= 8) break; // 120 ms at 66 Hz is already a useful strike window.
					}
					// A small continuity bonus prevents left/right oscillation over
					// a one-Tick difference. Blocked paths receive no bonus at all.
					const int rank = bestLength + (retainSide && offset.Dot(previousSide) > 0.f ? 2 : 0);
					const int oldRank = result.windowTicks + (retainSide && result.target == target && result.side.Dot(previousSide) > 0.f ? 2 : 0);
					if (bestLength >= std::clamp(minimumWindow, 1, MaxTicks) && (score < bestScore || (score == bestScore
						&& (rank > oldRank || (rank == oldRank && bestStart < result.hitTick)))))
					{
						bestScore = score; result.target = target; result.hitTick = bestStart; result.windowTicks = bestLength;
						result.endTick = bestStart + bestLength - 1; result.wish = firstWish; result.side = offset;
						std::copy_n(m_path.begin(), result.endTick + 1, result.path.begin());
					}
				}
			}
			result.traces = m_traces;
		}
	};
}
