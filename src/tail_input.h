#pragma once
#include "tail_motion.h"

// A current ChrTaeAnimCtrl queue entry is reused by all palette refreshes
// within one actor update. Its ring index and time pair identify that update.
struct TailUpdateStamp {
    unsigned index=0;
    int animation=-1;
    float start=0,end=0;
    bool valid=false;
};
struct TailInputResult {
    bool advance=true;
    float dt=0;
    TailVector origin_shift{};
};
struct TailInputTracker {
    TailUpdateStamp previous_stamp{};
    TailVector physics{},anchor{};
    bool have_sample=false;
    unsigned duplicates=0,rebases=0;

    TailInputResult sample(const TailUpdateStamp& stamp,float dt,TailVector root,TailVector tail) {
        TailInputResult result;result.dt=dt;
        if(dt==0){result.advance=false;return result;}
        if(have_sample&&stamp.valid&&previous_stamp.valid&&stamp.index==previous_stamp.index&&
           stamp.animation==previous_stamp.animation&&stamp.start==previous_stamp.start&&stamp.end==previous_stamp.end) {
            ++duplicates;result.advance=false;return result;
        }
        if(have_sample) {
            auto delta=root-physics;
            // Recorded map-origin translations are common 8-metre lattice
            // shifts (32m horizontally). Require matching tail/root changes
            // and a small physically plausible remainder, never arbitrary
            // discontinuity smoothing. Genuine other teleports still reset.
            auto shift=TailVector{std::round(delta.x/8.f)*8.f,std::round(delta.y/8.f)*8.f,std::round(delta.z/8.f)*8.f};
            float budget=std::min(1.5f,.15f+40.f*result.dt);
            if(tail_length(shift)>=8.f&&tail_length(delta-shift)<=budget&&
               tail_length((tail-anchor)-delta)<.25f) {
                result.origin_shift=shift;++rebases;
            }
        }
        physics=root;anchor=tail;previous_stamp=stamp;have_sample=true;
        return result;
    }
};
