#pragma once
// Exact installed 1.17 c0000 behavior: RideGallop -> *_120200;
// RideDashAccelerate and RideDashAccelerateContinue -> *_120205.
// Only these fast riding states sustain damping when translational speed
// fluctuates below the speed-only threshold. Idle/walk/dash stay unchanged.
inline bool tail_is_fast_riding_animation(int animation) {
    if(animation<0)return false;
    int clip=animation%1000000;
    if(clip!=120200&&clip!=120205)return false;
    switch(animation/1000000) {
        case 0:case 2:case 3:case 10:case 12:case 13:case 14:case 15:case 16:return true;
        default:return false;
    }
}
