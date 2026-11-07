#pragma once
#include <algorithm>
#include <cmath>

namespace Trolldier
{
	inline float WrapAngle(float angle)
	{
		return std::remainder(angle, 360.f);
	}
	// Same speed/acceleration limits as NavBot's enemy tracking. Keep a
	// separate state so flight tracking cannot select a different enemy.
	inline float SmoothRotation(float from, float to, float interval, float speed, bool pitch, float& velocity)
	{
		if (!std::isfinite(from) || !std::isfinite(to) || !std::isfinite(interval)
			|| !std::isfinite(speed) || interval <= 0.f || interval > 0.1f)
		{
			velocity = 0.f;
			return std::isfinite(from) ? from : 0.f;
		}
		const float delta = pitch ? to - from : WrapAngle(to - from);
		const float limit = (pitch ? 125.f : 180.f) * std::clamp(speed / 25.f, 0.45f, 2.2f);
		const float acceleration = (pitch ? 520.f : 720.f) * interval;
		if (!std::isfinite(velocity)) velocity = 0.f;
		const float desired = std::clamp(delta / interval, -limit, limit);
		velocity = std::clamp(velocity + std::clamp(desired - velocity, -acceleration, acceleration), -limit, limit);
		float step = velocity * interval;
		if ((step * delta >= 0.f && std::abs(step) >= std::abs(delta)) || std::abs(delta) < 0.001f)
		{
			step = delta;
			velocity = 0.f;
		}
		return pitch ? std::clamp(from + step, -89.f, 89.f) : WrapAngle(from + step);
	}
}
