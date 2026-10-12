#pragma once
struct EarSelection { int flags=0,eyepatch=0,style=2,first_bone=4,palette_count=6;bool private_part=false,tail=false; };
inline bool valid_eyepatch(int model) {return model==0||model==1||model==2||model==10;}
inline int tail_eyepatch_code(int eye,int type) {
    if(type==32)return eye==1?9:eye==2?11:eye==10?12:8;
    return (valid_eyepatch(eye)?eye:0)+4;
}
inline int tail_eyepatch_decode(int low,int type) {
    if(type==32)return low==8?0:low==9?1:low==11?2:low==12?10:-1;
    return low==4?0:low==5?1:low==6?2:low==14?10:-1;
}
inline EarSelection decode_ear_selection(int model) {
    EarSelection s{};
    if(model>=100&&model<=103) {
        s.flags=(model-100)&2;s.first_bone=2;s.palette_count=4;s.private_part=true;return s;
    }
    if(model<0||model>255)return s;
    int flags=model>>6,type=model&48,low=model&15;
    if(type&&!(flags&1))return s;
    int tail_eye=tail_eyepatch_decode(low,type);
    if(!valid_eyepatch(low)&&tail_eye<0)return s;
    s.flags=flags;s.tail=tail_eye>=0;s.eyepatch=s.tail?tail_eye:low;
    s.style=type==32?2:(type==16?3:(type==48?4:2));
    // Retired standard-ear save IDs retain tail/eyepatch/sync, with ears off.
    if(!type)s.flags&=~1;
    s.private_part=s.flags!=0||s.tail;s.palette_count=s.tail?12:6;
    return s;
}
inline int encode_ear_selection(int flags,int eyepatch,int style=2,bool tail=false) {
    if(style<2||style>4)flags&=~1;
    int type=(flags&1)?(style==2?32:(style==3?16:(style==4?48:0))):0;
    // Rudolf uses a separate nibble map to preserve legacy saved IDs100..103.
    return ((flags&3)<<6)|type|(tail?tail_eyepatch_code(eyepatch,type):(valid_eyepatch(eyepatch)?eyepatch:0));
}
inline int ear_option(const EarSelection& s) {return (s.flags&1)&&s.style>=2&&s.style<=4?s.style:0;}
inline bool set_ear_option(EarSelection& s,int option) {
    if(option!=0&&(option<2||option>4))return false;
    s.flags=option?(s.flags|1):(s.flags&~1);s.style=option?option:2;return true;
}
