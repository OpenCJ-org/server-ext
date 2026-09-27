#include "gsc_custom_utils.hpp"
#ifdef COD4
extern "C" {
#include "../cod4x-server/src/cm_local.h"
}
#include <cmath>
#include <algorithm>

namespace {
// Double precision avoids losing the landing plane on distant, thin slopes.
struct Point { double x[3]; };
struct Plane { Point n; double d; };
double dot(const Point& a, const Point& b) { return a.x[0]*b.x[0]+a.x[1]*b.x[1]+a.x[2]*b.x[2]; }
Point add(const Point& a,const Point& b,double scale) {
    return {{a.x[0]+b.x[0]*scale,a.x[1]+b.x[1]*scale,a.x[2]+b.x[2]*scale}};
}
Point cross(const Point& a,const Point& b) {
    return {{a.x[1]*b.x[2]-a.x[2]*b.x[1],a.x[2]*b.x[0]-a.x[0]*b.x[2],a.x[0]*b.x[1]-a.x[1]*b.x[0]}};
}
Plane plane(const cbrush_t& b,unsigned i) {
    Plane p={};
    if(i<6) { const unsigned axis=i/2; const bool upper=i%2; p.n.x[axis]=upper?1:-1; p.d=upper?b.maxs[axis]:-b.mins[axis]; }
    else { const cplane_t& s=*b.sides[i-6].plane; for(int j=0;j<3;j++)p.n.x[j]=s.normal[j];p.d=s.dist; }
    return p;
}
// Sutherland-Hodgman clipping against the brush's own half-spaces.
int clip(Point* points,int count,const Plane& p) {
    Point out[64];int used=0;
    for(int i=0;i<count;i++) {
        const Point a=points[i],b=points[(i+1)%count];
        const double da=dot(a,p.n)-p.d,db=dot(b,p.n)-p.d;
        if(da<=0) { if(used==64)return 0;out[used++]=a; }
        if((da<0 && db>0)||(da>0 && db<0)) { if(used==64)return 0;out[used++]=add(a,add(b,a,-1),da/(da-db)); }
    }
    std::copy(out,out+used,points);return used;
}
struct Search {
    Point origin,normal,inside;
    Point best[8];int count=0;double bestArea=0;
    void brush(unsigned index) {
        if(index>=cm.numBrushes)return;
        const cbrush_t& b=cm.brushes[index];
        if(!(b.contents & 1) || b.numsides>128)return; // solid world brushes only
        for(unsigned i=0;i<b.numsides+6;i++) {
            const Plane p=plane(b,i);
            if(dot(origin,p.n)-p.d>0.5)return;
        }
        for(unsigned face=0;face<b.numsides+6;face++) {
            const Plane top=plane(b,face);
            if(dot(top.n,normal)<0.99999 || std::abs(dot(origin,top.n)-top.d)>0.5)continue;
            Point center=add(origin,top.n,(top.d-dot(origin,top.n))/dot(top.n,top.n));
            Point u={{top.n.x[2],0,-top.n.x[0]}};u=add(Point{},u,1/std::sqrt(dot(u,u)));
            Point v=cross(top.n,u);v=add(Point{},v,1/std::sqrt(dot(v,v)));
            Point pts[64]={add(add(center,u,-8192),v,-8192),add(add(center,u,8192),v,-8192),add(add(center,u,8192),v,8192),add(add(center,u,-8192),v,8192)};
            int n=4;
            for(unsigned i=0;i<b.numsides+6 && n>=3;i++)if(i!=face)n=clip(pts,n,plane(b,i));
            // Remove duplicate and straight-line vertices introduced by bevel planes.
            bool changed=true;
            while(changed && n>=3) {
                changed=false;
                for(int i=0;i<n;i++) {
                    Point a=add(pts[i],pts[(i+n-1)%n],-1),z=add(pts[(i+1)%n],pts[i],-1);
                    Point c=cross(a,z);
                    if(dot(a,a)<0.0001 || dot(c,c)<0.000001*dot(a,a)*dot(z,z)) {
                        for(int j=i;j<n-1;j++) { pts[j]=pts[j+1]; }
                        --n;changed=true;break;
                    }
                }
            }
            if(n<3 || n>8)continue;
            double area=0;bool bounded=true;
            for(int i=0;i<n;i++) {
                const Point a=add(pts[i],center,-1),z=add(pts[(i+1)%n],center,-1);
                area+=dot(cross(a,z),top.n);
                if(dot(a,a)>4096.0*4096.0)bounded=false;
            }
            area=std::abs(area);
            if(bounded && area>bestArea) { bestArea=area;count=n;std::copy(pts,pts+n,best); }
        }
    }
    // Follow the world leaf tree, not inline/moving brush models.
    void node(int index,int depth=0) {
        if(index<=0 || unsigned(index)>=cm.leafbrushNodesCount || depth>64)return;
        const cLeafBrushNode_t& n=cm.leafbrushNodes[index];
        if(n.leafBrushCount>0) { for(int i=0;i<n.leafBrushCount;i++)brush(n.data.leaf.brushes[i]);return; }
        if(n.leafBrushCount<0)node(index+1,depth+1);
        if(n.axis>2)return;
        double d=inside.x[n.axis]-n.data.children.dist;
        if(d>=-0.5)node(index+n.data.children.childOffset[0],depth+1);
        if(d<=0.5)node(index+n.data.children.childOffset[1],depth+1);
    }
};
}
#endif

extern "C" void Gsc_Platform_Face()
{
#ifdef COD4
    vec3_t origin,normal;stackGetParamVector(0,origin);stackGetParamVector(1,normal);
    Search search;
    for(int i=0;i<3;i++) {
        if(!std::isfinite(origin[i])||std::abs(origin[i])>131072||!std::isfinite(normal[i])) {stackPushUndefined();return;}
        search.origin.x[i]=origin[i];search.normal.x[i]=normal[i];
    }
    if(normal[2]<0.7 || std::abs(dot(search.normal,search.normal)-1)>0.01) {stackPushUndefined();return;}
    search.inside=add(search.origin,search.normal,-0.25);
    vec3_t inside;for(int i=0;i<3;i++)inside[i]=search.inside.x[i];
    int leaf=CM_PointLeafnum(inside);
    if(leaf>=0 && unsigned(leaf)<cm.numLeafs)search.node(cm.leafs[leaf].leafBrushNode);
    if(!search.count) {stackPushUndefined();return;}
    stackMakeArray();
    for(int i=0;i<search.count;i++) {vec3_t p;for(int j=0;j<3;j++)p[j]=search.best[i].x[j];stackPushVector(p);stackPushArrayNext();}
#else
    stackPushUndefined(); // CoD2 continues to use the GSC detector.
#endif
}
