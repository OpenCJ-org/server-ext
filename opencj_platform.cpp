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

// A landing can be built from several coplanar brushes (e.g. a solid rim
// around player-solid glass). Merge only when their union covers the hull.
// Convex hull alone would incorrectly fill holes and concave cutouts.
bool coveredHull(const Obstacle& hull,const std::vector<Obstacle>& surfaces,const Point& normal,std::vector<Obstacle>* gaps=nullptr) {
    std::vector<Obstacle> remaining(1,hull);
    for(const auto& surface:surfaces) {
        std::vector<Obstacle> next;
        for(const auto& piece:remaining) {
            Obstacle inside=piece;
            for(int e=0;e<surface.count && inside.count>=3;e++) {
                Plane boundary=edgePlane(surface.points[e],surface.points[(e+1)%surface.count],normal);
                boundary.d+=0.001; // BSP float precision at shared seams (0.001 game units).
                Obstacle outside=inside;
                Plane reverse={add(Point{},boundary.n,-1),-boundary.d};
                outside.count=clip(outside.points,outside.count,reverse);
                if(outside.count>=3 && area(outside.points,outside.count,normal)>0.0001)next.push_back(outside);
                inside.count=clip(inside.points,inside.count,boundary);
                if(next.size()>512)return false;
            }
        }
        remaining.swap(next);
        if(remaining.empty())return true;
    }
    double uncovered=0;for(const auto& p:remaining)uncovered+=area(p.points,p.count,normal);
    if(gaps)*gaps=remaining;
    return uncovered<0.001;
}
bool convexHull(const std::vector<Obstacle>& surfaces,Obstacle& hull) {
    std::vector<Point> points;
    for(const auto& s:surfaces)points.insert(points.end(),s.points,s.points+s.count);
    std::sort(points.begin(),points.end(),[](const Point& a,const Point& b){return a.x[0]<b.x[0] || (a.x[0]==b.x[0] && a.x[1]<b.x[1]);});
    points.erase(std::unique(points.begin(),points.end(),[](const Point& a,const Point& b){return std::abs(a.x[0]-b.x[0])<0.00001 && std::abs(a.x[1]-b.x[1])<0.00001;}),points.end());
    if(points.size()<3)return false;
    std::vector<Point> out(points.size()*2);int k=0;
    for(const auto& p:points) {
        while(k>=2 && cross(add(out[k-1],out[k-2],-1),add(p,out[k-1],-1)).x[2]<=0.00001)--k;
        out[k++]=p;
    }
    const int lower=k+1;
    for(int i=int(points.size())-2;i>=0;i--) {
        while(k>=lower && cross(add(out[k-1],out[k-2],-1),add(points[i],out[k-1],-1)).x[2]<=0.00001)--k;
        out[k++]=points[i];
    }
    if(--k<3 || k>64)return false;
    std::copy(out.begin(),out.begin()+k,hull.points);hull.count=simplify(hull.points,k,1.0);
    return hull.count>=3 && hull.count<=8;
}
// Search all useful separating edges instead of greedily clipping each gap.
// Bounds keep pathological map geometry from monopolizing a server frame.
struct SupportedRegion {
    const std::vector<Obstacle>& gaps;
    Point normal,seed;
    Obstacle best={};double bestArea=0;int visits=0;const Obstacle* required=nullptr;
    void search(Obstacle region) {
        if(++visits>4096 || region.count<3)return;
        const double size=area(region.points,region.count,normal);
        if(size<=bestArea+0.001)return;
        if(required)for(int i=0;i<region.count;i++) {
            const Plane edge=edgePlane(region.points[i],region.points[(i+1)%region.count],normal);
            for(int j=0;j<required->count;j++)if(dot(required->points[j],edge.n)>edge.d+0.15)return;
        }
        const Obstacle* obstruction=nullptr;double largest=0;
        for(const auto& gap:gaps) {
            Point overlap[64];std::copy(region.points,region.points+region.count,overlap);
            const int n=intersection(overlap,region.count,gap,normal);
            const double a=n>=3?area(overlap,n,normal):0;
            if(a>largest+0.001){largest=a;obstruction=&gap;}
        }
        if(!obstruction) {
            region.count=simplify(region.points,region.count,1.0);
            if(region.count>=3 && region.count<=8){best=region;bestArea=area(region.points,region.count,normal);}
            return;
        }
        std::vector<Obstacle> choices;
        for(int i=0;i<obstruction->count;i++) {
            const Plane edge=edgePlane(obstruction->points[i],obstruction->points[(i+1)%obstruction->count],normal);
            if(dot(seed,edge.n)<edge.d+0.01)continue;
            const Plane keep={add(Point{},edge.n,-1),-edge.d-0.01};
            Obstacle candidate=region;candidate.count=clip(candidate.points,candidate.count,keep);
            candidate.count=simplify(candidate.points,candidate.count);
            if(candidate.count>=3 && area(candidate.points,candidate.count,normal)>bestArea+0.001)choices.push_back(candidate);
        }
        std::sort(choices.begin(),choices.end(),[this](const Obstacle& a,const Obstacle& b){return area(a.points,a.count,normal)>area(b.points,b.count,normal);});
        for(const auto& candidate:choices)search(candidate);
    }
};

void mergeLanding(Point* points,int& count,const Point& origin) {
    const Point normal=cross(add(points[1],points[0],-1),add(points[2],points[0],-1));
    const Point unit=add(Point{},normal,1/std::sqrt(dot(normal,normal)));
    const double height=dot(points[0],unit);
    std::vector<Obstacle> surfaces(1);surfaces[0].count=count;std::copy(points,points+count,surfaces[0].points);
    std::vector<unsigned> visited;
    for(size_t current=0;current<surfaces.size();current++) {
        const Obstacle source=surfaces[current];vec3_t mins,maxs;
        for(int axis=0;axis<3;axis++) {
            double low=source.points[0].x[axis],high=low;
            for(int j=1;j<source.count;j++){low=std::min(low,source.points[j].x[axis]);high=std::max(high,source.points[j].x[axis]);}
            mins[axis]=low-0.05;maxs[axis]=high+0.05;
        }
        std::vector<uint16_t> leaves(cm.numLeafs);int last;
        const int leafCount=CM_BoxLeafnums(mins,maxs,leaves.data(),leaves.size(),&last);
        std::vector<unsigned> indices;
        for(int i=0;i<leafCount;i++)worldBrushes(cm.leafs[leaves[i]].leafBrushNode,indices);
        std::sort(indices.begin(),indices.end());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
        for(unsigned index:indices) {
            if(index>=cm.numBrushes || std::find(visited.begin(),visited.end(),index)!=visited.end())continue;
            const cbrush_t& b=cm.brushes[index];
            if(!(b.contents&0x11) || b.numsides>128)continue; // Solid and player-solid glass.
            for(unsigned face=0;face<b.numsides+6;face++) {
                const Plane top=plane(b,face);
                if(dot(top.n,unit)<0.99999 || std::abs(top.d-height)>0.01)continue;
                Point u={{unit.x[2],0,-unit.x[0]}};u=add(Point{},u,1/std::sqrt(dot(u,u)));const Point v=cross(unit,u);
                Obstacle candidate={};candidate.count=4;
                candidate.points[0]=add(add(points[0],u,-8192),v,-8192);candidate.points[1]=add(add(points[0],u,8192),v,-8192);
                candidate.points[2]=add(add(points[0],u,8192),v,8192);candidate.points[3]=add(add(points[0],u,-8192),v,8192);
                for(unsigned j=0;j<b.numsides+6 && candidate.count>=3;j++)if(j!=face)candidate.count=clip(candidate.points,candidate.count,plane(b,j));
                candidate.count=simplify(candidate.points,candidate.count);
                if(candidate.count<3)continue;
                bool bounded=true;for(int j=0;j<candidate.count;j++){Point d=add(candidate.points[j],points[0],-1);if(dot(d,d)>4096.0*4096.0)bounded=false;}
                if(!bounded)continue;
                Obstacle contact=candidate;
                for(int j=0;j<source.count && contact.count>=3;j++) {
                    Plane edge=edgePlane(source.points[j],source.points[(j+1)%source.count],unit);edge.d+=0.01;
                    contact.count=clip(contact.points,contact.count,edge);
                }
                if(contact.count<3 || area(contact.points,contact.count,unit)<0.0001)continue;
                visited.push_back(index);surfaces.push_back(candidate);
                if(surfaces.size()>128)return; // Retain the original safe face on oversized components.
                break;
            }
        }
    }
    if(surfaces.size()<2)return;
    Obstacle hull={};
    if(!convexHull(surfaces,hull) || area(hull.points,hull.count,unit)<=area(points,count,unit)+0.01)return;
    // Inset the outer perimeter by 0.01 units to discard float-quantized brush
    // seam slivers at distant map coordinates. Interior holes remain unfilled.
    const Obstacle boundary=hull;
    for(int i=0;i<boundary.count && hull.count>=3;i++) {
        Plane edge=edgePlane(boundary.points[i],boundary.points[(i+1)%boundary.count],unit);edge.d-=0.01;
        hull.count=clip(hull.points,hull.count,edge);
    }
    hull.count=simplify(hull.points,hull.count,1.0);
    if(hull.count<3 || hull.count>8)return;
    std::vector<Obstacle> gaps;
    if(!coveredHull(hull,surfaces,unit,&gaps)) {
        if(gaps.empty())return; // Coverage complexity limit: keep the original safe face.
        // The supported position, not the original brush center or brush order,
        // determines which side of a cutout remains eligible.
        Point seed=add(origin,unit,height-dot(origin,unit));
        bool inside=true;
        for(int i=0;i<count;i++){Plane e=edgePlane(points[i],points[(i+1)%count],unit);if(dot(seed,e.n)>e.d)inside=false;}
        if(!inside) {
            Obstacle contact={};contact.count=count;std::copy(points,points+count,contact.points);
            for(int axis=0;axis<2;axis++)for(int sign=-1;sign<=1;sign+=2) {
                Plane edge={};edge.n.x[axis]=sign;edge.d=sign*origin.x[axis]+15;
                contact.count=clip(contact.points,contact.count,edge);
            }
            if(contact.count<3)return;
            seed=Point{};for(int i=0;i<contact.count;i++)seed=add(seed,contact.points[i],1.0/contact.count);
        }
        SupportedRegion search={gaps,unit,seed};
        search.best.count=count;std::copy(points,points+count,search.best.points);search.bestArea=area(points,count,unit);
        const Obstacle original=search.best;search.required=&original;
        search.search(hull);
        hull=search.best;
        if(!coveredHull(hull,surfaces,unit))return;
    }
    count=hull.count;std::copy(hull.points,hull.points+count,points);
}

struct Search {
    Point origin,normal,inside;
    Point best[64];int count=0;double bestArea=0,bestDistance=1e30;bool footprint=false;
    void brush(unsigned index) {
        if(index>=cm.numBrushes)return;
        const cbrush_t& b=cm.brushes[index];
        if(!(b.contents & 0x11) || b.numsides>128)return; // solid world brushes only
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
        mergeLanding(search.best,search.count,search.origin);
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
