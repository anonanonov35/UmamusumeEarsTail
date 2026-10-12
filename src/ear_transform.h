#pragma once
#include <cmath>
struct EarMatrix43 { float m[3][4]; };
static_assert(sizeof(EarMatrix43)==48,"Engine palette stride");

// Compose a local rotation about the authored ear root with the fresh
// skinning matrix. Never feed a matrix modified on the previous frame back.
inline EarMatrix43 rotate_ear(const EarMatrix43& base,const float root[3],float pitch,float yaw,float roll) {
    const float cx=std::cos(pitch),sx=std::sin(pitch);
    const float cy=std::cos(yaw),sy=std::sin(yaw);
    const float cz=std::cos(roll),sz=std::sin(roll);
    const float r[3][3]={{cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx},
                         {sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx},
                         {-sy,cy*sx,cy*cx}};
    float t[3]={root[0],root[1],root[2]};
    for(int i=0;i<3;i++)for(int j=0;j<3;j++)t[i]-=r[i][j]*root[j];
    EarMatrix43 out{};
    for(int i=0;i<3;i++) {
        for(int j=0;j<3;j++)for(int k=0;k<3;k++)out.m[i][j]+=base.m[i][k]*r[k][j];
        out.m[i][3]=base.m[i][3];
        for(int k=0;k<3;k++)out.m[i][3]+=base.m[i][k]*t[k];
    }
    return out;
}
