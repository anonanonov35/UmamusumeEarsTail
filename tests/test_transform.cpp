#include "../src/ear_transform.h"
#include <cassert>
#include <cstdio>
static void point(const EarMatrix43& m,const float p[3],float out[3]) {
    for(int i=0;i<3;i++) {out[i]=m.m[i][3];for(int j=0;j<3;j++)out[i]+=m.m[i][j]*p[j];}
}
static float distance(const float a[3],const float b[3]) {
    float s=0;for(int i=0;i<3;i++)s+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(s);
}
int main() {
    const EarMatrix43 identity{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
    const float origin[3]={0,0,0};
    const float root[3]={-.057f,1.700f,.004f};
    const float tip[3]={-.078f,1.845f,.014f};
    for(int n=0;n<1000;n++) {
        auto base=rotate_ear(identity,origin,n*.013f,n*.019f,n*.027f);
        base.m[0][3]=15;base.m[1][3]=-32;base.m[2][3]=7;
        auto rotated=rotate_ear(base,root,.3f*std::sin(n),.4f*std::cos(n),-.2f);
        float root0[3],root1[3],tip1[3];
        point(base,root,root0);point(rotated,root,root1);point(rotated,tip,tip1);
        assert(distance(root0,root1)<1e-5f);
        assert(std::abs(distance(root1,tip1)-distance(root,tip))<1e-5f);
    }
    auto zero=rotate_ear(identity,root,0,0,0);
    for(int i=0;i<3;i++)for(int j=0;j<4;j++)assert(zero.m[i][j]==identity.m[i][j]);
    std::puts("PASS: fixed root, rigid ear length, zero-pose identity under 1000 world transforms");
}
