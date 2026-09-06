#pragma once
#include <algorithm>
#include <cmath>

inline double DistanceFogAmount(double distance, double start, double end)
{
    const double amount = std::clamp((distance - start) / (end - start), 0.0, 1.0);
    return amount * amount * (3.0 - 2.0 * amount);
}

inline double WallSurfaceDistance(double leftX, double leftZ, double rightX,
    double rightZ, double centerX, double focalTangent, double screenX)
{
    const double ray = (screenX + 0.5 - centerX) / centerX;
    const double dx = rightX - leftX, dz = rightZ - leftZ;
    const double denominator = dx - ray * dz;
    if (std::abs(denominator) < 1e-12) return 0.0;
    const double fraction = std::clamp((ray * leftZ - leftX) / denominator, 0.0, 1.0);
    const double lateral = leftX + fraction * dx;
    const double forward = (leftZ + fraction * dz) / focalTangent;
    return std::sqrt(lateral * lateral + forward * forward);
}
