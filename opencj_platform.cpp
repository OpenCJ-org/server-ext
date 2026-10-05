#include "gsc_custom_utils.hpp"
#ifdef COD4
extern "C" {
#include "../cod4x-server/src/cm_local.h"
}
#include <cmath>
#include <algorithm>
#include <vector>

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

// Geometry utilities shared by brush tops and wall footprints.
int simplify(Point* pts,int n,double minEdgeSquared=0.0001) {
    bool changed=true;
    while(changed && n>=3) {
        changed=false;
        for(int i=0;i<n;i++) {
            const Point a=add(pts[i],pts[(i+n-1)%n],-1),b=add(pts[(i+1)%n],pts[i],-1),c=cross(a,b);
            if(dot(a,a)<minEdgeSquared || dot(c,c)<0.000001*dot(a,a)*dot(b,b)) {
                for(int j=i;j<n-1;j++)pts[j]=pts[j+1];
                --n;changed=true;break;
            }
        }
    }
    return n;
}
double area(const Point* pts,int n,const Point& normal) {
    double sum=0;
    for(int i=1;i<n-1;i++)sum+=dot(cross(add(pts[i],pts[0],-1),add(pts[i+1],pts[0],-1)),normal);
    return std::abs(sum)*0.5;
}
Plane edgePlane(const Point& a,const Point& b,const Point& normal) {
    Point n=cross(add(b,a,-1),normal);n=add(Point{},n,1/std::sqrt(dot(n,n)));
    return {n,dot(n,a)};
}
struct Obstacle { Point points[64];int count;bool wall; };
int intersection(Point* points,int count,const Obstacle& obstacle,const Point& normal) {
    for(int i=0;i<obstacle.count && count>=3;i++)count=clip(points,count,edgePlane(obstacle.points[i],obstacle.points[(i+1)%obstacle.count],normal));
    return count;
}
void worldBrushes(int node,std::vector<unsigned>& indices,int depth=0) {
    if(node<=0 || unsigned(node)>=cm.leafbrushNodesCount || depth>64)return;
    const cLeafBrushNode_t& n=cm.leafbrushNodes[node];
    if(n.leafBrushCount>0) {for(int i=0;i<n.leafBrushCount;i++)indices.push_back(n.data.leaf.brushes[i]);return;}
    if(n.leafBrushCount<0)worldBrushes(node+1,indices,depth+1);
    worldBrushes(node+n.data.children.childOffset[0],indices,depth+1);
    worldBrushes(node+n.data.children.childOffset[1],indices,depth+1);
}
bool clipWalls(Point* points,int& count,const Point& normal,const Point& seed,std::vector<Obstacle>& obstacles) {
    // Brushes can form a wall together. Propagate boundary connectivity through
    // their complete planar footprints, including touching brush seams.
    bool changed=true;
    while(changed) {
        changed=false;
        for(auto& candidate:obstacles)if(!candidate.wall)for(const auto& wall:obstacles)if(wall.wall) {
            Point section[64];std::copy(candidate.points,candidate.points+candidate.count,section);int n=candidate.count;
            for(int i=0;i<wall.count && n>=3;i++) {
                Plane p=edgePlane(wall.points[i],wall.points[(i+1)%wall.count],normal);p.d+=0.25;n=clip(section,n,p);
            }
            if(n>=3 && area(section,n,normal)>0.001){candidate.wall=true;changed=true;break;}
        }
    }
    for(const auto& wall:obstacles)if(wall.wall) {
        Point overlap[64];std::copy(points,points+count,overlap);
        const int overlaps=intersection(overlap,count,wall,normal);
        if(overlaps<3 || area(overlap,overlaps,normal)<0.01)continue;
        // Checkpoints are convex. Retain the largest convex side containing the
        // supported feet, rather than filling a concavity or extending through solid.
        Point best[64];int bestCount=0;double bestArea=0;
        for(int i=0;i<wall.count;i++) {
            Plane p=edgePlane(wall.points[i],wall.points[(i+1)%wall.count],normal);
            if(dot(seed,p.n)-p.d<0.125)continue;
            p.n=add(Point{},p.n,-1);p.d=-p.d-0.125;
            Point candidate[64];std::copy(points,points+count,candidate);int n=clip(candidate,count,p);n=simplify(candidate,n);
            const double a=n>=3?area(candidate,n,normal):0;
            if(a>bestArea){bestArea=a;bestCount=n;std::copy(candidate,candidate+n,best);}
        }
        if(bestCount<3)return false;
        count=bestCount;std::copy(best,best+count,points);
    }
    // Never emit an unverified polygon when clipping exceeds editor limits.
    count=simplify(points,count,1.0);
    if(count<3 || count>8)return false;
    for(const auto& wall:obstacles)if(wall.wall) {
        Point overlap[64];std::copy(points,points+count,overlap);
        int n=intersection(overlap,count,wall,normal);
        if(n>=3 && area(overlap,n,normal)>0.01)return false;
    }
    return true;
}// Work from solid brush intersections, never sampled rays. Freestanding objects
// remain ignored; solids connected to the landing boundary are walls.
bool trimWalls(Point* points,int& count,const Point& origin) {
    Point normal=cross(add(points[1],points[0],-1),add(points[2],points[0],-1));
    normal=add(Point{},normal,1/std::sqrt(dot(normal,normal)));
    Point reference[64];std::copy(points,points+count,reference);int refs=count;
    for(int axis=0;axis<2;axis++)for(int sign=-1;sign<=1;sign+=2) {
        Plane p={};p.n.x[axis]=sign;p.d=sign*origin.x[axis]+15;refs=clip(reference,refs,p);
    }
    if(refs<3)return false;
    Point seed={};for(int i=0;i<refs;i++)seed=add(seed,reference[i],1.0/refs);
    Point projected=add(origin,normal,-dot(add(origin,points[0],-1),normal));
    bool contained=true;
    for(int i=0;i<count;i++) {
        const Plane p=edgePlane(points[i],points[(i+1)%count],normal);
        if(dot(projected,p.n)>p.d+0.001)contained=false;
    }
    if(contained)seed=projected;
    vec3_t mins,maxs;
    for(int axis=0;axis<3;axis++) {
        mins[axis]=maxs[axis]=points[0].x[axis];
        for(int i=1;i<count;i++){mins[axis]=std::min(double(mins[axis]),points[i].x[axis]);maxs[axis]=std::max(double(maxs[axis]),points[i].x[axis]);}
    }
    maxs[2]+=0.5;
    std::vector<uint16_t> leaves(cm.numLeafs);int last;
    const int leafCount=CM_BoxLeafnums(mins,maxs,leaves.data(),leaves.size(),&last);
    std::vector<unsigned> indices;
    for(int i=0;i<leafCount;i++)worldBrushes(cm.leafs[leaves[i]].leafBrushNode,indices);
    std::sort(indices.begin(),indices.end());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
    std::vector<Obstacle> obstacles;
    for(unsigned index:indices) {
        if(index>=cm.numBrushes)continue;
        const cbrush_t& brush=cm.brushes[index];
        if(!(brush.contents&1) || brush.numsides>128)continue;
        bool overlaps=true;
        for(int axis=0;axis<3;axis++)if(brush.maxs[axis]<mins[axis] || brush.mins[axis]>maxs[axis])overlaps=false;
        if(!overlaps)continue;
        Obstacle obstacle={};obstacle.count=count;
        std::copy(points,points+count,obstacle.points);
        // Intersect just above the landing plane. Connectivity, not arbitrary
        // wall height, distinguishes an enclosing obstruction from a loose prop.
        for(unsigned i=0;i<brush.numsides+6 && obstacle.count>=3;i++) {
            Plane p=plane(brush,i);p.d-=p.n.x[2]*0.5;
            obstacle.count=clip(obstacle.points,obstacle.count,p);
        }
        obstacle.count=simplify(obstacle.points,obstacle.count);
        if(obstacle.count<3 || area(obstacle.points,obstacle.count,normal)<0.01)continue;
        for(int edge=0;edge<count;edge++) {
            const Plane p=edgePlane(points[edge],points[(edge+1)%count],normal);
            for(int i=0;i<obstacle.count;i++)if(std::abs(dot(obstacle.points[i],p.n)-p.d)<0.25)obstacle.wall=true;
        }
        obstacles.push_back(obstacle);
    }
    return clipWalls(points,count,normal,seed,obstacles);
}

struct Search {
    Point origin,normal,inside;
    Point best[8];int count=0;double bestArea=0,bestDistance=1e30;bool footprint=false;
    void brush(unsigned index) {
        if(index>=cm.numBrushes)return;
        const cbrush_t& b=cm.brushes[index];
        if(!(b.contents & 1) || b.numsides>128)return; // solid world brushes only
        for(unsigned i=0;i<b.numsides+6;i++) {
            const Plane p=plane(b,i);
            const double reach=footprint ? 15*(std::abs(p.n.x[0])+std::abs(p.n.x[1]))+0.5*std::abs(p.n.x[2]) : 0.5;
            if(dot(origin,p.n)-p.d>reach)return;
        }
        for(unsigned face=0;face<b.numsides+6;face++) {
            const Plane top=plane(b,face);
            if(footprint) { if(top.n.x[2]<0.7)continue; }
            else if(dot(top.n,normal)<0.99999 || std::abs(dot(origin,top.n)-top.d)>0.5)continue;
            Point center=add(origin,top.n,(top.d-dot(origin,top.n))/dot(top.n,top.n));
            Point u={{top.n.x[2],0,-top.n.x[0]}};u=add(Point{},u,1/std::sqrt(dot(u,u)));
            Point v=cross(top.n,u);v=add(Point{},v,1/std::sqrt(dot(v,v)));
            Point pts[64]={add(add(center,u,-8192),v,-8192),add(add(center,u,8192),v,-8192),add(add(center,u,8192),v,8192),add(add(center,u,-8192),v,8192)};
            int n=4;
            for(unsigned i=0;i<b.numsides+6 && n>=3;i++)if(i!=face)n=clip(pts,n,plane(b,i));
            // Match the editor's one-unit corner spacing. Removing a convex
            // vertex only shrinks the face; never extend a bevel into unsupported air.
            n=simplify(pts,n,1.0);
            if(n<3 || n>8)continue;
            double area=0;bool bounded=true;
            for(int i=0;i<n;i++) {
                const Point a=add(pts[i],center,-1),z=add(pts[(i+1)%n],center,-1);
                area+=dot(cross(a,z),top.n);
                if(dot(a,a)>4096.0*4096.0)bounded=false;
            }
            area=std::abs(area);
            double supportDistance=0;
            if(footprint) {
                // Clip a copy to the player's actual feet, not an expanded checkpoint.
                // The half-unit vertical tolerance covers collision padding only.
                Point contact[64];std::copy(pts,pts+n,contact);int contacts=n;
                for(int axis=0;axis<3 && contacts>=3;axis++)for(int sign=-1;sign<=1;sign+=2) {
                    Plane bound={};bound.n.x[axis]=sign;
                    bound.d=sign*origin.x[axis]+(axis==2?0.5:15.0);
                    contacts=clip(contact,contacts,bound);
                }
                if(contacts<3)continue;
                Point middle={};for(int i=0;i<contacts;i++)middle=add(middle,contact[i],1.0/contacts);
                const Point offset=add(middle,origin,-1);supportDistance=dot(offset,offset);
            }
            if(bounded && area>0 && (footprint ? supportDistance<bestDistance : area>bestArea)) {
                bestArea=area;bestDistance=supportDistance;count=n;std::copy(pts,pts+n,best);
            }
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
        const double reach=footprint && n.axis<2 ? 15.5 : 0.5;
        if(d>=-reach)node(index+n.data.children.childOffset[0],depth+1);
        if(d<=reach)node(index+n.data.children.childOffset[1],depth+1);
    }
};
}
#endif

extern "C" void Gsc_Platform_Face()
{
#ifdef COD4
    vec3_t origin,normal;stackGetParamVector(0,origin);stackGetParamVector(1,normal);
    Search search;
    int footprint=0;if(Scr_GetNumParam()>2)stackGetParamInt(2,&footprint);
    search.footprint=footprint!=0;
    for(int i=0;i<3;i++) {
        if(!std::isfinite(origin[i])||std::abs(origin[i])>131072||!std::isfinite(normal[i])) {stackPushUndefined();return;}
        search.origin.x[i]=origin[i];search.normal.x[i]=normal[i];
    }
    if(normal[2]<0.7 || std::abs(dot(search.normal,search.normal)-1)>0.01) {stackPushUndefined();return;}
    search.inside=search.footprint ? search.origin : add(search.origin,search.normal,-0.25);
    vec3_t inside;for(int i=0;i<3;i++)inside[i]=search.inside.x[i];
    if(search.footprint) {
        vec3_t mins,maxs;
        for(int axis=0;axis<3;axis++) {
            const float extent=axis==2?0.5f:15.0f;
            mins[axis]=origin[axis]-extent;maxs[axis]=origin[axis]+extent;
        }
        uint16_t leaves[1024];int lastLeaf;
        const int count=CM_BoxLeafnums(mins,maxs,leaves,1024,&lastLeaf);
        for(int i=0;i<count;i++)search.node(cm.leafs[leaves[i]].leafBrushNode);
    } else {
        int leaf=CM_PointLeafnum(inside);
        if(leaf>=0 && unsigned(leaf)<cm.numLeafs)search.node(cm.leafs[leaf].leafBrushNode);
    }
    if(!search.count) {stackPushUndefined();return;}
    int trim=0;if(Scr_GetNumParam()>3)stackGetParamInt(3,&trim);
    if(trim) {
        Point points[64];std::copy(search.best,search.best+search.count,points);
        if(!trimWalls(points,search.count,search.origin)){stackPushUndefined();return;}
        std::copy(points,points+search.count,search.best);
    }
    stackMakeArray();
    for(int i=0;i<search.count;i++) {vec3_t p;for(int j=0;j<3;j++)p[j]=search.best[i].x[j];stackPushVector(p);stackPushArrayNext();}
#else
    stackPushUndefined(); // CoD2 continues to use the GSC detector.
#endif
}
