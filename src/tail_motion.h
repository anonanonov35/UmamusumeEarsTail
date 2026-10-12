#pragma once
#include <cmath>
#include <algorithm>
#include <initializer_list>
#include "tail_idle.h"

struct TailVector {
    float x=0,y=0,z=0;
    TailVector operator+(TailVector b) const {return {x+b.x,y+b.y,z+b.z};}
    TailVector operator-(TailVector b) const {return {x-b.x,y-b.y,z-b.z};}
    TailVector operator*(float s) const {return {x*s,y*s,z*s};}
};
inline float tail_dot(TailVector a,TailVector b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline TailVector tail_cross(TailVector a,TailVector b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float tail_length(TailVector a) {return std::sqrt(tail_dot(a,a));}
inline TailVector tail_unit(TailVector a) {float n=tail_length(a);return n>1e-7f?a*(1/n):TailVector{};}

struct TailFrame {
    float dt=0,speed=0;
    // The actual fresh Tail_0 transform. Particles retain world-space inertia
    // when the pelvis translates or rotates; no animation/weapon whitelist.
    TailVector anchor{};
    TailVector basis[3]={{1,0,0},{0,1,0},{0,0,1}};
    TailVector body_origin{0,-.06f,-.115f};
    bool enabled=true,paused=false,collisions=false,fast_riding=false,idle=false;
};
struct TailPose {float angle[5][3]{};};
// Accepted bind-chain coordinates relative to Tail_0, in metres. The final
// virtual particle gives the last skinned bone a free tip direction.
inline constexpr TailVector TAIL_REST[6]={
    {0,0,0},{0,-.1829854f,.0433151f},{0,-.3659708f,.0930077f},
    {0,-.5489562f,.1267344f},{0,-.7319416f,.1694614f},{0,-.8219416f,.1904754f}
};
inline TailVector tail_world(const TailFrame& f,TailVector local) {
    return f.basis[0]*local.x+f.basis[1]*local.y+f.basis[2]*local.z;
}
inline TailVector tail_local(const TailFrame& f,TailVector world) {
    return {tail_dot(world,f.basis[0]),tail_dot(world,f.basis[1]),tail_dot(world,f.basis[2])};
}
inline TailVector tail_cone(TailVector direction,TailVector axis,float limit) {
    direction=tail_unit(direction);axis=tail_unit(axis);
    float cosine=std::clamp(tail_dot(direction,axis),-1.f,1.f);
    if(cosine>=std::cos(limit))return direction;
    auto side=tail_unit(direction-axis*cosine);
    if(tail_length(side)<.1f)side=tail_unit(tail_cross(axis,{1,0,0}));
    return axis*std::cos(limit)+side*std::sin(limit);
}

inline constexpr float TAIL_MASS_MULTIPLIER=2.5f;
inline constexpr float TAIL_AIR_DRAG=.68f;
// Foot travel retains the accepted solver. Fast transport (e.g. Torrent)
// blends in drag relative to the moving root, rather than world-space drag
// which would pull the whole chain backwards at a constant high speed.
inline constexpr float TAIL_FAST_START=8.f,TAIL_FAST_FULL=12.f;
inline constexpr float TAIL_FAST_DRAG=7.f;

struct TailMotion {
    TailPose pose{};
    TailIdleSwish idle_swish{};
    // World-axis particle offsets relative to the latest anchor: a floating
    // origin avoids loss of sub-millimetre motion far from the map origin.
    TailVector position[6]{},velocity[6]{};
    TailFrame previous{},latest{};
    double remainder=0;
    float transport_speed=0,riding_weight=0;
    TailVector transport_velocity{};
    bool initialized=false;

    void reset(const TailFrame& f) {
        pose=TailPose{};idle_swish=TailIdleSwish{};
        for(int i=0;i<6;i++){position[i]=tail_world(f,TAIL_REST[i]);velocity[i]={};}
        previous=latest=f;remainder=0;transport_speed=0;riding_weight=0;transport_velocity={};initialized=true;
    }
    // Particle positions already use offsets from the anchor. A change of
    // the game's map origin only translates the two interpolation anchors;
    // velocities, cloth pose and persistent riding damping remain intact.
    void rebase(TailVector shift) {
        if(!initialized)return;
        previous.anchor=previous.anchor+shift;latest.anchor=latest.anchor+shift;
    }
    // Conservative pelvis/leg capsules in the authored body axes. These
    // cover the lower-body volume without reading any unverified leg offsets.
    static TailVector contact(const TailFrame& f,TailVector point,float radius) {
        if(!f.collisions)return {};
        auto local=tail_local(f,point-f.anchor)-f.body_origin;
        TailVector original=local;
        const TailVector start[3]={{0,.08f,0},{-.09f,-.07f,0},{.09f,-.07f,0}};
        const TailVector end[3]={{0,-.17f,0},{-.09f,-.84f,0},{.09f,-.84f,0}};
        const float body_radius[3]={.135f,.1305f,.1305f}; // 10% narrower than the accepted first contact test.
        for(int capsule=0;capsule<3;capsule++) {
            auto axis=end[capsule]-start[capsule];
            float t=std::clamp(tail_dot(local-start[capsule],axis)/tail_dot(axis,axis),0.f,1.f);
            auto nearest=start[capsule]+axis*t,delta=local-nearest;
            float distance=tail_length(delta),required=body_radius[capsule]+radius;
            if(distance<required) {
                // Exactly on an axis: choose the rear surface rather than
                // pushing the strand forward through the character.
                auto normal=distance>1e-6f?delta*(1/distance):TailVector{0,0,1};
                local=nearest+normal*required;
            }
        }
        return tail_world(f,local-original);
    }
    // Measured mesh radii are 59..85 mm; include a small skinning margin.
    static float strand_radius(int i) {return i<2?.085f:i<4?.090f:.085f;}
    void make_pose(const TailFrame& f) {
        float parent[3][3]={{1,0,0},{0,1,0},{0,0,1}};
        for(int i=0;i<5;i++) {
            auto a=tail_unit(TAIL_REST[i+1]-TAIL_REST[i]);
            auto b=tail_unit(tail_local(f,position[i+1]-position[i]));
            auto v=tail_cross(a,b);float c=std::clamp(tail_dot(a,b),-1.f,1.f);
            // Shortest-arc swing preserves the authored surface, without adding
            // twist. Cones keep c away from the antiparallel singularity.
            float k[3][3]={{0,-v.z,v.y},{v.z,0,-v.x},{-v.y,v.x,0}},rotation[3][3]{},local[3][3]{};
            for(int r=0;r<3;r++)for(int s=0;s<3;s++) {
                rotation[r][s]=(r==s?1.f:0.f)+k[r][s];
                for(int t=0;t<3;t++)rotation[r][s]+=k[r][t]*k[t][s]/std::max(1+c,.001f);
            }
            for(int r=0;r<3;r++)for(int s=0;s<3;s++)for(int t=0;t<3;t++)local[r][s]+=parent[t][r]*rotation[t][s];
            pose.angle[i][0]=std::atan2(local[2][1],local[2][2]);
            pose.angle[i][1]=std::asin(std::clamp(-local[2][0],-1.f,1.f));
            pose.angle[i][2]=std::atan2(local[1][0],local[0][0]);
            for(int r=0;r<3;r++)for(int s=0;s<3;s++)parent[r][s]=rotation[r][s];
        }
    }
    bool tick(const TailFrame& f) {
        if(!std::isfinite(f.dt)||!std::isfinite(f.speed)||f.dt<0||f.dt>.25f)return false;
        const TailVector inputs[]={f.anchor,f.basis[0],f.basis[1],f.basis[2],f.body_origin};
        for(auto v:inputs)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return false;
        for(int i=0;i<3;i++) {
            if(std::abs(tail_length(f.basis[i])-1)>.03f)return false;
            for(int j=0;j<i;j++)if(std::abs(tail_dot(f.basis[i],f.basis[j]))>.03f)return false;
        }
        if(tail_dot(tail_cross(f.basis[0],f.basis[1]),f.basis[2])<.95f)return false;
        if(!f.enabled){*this=TailMotion{};return true;}
        if(f.paused||f.dt==0)return true;
        if(!initialized||tail_length(f.anchor-latest.anchor)>2.f){reset(f);return true;}

        // Substepped XPBD chain: exact-length links, compliant bending, free
        // distal particles. Constraints transmit motion; no sinusoid forces
        // every bone into a predetermined pose.
        constexpr double fixed_step=1.0/240;
        int steps=int((remainder+double(f.dt)+1e-9)/fixed_step);float dt=float(fixed_step);
        const float inverse_mass[6]={0,1/TAIL_MASS_MULTIPLIER,1.15f/TAIL_MASS_MULTIPLIER,1.35f/TAIL_MASS_MULTIPLIER,1.6f/TAIL_MASS_MULTIPLIER,1.9f/TAIL_MASS_MULTIPLIER};
        float air_drag=TAIL_AIR_DRAG+std::clamp(f.speed,0.f,12.f)*.055f;
        for(int step=0;step<steps;step++) {
            float blend=std::clamp(float((fixed_step-remainder+step*fixed_step)/f.dt),0.f,1.f);
            TailFrame frame=f;auto anchor=latest.anchor*(1-blend)+f.anchor*blend;
            auto movement=anchor-previous.anchor;frame.anchor=movement;
            float measured=std::sqrt(movement.x*movement.x+movement.z*movement.z)/dt;
            float target=std::max(std::max(0.f,f.speed),measured);
            transport_speed+=(target-transport_speed)*(1-std::exp(-dt*4.f));
            float fast=std::clamp((transport_speed-TAIL_FAST_START)/(TAIL_FAST_FULL-TAIL_FAST_START),0.f,1.f);
            fast=fast*fast*(3-2*fast);
            // Gallop and repeated acceleration use the same persistent blend.
            // Speed alone drops below the fast threshold during these clips;
            // it must not repeatedly remove damping and pump the chain.
            riding_weight+=((f.fast_riding?1.f:0.f)-riding_weight)*(1-std::exp(-dt*(f.fast_riding?4.f:3.f)));
            fast=std::max(fast,riding_weight);
            // Riding translation is a reference velocity, not a deformation.
            // Filter the full root velocity so gallop/frame sampling noise is
            // not injected directly into the drag force.
            auto measured_velocity=movement*(1/dt);
            transport_velocity=transport_velocity+(measured_velocity-transport_velocity)*(1-std::exp(-dt*8.f));
            auto carried_velocity=transport_velocity*fast;
            frame.basis[0]=tail_unit(latest.basis[0]*(1-blend)+f.basis[0]*blend);
            auto up=latest.basis[1]*(1-blend)+f.basis[1]*blend;
            frame.basis[1]=tail_unit(up-frame.basis[0]*tail_dot(up,frame.basis[0]));
            frame.basis[2]=tail_unit(tail_cross(frame.basis[0],frame.basis[1]));
            float idle_target=idle_swish.tick(f.idle&&!f.fast_riding&&f.speed<.04f,dt);
            TailVector before[6],bend_lambda[5]{};
            for(int i=0;i<6;i++)before[i]=position[i];
            // World gravity makes the distal chain hang and swing naturally.
            // Only the proximal segment has a shape spring; the tip is free.
            auto gravity=TailVector{0,-3.8f,0};
            for(int i=1;i<6;i++) {
                auto acceleration=gravity;
                if(i==1)acceleration=acceleration+(frame.anchor+tail_world(frame,TAIL_REST[1])-position[i])*(70.f/TAIL_MASS_MULTIPLIER);
                if(i==1&&idle_target!=0)acceleration=acceleration+tail_world(frame,{idle_target*(70.f/TAIL_MASS_MULTIPLIER),0,0});
                if(i==2&&idle_target!=0)acceleration=acceleration+tail_world(frame,{idle_target*(TailIdleSwish::second_segment_gain*70.f/TAIL_MASS_MULTIPLIER),0,0});
                if(fast>0) {
                    // Damping and safety limits must use the same moving
                    // reference. Otherwise constant vehicle speed consumes the
                    // velocity budget and clips each particle at every step.
                    velocity[i]=carried_velocity+(velocity[i]-carried_velocity+acceleration*dt)*std::exp(-air_drag*dt);
                    velocity[i]=transport_velocity+(velocity[i]-transport_velocity)*std::exp(-TAIL_FAST_DRAG*fast*dt);
                } else velocity[i]=(velocity[i]+acceleration*dt)*std::exp(-air_drag*dt);
                position[i]=position[i]+velocity[i]*dt;
            }
            position[0]=frame.anchor;
            auto lengths=[&](bool reverse) {
                for(int n=1;n<6;n++) {
                    int i=reverse?6-n:n;auto delta=position[i]-position[i-1];float length=tail_length(delta);
                    if(length<1e-6f)delta=tail_world(frame,TAIL_REST[i]-TAIL_REST[i-1]),length=tail_length(delta);
                    float rest=tail_length(TAIL_REST[i]-TAIL_REST[i-1]);
                    auto correction=delta*((length-rest)/(length*(inverse_mass[i]+inverse_mass[i-1])));
                    position[i-1]=position[i-1]+correction*inverse_mass[i-1];
                    position[i]=position[i]-correction*inverse_mass[i];
                }
            };
            for(int iteration=0;iteration<10;iteration++) {
                for(int i=1;i<5;i++) {
                    auto rest=tail_world(frame,TAIL_REST[i-1]-TAIL_REST[i]*2+TAIL_REST[i+1]);
                    auto error=position[i-1]-position[i]*2+position[i+1]-rest;
                    float alpha=(.035f+(i-1)*.025f)/(dt*dt);
                    auto lambda=(error*(-1)-bend_lambda[i]*alpha)*(1/(inverse_mass[i-1]+4*inverse_mass[i]+inverse_mass[i+1]+alpha));
                    bend_lambda[i]=bend_lambda[i]+lambda;
                    position[i-1]=position[i-1]+lambda*inverse_mass[i-1];
                    position[i]=position[i]-lambda*(2*inverse_mass[i]);
                    position[i+1]=position[i+1]+lambda*inverse_mass[i+1];
                }
                lengths(iteration%2!=0);
                for(int i=1;i<6;i++)position[i]=position[i]+contact(frame,position[i],strand_radius(i));
                // Skinning spans the links too. Check their interiors, not just
                // joints, and distribute a contact correction to both ends.
                for(int i=2;i<6;i++)for(float t:{.25f,.5f,.75f}) {
                    auto point=position[i-1]*(1-t)+position[i]*t;
                    auto correction=contact(frame,point,strand_radius(i));
                    float divisor=inverse_mass[i-1]*(1-t)*(1-t)+inverse_mass[i]*t*t;
                    position[i-1]=position[i-1]+correction*(inverse_mass[i-1]*(1-t)/divisor);
                    position[i]=position[i]+correction*(inverse_mass[i]*t/divisor);
                }
            }
            // Exact final link lengths and bounded curvature. This also limits
            // implausible inversions under a roll/impact without moving the root.
            TailVector previous_direction{};
            for(int i=1;i<6;i++) {
                auto rest=tail_world(frame,TAIL_REST[i]-TAIL_REST[i-1]);
                auto direction=tail_cone(position[i]-position[i-1],rest,i==1?1.22f:1.65f);
                if(i>1)direction=tail_cone(direction,previous_direction,.85f);
                position[i]=position[i-1]+direction*tail_length(rest);
                for(int contact_iteration=0;frame.collisions&&contact_iteration<24;contact_iteration++) {
                    auto correction=contact(frame,position[i],strand_radius(i));
                    if(tail_length(correction)<1e-5f)break;
                    direction=tail_unit(position[i]+correction-position[i-1]);
                    position[i]=position[i-1]+direction*tail_length(rest);
                }
                velocity[i]=(position[i]-before[i])*(1/dt);
                if(fast>0) {
                    auto relative_velocity=velocity[i]-carried_velocity;
                    float magnitude=tail_length(relative_velocity);
                    if(magnitude>18.f)velocity[i]=carried_velocity+relative_velocity*(18.f/magnitude);
                } else {
                    float magnitude=tail_length(velocity[i]);if(magnitude>18.f)velocity[i]=velocity[i]*(18.f/magnitude);
                }
                previous_direction=direction;
            }
            for(auto& particle:position)particle=particle-movement;
            frame.anchor=anchor;previous=frame;
        }
        remainder=std::max(0.0,remainder+double(f.dt)-steps*fixed_step);
        latest=f;make_pose(previous);return true;
    }
};
