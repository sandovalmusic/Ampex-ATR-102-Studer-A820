#pragma once

// Shared math constants for DSP modules
// Ensures consistent M_PI definition across all platforms

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_2PI
#define M_2PI 6.28318530717958647692
#endif

namespace TapeMachine
{
    // Named constants for common filter values
    constexpr double BUTTERWORTH_Q = 0.7071067811865476;  // 1/sqrt(2)
}
