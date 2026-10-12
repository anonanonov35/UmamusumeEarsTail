#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ear_settings.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    // Public choices retain their saved model bytes, including independent tail.
    bool used[256]={};int combinations=0;
    for(bool tail:{false,true})for(int eye:{0,1,2,10})for(int sync:{0,2})for(int option:{0,2,3,4}) {
        EarSelection current{};current.flags=sync;current.eyepatch=eye;current.tail=tail;
        assert(set_ear_option(current,option));
        int low=tail?(option==2?tail_eyepatch_code(eye,32):eye+4):eye;
        int expected=sync*64+(option?64:0)+(option==2?32:option==3?16:option==4?48:0)+low;
        int model=encode_ear_selection(current.flags,eye,current.style,tail);
        assert(model==expected&&model>=0&&model<256&&!used[model]&&!(model>=100&&model<=103));used[model]=true;++combinations;
        auto selected=decode_ear_selection(model);
        assert(selected.tail==tail&&ear_option(selected)==option&&selected.eyepatch==eye&&(selected.flags&2)==sync);
        assert(selected.palette_count==(tail?12:6)&&selected.private_part==(tail||current.flags!=0));
        for(int next_option:{0,2,3,4}) {
            auto changed=selected;assert(set_ear_option(changed,next_option));
            auto next=decode_ear_selection(encode_ear_selection(changed.flags,eye,changed.style,changed.tail));
            assert(next.tail==tail&&ear_option(next)==next_option&&next.eyepatch==eye&&(next.flags&2)==sync);
        }
        for(int next_eye:{0,1,2,10}) {
            auto next=decode_ear_selection(encode_ear_selection(selected.flags,next_eye,selected.style,tail));
            assert(next.tail==tail&&ear_option(next)==option&&next.eyepatch==next_eye);
        }
        auto toggled=decode_ear_selection(encode_ear_selection(selected.flags,eye,selected.style,!tail));
        assert(toggled.tail!=tail&&ear_option(toggled)==option&&toggled.eyepatch==eye);
        toggled=decode_ear_selection(encode_ear_selection(selected.flags^2,eye,selected.style,tail));
        assert(toggled.tail==tail&&ear_option(toggled)==option&&(toggled.flags&2)==(sync^2));
        auto before=current;
        for(int invalid:{-1,1,5})assert(!set_ear_option(current,invalid));
        assert(current.flags==before.flags&&current.style==before.style&&current.tail==before.tail);
    }
    assert(combinations==64);
    // Previous standard-ear IDs now resolve to off; existing tail/eyepatch stay.
    for(bool tail:{false,true})for(int eye:{0,1,2,10})for(int sync:{0,2}) {
        int retired=(sync|1)*64+eye+(tail?4:0);
        auto s=decode_ear_selection(retired);
        assert(ear_option(s)==0&&!(s.flags&1)&&s.tail==tail&&s.eyepatch==eye&&(s.flags&2)==sync);
        int normalized=encode_ear_selection(s.flags,s.eyepatch,s.style,s.tail);
        assert(normalized==sync*64+eye+(tail?4:0));
        assert(encode_ear_selection(sync|1,eye,1,tail)==normalized);
        assert(set_ear_option(s,2));
        auto rudolf=decode_ear_selection(encode_ear_selection(s.flags,s.eyepatch,s.style,s.tail));
        assert(ear_option(rudolf)==2&&rudolf.tail==tail&&rudolf.eyepatch==eye);
    }
    for(int model=100;model<=103;model++) {
        auto s=decode_ear_selection(model);
        assert(ear_option(s)==0&&s.flags==((model-100)&2)&&s.eyepatch==0&&s.first_bone==2&&s.palette_count==4);
    }
    for(int model:{-1,255,63,32,16,160,144,48,176,109,300})assert(!decode_ear_selection(model).private_part);
    puts("PASS: 64 combinations, stable Rudolf/Agnes/Oguri IDs, tail/colour/eyepatch independence, retired standard IDs off");
}
