#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include "../src/tail_input.h"
static float difference(const TailMotion& a,const TailMotion& b) {
    float d=0;for(int j=0;j<6;j++)d=std::max(d,tail_length(a.position[j]-b.position[j]));return d;
}
int main(int argc,char** argv) {
    setbuf(stdout,nullptr);
    // Same 9m/s gallop with and without cell origin shifts. Duplicate palette
    // refreshes must not advance physics or lower the persistent riding blend.
    for(int fps:{30,60,144}) {
        TailMotion reference,m;TailInputTracker tracking;TailVector shift{};
        unsigned version=0;float largest=0;
        for(int i=0;i<fps*12;i++) {
            float t=float(i)/fps;TailFrame f;f.dt=1.f/fps;f.speed=9;f.fast_riding=true;f.collisions=true;
            f.anchor={t*9,.04f*std::sin(t*12),.03f*std::sin(t*9)};
            TailVector root={t*9,0,0};assert(reference.tick(f));
            if(i>0&&i%fps==0) {
                // These exact lattice translations occurred in the live trace.
                const TailVector moves[]={{-32,0,0},{-32,0,-8},{32,0,16},{32,-8,-16}};
                shift=shift+moves[(i/fps-1)%4];++version;
            }
            f.anchor=f.anchor+shift;root=root+shift;
            TailUpdateStamp stamp{unsigned(i%10),12120205,t-f.dt,t,true};
            auto input=tracking.sample(stamp,f.dt,root,f.anchor);assert(input.advance);
            m.rebase(input.origin_shift);assert(m.tick(f));
            if(i>fps&&i%fps==0)assert(m.riding_weight>.98f);
            largest=std::max(largest,difference(m,reference));
            auto frozen=m;auto duplicate=tracking.sample(stamp,f.dt,root,f.anchor);assert(!duplicate.advance);
            assert(difference(frozen,m)==0&&frozen.riding_weight==m.riding_weight);
        }
        assert(tracking.rebases==version&&tracking.duplicates==unsigned(fps*12));
        assert(largest<.004f);
        printf("Map shifts + duplicate refreshes %d FPS: max particle difference %.6fm, rebases %u\n",fps,largest,tracking.rebases);
    }
    // Pauses preserve the token/state. Ordinary teleport and mismatched
    // coordinate jumps do not qualify as map-origin translations.
    TailInputTracker tr;TailMotion m;TailFrame f;f.dt=.02f;
    TailUpdateStamp s{0,120200,0,.02f,true};auto in=tr.sample(s,f.dt,{},{});assert(in.advance);assert(m.tick(f));
    auto pause=tr.sample(s,0,{32,0,0},{32,0,0});assert(!pause.advance);
    s.index=1;s.start=.02f;s.end=.04f;in=tr.sample(s,f.dt,{5,0,0},{5,0,0});assert(tail_length(in.origin_shift)==0);
    f.anchor={5,0,0};m.rebase(in.origin_shift);assert(m.tick(f));assert(m.riding_weight==0&&m.pose.angle[0][0]==0);
    s.index=2;s.end=.06f;in=tr.sample(s,f.dt,{37,0,0},{6,0,0});assert(tail_length(in.origin_shift)==0);
    // Replay actual user-controlled samples, not an invented mounted flag.
    if(argc==2) {
        FILE* file=std::fopen(argv[1],"r");assert(file);
        TailInputTracker tracker;TailMotion tail;float t,dt,root[3],a[3],b[9],start,end;unsigned index;int animation;
        unsigned accepted=0,resets=0,raw_resets=0;TailVector raw_previous{};bool have=false;
        while(std::fscanf(file,"%f,%f,%u,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n",&t,&dt,&index,&animation,&start,&end,&root[0],&root[1],&root[2],&a[0],&a[1],&a[2],&b[0],&b[1],&b[2],&b[3],&b[4],&b[5],&b[6],&b[7],&b[8])==21) {
            TailFrame frame;frame.dt=dt;frame.anchor={a[0],a[1],a[2]};frame.fast_riding=animation%1000000==120200||animation%1000000==120205;
            for(int axis=0;axis<3;axis++)frame.basis[axis]=tail_unit({b[axis*3],b[axis*3+1],b[axis*3+2]});
            auto input=tracker.sample({index,animation,start,end,true},dt,{root[0],root[1],root[2]},frame.anchor);
            if(!input.advance)continue;
            if(have&&tail_length(frame.anchor-raw_previous)>2)++raw_resets;
            raw_previous=frame.anchor;have=true;
            tail.rebase(input.origin_shift);
            if(tail.initialized&&tail_length(frame.anchor-tail.latest.anchor)>2)++resets;
            assert(tail.tick(frame));++accepted;
            for(auto p:tail.position)assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z));
        }
        std::fclose(file);assert(accepted>500&&tracker.rebases>=4&&raw_resets>=4&&resets==0);
        printf("Recorded map-crossing trace: %u updates, %u duplicates, %u origin shifts, old resets %u, corrected resets %u\n",accepted,tracker.duplicates,tracker.rebases,raw_resets,resets);
    }
    puts("PASS: map origin shifts preserve cloth state; duplicate render refreshes skipped; other teleports reset");
}
