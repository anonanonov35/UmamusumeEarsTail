#pragma once
#include "ear_transform.h"
inline EarMatrix43 tail_multiply(const EarMatrix43& a,const EarMatrix43& b) {
    EarMatrix43 out{};
    for(int i=0;i<3;i++) {
        for(int j=0;j<3;j++)for(int k=0;k<3;k++)out.m[i][j]+=a.m[i][k]*b.m[k][j];
        out.m[i][3]=a.m[i][3];for(int k=0;k<3;k++)out.m[i][3]+=a.m[i][k]*b.m[k][3];
    }
    return out;
}
inline bool tail_inverse(const EarMatrix43& a,EarMatrix43& out) {
    float det=a.m[0][0]*(a.m[1][1]*a.m[2][2]-a.m[1][2]*a.m[2][1])-a.m[0][1]*(a.m[1][0]*a.m[2][2]-a.m[1][2]*a.m[2][0])+a.m[0][2]*(a.m[1][0]*a.m[2][1]-a.m[1][1]*a.m[2][0]);
    if(!std::isfinite(det)||std::abs(det)<1e-6f)return false;
    for(int i=0;i<3;i++)for(int j=0;j<3;j++) {
        int r1=(j+1)%3,r2=(j+2)%3,c1=(i+1)%3,c2=(i+2)%3;
        out.m[i][j]=(a.m[r1][c1]*a.m[r2][c2]-a.m[r1][c2]*a.m[r2][c1])/det;
    }
    for(int i=0;i<3;i++){out.m[i][3]=0;for(int j=0;j<3;j++)out.m[i][3]-=out.m[i][j]*a.m[j][3];}
    return true;
}
inline bool pose_tail_chain(const EarMatrix43 (&fresh)[5],const float (&angles)[5][3],EarMatrix43 (&posed)[5]) {
    const float origin[3]={0,0,0};
    posed[0]=rotate_ear(fresh[0],origin,angles[0][0],angles[0][1],angles[0][2]);
    for(int i=1;i<5;i++) {
        EarMatrix43 inverse{};if(!tail_inverse(fresh[i-1],inverse))return false;
        auto local=tail_multiply(inverse,fresh[i]);
        posed[i]=rotate_ear(tail_multiply(posed[i-1],local),origin,angles[i][0],angles[i][1],angles[i][2]);
    }
    for(auto& bone:posed)for(auto& row:bone.m)for(float x:row)if(!std::isfinite(x))return false;
    return true;
}
