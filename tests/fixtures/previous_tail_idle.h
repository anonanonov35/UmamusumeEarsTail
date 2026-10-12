#pragma once
#include <cmath>
#include <cstdint>

// Simulation time only: renders, pause and map-origin changes cannot speed up
// this schedule. A short lateral root force launches a wave through the cloth.
struct TailIdleSwish {
    static constexpr double interval=8.0,duration=1.6;
    static constexpr float strength=.44f; // Lateral proximal spring target, metres.
    static constexpr float second_segment_gain=.3f;
    double idle_seconds=0,next_swish=interval,age=-1;
    uint32_t count=0;
    float tick(bool idle,float dt) {
        if(!idle){idle_seconds=0;next_swish=interval;age=-1;return 0;}
        idle_seconds+=dt;
        if(idle_seconds+1e-6>=next_swish) {
            next_swish+=interval;age=0;++count;
        }
        float target=0;
        if(age>=0&&age<duration) {
            double phase=age/duration,pi=3.141592653589793;
            float side=count%2?1.f:-1.f;
            // Zero force and zero slope at both ends. The tip follows the
            // proximal motion with the accepted cloth inertia and damping.
            target=side*strength*float(std::sin(pi*phase)*std::sin(2*pi*phase));
            age+=dt;
            if(age>=duration)age=-1;
        }
        return target;
    }
};
