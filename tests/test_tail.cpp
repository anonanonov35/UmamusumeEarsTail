#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/tail_motion.h"
#include "../src/tail_transform.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

float difference(const TailPose& a,const TailPose& b) {
 float d=0;for(int i=0;i<5;i++)for(int j=0;j<3;j++)d=std::max(d,std::abs(a.angle[i][j]-b.angle[i][j]));return d;
}
void check_chain(const TailMotion& m,const TailFrame& f) {
 assert(tail_length(m.position[0])<1e-5f);
 for(int i=1;i<6;i++)assert(std::abs(tail_length(m.position[i]-m.position[i-1])-tail_length(TAIL_REST[i]-TAIL_REST[i-1]))<1e-5f);
 for(auto& bone:m.pose.angle)for(float a:bone)assert(std::isfinite(a)&&std::abs(a)<3.1416f);
 // The FK matrices used in the real hook must reproduce simulated joint
 // locations. This catches incorrect world/local rotation conversion.
 EarMatrix43 fresh[5]{},posed[5]{};
 for(int i=0;i<5;i++) {
  for(int r=0;r<3;r++) {fresh[i].m[r][0]=(&f.basis[0].x)[r];fresh[i].m[r][1]=(&f.basis[1].x)[r];fresh[i].m[r][2]=(&f.basis[2].x)[r];}
  auto p=f.anchor+tail_world(f,TAIL_REST[i]);fresh[i].m[0][3]=p.x;fresh[i].m[1][3]=p.y;fresh[i].m[2][3]=p.z;
 }
 assert(pose_tail_chain(fresh,m.pose.angle,posed));
 for(int i=0;i<5;i++) {
  TailVector p{posed[i].m[0][3],posed[i].m[1][3],posed[i].m[2][3]};
  assert(tail_length(p-f.anchor-tail_world(f,tail_local(m.previous,m.position[i])))<.0003f);
 }
}
TailFrame path(float t,float dt) {
 TailFrame f;f.dt=dt;
 float run=std::clamp(t-1.f,0.f,4.f);
 float envelope=t>1&&t<5?std::pow(std::sin((t-1)*3.14159265f/4),2):0;
 f.anchor={.20f*std::sin(t*2.7f)*envelope,1.03f+.024f*std::sin(t*10.f)*envelope,-run*3};
 f.speed=t>=1&&t<5?3:0;
 float yaw=t>3&&t<4?.9f*std::pow(std::sin((t-3)*3.14159265f),2):0;
 f.basis[0]={std::cos(yaw),0,-std::sin(yaw)};f.basis[2]={std::sin(yaw),0,std::cos(yaw)};
 if(t>4&&t<5)f.anchor.y+=.65f*std::sin((t-4)*3.14159265f);
 return f;
}
int main() {
 setbuf(stdout,nullptr);
 TailPose results[3];float peaks[3]{};int n=0;
 for(int fps:{30,60,144}) {
  TailMotion m;TailFrame f=path(0,1.f/fps);assert(m.tick(f));
  for(int i=1;i<=fps*10;i++) {
   f=path(float(i)/fps,1.f/fps);assert(m.tick(f));check_chain(m,f);
   if(i==fps*4)results[n]=m.pose;
   if(i>fps*5&&i<fps*7)peaks[n]=std::max(peaks[n],std::abs(m.position[4].x));
  }
  TailMotion frozen=m;f.paused=true;f.anchor.x+=1;
  for(int i=0;i<100;i++)assert(m.tick(f));assert(difference(m.pose,frozen.pose)==0&&tail_length(m.position[4]-frozen.position[4])==0);
  f.paused=false;f.dt=std::numeric_limits<float>::quiet_NaN();assert(!m.tick(f));assert(difference(m.pose,frozen.pose)==0);
  f.dt=.016f;f.basis[0]={0,0,0};assert(!m.tick(f));assert(difference(m.pose,frozen.pose)==0);
  f=path(10,.016f);f.anchor.x+=20;assert(m.tick(f));assert(difference(m.pose,TailPose{})<1e-6f);check_chain(m,f);
  f.enabled=false;assert(m.tick(f));assert(!m.initialized&&difference(m.pose,TailPose{})==0);
  n++;
 }
 printf("FPS pose differences %.6f %.6f; post-stop lateral peaks %.4f %.4f %.4f m\n",difference(results[0],results[1]),difference(results[1],results[2]),peaks[0],peaks[1],peaks[2]);
 assert(difference(results[0],results[1])<.035f&&difference(results[1],results[2])<.035f);
 for(float p:peaks)assert(p>.04f);
 assert(std::abs(peaks[0]-peaks[1])<.02f&&std::abs(peaks[1]-peaks[2])<.02f);
 // A single finite root displacement creates a bending wave, not one rigid
 // rotation. The tip follows later and continues moving after the root stops.
 TailMotion impulse,calm;TailFrame f;f.dt=1.f/240;assert(impulse.tick(f));assert(calm.tick(f));
 for(int i=0;i<240*12;i++)assert(calm.tick(f));
 float early_root=0,early_tip=0,late_tip=0,after_stop=0,settled=0;
 for(int i=1;i<=240*12;i++) {
  float t=float(i)/240;f.anchor.x=t<.15f?.10f*t/.15f:.10f;
  assert(impulse.tick(f));check_chain(impulse,f);
  if(i==24){early_root=std::abs(impulse.pose.angle[0][2]);early_tip=std::abs(impulse.pose.angle[3][2]);}
  if(t>.15f&&t<1.5f){late_tip=std::max(late_tip,std::abs(impulse.pose.angle[3][2]));after_stop=std::max(after_stop,tail_length(impulse.velocity[4]));}
  if(t>11)settled=std::max(settled,tail_length(impulse.position[4]-calm.position[4]));
 }
 printf("Wave: early root/tip %.4f/%.4f rad, delayed local tip %.4f rad, post-stop speed %.4f m/s, settled offset %.4f m\n",early_root,early_tip,late_tip,after_stop,settled);
 assert(early_root>.04f&&early_tip<early_root*.6f&&late_tip>early_tip*2&&after_stop>.10f&&settled<.03f&&tail_length(impulse.velocity[4])<.03f);
 // Rotation without translation still leaves the tip behind the pelvis.
 TailMotion turn;f=TailFrame{};f.dt=1.f/120;assert(turn.tick(f));
 for(int i=1;i<=60;i++){float a=1.3f*i/60;f.basis[0]={std::cos(a),0,-std::sin(a)};f.basis[2]={std::sin(a),0,std::cos(a)};assert(turn.tick(f));check_chain(turn,f);}
 auto localtip=tail_local(f,turn.position[4]);printf("Turn lag tip %.4f m\n",localtip.x);assert(std::abs(localtip.x)>.04f);
 // Stress body flips, high-speed motion, low FPS, and a distant world origin.
 TailMotion stress;f=TailFrame{};f.dt=1.f/20;assert(stress.tick(f));
 for(int i=1;i<=600;i++) {
  float t=i*f.dt,a=t*7;f.anchor={t*10,50+std::sin(t*4),-300+std::sin(t*8)*.1f};f.speed=12;
  f.basis[1]={0,std::cos(a),std::sin(a)};f.basis[2]={0,-std::sin(a),std::cos(a)};
  assert(stress.tick(f));check_chain(stress,f);
 }
 // Body collisions retain exact links and keep joints/link interiors outside
 // conservative capsules while the pelvis turns and translates.
 TailMotion collision;f=TailFrame{};f.dt=1.f/60;f.collisions=true;assert(collision.tick(f));
 float penetration=0;
 for(int i=1;i<=60*14;i++) {
  float t=i*f.dt,a=.8f*std::sin(t*2);f.anchor={.15f*std::sin(t*3),.03f*std::sin(t*8),-.5f*t};f.speed=2;
  f.basis[0]={std::cos(a),0,-std::sin(a)};f.basis[2]={std::sin(a),0,std::cos(a)};
  assert(collision.tick(f));check_chain(collision,f);
  if(i>60)for(int j=1;j<5;j++) {
   auto point=f.anchor+tail_world(f,tail_local(collision.previous,collision.position[j]));
   penetration=std::max(penetration,tail_length(TailMotion::contact(f,point,TailMotion::strand_radius(j))));
  }
  if(i>60)for(int j=2;j<5;j++)for(float blend:{.25f,.5f,.75f}) {
   auto offset=collision.position[j-1]*(1-blend)+collision.position[j]*blend;
   auto point=f.anchor+tail_world(f,tail_local(collision.previous,offset));
   penetration=std::max(penetration,tail_length(TailMotion::contact(f,point,TailMotion::strand_radius(j))));
  }
 }
 printf("Body capsule worst joint penetration %.6f m\n",penetration);assert(penetration<.002f);
 puts("PASS: inertial tail wave/turn lag/stop continuation/settling, FPS, pause/reset, finite bounds and exact connected render FK");
}
