#pragma once
#include "../../../SDK/SDK.h"
#include "AimPolicy.h"

namespace AimGeometry
{
    inline AimPolicy::Vector Point(const Vec3& value) {return {value.x,value.y,value.z};}
    inline float Entry(const Vec3& eye,const Vec3& direction,const Vec3& mins,const Vec3& maxs,
        const matrix3x4& matrix,float limit)
    {
        return AimPolicy::RayOBB(Point(eye),Point(direction),Point(mins),Point(maxs),matrix,limit);
    }
}
