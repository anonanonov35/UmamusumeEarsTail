#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cmath>
#include <algorithm>
#include <initializer_list>
#include <cassert>
#include <cstdio>
#include "../src/tail_motion.h"
#include "../src/tail_riding.h"
namespace accepted {
#include "fixtures/accepted_tail_motion.h"
}
namespace released {
#include "fixtures/released_tail_motion_v6.h"
}
namespace previous_test {
#include "fixtures/released_tail_motion_v7.h"
}
static accepted::TailFrame old_frame(const TailFrame& f) {
 accepted::TailFrame g;g.dt=f.dt;g.speed=f.speed;g.enabled=f.enabled;g.paused=f.paused;g.collisions=f.collisions;
 g.anchor={f.anchor.x,f.anchor.y,f.anchor.z};g.body_origin={f.body_origin.x,f.body_origin.y,f.body_origin.z};
 for(int i=0;i<3;i++)g.basis[i]={f.basis[i].x,f.basis[i].y,f.basis[i].z};return g;
}
static TailFrame travel(float t,int fps,float speed) {
 TailFrame f;f.dt=1.f/fps;f.speed=speed;f.collisions=true;
 float ramp=std::clamp(t,0.f,1.f),distance=t<1?speed*t*t/2:speed*(t-.5f);
 f.anchor={.05f*std::sin(t*7)*ramp,.045f*std::sin(t*13)*ramp,-distance};
 float yaw=.09f*std::sin(t*7)*ramp;
 f.basis[0]={std::cos(yaw),0,-std::sin(yaw)};f.basis[2]={std::sin(yaw),0,std::cos(yaw)};return f;
}
int main() {
 setbuf(stdout,nullptr);
 // The accepted walk/run/turn/jump solver must remain numerically exact.
 for(int fps:{30,60,144})for(float speed:{0.f,2.f,5.f,7.f}) {
  TailMotion m;accepted::TailMotion old;
  for(int i=0;i<fps*7;i++) {
   auto f=travel(float(i)/fps,fps,speed);assert(m.tick(f));assert(old.tick(old_frame(f)));
   for(int j=0;j<6;j++){assert(m.position[j].x==old.position[j].x);assert(m.position[j].y==old.position[j].y);assert(m.position[j].z==old.position[j].z);}
   for(int j=0;j<5;j++)for(int k=0;k<3;k++)assert(m.pose.angle[j][k]==old.pose.angle[j][k]);
  }
 }
 double ratio[3]{};int n=0;
 for(int fps:{30,60,144}) {
  TailMotion m;accepted::TailMotion old;double energy=0,old_energy=0;float maximum_step=0;
  TailVector last{},old_last{};
  for(int i=0;i<fps*12;i++) {
   auto f=travel(float(i)/fps,fps,16.f);assert(m.tick(f));assert(old.tick(old_frame(f)));
   auto tip=tail_local(m.previous,m.position[4]);auto q=accepted::tail_local(old.previous,old.position[4]);TailVector old_tip{q.x,q.y,q.z};
   if(i>fps*3) {auto d=tip-last,od=old_tip-old_last;energy+=tail_dot(d,d);old_energy+=tail_dot(od,od);maximum_step=std::max(maximum_step,tail_length(d));}
   last=tip;old_last=old_tip;
   for(int j=1;j<6;j++)assert(std::abs(tail_length(m.position[j]-m.position[j-1])-tail_length(TAIL_REST[j]-TAIL_REST[j-1]))<1e-5f);
  }
  ratio[n++]=std::sqrt(energy/old_energy);
  printf("Fast travel %d FPS: relative tip oscillation %.3f of accepted, max step %.4f m\n",fps,ratio[n-1],maximum_step);
  assert(ratio[n-1]<.8&&ratio[n-1]>.05&&maximum_step<.12f);
  // Riding speed can be absent from the entity module; anchor translation
  // must still activate damping. Pause must not advance that state.
  auto f=travel(12,fps,16);f.speed=0;assert(m.tick(f));assert(m.transport_speed>12);
  float frozen=m.transport_speed;f.paused=true;assert(m.tick(f));assert(m.transport_speed==frozen);
 }
 assert(std::abs(ratio[0]-ratio[1])<.06&&std::abs(ratio[1]-ratio[2])<.06);
 // A uniform boost must carry the chain without consuming its deformation
 // budget. Same rider bob/yaw at 14 and 32 m/s, after transient settling.
 for(int fps:{30,60,144}) {
  TailMotion slow,fast;released::TailMotion prior_slow,prior_fast;
  double error=0,prior_error=0,carried_speed=0;float maximum_error=0;int count=0;
  for(int i=0;i<fps*12;i++) {
   auto a=travel(float(i)/fps,fps,14),b=travel(float(i)/fps,fps,32);
   assert(slow.tick(a)&&fast.tick(b));
   auto convert=[](const TailFrame& f){released::TailFrame g;g.dt=f.dt;g.speed=f.speed;g.collisions=f.collisions;g.anchor={f.anchor.x,f.anchor.y,f.anchor.z};for(int k=0;k<3;k++)g.basis[k]={f.basis[k].x,f.basis[k].y,f.basis[k].z};return g;};
   assert(prior_slow.tick(convert(a))&&prior_fast.tick(convert(b)));
   if(i>fps*6) {
    auto d=slow.position[4]-fast.position[4];float e=tail_length(d);
    auto q=prior_slow.position[4]-prior_fast.position[4];
    error+=e*e;prior_error+=q.x*q.x+q.y*q.y+q.z*q.z;
    maximum_error=std::max(maximum_error,e);carried_speed+=-fast.velocity[4].z;count++;
   }
  }
  double rms=std::sqrt(error/count),prior_rms=std::sqrt(prior_error/count);
  printf("Transport boost %d FPS: RMS %.4f m vs released %.4f m, max %.4f m, carried speed %.2f m/s\n",fps,rms,prior_rms,maximum_error,carried_speed/count);
  assert(rms<.01&&maximum_error<.015&&prior_rms>.08&&carried_speed/count>30);
 }
 // Accelerate, vary gallop speed, decelerate and stop. Irregular frame times
 // exercise interpolation and speed blending without a mounted-state offset.
 for(int fps:{30,60,144})for(bool missing_speed:{false,true}) {
  TailMotion m;float time=0,maximum_tip_speed=0,maximum_tip_step=0;TailVector last{};int count=0;
  while(time<16) {
   float dt=(1.f/fps)*(1+.17f*std::sin(count*1.7f));time+=dt;
   float speed,distance;
   if(time<2){speed=16*time;distance=8*time*time;}
   else if(time<8){float u=time-2;speed=24+8*std::cos(u*3.14159265f/3);distance=32+24*u+24/3.14159265f*std::sin(u*3.14159265f/3);}
   else if(time<11){float u=time-8;speed=16*(1+std::cos(u*3.14159265f/3));distance=176+16*u+48/3.14159265f*std::sin(u*3.14159265f/3);}
   else {speed=0;distance=224;}
   auto f=travel(time,fps,0);f.dt=dt;f.speed=missing_speed?0:speed;
   float bob=std::clamp(speed/8,0.f,1.f);f.anchor={.05f*std::sin(time*7)*bob,.045f*std::sin(time*13)*bob,-distance};
   assert(m.tick(f));auto tip=tail_local(m.previous,m.position[4]);
   // Initial reset uses the authored pose before contact settling.
   if(time>2){maximum_tip_speed=std::max(maximum_tip_speed,tail_length(tip-last)/dt);maximum_tip_step=std::max(maximum_tip_step,tail_length(tip-last));}
   last=tip;count++;
   for(int j=0;j<5;j++)for(int k=0;k<3;k++)assert(std::isfinite(m.pose.angle[j][k]));
   for(int j=1;j<6;j++)assert(std::abs(tail_length(m.position[j]-m.position[j-1])-tail_length(TAIL_REST[j]-TAIL_REST[j-1]))<1e-5f);
  }
  printf("Variable gallop %d FPS, missing speed %d: max relative tip speed %.3f m/s, max step %.4f m\n",fps,missing_speed,maximum_tip_speed,maximum_tip_step);
  // Even the full 32 m/s braking manoeuvre must not snap the tip
  // across a substantial fraction of its 0.82 m length in one frame.
  assert(maximum_tip_step<.15f);
 }
 // Exact installed fast riding clips, including all weapon categories.
 for(int category:{0,2,3,10,12,13,14,15,16}) {
  assert(tail_is_fast_riding_animation(category*1000000+120200));
  assert(tail_is_fast_riding_animation(category*1000000+120205));
  for(int clip:{0,20110,20210,100000,120000,120100,120210})assert(!tail_is_fast_riding_animation(category*1000000+clip));
 }
 for(int id:{-1,1120205,17120205,23120205,999120205})assert(!tail_is_fast_riding_animation(id));
 // Repeated acceleration presses disturb the rider's pelvis/basis without
 // necessarily reporting fast translation. Compare the previous game-tested
 // solver; no reset is allowed when the same acceleration clip restarts.
 for(int fps:{30,60,144})for(bool missing_speed:{false,true}) {
  TailMotion m;previous_test::TailMotion old;double early=0,late=0,old_late=0;int en=0,ln=0;
  float maximum=0,old_maximum=0,maximum_step=0;TailVector last{},old_last{};
  for(int i=0;i<fps*60;i++) {
   float t=float(i)/fps,phase=std::fmod(t,1.f),burst=phase<.35f?std::sin(phase/.35f*3.14159265f):0;
   float pitch=.12f*std::sin(t*14)+.38f*burst;
   TailFrame f;f.dt=1.f/fps;f.collisions=true;f.speed=missing_speed?0:6+5*burst;
   int animation=phase<.35f?120205:120200;
   // Short upper-body/landing interruptions must release smoothly.
   if(phase>.82f&&phase<.90f)animation=120100;
   f.fast_riding=tail_is_fast_riding_animation(animation);
   f.anchor={.025f*std::sin(t*9),.06f*std::sin(t*14),.025f*burst};
   f.basis[1]={0,std::cos(pitch),std::sin(pitch)};f.basis[2]={0,-std::sin(pitch),std::cos(pitch)};
   previous_test::TailFrame g;g.dt=f.dt;g.speed=f.speed;g.collisions=f.collisions;g.anchor={f.anchor.x,f.anchor.y,f.anchor.z};
   for(int k=0;k<3;k++)g.basis[k]={f.basis[k].x,f.basis[k].y,f.basis[k].z};
   assert(m.tick(f)&&old.tick(g));auto tip=tail_local(m.previous,m.position[4]);auto q=previous_test::tail_local(old.previous,old.position[4]);TailVector old_tip{q.x,q.y,q.z};
   auto v=(tip-last)*fps,ov=(old_tip-old_last)*fps;
   if(t>3&&t<10){early+=tail_dot(v,v);en++;}
   if(t>40){late+=tail_dot(v,v);old_late+=tail_dot(ov,ov);ln++;}
   if(t>3){maximum=std::max(maximum,tail_length(tip-TAIL_REST[4]));old_maximum=std::max(old_maximum,tail_length(old_tip-TAIL_REST[4]));maximum_step=std::max(maximum_step,tail_length(tip-last));}
   last=tip;old_last=old_tip;
   for(int j=1;j<6;j++)assert(std::abs(tail_length(m.position[j]-m.position[j-1])-tail_length(TAIL_REST[j]-TAIL_REST[j-1]))<1e-5f);
  }
  double early_rms=std::sqrt(early/en),late_rms=std::sqrt(late/ln),prior_rms=std::sqrt(old_late/ln);
  printf("Repeat gallop %d FPS, missing speed %d: early/late RMS %.3f/%.3f vs v7 %.3f m/s; deflection %.3f vs %.3f m; max step %.4f m\n",fps,missing_speed,early_rms,late_rms,prior_rms,maximum,old_maximum,maximum_step);
  assert(late_rms<early_rms*1.25&&late_rms<prior_rms*.85&&maximum<old_maximum*.75&&maximum<.65f&&maximum_step<.15f);
  float blend=m.riding_weight;auto f=m.latest;f.paused=true;f.fast_riding=false;assert(m.tick(f)&&m.riding_weight==blend);
  f.paused=false;for(int i=0;i<fps*4;i++)assert(m.tick(f));assert(m.riding_weight<.0001f);
 }
 puts("PASS: accepted foot physics exact; fast travel damped continuously at 30/60/144 FPS");
}
