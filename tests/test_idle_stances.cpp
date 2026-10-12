#include "../src/ear_idle.h"
#include "../src/ear_motion.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

static constexpr int stances[]={0,2000000,3000000,10000000,12000000,
                                13000000,14000000,15000000,16000000};
static void tick(EarMotion* motion, EarPose& pose, int animation, int fps,
                 float speed=0, float vertical=0, bool paused=false) {
    EarFrame frame{};
    frame.dt=1.f/fps; frame.speed=speed; frame.vertical_speed=vertical;
    frame.idle=ear_is_stationary_idle(animation,0,speed,vertical);
    frame.paused=paused; frame.enabled=1; frame.visible=1;
    assert(uma_motion_tick(motion,&frame,&pose)==1);
}
int main() {
    // These are the actual standing-idle families in installed c0000 TAE.
    for(int id:stances) assert(ear_is_idle_animation(id,0));
    assert(!ear_is_idle_animation(-1,0));
    assert(!ear_is_idle_animation(0,-1));
    assert(ear_is_idle_animation(9,9));
    assert(!ear_is_idle_animation(10000009,9));
    // Walk, stop, attack, casting, gestures, hand switch, jump and roll do
    // not become idle even if their root is momentarily stationary.
    const int actions[]={9,30,100,20000,22100,30000,40000,80000,240000,
                         250000,202000,202100,10020000,12300000,14040000,
                         16030000,20000000,999000000,2147483647};
    for(int id:actions) assert(!ear_is_idle_animation(id,0));
    for(int id:stances) {
        assert(!ear_is_stationary_idle(id,0,.04f,0));
        assert(!ear_is_stationary_idle(id,0,0,.04f));
        assert(!ear_is_stationary_idle(id,0,0,-.04f));
        assert(!ear_is_stationary_idle(id,0,-1,0));
        assert(!ear_is_stationary_idle(id,0,std::numeric_limits<float>::quiet_NaN(),0));
    }
    int runs=0;
    for(int fps:{30,60,144}) {
        for(int id:stances) {
            auto motion=uma_motion_create(); assert(motion); EarPose pose{};
            for(int step=0;step<21*fps;step++) {
                tick(motion,pose,id,fps);
                assert(pose.twitch_count==unsigned(((step+1)/double(fps)+1e-5)/10));
            }
            uma_motion_destroy(motion);runs++;
        }
        auto motion=uma_motion_create();EarPose pose{};
        // Weapon categories can change without suppressing the idle timer.
        for(int s=0;s<11*fps;s++)tick(motion,pose,stances[(s/fps)%9],fps);
        assert(pose.twitch_count==1);
        // A real action resets continuous idle; pause must not consume it.
        for(int s=0;s<fps;s++)tick(motion,pose,12300000,fps);
        for(int s=0;s<9*fps;s++)tick(motion,pose,12000000,fps);
        for(int s=0;s<20*fps;s++)tick(motion,pose,12000000,fps,0,0,true);
        assert(pose.twitch_count==1);
        for(int s=0;s<fps;s++)tick(motion,pose,12000000,fps);
        assert(pose.twitch_count==2);
        // Vertical motion and locomotion both reset the next idle interval.
        for(int s=0;s<fps;s++)tick(motion,pose,12000000,fps,0,.2f);
        for(int s=0;s<9*fps;s++)tick(motion,pose,16000000,fps);
        assert(pose.twitch_count==2);
        for(int s=0;s<fps;s++)tick(motion,pose,16000000,fps,1,0);
        for(int s=0;s<9*fps;s++)tick(motion,pose,14000000,fps);
        assert(pose.twitch_count==2);
        for(int s=0;s<fps;s++)tick(motion,pose,14000000,fps);
        assert(pose.twitch_count==3);
        uma_motion_destroy(motion);
    }
    std::printf("PASS: %d idle stance/FPS cases, 10-second trigger, action/movement reset, pause, invalid IDs\n",runs);
}
