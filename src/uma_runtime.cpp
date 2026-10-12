// ModEngine2 appearance extension with a guarded private ear-palette adapter.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "ear_motion.h"
#include "ear_idle.h"
#include "ear_transform.h"
#include "ear_settings.h"
#include "tail_motion.h"
#include "tail_riding.h"
#include "tail_input.h"
#include "tail_transform.h"
#include "MinHook.h"
#include "native_guards.h"
#include "singleton_sites.h"

using GetFace = int (*)(void*,int);
using SetFace = void (*)(void*,int,int);
static GetFace original_get;
static SetFace original_set;
static HMODULE self_module;
static FILE* log_file;
static SRWLOCK log_lock=SRWLOCK_INIT;
static std::atomic<unsigned> get_count{0},set_count{0};
static void log_event(const char* op,void* face,int id,int value);
using Refresh = unsigned (*)(void*);
static Refresh original_refresh;
static std::atomic<unsigned> refresh_count{0};
static SRWLOCK palette_lock=SRWLOCK_INIT;
static bool motion_enabled=false;
static int idle_animation=-1,roll_animation=-1;
static bool debug_motion=false;
static bool debug_render_pose=false;
static bool shape_only=true;
static bool basic_editor=false;
static bool solid_rgb=false;
static bool hair_material=false;
static bool tail_hair_material=false;
using CopyMatrices = unsigned (*)(void*,void*,unsigned,unsigned);
static CopyMatrices original_copy43,original_copy44;
using ApplyColors = void (*)(void*,int,void*,void*);
using ColorVector = void* (*)(void*,void*,unsigned);
static ApplyColors original_apply_colors;
static ColorVector original_color_vector;
using ApplyOneColor = void (*)(void*,int,void*,unsigned);
static ApplyOneColor native_apply_one;
using ShaderParamKey = void* (*)(unsigned);
using SetShaderValue = void (*)(void*,int,void*,void*,float);
static ShaderParamKey native_shader_key;
static SetShaderValue native_set_shader_value;
static ColorVector native_color_value;
static thread_local void* custom_color_face=nullptr;
static std::atomic<unsigned> color_count{0};
static std::atomic<unsigned> color_vector_count{0};
static std::atomic<unsigned> copy43_count{0},copy44_count{0},tail_copy_count{0},tail_color_count{0};
static LARGE_INTEGER timer_frequency{};
struct PlayerMotion {
    uintptr_t player=0,entity=0;
    EarMotion* motion=nullptr;
    EarPose pose{};
    TailMotion tail_motion{};
    TailInputTracker tail_input{};
    bool ears=true,tail=false,tail_have_position=false;
    LARGE_INTEGER previous_tick{};
    float position[3]{},velocity[3]{};
    float head_position[3]{},head_velocity[3]{},head_acceleration[3]{},basis[3][3]{};
    bool have_position=false;
    int last_animation=-1;
    unsigned updates=0;
    int first_bone=2,palette_count=4;
};
// Player ChrSet is shared by local and remote player instances. Each actor
// gets an independent spring/timer; no co-op packets or save padding needed.
static PlayerMotion players[32];
static bool read_memory(const void* address,void* out,size_t size) {
    SIZE_T copied=0;
    return address&&ReadProcessMemory(GetCurrentProcess(),address,out,size,&copied)&&copied==size;
}
static uintptr_t read_pointer(uintptr_t at) {
    uintptr_t result=0;read_memory(reinterpret_cast<void*>(at),&result,sizeof result);return result;
}
static int model_of_part(uintptr_t part) {
    uintptr_t item=part?read_pointer(part+0x10):0;
    if(!item)return -1;
    if(read_pointer(item+0x6d8)!=9)return -1;
    wchar_t name[10]={};
    if(!read_memory(reinterpret_cast<void*>(read_pointer(item+0x6c8)),name,18)||wcsncmp(name,L"FG_A_",5))return -1;
    int id=0;for(int i=5;i<9;i++) {if(name[i]<L'0'||name[i]>L'9')return -1;id=id*10+name[i]-L'0';}
    return id-5000;
}
static bool own_ear_part(uintptr_t player,uintptr_t expected_entity) {
    uintptr_t assembly=read_pointer(player+0x648);
    uintptr_t part=assembly?read_pointer(assembly+0xf0):0;
    uintptr_t item=part?read_pointer(part+0x10):0;
    if(!item||read_pointer(item+0x640)!=expected_entity)return false;
    auto s=decode_ear_selection(model_of_part(part));
    return s.private_part&&((s.flags&1)||s.tail);
}
static void tick_tail(PlayerMotion& state,const EarMatrix43* base,float game_dt,
                      TailVector physics_position,const TailUpdateStamp& stamp,float speed,int animation,bool idle) {
    if(!state.tail)return;
    TailFrame frame{};frame.dt=game_dt;frame.paused=game_dt==0;frame.speed=speed;
    frame.fast_riding=tail_is_fast_riding_animation(animation);frame.idle=idle;
    frame.anchor={base[7].m[0][3],base[7].m[1][3],base[7].m[2][3]};
    for(int axis=0;axis<3;axis++) {
        TailVector column{base[7].m[0][axis],base[7].m[1][axis],base[7].m[2][axis]};
        float length=tail_length(column);if(!std::isfinite(length)||length<.1f||length>10)return;
        frame.basis[axis]=column*(1/length);
    }
    TailVector pelvis{base[6].m[0][3],base[6].m[1][3],base[6].m[2][3]};
    frame.body_origin=tail_local(frame,pelvis-frame.anchor);frame.collisions=true;
    auto input=state.tail_input.sample(stamp,game_dt,physics_position,frame.anchor);
    if(!input.advance)return;
    frame.dt=input.dt;
    if(tail_length(input.origin_shift)>0) {
        state.tail_motion.rebase(input.origin_shift);
        if(debug_motion) {
            log_event("tail-map-rebase-xz",reinterpret_cast<void*>(state.entity),int(input.origin_shift.x*1000),int(input.origin_shift.z*1000));
            log_event("tail-map-rebase-y",reinterpret_cast<void*>(state.entity),int(input.origin_shift.y*1000),animation);
        }
    }
    if(debug_motion&&state.tail_motion.initialized&&tail_length(frame.anchor-state.tail_motion.latest.anchor)>2.f)
        log_event("tail-anchor-reset",reinterpret_cast<void*>(state.entity),animation,int(tail_length(frame.anchor-state.tail_motion.latest.anchor)*1000));
    auto old_swishes=state.tail_motion.idle_swish.count;
    if(!state.tail_motion.tick(frame))return;
    if(state.tail_motion.idle_swish.count>old_swishes)log_event("tail-idle-swish",reinterpret_cast<void*>(state.entity),animation,int(state.tail_motion.idle_swish.count));
    if(!state.tail_have_position)log_event("tail-animation-start",reinterpret_cast<void*>(state.entity),animation,12);
    if(debug_motion&&state.updates%120==0) {
        log_event("tail-clock-duplicates",reinterpret_cast<void*>(state.entity),int(state.tail_input.duplicates),int(state.tail_input.rebases));
        log_event("tail-pose",reinterpret_cast<void*>(state.entity),int(state.tail_motion.pose.angle[0][0]*10000),int(state.tail_motion.pose.angle[0][2]*10000));
        log_event("tail-speed",reinterpret_cast<void*>(state.entity),animation,int(speed*1000));
        log_event("tail-riding-blend",reinterpret_cast<void*>(state.entity),animation,int(state.tail_motion.riding_weight*1000));
        log_event("tail-transport-speed",reinterpret_cast<void*>(state.entity),animation,int(state.tail_motion.transport_speed*1000));
        auto tip=tail_local(state.tail_motion.previous,state.tail_motion.position[4]);
        log_event("tail-cloth-tip",reinterpret_cast<void*>(state.entity),int(tip.x*1000),int(tip.z*1000));
    }
    state.tail_have_position=true;
}
static void animate_palette(PlayerMotion& state) {
    auto entity=reinterpret_cast<void*>(state.entity);
    if(!motion_enabled||!state.player||!own_ear_part(state.player,state.entity))return;
    uintptr_t modules=read_pointer(state.player+0x190);
    uintptr_t physics=modules?read_pointer(modules+0x68):0;
    uintptr_t tae=modules?read_pointer(modules+0x18):0;
    if(!physics||!tae||read_pointer(physics+8)!=state.player||read_pointer(tae+8)!=state.player)return;
    float position[4]={},game_dt=0;
    if(!read_memory(reinterpret_cast<void*>(physics+0x70),position,sizeof position)||
       !read_memory(reinterpret_cast<void*>(state.player+0xb0),&game_dt,4))return;
    for(float p:position)if(!std::isfinite(p)||std::abs(p)>1000000)return;
    if(!std::isfinite(game_dt)||game_dt<0||game_dt>.25f)return;
    unsigned count=0;uintptr_t palette=read_pointer(state.entity+0x70);
    if(!read_memory(static_cast<uint8_t*>(entity)+0x68,&count,4)||count!=state.palette_count||!palette)return;
    EarMatrix43 base[12];
    if(!read_memory(reinterpret_cast<void*>(palette),base,count*sizeof(EarMatrix43)))return;
    for(unsigned i=0;i<count;i++)for(auto& row:base[i].m)for(float x:row)if(!std::isfinite(x))return;
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    double elapsed=state.previous_tick.QuadPart?double(now.QuadPart-state.previous_tick.QuadPart)/timer_frequency.QuadPart:0;
    // Some render paths refresh more than once in a frame. Reapply the same
    // pose to the new base matrices; advance the spring at most once.
    if(elapsed>.003||!state.previous_tick.QuadPart) {
        state.previous_tick=now;
        if(!state.motion)state.motion=uma_motion_create();
        if(!state.motion)return;
        float velocity[3]={},acceleration[3]={},basis[3][3]={},angular[3]={};
        float head_position[3]={base[1].m[0][3],base[1].m[1][3],base[1].m[2][3]};
        // Head bone has rotated bind axes. The private ear bases cancel them.
        for(int axis=0;axis<3;axis++) {
            float length=0;
            for(int i=0;i<3;i++)length+=base[state.first_bone].m[i][axis]*base[state.first_bone].m[i][axis];
            length=std::sqrt(length);
            if(!std::isfinite(length)||length<.1f||length>10)return;
            for(int i=0;i<3;i++)basis[axis][i]=base[state.first_bone].m[i][axis]/length;
        }
        bool continuous=state.have_position&&elapsed>0&&elapsed<.25&&game_dt>0;
        if(continuous) {
            float distance=0;
            for(int i=0;i<3;i++)distance+=(position[i]-state.position[i])*(position[i]-state.position[i]);
            if(distance>4)continuous=false; // Spawn/teleport: do not treat a discontinuity as force.
        }
        if(continuous) {
            float velocity_alpha=1-std::exp(-game_dt*28.f);
            float acceleration_alpha=1-std::exp(-game_dt*18.f);
            for(int i=0;i<3;i++) {
                velocity[i]=float((position[i]-state.position[i])/elapsed);
                float raw=float((head_position[i]-state.head_position[i])/elapsed);
                float filtered=state.head_velocity[i]+velocity_alpha*(raw-state.head_velocity[i]);
                float change=float((filtered-state.head_velocity[i])/elapsed);
                acceleration[i]=state.head_acceleration[i]+acceleration_alpha*(change-state.head_acceleration[i]);
                state.head_velocity[i]=filtered;state.head_acceleration[i]=acceleration[i];
            }
            // Small-angle angular velocity from the three normalized world axes.
            for(int axis=0;axis<3;axis++) {
                auto p=state.basis[axis];auto q=basis[axis];
                angular[0]+=float((p[1]*q[2]-p[2]*q[1])/(2*elapsed));
                angular[1]+=float((p[2]*q[0]-p[0]*q[2])/(2*elapsed));
                angular[2]+=float((p[0]*q[1]-p[1]*q[0])/(2*elapsed));
            }
        } else if(game_dt>0&&state.have_position) {
            uma_motion_destroy(state.motion);state.motion=uma_motion_create();state.pose=EarPose{};
            if(!state.motion)return;
            std::memset(state.head_velocity,0,sizeof state.head_velocity);
            std::memset(state.head_acceleration,0,sizeof state.head_acceleration);
        }
        unsigned read_index=0;int animation=-1;TailUpdateStamp tail_stamp{};
        struct TaeEntry {int animation;float start,end,flags;} entry{};
        if(read_memory(reinterpret_cast<void*>(tae+0xc4),&read_index,4)&&read_index<10&&
           read_memory(reinterpret_cast<void*>(tae+0x20+read_index*16),&entry,sizeof entry)) {
            animation=entry.animation;
            tail_stamp={read_index,animation,entry.start,entry.end,std::isfinite(entry.start)&&std::isfinite(entry.end)&&entry.end>=entry.start};
        }
        float speed=std::sqrt(velocity[0]*velocity[0]+velocity[2]*velocity[2]);
        EarFrame frame{};
        frame.dt=game_dt;frame.speed=std::min(speed,20.f);
        float right=0,forward=0,up=0,pitch_rate=0,turn_rate=0,roll_rate=0;
        for(int i=0;i<3;i++) {
            right+=acceleration[i]*basis[0][i];forward-=acceleration[i]*basis[2][i];
            up+=acceleration[i]*basis[1][i];pitch_rate+=angular[i]*basis[0][i];
            turn_rate+=angular[i]*basis[1][i];roll_rate+=angular[i]*basis[2][i];
        }
        frame.head_accel_right=std::clamp(right,-40.f,40.f);
        frame.head_accel_forward=std::clamp(forward,-40.f,40.f);
        frame.head_accel_up=std::clamp(up,-60.f,60.f);
        frame.vertical_speed=std::clamp(velocity[1],-15.f,15.f);
        frame.head_pitch_rate=std::clamp(pitch_rate,-12.f,12.f);
        frame.turn_rate=std::clamp(turn_rate,-12.f,12.f);
        frame.head_roll_rate=std::clamp(roll_rate,-12.f,12.f);
        frame.idle=ear_is_stationary_idle(animation,idle_animation,speed,velocity[1]);
        // Directional variants observed in the synchronized game recording:
        // 202000/202020 = roll, 202100/202115 = its recovery.
        frame.rolling=roll_animation>=0&&animation>=roll_animation&&animation<roll_animation+200;
        frame.paused=game_dt==0;
        frame.enabled=state.ears;frame.visible=state.ears;
        auto old_twitch=state.pose.twitch_count;
        uma_motion_tick(state.motion,&frame,&state.pose);
        if(state.pose.twitch_count!=old_twitch)log_event("ear-twitch",entity,animation,state.pose.twitch_count);
        if(!state.have_position)log_event("ear-animation-start",entity,animation,0);
        if(debug_motion&&animation!=state.last_animation) {
            log_event("ear-animation",entity,animation,frame.rolling);
            log_event("ear-idle-state",entity,animation,frame.idle);
        }
        if(debug_motion&&++state.updates%120==0)log_event("ear-pose",entity,animation,int(state.pose.left_pitch*10000));
        state.last_animation=animation;
        if(debug_motion&&state.updates%12==0) {
            log_event("ear-physics-input",entity,int(speed*1000),int(frame.vertical_speed*1000));
            log_event("ear-physics-pose",entity,int(state.pose.left_pitch*10000),int(state.pose.left_roll*10000));
        }
        tick_tail(state,base,game_dt,{position[0],position[1],position[2]},tail_stamp,speed,animation,frame.idle);
        std::memcpy(state.head_position,head_position,12);
        std::memcpy(state.basis,basis,sizeof basis);
        std::memcpy(state.position,position,12);
        std::memcpy(state.velocity,velocity,12);
        state.have_position=true;
    }
    // The location palette is refreshed again before rendering. Keep it
    // native; apply the current pose to the final copied render matrices.
}
static void inspect_render_copy(void* entity,void* output,unsigned count,unsigned first,bool transposed) {
    if(!motion_enabled||!output||!count||first>=12||count>12||first+count>12)return;
    // Do not leave a raw bind pose on a frame that overlaps the physics update.
    // This path only reads PlayerMotion; concurrent render copies can share it.
    AcquireSRWLockShared(&palette_lock);
    for(auto& state:players)if(state.entity==reinterpret_cast<uintptr_t>(entity)&&own_ear_part(state.player,state.entity)) {
        auto& counter=transposed?copy44_count:copy43_count;
        if(counter.fetch_add(1)<4)log_event(transposed?"render-copy44":"render-copy43",entity,int(first),int(count));
        // A bounded diagnostic pose distinguishes an ineffective refresh
        // hook from a mesh that does not consume its private bone palette.
        for(unsigned bone=state.first_bone;state.ears&&bone<state.first_bone+2;bone++) {
            if(bone<first||bone>=first+count)continue;
            auto dest=static_cast<uint8_t*>(output)+(bone-first)*(transposed?64:48);
            EarMatrix43 source{};
            float matrix[4][4];
            if(transposed) {
                if(!read_memory(dest,matrix,sizeof matrix))continue;
                for(int i=0;i<3;i++)for(int j=0;j<4;j++)source.m[i][j]=matrix[j][i];
            } else if(!read_memory(dest,&source,sizeof source))continue;
            const float root[3]={0,0,0};
            bool left=bone==state.first_bone;
            float pitch=left?state.pose.left_pitch:state.pose.right_pitch;
            float yaw=left?state.pose.left_yaw:state.pose.right_yaw;
            float roll=left?state.pose.left_roll:state.pose.right_roll;
            if(debug_render_pose) {pitch=0;yaw=0;roll=left?.65f:-.65f;}
            auto rotated=rotate_ear(source,root,pitch,yaw,roll);
            SIZE_T copied=0;
            if(transposed) {
                for(int i=0;i<3;i++)for(int j=0;j<4;j++)matrix[j][i]=rotated.m[i][j];
                if(!WriteProcessMemory(GetCurrentProcess(),dest,matrix,sizeof matrix,&copied)||copied!=sizeof matrix)
                    log_event("render-write-failed",entity,int(GetLastError()),int(copied));
            } else if(!WriteProcessMemory(GetCurrentProcess(),dest,&rotated,sizeof rotated,&copied)||copied!=sizeof rotated)
                log_event("render-write-failed",entity,int(GetLastError()),int(copied));
        }
        if(state.tail&&first+count>7&&first<12) {
            bool log_tail=tail_copy_count.fetch_add(1)<4;
            if(log_tail)log_event("tail-render-copy",entity,int(first),int(count));
            EarMatrix43 fresh[5]{},posed[5]{};bool valid=true;
            for(unsigned i=0;i<5;i++) {
                unsigned bone=7+i;
                if(bone>=first&&bone<first+count) {
                    auto dest=static_cast<uint8_t*>(output)+(bone-first)*(transposed?64:48);
                    if(transposed) {
                        float matrix[4][4]{};valid=valid&&read_memory(dest,matrix,sizeof matrix);
                        for(int r=0;r<3;r++)for(int c=0;c<4;c++)fresh[i].m[r][c]=matrix[c][r];
                    } else valid=valid&&read_memory(dest,&fresh[i],sizeof fresh[i]);
                } else {
                    uintptr_t palette=read_pointer(state.entity+0x70);
                    valid=valid&&palette&&read_memory(reinterpret_cast<void*>(palette+bone*48),&fresh[i],48);
                }
            }
            if(valid&&log_tail&&std::isfinite(fresh[0].m[1][3])&&std::isfinite(fresh[1].m[1][3]))
                log_event("tail-copy-root-y",entity,int(std::clamp(fresh[0].m[1][3]*1000.f,-1e8f,1e8f)),int(std::clamp((fresh[1].m[1][3]-fresh[0].m[1][3])*1000000.f,-1e8f,1e8f)));
            if(valid&&pose_tail_chain(fresh,state.tail_motion.pose.angle,posed))for(unsigned i=0;i<5;i++) {
                unsigned bone=7+i;if(bone<first||bone>=first+count)continue;
                auto dest=static_cast<uint8_t*>(output)+(bone-first)*(transposed?64:48);SIZE_T copied=0;
                if(transposed) {
                    float matrix[4][4]{};
                    if(!read_memory(dest,matrix,sizeof matrix))continue;
                    for(int r=0;r<3;r++)for(int c=0;c<4;c++)matrix[c][r]=posed[i].m[r][c];
                    if(!WriteProcessMemory(GetCurrentProcess(),dest,matrix,sizeof matrix,&copied)||copied!=sizeof matrix)log_event("tail-render-write-failed",entity,int(GetLastError()),int(copied));
                } else if(!WriteProcessMemory(GetCurrentProcess(),dest,&posed[i],48,&copied)||copied!=48)log_event("tail-render-write-failed",entity,int(GetLastError()),int(copied));
            }
        }
        break;
    }
    ReleaseSRWLockShared(&palette_lock);
}
static unsigned copy43(void* entity,void* output,unsigned count,unsigned first) {
    auto result=original_copy43(entity,output,count,first);
    inspect_render_copy(entity,output,result,first,false);return result;
}
static unsigned copy44(void* entity,void* output,unsigned count,unsigned first) {
    auto result=original_copy44(entity,output,count,first);
    inspect_render_copy(entity,output,result,first,true);return result;
}
static unsigned refresh_palette(void* entity) {
    auto result=original_refresh(entity);
    if((result==4||result==6||result==12)&&TryAcquireSRWLockExclusive(&palette_lock)) {
        for(auto& state:players)if(state.entity==reinterpret_cast<uintptr_t>(entity)) {
            refresh_count.fetch_add(1,std::memory_order_relaxed);
            animate_palette(state);break;
        }
        ReleaseSRWLockExclusive(&palette_lock);
    }
    return result;
}
static void observe_player_palette(uint8_t* base) {
    // Read-only diagnostic. The complete owner chain has been validated on
    // this exact build. Never use an arbitrary model or matrix array.
    unsigned polls=0;
    for(;;) {
        uintptr_t world=read_pointer(reinterpret_cast<uintptr_t>(base)+0x3d69ff8);
        uintptr_t set=world?world+0x10ee0:0;
        unsigned capacity=0;
        if(set&&read_pointer(set)==reinterpret_cast<uintptr_t>(base)+0x2a41348)
            read_memory(reinterpret_cast<void*>(set+0x10),&capacity,4);
        uintptr_t entries=capacity&&capacity<=32?read_pointer(set+0x18):0;
        uintptr_t found_players[32]{},found_entities[32]{};
        EarSelection selections[32];
        if(entries)for(unsigned i=0;i<capacity;i++) {
            uintptr_t player=read_pointer(entries+i*16);
            if(!player||read_pointer(player)!=reinterpret_cast<uintptr_t>(base)+0x2a7fbb0)continue;
            uintptr_t data=read_pointer(player+0x580);
            uintptr_t assembly=data?read_pointer(player+0x648):0;
            uintptr_t part=assembly?read_pointer(assembly+0xf0):0;
            uintptr_t item=part?read_pointer(part+0x10):0;
            uintptr_t entity=item?read_pointer(item+0x640):0;
            unsigned count=0;
            if(entity)read_memory(reinterpret_cast<void*>(entity+0x68),&count,4);
            auto selection=decode_ear_selection(model_of_part(part));
            if(count!=selection.palette_count||!own_ear_part(player,entity))continue;
            found_players[i]=player;found_entities[i]=entity;
            selections[i]=selection;
        }
        AcquireSRWLockExclusive(&palette_lock);
        for(unsigned i=0;i<32;i++) {
            auto& state=players[i];
            if(state.entity!=found_entities[i]||state.player!=found_players[i]) {
                if(state.motion)uma_motion_destroy(state.motion);
                state=PlayerMotion{};
            }
            state.player=found_players[i];state.entity=found_entities[i];
            state.first_bone=selections[i].first_bone;state.palette_count=selections[i].palette_count;
            state.ears=(selections[i].flags&1)!=0;state.tail=selections[i].tail;
        }
        ReleaseSRWLockExclusive(&palette_lock);
        if(++polls%20==0) {
            log_event("palette-refresh-count",nullptr,capacity,refresh_count.exchange(0));
            log_event("render-copy-counts",nullptr,copy43_count.load(),copy44_count.load());
        }
        Sleep(250);
    }
}

static void log_event(const char* op,void* face,int id,int value) {
    AcquireSRWLockExclusive(&log_lock);
    if(log_file) { std::fprintf(log_file,"%s face=%p id=%d value=%d\n",op,face,id,value);std::fflush(log_file); }
    ReleaseSRWLockExclusive(&log_lock);
}
static bool relevant(int id) { return id==1005 || (id>=3068&&id<=3070) || (id>=3089&&id<=3091); }
// FACE v4 has a 17-byte reserved tail, copied verbatim by its native save/
// load and buffer-copy functions. The exact build guard and FACE header
// prevent using this extension on another layout. Stock RGB fields stay intact.
static bool face_buffer(void* face,uint8_t (&buffer)[288]) {
    if(!read_memory(static_cast<uint8_t*>(face)+8,buffer,sizeof buffer))return false;
    uint32_t version=0,size=0;std::memcpy(&version,buffer+4,4);std::memcpy(&size,buffer+8,4);
    return std::memcmp(buffer,"FACE",4)==0&&version==4&&size==288;
}
static void ear_rgb(void* face,uint8_t (&rgb)[3]) {
    rgb[0]=208;rgb[1]=208;rgb[2]=220;
    uint8_t buffer[288];if(!face_buffer(face,buffer))return;
    if(std::memcmp(buffer+0x11c,"UE1\0",4)==0)std::memcpy(rgb,buffer+0x110,3);
    else {int model=original_get(face,1005);if(model>=100&&model<=103)std::memcpy(rgb,buffer+0x10b,3);}
}
static bool store_ear_rgb(void* face,const uint8_t (&rgb)[3]) {
    uint8_t buffer[288];if(!face_buffer(face,buffer))return false;
    SIZE_T copied=0;auto base=static_cast<uint8_t*>(face)+8;
    if(!WriteProcessMemory(GetCurrentProcess(),base+0x110,rgb,3,&copied)||copied!=3)return false;
    if(!WriteProcessMemory(GetCurrentProcess(),base+0x11c,"UE1\0",4,&copied)||copied!=4)return false;
    // Use the native setter to invalidate its cached appearance and mark the
    // data dirty, including when just the reserved color bytes have changed.
    original_set(face,1005,original_get(face,1005));return true;
}
static EarSelection selection_of(void* face) {return decode_ear_selection(original_get(face,1005));}
static void* color_vector(void* face,void* output,unsigned group) {
    if(face==custom_color_face&&group==37) {
        // Reuse the game's own byte-to-shader conversion, with a local copy
        // containing our RGB in its hair-color field. Neither the real hair
        // color nor the eyepatch color is modified.
        alignas(16) uint8_t copy[0x160];uint8_t rgb[3],buffer[288];
        if(face_buffer(face,buffer)&&read_memory(face,copy,sizeof copy)) {
            ear_rgb(face,rgb);std::memcpy(copy+8+0xf6,rgb,3);
            std::memset(copy+0x128,0,8);copy[0x154]&=~2;
            auto result=original_color_vector(copy,output,group);
            if(color_vector_count.fetch_add(1)<24) {
                log_event("ear-color-red",face,group,rgb[0]);
                log_event("ear-color-green",face,group,rgb[1]);
                log_event("ear-color-blue",face,group,rgb[2]);
                float values[4]={};
                if(read_memory(output,values,sizeof values))
                    for(int channel=0;channel<3;channel++)log_event("ear-color-vector",face,channel,int(values[channel]*1000));
            }
            return result;
        }
    }
    return original_color_vector(face,output,group);
}
static void apply_colors(void* face,int kind,void* part,void* override_material) {
    auto s=decode_ear_selection(model_of_part(reinterpret_cast<uintptr_t>(part)));
    bool ears=s.private_part&&(s.flags&1);
    if(solid_rgb) {
        // Keep the stock eyepatch bank intact. The native parameter modifier
        // accepts g_GX_MaterialID as a filter; the outer shell uses64.
        // Neither its #64# display mask nor its FLVER index selects colour.
        original_apply_colors(face,kind,part,override_material);
        if(ears) {
            alignas(16) uint8_t copy[0x160],buffer[288],rgb[3],value[24];
            if(face_buffer(face,buffer)&&read_memory(face,copy,sizeof copy)) {
                if(s.flags&2)for(int i=0;i<3;i++)rgb[i]=uint8_t(original_get(face,3068+i));
                else ear_rgb(face,rgb);
                std::memset(copy+0x128,0,8);copy[0x154]&=~2;
                if(hair_material) {
                    // FaceRangeParam bank37 starts at0 (hair); bank50 starts
                    // at.1 (eyepatch). Feed the actual hair shader and bank,
                    // including the character's lustre/root/white-hair values.
                    std::memcpy(copy+8+0xf6,rgb,3);
                    native_color_value((s.flags&2)?face:copy,value,37);
                    if(auto key=native_shader_key(800040))native_set_shader_value(part,64,key,value,1.f);
                    for(unsigned group=38;group<=40;group++) {
                        native_color_value(face,value,group);
                        if(auto key=native_shader_key(800040+group-37))native_set_shader_value(part,64,key,value,1.f);
                    }
                } else {
                    std::memcpy(copy+8+0x10b,rgb,3);
                    native_color_value(copy,value,50);
                    if(auto key=native_shader_key(800050))native_set_shader_value(part,64,key,value,1.f);
                }
                if(color_count.load()<24)for(int i=0;i<3;i++)log_event("solid-rgb-channel",part,i,rgb[i]);
            }
            if(color_count.fetch_add(1)<24)log_event("solid-ear-color",part,kind,s.flags);
        }
        if(s.tail) {
            alignas(16) uint8_t copy[0x160],buffer[288],value[24],rgb[3];
            if(face_buffer(face,buffer)&&read_memory(face,copy,sizeof copy)) {
                for(int i=0;i<3;i++)rgb[i]=uint8_t(original_get(face,3068+i));
                if(tail_hair_material) {
                    // Use the actual hair colour and all its appearance banks,
                    // independently of ear selection and the eyepatch colour.
                    for(unsigned group=37;group<=40;group++) {
                        native_color_value(face,value,group);
                        if(auto key=native_shader_key(800040+group-37))native_set_shader_value(part,65,key,value,1.f);
                    }
                } else {
                    std::memcpy(copy+8+0x10b,rgb,3);std::memset(copy+0x128,0,8);copy[0x154]&=~2;
                    native_color_value(copy,value,50);
                    if(auto key=native_shader_key(800050))native_set_shader_value(part,65,key,value,1.f);
                }
                if(tail_color_count.fetch_add(1)<12)for(int i=0;i<3;i++)log_event("tail-hair-channel",part,i,rgb[i]);
            }
        }
        return;
    }
    auto previous=custom_color_face;
    custom_color_face=ears&&!(s.flags&2)?face:nullptr;
    original_apply_colors(face,kind,part,override_material);
    // Ears use hair uniforms; stock eyepatch surfaces use eyepatch uniforms.
    // Apply the hair bank to this combined part as well as the native bank.
    if(ears&&kind!=1)original_apply_colors(face,1,part,nullptr);
    if(ears&&color_count.fetch_add(1)<24)log_event("ear-material-color",part,kind,s.flags);
    custom_color_face=previous;
}
static int get_face(void* face,int id) {
    if(basic_editor&&(id==60001||(id>=60010&&id<=60012)))return 0;
    if(id==60000)return ear_option(selection_of(face));
    if(id==60001)return (selection_of(face).flags>>1)&1;
    if(id==60002)return selection_of(face).eyepatch;
    if(id==60020)return selection_of(face).tail?1:0;
    if(id>=60010&&id<=60012) {uint8_t rgb[3];ear_rgb(face,rgb);return rgb[id-60010];}
    int value=original_get(face,id);
    if(relevant(id)&&get_count.fetch_add(1)<120)log_event("get",face,id,value);
    return value;
}
static void set_face(void* face,int id,int value) {
    if(basic_editor&&(id==60001||(id>=60010&&id<=60012)))return;
    if(id==60000||id==60001||id==60002||id==60020) {
        auto selected=selection_of(face);
        int state=selected.flags;
        if(!basic_editor) {
            uint8_t rgb[3];ear_rgb(face,rgb);
            if(!store_ear_rgb(face,rgb)) {log_event("ear-storage-failed",face,id,0);return;}
        }
        if(id==60000) {
            if(!set_ear_option(selected,value))return;
            state=selected.flags;
        } else if(id==60002) {if(!valid_eyepatch(value))return;selected.eyepatch=value;}
        else if(id==60020) {if(value!=0&&value!=1)return;selected.tail=value!=0;}
        else state=value?state|2:state&~2;
        log_event("ear-toggle",face,id,state);
        if(id==60020)log_event("tail-toggle",face,id,selected.tail);
        if(id==60000)log_event("ear-style",face,id,ear_option(selected));
        original_set(face,1005,encode_ear_selection(state,selected.eyepatch,selected.style,selected.tail));
        return;
    }
    if(id>=60010&&id<=60012) {
        value=value<0?0:(value>255?255:value);
        log_event("ear-rgb",face,id,value);
        uint8_t rgb[3];ear_rgb(face,rgb);rgb[id-60010]=uint8_t(value);
        if(!store_ear_rgb(face,rgb))log_event("ear-storage-failed",face,id,0);
        return;
    }
    if(relevant(id)&&set_count.fetch_add(1)<120)log_event("set",face,id,value);
    original_set(face,id,value);
}
static unsigned guard(const uint8_t* p) { unsigned h=2166136261;for(unsigned i=0;i<64;i++)h=(h^p[i])*16777619;return h; }
static void inspect_singletons(uint8_t* base) {
    // The engine registry is populated during startup, after DLL loading.
    // No memory is changed here, and none of these objects is retained.
    for(unsigned i=0;i<60&&!FindWindowW(L"ELDEN RING\u2122",nullptr);i++)Sleep(500);
    if(!FindWindowW(L"ELDEN RING\u2122",nullptr))return;
    Sleep(5000);
    if(guard(base+class_name_rva)!=class_name_guard)return;
    using NameGetter=const char*(*)(void*);
    auto name_of=reinterpret_cast<NameGetter>(base+class_name_rva);
    for(auto site:singleton_sites) {
        if(site.slot>=expected_image_size||site.klass>=expected_image_size)continue;
        const char* name=name_of(base+site.klass);
        if(!name)continue;
        AcquireSRWLockExclusive(&log_lock);
        if(log_file)std::fprintf(log_file,"singleton %.80s slot_rva=%x class_rva=%x\n",name,site.slot,site.klass);
        ReleaseSRWLockExclusive(&log_lock);
    }
    if(log_file)std::fflush(log_file);
}
static DWORD WINAPI start(void*) {
    wchar_t path[32768];
    DWORD n=GetModuleFileNameW(self_module,path,32768);
    if(!n||n>=32768)return 0;
    wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return 0;
    wcscpy(slash+1,L"uma-runtime.log");
    log_file=_wfopen(path,L"w");
    wcscpy(slash+1,L"uma-ears.ini");
    motion_enabled=GetPrivateProfileIntW(L"runtime",L"motion_enabled",0,path)==1;
    idle_animation=GetPrivateProfileIntW(L"runtime",L"idle_animation",-1,path);
    roll_animation=GetPrivateProfileIntW(L"runtime",L"roll_animation",-1,path);
    debug_motion=GetPrivateProfileIntW(L"runtime",L"debug_motion",0,path)==1;
    debug_render_pose=GetPrivateProfileIntW(L"runtime",L"debug_render_pose",0,path)==1;
    shape_only=GetPrivateProfileIntW(L"runtime",L"shape_only",1,path)==1;
    basic_editor=GetPrivateProfileIntW(L"runtime",L"basic_editor",0,path)==1;
    solid_rgb=!basic_editor&&GetPrivateProfileIntW(L"runtime",L"solid_rgb",0,path)==1;
    hair_material=solid_rgb&&GetPrivateProfileIntW(L"runtime",L"hair_material",0,path)==1;
    tail_hair_material=solid_rgb&&GetPrivateProfileIntW(L"runtime",L"tail_hair_material",0,path)==1;
    QueryPerformanceFrequency(&timer_frequency);
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(L"eldenring.exe"));
    if(!base || GetModuleHandleW(L"EasyAntiCheat_EOS.dll") || GetModuleHandleW(L"EasyAntiCheat.dll")) {
        log_event("refused-process",nullptr,0,0);return 0;
    }
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->FileHeader.TimeDateStamp!=expected_timestamp || nt->OptionalHeader.SizeOfImage!=expected_image_size ||
       guard(base+get_rva)!=get_guard || guard(base+set_rva)!=set_guard || guard(base+refresh_rva)!=refresh_guard ||
       guard(base+copy43_rva)!=copy43_guard || guard(base+copy44_rva)!=copy44_guard ||
       guard(base+apply_colors_rva)!=apply_colors_guard || guard(base+color_vector_rva)!=color_vector_guard ||
       guard(base+apply_one_rva)!=apply_one_guard || guard(base+shader_key_rva)!=shader_key_guard ||
       guard(base+set_shader_value_rva)!=set_shader_value_guard) {
        log_event("unsupported-build",nullptr,0,0);return 0;
    }
    if(MH_Initialize()!=MH_OK) { log_event("hook-init-failed",nullptr,0,0);return 0; }
    native_apply_one=reinterpret_cast<ApplyOneColor>(base+apply_one_rva);
    native_shader_key=reinterpret_cast<ShaderParamKey>(base+shader_key_rva);
    native_set_shader_value=reinterpret_cast<SetShaderValue>(base+set_shader_value_rva);
    native_color_value=reinterpret_cast<ColorVector>(base+color_vector_rva);
    bool editor_hooks=!shape_only||basic_editor||solid_rgb;
    bool color_hooks=!basic_editor&&(!shape_only||solid_rgb);
    bool vector_hooks=color_hooks&&!solid_rgb;
    auto a=editor_hooks?MH_CreateHook(base+get_rva,reinterpret_cast<void*>(get_face),reinterpret_cast<void**>(&original_get)):MH_OK;
    auto b=editor_hooks?MH_CreateHook(base+set_rva,reinterpret_cast<void*>(set_face),reinterpret_cast<void**>(&original_set)):MH_OK;
    auto c=MH_CreateHook(base+refresh_rva,reinterpret_cast<void*>(refresh_palette),reinterpret_cast<void**>(&original_refresh));
    auto d=MH_CreateHook(base+copy43_rva,reinterpret_cast<void*>(copy43),reinterpret_cast<void**>(&original_copy43));
    auto e=MH_CreateHook(base+copy44_rva,reinterpret_cast<void*>(copy44),reinterpret_cast<void**>(&original_copy44));
    auto f=color_hooks?MH_CreateHook(base+apply_colors_rva,reinterpret_cast<void*>(apply_colors),reinterpret_cast<void**>(&original_apply_colors)):MH_OK;
    auto g=vector_hooks?MH_CreateHook(base+color_vector_rva,reinterpret_cast<void*>(color_vector),reinterpret_cast<void**>(&original_color_vector)):MH_OK;
    if(a!=MH_OK||b!=MH_OK||c!=MH_OK||d!=MH_OK||e!=MH_OK||f!=MH_OK||g!=MH_OK) {log_event("hook-create-failed",nullptr,a,b);log_event("palette-hook-status",nullptr,c,0);log_event("copy-hook-status",nullptr,d,e);log_event("color-hook-status",nullptr,f,g);MH_Uninitialize();return 0;}
    if(editor_hooks) {MH_QueueEnableHook(base+get_rva);MH_QueueEnableHook(base+set_rva);}
    MH_QueueEnableHook(base+refresh_rva);
    MH_QueueEnableHook(base+copy43_rva);MH_QueueEnableHook(base+copy44_rva);
    if(color_hooks) MH_QueueEnableHook(base+apply_colors_rva);
    if(vector_hooks) MH_QueueEnableHook(base+color_vector_rva);
    auto enabled=MH_ApplyQueued();
    log_event("appearance-hooks",nullptr,enabled,0);
    log_event("shape-only-mode",nullptr,shape_only,0);
    log_event("basic-editor-mode",nullptr,basic_editor,color_hooks);
    log_event("solid-rgb-mode",nullptr,solid_rgb,vector_hooks);
    log_event("hair-material-mode",nullptr,hair_material,64);
    log_event("tail-hair-material-mode",nullptr,tail_hair_material,65);
    observe_player_palette(base);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        self_module=module;DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,start,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
    }
    return TRUE;
}
