// Pure cosmetic spring simulation. The adapter supplies validated head motion.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include "ear_motion.h"

struct Spring {
    double position = 0;
    double velocity = 0;

    void step(double target, double dt) {
        constexpr double omega = 17.0;
        constexpr double damping = 0.62;
        velocity += (omega * omega * (target - position)
                     - 2.0 * damping * omega * velocity) * dt;
        position += velocity * dt;
        position = std::clamp(position, -0.55, 0.55);
    }
};

struct InertiaSpring {
    double position = 0, velocity = 0;
    void step(double target,double dt,double limit) {
        constexpr double omega=18.0,damping=.62;
        velocity+=(omega*omega*(target-position)-2*damping*omega*velocity)*dt;
        position+=velocity*dt;
        if(position>limit) {position=limit;if(velocity>0)velocity=0;}
        if(position<-limit) {position=-limit;if(velocity<0)velocity=0;}
    }
};

struct EarMotion {
    static constexpr double physics_intensity=3.75;
    // Scale the physical envelope with forces so caps do not erase the +25%.
    static constexpr double physics_envelope=physics_intensity/3.0;
    static constexpr double pitch_limit=.50*physics_envelope;
    static constexpr double yaw_limit=.36*physics_envelope;
    static constexpr double roll_limit=.42*physics_envelope;
    // The accepted idle twitch keeps its original spring and timing.
    Spring yaw[2], roll[2];
    InertiaSpring physics_pitch[2], physics_yaw[2], physics_roll[2];
    double idle_seconds = 0;
    double twitch_age = -1;
    double next_twitch = 10;
    uint32_t twitch_count = 0;
    bool was_rolling = false;

    static double finite_clamp(float n, double low, double high) {
        return std::isfinite(n) ? std::clamp(double(n), low, high) : 0;
    }

    void tick(const EarFrame& frame, EarPose& output) {
        if (!frame.enabled) {
            *this = EarMotion{};
            output = EarPose{};
            return;
        }
        double dt = finite_clamp(frame.dt, 0, 0.25);
        double speed = finite_clamp(frame.speed, 0, 20);
        if (!frame.paused && dt > 0) {
            bool idle = frame.idle && !frame.rolling && speed < 0.04;
            if (idle) {
                idle_seconds += dt;
                if (idle_seconds + 1e-6 >= next_twitch) {
                    next_twitch += 10;
                    twitch_age = 0;
                    ++twitch_count;
                }
            } else {
                idle_seconds = 0;
                next_twitch = 10;
            }
            if (frame.rolling && !was_rolling) {
                // Brief inertial lag as the body starts tumbling, then free return.
                physics_pitch[0].velocity += 3.8*physics_intensity;
                physics_pitch[1].velocity += 3.8*physics_intensity;
                physics_roll[0].velocity += .65*physics_intensity;
                physics_roll[1].velocity -= .65*physics_intensity;
            }
            was_rolling = frame.rolling;
            double forward=finite_clamp(frame.head_accel_forward,-40,40);
            double lateral=finite_clamp(frame.head_accel_right,-40,40);
            double up=finite_clamp(frame.head_accel_up,-60,60);
            double vertical=finite_clamp(frame.vertical_speed,-15,15);
            double pitch_rate=finite_clamp(frame.head_pitch_rate,-12,12);
            double roll_rate=finite_clamp(frame.head_roll_rate,-12,12);
            double turn=finite_clamp(frame.turn_rate,-12,12);
            // +pitch tips an upright ear backwards; +roll tips it left.
            // Translational inertia opposes acceleration; angular drag lags the head.
            double pitch_target=std::clamp((forward*.010-up*.010-pitch_rate*.028
                                         -vertical*.008+std::min(speed,8.0)*.009)*physics_intensity,-pitch_limit,pitch_limit);
            double yaw_target=std::clamp(-turn*.026*physics_intensity,-yaw_limit,yaw_limit);
            int steps = std::max(1, int(std::ceil(dt * 120.0)));
            double h = dt / steps;
            for (int step = 0; step < steps; ++step) {
                if (twitch_age >= 0) {
                    twitch_age += h;
                    if (twitch_age > 0.85) twitch_age = -1;
                }
                for (int side = 0; side < 2; ++side) {
                    double mirrored = side == 0 ? -1.0 : 1.0;
                    double twitch = 0;
                    double age = twitch_age - side * 0.045;
                    if (twitch_age >= 0 && age >= 0 && age < 0.8) {
                        twitch = std::sin(age * 24.0) * std::exp(-age * 6.0) * 0.84;
                    }
                    yaw[side].step(mirrored*twitch,h);
                    roll[side].step(mirrored*twitch*.60,h);
                    double roll_target=std::clamp((lateral*.008-roll_rate*.024-mirrored*up*.0035)*physics_intensity,-roll_limit,roll_limit);
                    physics_pitch[side].step(pitch_target,h,pitch_limit);
                    physics_yaw[side].step(yaw_target,h,yaw_limit);
                    physics_roll[side].step(roll_target,h,roll_limit);
                }
            }
        }
        output = EarPose{};
        output.twitch_count = twitch_count;
        if (frame.visible) {
            output.left_pitch = float(physics_pitch[0].position);
            output.left_yaw = float(std::clamp(yaw[0].position+physics_yaw[0].position,-.55,.55));
            output.left_roll = float(std::clamp(roll[0].position+physics_roll[0].position,-.55,.55));
            output.right_pitch = float(physics_pitch[1].position);
            output.right_yaw = float(std::clamp(yaw[1].position+physics_yaw[1].position,-.55,.55));
            output.right_roll = float(std::clamp(roll[1].position+physics_roll[1].position,-.55,.55));
        }
    }
};

UMA_MOTION_API EarMotion* uma_motion_create() {
    return new (std::nothrow) EarMotion{};
}
UMA_MOTION_API void uma_motion_destroy(EarMotion* motion) {delete motion;}
UMA_MOTION_API int uma_motion_tick(EarMotion* motion,const EarFrame* frame,EarPose* pose) {
    if(!motion||!frame||!pose)return 0;
    motion->tick(*frame,*pose);return 1;
}
UMA_MOTION_API uint32_t uma_motion_abi_version() {return 2;}
