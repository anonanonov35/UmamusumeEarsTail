#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include "../src/tail_motion.h"
#include "../src/ear_idle.h"
namespace accepted_v9 {
#include "fixtures/accepted_tail_motion_v9.h"
}
namespace first_idle {
#include "fixtures/first_tail_idle.h"
#include "fixtures/first_idle_tail_motion.h"
}
namespace previous_idle {
#include "fixtures/previous_tail_idle.h"
#include "fixtures/previous_idle_tail_motion.h"
}
static void assert_same(const TailMotion& a,const accepted_v9::TailMotion& b) {
    for(int i=0;i<6;i++) {
        assert(a.position[i].x==b.position[i].x&&a.position[i].y==b.position[i].y&&a.position[i].z==b.position[i].z);
        assert(a.velocity[i].x==b.velocity[i].x&&a.velocity[i].y==b.velocity[i].y&&a.velocity[i].z==b.velocity[i].z);
    }
    for(int i=0;i<5;i++)for(int j=0;j<3;j++)assert(a.pose.angle[i][j]==b.pose.angle[i][j]);
    assert(a.riding_weight==b.riding_weight&&a.transport_speed==b.transport_speed);
}
int main() {
    setbuf(stdout,nullptr);
    for(int fps:{30,60,144}) {
        TailMotion m,calm;first_idle::TailMotion first_motion;previous_idle::TailMotion previous_motion;TailFrame f;f.dt=1.f/fps;f.idle=true;f.collisions=true;
        assert(m.tick(f));
        first_idle::TailFrame first_frame;first_frame.dt=f.dt;first_frame.idle=true;first_frame.collisions=true;assert(first_motion.tick(first_frame));previous_idle::TailFrame previous_frame;previous_frame.dt=f.dt;previous_frame.idle=true;previous_frame.collisions=true;assert(previous_motion.tick(previous_frame));f.idle=false;assert(calm.tick(f));f.idle=true;
        float first=0,peak=0,settled=0,first_peak=0,previous_peak=0;unsigned count=0;
        for(int i=1;i<=fps*26;i++) {
            float t=float(i)/fps;assert(m.tick(f));assert(first_motion.tick(first_frame));assert(previous_motion.tick(previous_frame));auto passive=f;passive.idle=false;assert(calm.tick(passive));
            if(m.idle_swish.count!=count) {
                ++count;assert(count==m.idle_swish.count);assert(std::abs(t-count*8.f)<=f.dt+.001f);
                if(count==1)first=t;
            }
            float difference=tail_length(m.position[4]-calm.position[4]);
            if(t<7.99f)assert(difference==0);
            if(t>8&&t<11){peak=std::max(peak,difference);auto p=first_motion.position[4];first_peak=std::max(first_peak,tail_length(TailVector{p.x,p.y,p.z}-calm.position[4]));auto q=previous_motion.position[4];previous_peak=std::max(previous_peak,tail_length(TailVector{q.x,q.y,q.z}-calm.position[4]));}
            if(t>15&&t<15.9f)settled=std::max(settled,difference);
            for(int j=1;j<6;j++)assert(std::abs(tail_length(m.position[j]-m.position[j-1])-tail_length(TAIL_REST[j]-TAIL_REST[j-1]))<1e-5f);
        }
        printf("Idle %d FPS: first %.4fs, count %u, tip deflection %.4fm, pre-next residual %.4fm\n",fps,first,count,peak,settled);
        printf("Stronger swish: first %.4fm -> current %.4fm, ratio %.3f; drive strength %.2f\n",first_peak,peak,peak/first_peak,TailIdleSwish::strength);
        printf("Additional 50 percent: previous %.4fm -> current %.4fm, ratio %.3f\n",previous_peak,peak,peak/previous_peak);
        assert(peak/previous_peak>1.45f&&peak/previous_peak<1.55f);
        assert(count==3&&peak>.06f&&peak<.7f&&settled<.06f);
        auto frozen=m;f.paused=true;for(int i=0;i<fps*12;i++)assert(m.tick(f));
        assert(m.idle_swish.idle_seconds==frozen.idle_swish.idle_seconds&&m.idle_swish.count==frozen.idle_swish.count);
        f.paused=false;f.idle=false;assert(m.tick(f));assert(m.idle_swish.idle_seconds==0&&m.idle_swish.age<0);
        auto before=m.idle_swish.count;f.idle=true;
        for(int i=0;i<fps*7;i++)assert(m.tick(f));assert(m.idle_swish.count==before);
        for(int i=0;i<fps;i++)assert(m.tick(f));assert(m.idle_swish.count==before+1);
        auto timer=m.idle_swish.idle_seconds;m.rebase({32,-8,16});assert(m.idle_swish.idle_seconds==timer);
        f.anchor={32,-8,16};assert(m.tick(f));assert(m.idle_swish.count==before+1);
        for(int i=0;i<fps/2;i++)assert(m.tick(f));
        auto active=m;assert(m.idle_swish.age>=0);f.paused=true;
        for(int i=0;i<fps*5;i++)assert(m.tick(f));
        assert(m.idle_swish.age==active.idle_swish.age&&m.idle_swish.idle_seconds==active.idle_swish.idle_seconds);
        assert(tail_length(m.position[4]-active.position[4])==0);
        f.paused=false;f.idle=false;auto previous_tip=m.position[4];assert(m.tick(f));
        assert(m.idle_swish.age<0&&tail_length(m.position[4]-previous_tip)<.08f);
        f.idle=true;f.fast_riding=true;for(int i=0;i<fps*9;i++)assert(m.tick(f));assert(m.idle_swish.count==before+1);
        f.enabled=false;assert(m.tick(f));assert(m.idle_swish.count==0&&!m.initialized);
    }
    // Accepted v9 integration is bit-exact when no idle swish is requested,
    // including travelling, collisions, riding and variable weapon stance.
    for(int fps:{30,60,144})for(bool riding:{false,true}) {
        TailMotion m;accepted_v9::TailMotion old;
        for(int i=0;i<fps*20;i++) {
            float t=float(i)/fps;TailFrame f;f.dt=1.f/fps;f.speed=riding?11:3;f.collisions=true;f.fast_riding=riding;
            f.anchor={.08f*std::sin(t*3),.035f*std::sin(t*12),-t*f.speed};
            accepted_v9::TailFrame g;g.dt=f.dt;g.speed=f.speed;g.collisions=f.collisions;g.fast_riding=riding;g.anchor={f.anchor.x,f.anchor.y,f.anchor.z};
            assert(m.tick(f)&&old.tick(g));assert_same(m,old);
        }
    }
    for(int category:{0,2,3,10,12,13,14,15,16})assert(ear_is_stationary_idle(category*1000000,0,0,0));
    for(int animation:{100000,120000,120200,120205,202000,10010000})assert(!ear_is_stationary_idle(animation,0,0,0));
    assert(!ear_is_stationary_idle(0,0,.05f,0)&&!ear_is_stationary_idle(0,0,0,.1f));
    puts("PASS: 8-second stationary swish, pause/movement/reset/rebase, all idle weapon categories, accepted v9 movement bit-exact");
}
