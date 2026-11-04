#include "gsc_custom_utils.hpp"
#ifdef COD4
extern "C" {
#include "../cod4x-server/src/cm_local.h"
#include "../cod4x-server/src/xassets/xmodel.h"
}
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
// Double precision avoids losing the landing plane on distant, thin slopes.
// Use one tolerance for joining floor faces and verifying their coverage.
// Otherwise a seam accepted during collection becomes a false gap afterward.
constexpr double surfaceSeamTolerance = 0.01;
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
// Start with the brush bounds projected onto the face plane, so the temporary
// polygon always encloses the complete face regardless of player position.
int brushFace(const cbrush_t& brush,unsigned face,const Point& center,const Point& normal,Point* points) {
    Point u={{normal.x[2],0,-normal.x[0]}};u=add(Point{},u,1/std::sqrt(dot(u,u)));
    const Point v=cross(normal,u);
    double loU=1e30,hiU=-1e30,loV=1e30,hiV=-1e30;
    for(int corner=0;corner<8;corner++) {
        Point p;for(int axis=0;axis<3;axis++)p.x[axis]=(corner&(1<<axis))?brush.maxs[axis]:brush.mins[axis];
        p=add(p,center,-1);const double du=dot(p,u),dv=dot(p,v);
        loU=std::min(loU,du);hiU=std::max(hiU,du);loV=std::min(loV,dv);hiV=std::max(hiV,dv);
    }
    points[0]=add(add(center,u,loU-1),v,loV-1);points[1]=add(add(center,u,hiU+1),v,loV-1);
    points[2]=add(add(center,u,hiU+1),v,hiV+1);points[3]=add(add(center,u,loU-1),v,hiV+1);
    int count=4;
    for(unsigned i=0;i<brush.numsides+6 && count>=3;i++)if(i!=face)count=clip(points,count,plane(brush,i));
    return simplify(points,count);
}

double area(const Point* pts,int n,const Point& normal) {
    double sum=0;
    for(int i=1;i<n-1;i++)sum+=dot(cross(add(pts[i],pts[0],-1),add(pts[i+1],pts[0],-1)),normal);
    return std::abs(sum)*0.5;
}
// Use the whole polygon: a tiny first bevel is a poor basis after float rounding.
Point polygonNormal(const Point* points,int count) {
    Point sum={};
    for(int i=1;i<count-1;i++)sum=add(sum,cross(add(points[i],points[0],-1),add(points[i+1],points[0],-1)),1);
    const double length=std::sqrt(dot(sum,sum));
    return length>1e-12?add(Point{},sum,1/length):Point{};
}
Plane edgePlane(const Point& a,const Point& b,const Point& normal) {
    Point n=cross(add(b,a,-1),normal);n=add(Point{},n,1/std::sqrt(dot(n,n)));
    return {n,dot(n,a)};
}
struct Obstacle { Point points[64];int count;bool wall;double clearance=0; };
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
// Canonical geometry ordering gives equal-area solutions and budget handling a
// stable result regardless of BSP brush order or triangle winding/start vertex.
void canonicalize(Obstacle& polygon) {
    int first=0;
    for(int i=1;i<polygon.count;i++) {
        for(int axis=0;axis<3;axis++) {
            if(polygon.points[i].x[axis]<polygon.points[first].x[axis]){first=i;break;}
            if(polygon.points[i].x[axis]>polygon.points[first].x[axis])break;
        }
    }
    std::rotate(polygon.points,polygon.points+first,polygon.points+polygon.count);
}
bool polygonLess(const Obstacle& a,const Obstacle& b) {
    if(a.count!=b.count)return a.count<b.count;
    for(int i=0;i<a.count;i++)for(int axis=0;axis<3;axis++)
        if(a.points[i].x[axis]!=b.points[i].x[axis])return a.points[i].x[axis]<b.points[i].x[axis];
    return false;
}
// Search all useful separating edges instead of greedily clipping each gap.
// Bounds keep pathological map geometry from monopolizing a server frame.
struct SupportedRegion {
    const std::vector<Obstacle>& gaps;
    Point normal,seed;
    Obstacle best={};double bestArea=0;int visits=0;
    double margin=0.01;bool exhausted=false;
    void search(Obstacle region,int depth=0) {
        if(++visits>4096 || depth>64){exhausted=true;return;}
        if(region.count<3)return;
        const double size=area(region.points,region.count,normal);
        if(size<=bestArea+0.001)return;
        const Obstacle* obstruction=nullptr;double largest=0;
        for(const auto& gap:gaps) {
            Point overlap[64];std::copy(region.points,region.points+region.count,overlap);
            const int n=intersection(overlap,region.count,gap,normal);
            const double a=n>=3?area(overlap,n,normal):0;
            if(a>largest+0.001){largest=a;obstruction=&gap;}
        }
        if(!obstruction) {
            // Small supported ledges are valid; discard only numerical duplicates.
            region.count=simplify(region.points,region.count);
            if(region.count>=3 && region.count<=8){best=region;bestArea=area(region.points,region.count,normal);}
            return;
        }
        std::vector<Obstacle> choices;
        for(int i=0;i<obstruction->count;i++) {
            const Plane edge=edgePlane(obstruction->points[i],obstruction->points[(i+1)%obstruction->count],normal);
            if(dot(seed,edge.n)<edge.d+std::max(margin,obstruction->clearance))continue;
            const double clearance=std::max(margin,obstruction->clearance);
            const Plane keep={add(Point{},edge.n,-1),-edge.d-clearance};
            Obstacle candidate=region;candidate.count=clip(candidate.points,candidate.count,keep);
            candidate.count=simplify(candidate.points,candidate.count);
            if(candidate.count>=3 && area(candidate.points,candidate.count,normal)>bestArea+0.001){canonicalize(candidate);choices.push_back(candidate);}
        }
        std::sort(choices.begin(),choices.end(),[this](const Obstacle& a,const Obstacle& b){const double aa=area(a.points,a.count,normal),bb=area(b.points,b.count,normal);
            if(aa!=bb)return aa>bb;
            return polygonLess(a,b);});
        for(const auto& candidate:choices){search(candidate,depth+1);if(exhausted)return;}
    }
};

void connectWalls(std::vector<Obstacle>& obstacles,const Point& normal) {
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
}

// Collision patches are triangle soups, not brushes. Intersect their surfaces
// with the same floor-height slice and feed their connected edges to the joint selection.
bool triangleSection(const Point* triangle,const Plane& slice,const Point& normal,Obstacle& out) {
    Point hits[3];int n=0;
    for(int i=0;i<3;i++) {
        const Point a=triangle[i],b=triangle[(i+1)%3];
        const double da=dot(a,slice.n)-slice.d,db=dot(b,slice.n)-slice.d;
        if((da<=0 && db>0)||(db<=0 && da>0))hits[n++]=add(a,add(b,a,-1),da/(da-db));
    }
    if(n!=2)return false; // Coplanar floors and triangles outside the slice.
    // Project the slice back down onto the landing plane.
    hits[0].x[2]-=0.5;hits[1].x[2]-=0.5;
    const Point edge=add(hits[1],hits[0],-1);const double length=std::sqrt(dot(edge,edge));
    if(length<0.001)return false;
    const Point offset=add(Point{},cross(edge,normal),0.01/length);
    out={};out.count=4;
    out.points[0]=add(hits[0],offset,1);out.points[1]=add(hits[1],offset,1);
    out.points[2]=add(hits[1],offset,-1);out.points[3]=add(hits[0],offset,-1);
    return true;
}
bool terrainTriangles(unsigned index,const vec3_t mins,const vec3_t maxs,std::vector<int>& triangles,int depth=0) {
    if(index>=unsigned(cm.aabbTreeCount) || depth>64)return false;
    const auto& tree=cm.aabbTrees[index];
    for(int axis=0;axis<3;axis++)if(tree.origin[axis]+tree.halfSize[axis]<mins[axis] || tree.origin[axis]-tree.halfSize[axis]>maxs[axis])return true;
    if(tree.materialIndex>=cm.numMaterials)return false;
    if(!(cm.materials[tree.materialIndex].contentFlags&0x11))return true;
    if(tree.childCount) {
        for(unsigned i=0;i<tree.childCount;i++)if(!terrainTriangles(tree.u.firstChildIndex+i,mins,maxs,triangles,depth+1))return false;
    } else {
        if(tree.u.partitionIndex<0 || tree.u.partitionIndex>=cm.partitionCount)return false;
        const auto& partition=cm.partitions[tree.u.partitionIndex];
        for(unsigned i=0;i<unsigned(static_cast<unsigned char>(partition.triCount));i++) {
            const int tri=partition.firstTri+i;
            if(tri<0 || tri>=cm.triCount || triangles.size()>=16384)return false;
            triangles.push_back(tri);
        }
    }
    return true;
}
// The runtime XModelTraceLine layout includes the triangle pointer/count omitted
// by the public XModelCollSurf header. Keep this engine view local to detection.
struct ModelCollisionSurface {
    const float (*triangles)[3][4];int triangleCount;
    float mins[3],maxs[3];int boneIndex,contents,surfaceFlags;
};
static_assert(sizeof(ModelCollisionSurface)==44,"CoD4 model collision surface ABI");
static_assert(offsetof(XModel,collSurfs)==0x98 && offsetof(XModel,collLod)==0xc6,"CoD4 model collision ABI");
bool planeIntersection(const Plane* p,Point& result) {
    const Point a=cross(p[1].n,p[2].n),b=cross(p[2].n,p[0].n),c=cross(p[0].n,p[1].n);
    const double determinant=dot(p[0].n,a);
    if(std::abs(determinant)<1e-15)return false;
    result=add(add(add(Point{},a,p[0].d),b,p[1].d),c,p[2].d);
    result=add(Point{},result,1/determinant);
    for(double v:result.x)if(!std::isfinite(v))return false;
    return true;
}
bool modelTriangle(const float encoded[3][4],const cStaticModel_t& model,Obstacle& triangle) {
    Plane local[3];
    for(int i=0;i<3;i++){for(int j=0;j<3;j++)local[i].n.x[j]=encoded[i][j];local[i].d=encoded[i][3];}
    triangle={};triangle.count=3;
    for(int i=0;i<3;i++) {
        Plane planes[3]={local[0],local[1],local[2]};
        if(i)planes[i].d+=1; // Barycentric coordinates (0,0), (1,0), (0,1).
        Point vertex;if(!planeIntersection(planes,vertex))return false;
        Plane transform[3];
        for(int j=0;j<3;j++){for(int k=0;k<3;k++)transform[j].n.x[k]=model.invScaledAxis[k][j];transform[j].d=vertex.x[j];}
        if(!planeIntersection(transform,triangle.points[i]))return false;
        for(int j=0;j<3;j++)triangle.points[i].x[j]+=model.origin[j];
    }
    return true;
}
bool boundsOverlap(const float* low,const float* high,const vec3_t mins,const vec3_t maxs) {
    for(int axis=0;axis<3;axis++)if(high[axis]<mins[axis] || low[axis]>maxs[axis])return false;
    return true;
}
bool appendTriangle(const Obstacle& triangle,const vec3_t mins,const vec3_t maxs,std::vector<Obstacle>& out) {
    for(int axis=0;axis<3;axis++) {
        double low=triangle.points[0].x[axis],high=low;
        for(int i=1;i<3;i++){low=std::min(low,triangle.points[i].x[axis]);high=std::max(high,triangle.points[i].x[axis]);}
        if(high<mins[axis] || low>maxs[axis])return true;
    }
    if(out.size()>=16384)return false;
    out.push_back(triangle);return true;
}
// One bounded source of exact triangles for both wall slices and landing unions.
// Static models use collision surfaces, never their render geometry or bounds as floors.
bool collisionTriangles(const vec3_t mins,const vec3_t maxs,std::vector<Obstacle>& out) {
    std::vector<uint16_t> leaves(cm.numLeafs);int last;
    const int count=CM_BoxLeafnums(mins,maxs,leaves.data(),leaves.size(),&last);
    std::vector<int> triangles;
    for(int i=0;i<count;i++) {
        const auto& leaf=cm.leafs[leaves[i]];
        for(unsigned j=0;j<leaf.collAabbCount;j++)if(!terrainTriangles(leaf.firstCollAabbIndex+j,mins,maxs,triangles))return false;
    }
    std::sort(triangles.begin(),triangles.end());triangles.erase(std::unique(triangles.begin(),triangles.end()),triangles.end());
    for(int index:triangles) {
        Obstacle triangle={};triangle.count=3;
        for(int i=0;i<3;i++) {
            const unsigned vertex=cm.triIndices[index*3+i];if(vertex>=cm.vertCount)return false;
            for(int axis=0;axis<3;axis++)triangle.points[i].x[axis]=cm.verts[vertex][axis];
        }
        if(!appendTriangle(triangle,mins,maxs,out))return false;
    }
    for(unsigned i=0;i<cm.numStaticModels;i++) {
        const auto& model=cm.staticModelList[i];
        if(!boundsOverlap(model.absmin,model.absmax,mins,maxs) || !model.xmodel || model.xmodel->collLod<0)continue;
        const auto* surfaces=reinterpret_cast<const ModelCollisionSurface*>(model.xmodel->collSurfs);
        if(!surfaces)continue;
        if(model.xmodel->numCollSurfs<0 || model.xmodel->numCollSurfs>4096)return false;
        unsigned visited=0;
        for(int j=0;j<model.xmodel->numCollSurfs;j++) {
            const auto& surface=surfaces[j];if(!(surface.contents&0x11))continue;
            if(surface.triangleCount<0 || unsigned(surface.triangleCount)>65536 || (surface.triangleCount && !surface.triangles))return false;
            for(int k=0;k<surface.triangleCount;k++) {
                if(++visited>65536)return false;
                Obstacle triangle;if(!modelTriangle(surface.triangles[k],model,triangle))return false;
                if(!appendTriangle(triangle,mins,maxs,out))return false;
            }
        }
    }
    return true;
}
// Work from player-solid brush intersections, never sampled rays. Freestanding objects
// remain ignored; solids connected to the landing boundary are walls.
bool collectWalls(const Point* points,int count,std::vector<Obstacle>& obstacles) {
    Point normal=polygonNormal(points,count);
    normal=add(Point{},normal,1/std::sqrt(dot(normal,normal)));
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
    for(unsigned index:indices) {
        if(index>=cm.numBrushes)continue;
        const cbrush_t& brush=cm.brushes[index];
        if(!(brush.contents&0x11) || brush.numsides>128)continue; // Solid and player-solid glass walls.
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
        obstacles.push_back(obstacle);
        if(obstacles.size()>2048)return false;
    }
    std::vector<Obstacle> triangles;
    if(!collisionTriangles(mins,maxs,triangles))return false;
    const Plane slice={normal,dot(points[0],normal)+normal.x[2]*0.5};
    for(const auto& triangle:triangles) {
        Obstacle obstacle;
        if(!triangleSection(triangle.points,slice,normal,obstacle))continue;
        for(int i=0;i<count && obstacle.count>=3;i++)obstacle.count=clip(obstacle.points,obstacle.count,edgePlane(points[i],points[(i+1)%count],normal));
        obstacle.count=simplify(obstacle.points,obstacle.count);
        if(obstacle.count<3 || area(obstacle.points,obstacle.count,normal)<0.001)continue;
        obstacles.push_back(obstacle);
        if(obstacles.size()>2048)return false;
    }
    return true;
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
                boundary.d+=surfaceSeamTolerance; // Same BSP seam tolerance as face connectivity.
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
    std::copy(out.begin(),out.begin()+k,hull.points);hull.count=simplify(hull.points,k);
    return hull.count>=3;
}

bool triangleFace(Obstacle& face,const Point& normal,double height) {
    Point n=cross(add(face.points[1],face.points[0],-1),add(face.points[2],face.points[0],-1));
    const double length=std::sqrt(dot(n,n));if(length<0.000001)return false;
    n=add(Point{},n,1/length);
    if(std::abs(dot(n,normal))<0.99999)return false;
    for(int i=0;i<face.count;i++)if(std::abs(dot(face.points[i],normal)-height)>0.01)return false;
    if(dot(n,normal)<0)std::reverse(face.points,face.points+face.count);
    canonicalize(face);return true;
}
bool connectedSurface(const Obstacle& source,const Obstacle& candidate,const Point& normal) {
    Obstacle contact=candidate;
    for(int i=0;i<source.count && contact.count>=3;i++) {
        Plane edge=edgePlane(source.points[i],source.points[(i+1)%source.count],normal);edge.d+=surfaceSeamTolerance;
        contact.count=clip(contact.points,contact.count,edge);
    }
    return contact.count>=3 && area(contact.points,contact.count,normal)>0.0001;
}
bool collectLanding(const Point* points,int count,std::vector<Obstacle>& surfaces) {
    const Point normal=polygonNormal(points,count);
    const Point unit=add(Point{},normal,1/std::sqrt(dot(normal,normal)));
    const double height=dot(points[0],unit);
    surfaces.resize(1);surfaces[0].count=count;std::copy(points,points+count,surfaces[0].points);
    // Bound work by face count and clipping complexity, not world-space size.
    // A long, simple floor is cheaper than a small, highly fragmented one.
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
                Obstacle candidate={};candidate.count=brushFace(b,face,points[0],unit,candidate.points);
                if(candidate.count<3)continue;
                Obstacle contact=candidate;
                for(int j=0;j<source.count && contact.count>=3;j++) {
                    Plane edge=edgePlane(source.points[j],source.points[(j+1)%source.count],unit);edge.d+=surfaceSeamTolerance;
                    contact.count=clip(contact.points,contact.count,edge);
                }
                if(contact.count<3 || area(contact.points,contact.count,unit)<0.0001)continue;
                visited.push_back(index);surfaces.push_back(candidate);
                if(surfaces.size()>4096)return false; // Bound editor work; never silently return a partial component.
                break;
            }
        }
        std::vector<Obstacle> triangles;
        if(!collisionTriangles(mins,maxs,triangles))return false;
        for(auto candidate:triangles) {
            if(!triangleFace(candidate,unit,height) || !connectedSurface(source,candidate,unit))continue;
            bool duplicate=false;
            for(auto existing:surfaces){canonicalize(existing);if(!polygonLess(candidate,existing) && !polygonLess(existing,candidate)){duplicate=true;break;}}
            if(duplicate)continue;
            surfaces.push_back(candidate);if(surfaces.size()>4096)return false;
        }
    }
    for(auto& surface:surfaces)canonicalize(surface);
    std::sort(surfaces.begin(),surfaces.end(),polygonLess);
    surfaces.erase(std::unique(surfaces.begin(),surfaces.end(),[](const Obstacle& a,const Obstacle& b){return !polygonLess(a,b)&&!polygonLess(b,a);}),surfaces.end());
    return true;
}

// Same circular edge contact and slope-height allowance as checkpointArea::touchesPlayer.
// The bounding box used to collect candidates is only a broad-phase query.
bool playerContact(const Point* points,int count,const Point& origin,Point& seed,double& distance) {
    Point normal=polygonNormal(points,count);
    normal=add(Point{},normal,1/std::sqrt(dot(normal,normal)));
    if(normal.x[2]<0.7)return false;
    bool inside=true;
    for(int i=0;i<count;i++) {
        const Point edge=add(points[(i+1)%count],points[i],-1),delta=add(origin,points[i],-1);
        if(edge.x[0]*delta.x[1]-edge.x[1]*delta.x[0]<-0.0001*std::hypot(edge.x[0],edge.x[1]))inside=false;
    }
    Point candidate=origin;candidate.x[2]=(dot(points[0],normal)-normal.x[0]*origin.x[0]-normal.x[1]*origin.x[1])/normal.x[2];
    double nearest=0;
    if(!inside) {
        nearest=1e30;
        for(int i=0;i<count;i++) {
            const Point a=points[i],d=add(points[(i+1)%count],a,-1);
            const double length=d.x[0]*d.x[0]+d.x[1]*d.x[1];if(length<1e-12)continue;
            const double t=std::max(0.0,std::min(1.0,((origin.x[0]-a.x[0])*d.x[0]+(origin.x[1]-a.x[1])*d.x[1])/length));
            const Point p=add(a,d,t);const double dx=p.x[0]-origin.x[0],dy=p.x[1]-origin.x[1];
            if(dx*dx+dy*dy<nearest){nearest=dx*dx+dy*dy;candidate=p;}
        }
    }
    const double height=origin.x[2]-candidate.x[2];
    if(nearest>225 || height< -2 || height>4+15*(1/normal.x[2]-1))return false;
    seed=candidate;distance=nearest+height*height;return true;
}

// Expansion is only for identifying the real boundary of the union. It never
// enlarges a checkpoint. Interior props remain covered by the supporting floor.
bool classifyWalls(std::vector<Obstacle>& obstacles,const std::vector<Obstacle>& surfaces,const Point& normal) {
    for(auto& obstacle:obstacles) {
        // Bounded round expansion avoids arbitrarily long miters at acute
        // triangle/brush corners, which could falsely connect an interior prop.
        Point u={{normal.x[2],0,-normal.x[0]}};u=add(Point{},u,1/std::sqrt(dot(u,u)));const Point v=cross(normal,u);
        std::vector<Obstacle> offsets;
        for(int direction=0;direction<16;direction++) {
            const double angle=direction*(2*3.141592653589793/16);
            const Point offset=add(add(Point{},u,0.25*std::cos(angle)),v,0.25*std::sin(angle));
            Obstacle translated=obstacle;
            for(int i=0;i<translated.count;i++)translated.points[i]=add(translated.points[i],offset,1);
            offsets.push_back(translated);
        }
        Obstacle expanded={};if(!convexHull(offsets,expanded))return false;
        std::vector<Obstacle> outside;
        if(!coveredHull(expanded,surfaces,normal,&outside)) {
            if(outside.empty())return false;
            obstacle.wall=true;
        }
    }
    connectWalls(obstacles,normal);
    return true;
}

bool finalizeLanding(Obstacle& hull,const std::vector<Obstacle>& surfaces,std::vector<Obstacle> obstacles,const Point& origin,const Point& seed) {
    Point normal=polygonNormal(hull.points,hull.count);normal=add(Point{},normal,1/std::sqrt(dot(normal,normal)));
    if(!classifyWalls(obstacles,surfaces,normal))return false;
    std::vector<Obstacle> constraints;
    if(!coveredHull(hull,surfaces,normal,&constraints) && constraints.empty())return false;
    for(auto wall:obstacles)if(wall.wall){wall.clearance=0.125;constraints.push_back(wall);}
    for(auto& c:constraints)canonicalize(c);
    std::sort(constraints.begin(),constraints.end(),polygonLess);
    // Solve real gaps and walls together, before committing to one convex area.
    SupportedRegion search={constraints,normal,seed};search.search(hull);
    if(search.exhausted || search.best.count<3)return false;
    hull=search.best;
    // Validate the exact float coordinates GSC will receive, not just doubles.
    for(int i=0;i<hull.count;i++)for(double& coordinate:hull.points[i].x)coordinate=float(coordinate);
    if(hull.count>8 || !coveredHull(hull,surfaces,normal))return false;
    for(int i=0;i<hull.count;i++) {
        const Point a=hull.points[i],b=hull.points[(i+1)%hull.count];const Point edge=add(b,a,-1);
        if(dot(edge,edge)<0.0001 || std::abs(dot(add(a,hull.points[0],-1),normal))>0.25)return false;
        for(int j=0;j<hull.count;j++)if(j!=i && j!=(i+1)%hull.count) {
            const Point d=add(hull.points[j],a,-1);
            if(edge.x[0]*d.x[1]-edge.x[1]*d.x[0]<=0.0001*std::hypot(edge.x[0],edge.x[1]))return false;
        }
    }
    for(const auto& wall:obstacles)if(wall.wall) {
        Point overlap[64];std::copy(hull.points,hull.points+hull.count,overlap);
        const int n=intersection(overlap,hull.count,wall,normal);
        if(n>=3 && area(overlap,n,normal)>0.01)return false;
    }
    Point contact;double distance;
    return playerContact(hull.points,hull.count,origin,contact,distance);
}

bool detectLanding(Point* points,int& count,const Point& origin) {
    std::vector<Obstacle> surfaces;if(!collectLanding(points,count,surfaces))return false;
    Obstacle hull={};if(!convexHull(surfaces,hull))return false;
    const Point n=polygonNormal(hull.points,hull.count);const Point normal=add(Point{},n,1/std::sqrt(dot(n,n)));
    Point seed;double distance;if(!playerContact(points,count,origin,seed,distance))return false;
    std::vector<Obstacle> walls;if(!collectWalls(hull.points,hull.count,walls))return false;
    // Inset only the outside hull for BSP seam/float precision; holes stay holes.
    const Obstacle boundary=hull;
    for(int i=0;i<boundary.count && hull.count>=3;i++) {
        Plane edge=edgePlane(boundary.points[i],boundary.points[(i+1)%boundary.count],normal);edge.d-=surfaceSeamTolerance;
        hull.count=clip(hull.points,hull.count,edge);
    }
    if(hull.count<3 || !finalizeLanding(hull,surfaces,walls,origin,seed))return false;
    count=hull.count;std::copy(hull.points,hull.points+count,points);return true;
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
            const double reach=footprint ? 15*(std::abs(p.n.x[0])+std::abs(p.n.x[1]))+11*std::abs(p.n.x[2]) : 0.5;
            if(dot(origin,p.n)-p.d>reach)return;
        }
        for(unsigned face=0;face<b.numsides+6;face++) {
            const Plane top=plane(b,face);
            if(footprint) { if(top.n.x[2]<0.7)continue; }
            else if(dot(top.n,normal)<0.99999 || std::abs(dot(origin,top.n)-top.d)>0.5)continue;
            Point center=add(origin,top.n,(top.d-dot(origin,top.n))/dot(top.n,top.n));
            Point pts[64];int n=brushFace(b,face,center,top.n,pts);
            if(n<3)continue;
            double area=0;
            for(int i=0;i<n;i++) {
                const Point a=add(pts[i],center,-1),z=add(pts[(i+1)%n],center,-1);
                area+=dot(cross(a,z),top.n);
            }
            area=std::abs(area);
            double supportDistance=0;
            if(footprint) {
                Point contact;
                if(!playerContact(pts,n,origin,contact,supportDistance))continue;
            }
            if(area>0 && (footprint ? supportDistance<bestDistance : area>bestArea)) {
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
        const double reach=footprint ? (n.axis<2?15.5:11.0) : 0.5;
        if(d>=-reach)node(index+n.data.children.childOffset[0],depth+1);
        if(d<=reach)node(index+n.data.children.childOffset[1],depth+1);
    }
    bool triangleSupport() {
        vec3_t mins,maxs;
        for(int axis=0;axis<3;axis++){const double reach=axis==2?(footprint?11.0:0.5):(footprint?15.0:0.5);mins[axis]=origin.x[axis]-reach;maxs[axis]=origin.x[axis]+reach;}
        std::vector<Obstacle> triangles;if(!collisionTriangles(mins,maxs,triangles))return false;
        for(auto face:triangles) {
            Point up=cross(add(face.points[1],face.points[0],-1),add(face.points[2],face.points[0],-1));
            const double length=std::sqrt(dot(up,up));if(length<0.000001)continue;
            up=add(Point{},up,1/length);if(up.x[2]<0)up=add(Point{},up,-1);
            if(up.x[2]<0.7 || (!footprint && dot(up,normal)<0.99999))continue;
            const double height=dot(face.points[0],up);
            if(!triangleFace(face,up,height))continue;
            Point contact;double distance;
            if(!playerContact(face.points,face.count,origin,contact,distance))continue;
            if(!footprint && distance>0.25)continue;
            if(!count || (footprint && distance<bestDistance)) {
                count=face.count;std::copy(face.points,face.points+count,best);bestDistance=distance;
            }
        }
        return true;
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
            const float extent=axis==2?11.0f:15.0f;
            mins[axis]=origin[axis]-extent;maxs[axis]=origin[axis]+extent;
        }
        uint16_t leaves[1024];int lastLeaf;
        const int count=CM_BoxLeafnums(mins,maxs,leaves,1024,&lastLeaf);
        for(int i=0;i<count;i++)search.node(cm.leafs[leaves[i]].leafBrushNode);
    } else {
        int leaf=CM_PointLeafnum(inside);
        if(leaf>=0 && unsigned(leaf)<cm.numLeafs)search.node(cm.leafs[leaf].leafBrushNode);
    }
    if(!search.triangleSupport() || !search.count) {stackPushUndefined();return;}
    int trim=0;if(Scr_GetNumParam()>3)stackGetParamInt(3,&trim);
    if(trim) {
        if(!detectLanding(search.best,search.count,search.origin)){stackPushUndefined();return;}
    }
    stackMakeArray();
    for(int i=0;i<search.count;i++) {vec3_t p;for(int j=0;j<3;j++)p[j]=search.best[i].x[j];stackPushVector(p);stackPushArrayNext();}
#else
    stackPushUndefined(); // CoD2 continues to use the GSC detector.
#endif
}

// Read once when binding an editor checkpoint. Gameplay uses getGroundEntity().
extern "C" void Gsc_Platform_EntityInfo()
{
#ifdef COD4
    int number;
    if(!stackGetParams("i", &number) || number < 0 || number >= ENTITYNUM_WORLD || !g_entities[number].r.inuse) {
        stackPushUndefined(); return;
    }
    const gentity_t& ent = g_entities[number];
    stackMakeArray();
    stackPushInt(ent.r.bmodel ? ent.s.index : 0); stackPushArrayNext();
    stackPushVector(ent.r.mins); stackPushArrayNext();
    stackPushVector(ent.r.maxs); stackPushArrayNext();
#else
    stackPushUndefined();
#endif
}
