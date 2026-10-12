#pragma once
#include <cstdint>
struct EarFrame {
    float dt,speed,head_accel_right,head_accel_forward,turn_rate;
    uint32_t idle,rolling,paused,enabled,visible;
    // ABI v2 appends inputs; the v1 prefix stays intact. Units: m/s^2, m/s, rad/s.
    float head_accel_up,vertical_speed,head_pitch_rate,head_roll_rate;
};
struct EarPose {
    float left_pitch,left_yaw,left_roll,right_pitch,right_yaw,right_roll;
    uint32_t twitch_count;
};
struct EarMotion;
#ifdef _WIN32
#define UMA_MOTION_API extern "C" __declspec(dllexport)
#else
#define UMA_MOTION_API extern "C"
#endif
UMA_MOTION_API EarMotion* uma_motion_create();
UMA_MOTION_API void uma_motion_destroy(EarMotion*);
UMA_MOTION_API int uma_motion_tick(EarMotion*,const EarFrame*,EarPose*);
UMA_MOTION_API uint32_t uma_motion_abi_version();
