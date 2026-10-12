#pragma once
#include <cmath>

// c0000 TAE IDs are category * 1,000,000 + six-digit animation number.
// The installed 1.17 tables contain standing idle 000000 in these nine
// locomotion categories. Do not accept 000000 in unrelated/special tables.
inline bool ear_is_idle_animation(int animation, int configured_idle) {
    if (animation < 0 || configured_idle < 0) return false;
    // Preserve explicit diagnostic overrides and the -1 disable convention.
    if (configured_idle != 0) return animation == configured_idle;
    if (animation % 1000000 != 0) return false;
    switch (animation / 1000000) {
        case 0: case 2: case 3: // one-handed light/heavy/polearm stances
        case 10: case 12: case 13: // two-handed equivalents
        case 14: case 15: case 16: // bow/shield/crossbow stances
            return true;
        default:
            return false;
    }
}

inline bool ear_is_stationary_idle(int animation, int configured_idle,
                                   float horizontal_speed, float vertical_speed) {
    return ear_is_idle_animation(animation, configured_idle) &&
        std::isfinite(horizontal_speed) && horizontal_speed >= 0 && horizontal_speed < .04f &&
        std::isfinite(vertical_speed) && std::abs(vertical_speed) < .04f;
}
