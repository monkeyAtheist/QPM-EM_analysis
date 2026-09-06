#include "antenna_surface_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace AntennaSurface
{
namespace
{
constexpr double Pi = NumericalEM::Pi;

NumericalEM::Vec3 add(const NumericalEM::Vec3 &a, const NumericalEM::Vec3 &b)
{
    return {a.x+b.x,a.y+b.y,a.z+b.z};
}
NumericalEM::Vec3 sub(const NumericalEM::Vec3 &a, const NumericalEM::Vec3 &b)
{
    return {a.x-b.x,a.y-b.y,a.z-b.z};
}
NumericalEM::Vec3 cross(const NumericalEM::Vec3 &a, const NumericalEM::Vec3 &b)
{
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double length(const NumericalEM::Vec3 &v)
{
    return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
}

NumericalEM::Vec3 rotateEuler(const NumericalEM::Vec3 &v, double yawDeg, double pitchDeg, double rollDeg)
{
    const double r=rollDeg*Pi/180.0,p=pitchDeg*Pi/180.0,y=yawDeg*Pi/180.0;
    const double cr=std::cos(r),sr=std::sin(r),cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y);
    // Intrinsic/local X roll, then world-equivalent Y pitch, then Z yaw (Rz*Ry*Rx).
    const double x1=v.x;
    const double y1=cr*v.y-sr*v.z;
    const double z1=sr*v.y+cr*v.z;
    const double x2=cp*x1+sp*z1;
    const double y2=y1;
    const double z2=-sp*x1+cp*z1;
    return {cy*x2-sy*y2,sy*x2+cy*y2,z2};
}

NumericalEM::Vec3 basePoint(int orientation, double u, double v, double normal)
{
    if(orientation==1) return {u,normal,v}; // XZ; historical antenna-editor convention
    if(orientation==2) return {normal,u,v}; // YZ
    return {u,v,normal};                    // XY
}

int limitedDivisions(double lengthM, double hintM, int minimum, int maximum)
{
    const double h=std::max(1e-9,hintM);
    return std::clamp(static_cast<int>(std::ceil(std::max(0.0,lengthM)/h)),minimum,maximum);
}

void appendQuad(std::vector<SurfaceTriangle> &out,
                const NumericalEM::Vec3 &a,const NumericalEM::Vec3 &b,
                const NumericalEM::Vec3 &c,const NumericalEM::Vec3 &d,
                std::size_t maxTriangles)
{
    if(out.size()+2>maxTriangles) return;
    out.push_back({a,b,c});
    out.push_back({a,c,d});
}

} // namespace

SurfaceFrame frameAxes(const SurfaceSpec &spec)
{
    SurfaceFrame f;
    f.u=rotateEuler(basePoint(std::clamp(spec.baseOrientation,0,2),1.0,0.0,0.0),spec.yawDeg,spec.pitchDeg,spec.rollDeg);
    f.v=rotateEuler(basePoint(std::clamp(spec.baseOrientation,0,2),0.0,1.0,0.0),spec.yawDeg,spec.pitchDeg,spec.rollDeg);
    f.normal=rotateEuler(basePoint(std::clamp(spec.baseOrientation,0,2),0.0,0.0,1.0),spec.yawDeg,spec.pitchDeg,spec.rollDeg);
    return f;
}

NumericalEM::Vec3 vectorFromLocal(const SurfaceSpec &spec, double u, double v, double normal)
{
    const auto f=frameAxes(spec);
    return {f.u.x*u+f.v.x*v+f.normal.x*normal,
            f.u.y*u+f.v.y*v+f.normal.y*normal,
            f.u.z*u+f.v.z*v+f.normal.z*normal};
}

NumericalEM::Vec3 localFromPoint(const SurfaceSpec &spec, const NumericalEM::Vec3 &worldPoint)
{
    const auto f=frameAxes(spec);
    const auto d=sub(worldPoint,spec.center);
    return {d.x*f.u.x+d.y*f.u.y+d.z*f.u.z,
            d.x*f.v.x+d.y*f.v.y+d.z*f.v.z,
            d.x*f.normal.x+d.y*f.normal.y+d.z*f.normal.z};
}

NumericalEM::Vec3 pointFromLocal(const SurfaceSpec &spec, double u, double v, double normal)
{
    return add(spec.center,vectorFromLocal(spec,u,v,normal));
}

std::vector<SurfaceTriangle> triangulate(const SurfaceSpec &in, std::size_t maxTriangles)
{
    SurfaceSpec s=in;
    s.radiusM=std::max(1e-9,std::abs(s.radiusM));
    s.innerRadiusM=std::clamp(std::abs(s.innerRadiusM),0.0,s.radiusM*0.999999);
    s.widthM=std::max(1e-9,std::abs(s.widthM));
    s.heightM=std::max(1e-9,std::abs(s.heightM));
    s.focalLengthM=std::max(1e-9,std::abs(s.focalLengthM));
    s.feedWidthM=std::clamp(std::abs(s.feedWidthM),1e-9,0.98*s.widthM);
    s.feedLengthM=std::max(0.0,std::abs(s.feedLengthM));
    s.insetDepthM=std::clamp(std::abs(s.insetDepthM),0.0,0.49*s.heightM);
    s.notchGapM=std::max(0.0,std::abs(s.notchGapM));
    s.meshHintM=std::max(1e-9,std::abs(s.meshHintM));
    maxTriangles=std::max<std::size_t>(2,maxTriangles);

    std::vector<SurfaceTriangle> out;
    out.reserve(std::min<std::size_t>(maxTriangles,2048));

    if(s.kind==SurfaceKind::Rectangle)
    {
        int nu=limitedDivisions(s.widthM,s.meshHintM,1,96),nv=limitedDivisions(s.heightM,s.meshHintM,1,96);
        while(static_cast<std::size_t>(2*nu*nv)>maxTriangles && (nu>1||nv>1)){if(nu>=nv&&nu>1)--nu;else if(nv>1)--nv;}
        for(int j=0;j<nv;++j) for(int i=0;i<nu;++i)
        {
            const double u0=-0.5*s.widthM+s.widthM*double(i)/nu,u1=-0.5*s.widthM+s.widthM*double(i+1)/nu;
            const double v0=-0.5*s.heightM+s.heightM*double(j)/nv,v1=-0.5*s.heightM+s.heightM*double(j+1)/nv;
            appendQuad(out,pointFromLocal(s,u0,v0),pointFromLocal(s,u1,v0),pointFromLocal(s,u1,v1),pointFromLocal(s,u0,v1),maxTriangles);
        }
    }
    else if(s.kind==SurfaceKind::InsetPatch)
    {
        // One continuous PEC mesh representing a rectangular patch with a centered
        // inset notch and microstrip feed. The decomposition is deliberately aligned
        // so the feed's top edge is exactly shared by the main patch body, creating
        // an interior RWG edge rather than relying on overlapping surfaces.
        const double halfW=0.5*s.widthM,halfL=0.5*s.heightM;
        const double feedHalf=0.5*s.feedWidthM;
        const double notchHalf=std::min(0.49*s.widthM,feedHalf+s.notchGapM);
        const double contactV=-halfL+s.insetDepthM;
        const double feedStartV=-halfL-s.feedLengthM;

        auto sortedUnique=[](std::vector<double> x)
        {
            std::sort(x.begin(),x.end());
            std::vector<double> outv;
            for(double v:x) if(outv.empty()||std::abs(v-outv.back())>1e-12) outv.push_back(v);
            return outv;
        };
        auto divisions=[&](double a,double b)
        {
            std::vector<double> q; const double len=std::max(0.0,b-a);
            const int n=limitedDivisions(len,s.meshHintM,1,72);
            for(int i=0;i<=n;++i) q.push_back(a+len*double(i)/n);
            return q;
        };
        auto appendGrid=[&](std::vector<double> xs,std::vector<double> ys)
        {
            xs=sortedUnique(std::move(xs));ys=sortedUnique(std::move(ys));
            for(std::size_t j=0;j+1<ys.size();++j)for(std::size_t i=0;i+1<xs.size();++i)
                appendQuad(out,pointFromLocal(s,xs[i],ys[j]),pointFromLocal(s,xs[i+1],ys[j]),
                           pointFromLocal(s,xs[i+1],ys[j+1]),pointFromLocal(s,xs[i],ys[j+1]),maxTriangles);
        };

        // Build one conformal x partition on the contact plane. Reusing the exact
        // left/right vectors for the body and side ears avoids hanging nodes that
        // would otherwise make touching rectangles electrically disconnected in RWG.
        auto leftX=divisions(-halfW,-notchHalf);
        auto rightX=divisions(notchHalf,halfW);
        std::vector<double> bodyX=leftX;
        bodyX.insert(bodyX.end(),{-feedHalf,feedHalf});
        bodyX.insert(bodyX.end(),rightX.begin(),rightX.end());
        appendGrid(std::move(bodyX),divisions(contactV,halfL));

        // Side ears beside the notch. Their top horizontal edges now exactly match
        // the corresponding body-bottom edges; only the two physical notch gaps
        // remain boundary edges on the contact plane.
        if(s.insetDepthM>1e-12 && notchHalf<halfW-1e-12)
        {
            appendGrid(leftX,divisions(-halfL,contactV));
            appendGrid(rightX,divisions(-halfL,contactV));
        }

        // Feed strip: use one cell across its width so the top contact edge exactly
        // equals the forced [-feedHalf,+feedHalf] edge in the body mesh.
        std::vector<double> feedY=divisions(feedStartV,contactV);
        appendGrid({-feedHalf,feedHalf},std::move(feedY));
    }
    else if(s.kind==SurfaceKind::Disk)
    {
        int nt=limitedDivisions(2.0*Pi*s.radiusM,s.meshHintM,16,144);
        int nr=limitedDivisions(s.radiusM-s.innerRadiusM,s.meshHintM,1,72);
        while(static_cast<std::size_t>(2*nt*nr)>maxTriangles && (nt>12||nr>1)){if(nt>24)--nt;else if(nr>1)--nr;else break;}
        if(s.innerRadiusM<1e-8*s.radiusM)
        {
            const auto center=pointFromLocal(s,0,0,0);
            const double r1=s.radiusM/double(nr);
            for(int i=0;i<nt && out.size()<maxTriangles;++i)
            {
                const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
                out.push_back({center,pointFromLocal(s,r1*std::cos(a0),r1*std::sin(a0)),pointFromLocal(s,r1*std::cos(a1),r1*std::sin(a1))});
            }
            for(int ir=1;ir<nr;++ir)
            {
                const double r0=s.radiusM*ir/nr,r2=s.radiusM*(ir+1)/nr;
                for(int i=0;i<nt;++i)
                {
                    const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
                    appendQuad(out,pointFromLocal(s,r0*std::cos(a0),r0*std::sin(a0)),pointFromLocal(s,r2*std::cos(a0),r2*std::sin(a0)),pointFromLocal(s,r2*std::cos(a1),r2*std::sin(a1)),pointFromLocal(s,r0*std::cos(a1),r0*std::sin(a1)),maxTriangles);
                }
            }
        }
        else
        {
            for(int ir=0;ir<nr;++ir)
            {
                const double r0=s.innerRadiusM+(s.radiusM-s.innerRadiusM)*ir/nr,r1=s.innerRadiusM+(s.radiusM-s.innerRadiusM)*(ir+1)/nr;
                for(int i=0;i<nt;++i)
                {
                    const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
                    appendQuad(out,pointFromLocal(s,r0*std::cos(a0),r0*std::sin(a0)),pointFromLocal(s,r1*std::cos(a0),r1*std::sin(a0)),pointFromLocal(s,r1*std::cos(a1),r1*std::sin(a1)),pointFromLocal(s,r0*std::cos(a1),r0*std::sin(a1)),maxTriangles);
                }
            }
        }
    }
    else if(s.kind==SurfaceKind::Cylinder)
    {
        int nt=limitedDivisions(2.0*Pi*s.radiusM,s.meshHintM,16,144),nz=limitedDivisions(s.heightM,s.meshHintM,1,96);
        while(static_cast<std::size_t>(2*nt*nz)>maxTriangles && (nt>12||nz>1)){if(nt>=2*nz&&nt>16)--nt;else if(nz>1)--nz;else --nt;}
        for(int iz=0;iz<nz;++iz)
        {
            const double n0=-0.5*s.heightM+s.heightM*iz/nz,n1=-0.5*s.heightM+s.heightM*(iz+1)/nz;
            for(int i=0;i<nt;++i)
            {
                const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
                appendQuad(out,pointFromLocal(s,s.radiusM*std::cos(a0),s.radiusM*std::sin(a0),n0),pointFromLocal(s,s.radiusM*std::cos(a1),s.radiusM*std::sin(a1),n0),pointFromLocal(s,s.radiusM*std::cos(a1),s.radiusM*std::sin(a1),n1),pointFromLocal(s,s.radiusM*std::cos(a0),s.radiusM*std::sin(a0),n1),maxTriangles);
            }
        }
    }
    else if(s.kind==SurfaceKind::Cone)
    {
        int nt=limitedDivisions(2.0*Pi*s.radiusM,s.meshHintM,16,144),nz=limitedDivisions(std::hypot(s.radiusM,s.heightM),s.meshHintM,2,72);
        while(static_cast<std::size_t>(2*nt*nz)>maxTriangles && (nt>12||nz>2)){if(nt>=2*nz&&nt>16)--nt;else if(nz>2)--nz;else --nt;}
        const double apexN=-0.5*s.heightM;
        const auto apex=pointFromLocal(s,0,0,apexN);
        const double r1=s.radiusM/double(nz),n1=apexN+s.heightM/double(nz);
        for(int i=0;i<nt&&out.size()<maxTriangles;++i)
        {
            const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
            out.push_back({apex,pointFromLocal(s,r1*std::cos(a0),r1*std::sin(a0),n1),pointFromLocal(s,r1*std::cos(a1),r1*std::sin(a1),n1)});
        }
        for(int iz=1;iz<nz;++iz)
        {
            const double f0=double(iz)/nz,f1=double(iz+1)/nz;
            const double r0=s.radiusM*f0,r2=s.radiusM*f1,n0=apexN+s.heightM*f0,n2=apexN+s.heightM*f1;
            for(int i=0;i<nt;++i)
            {
                const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
                appendQuad(out,pointFromLocal(s,r0*std::cos(a0),r0*std::sin(a0),n0),pointFromLocal(s,r2*std::cos(a0),r2*std::sin(a0),n2),pointFromLocal(s,r2*std::cos(a1),r2*std::sin(a1),n2),pointFromLocal(s,r0*std::cos(a1),r0*std::sin(a1),n0),maxTriangles);
            }
        }
    }
    else if(s.kind==SurfaceKind::Paraboloid)
    {
        int nt=limitedDivisions(2.0*Pi*s.radiusM,s.meshHintM,18,144),nr=limitedDivisions(s.radiusM,s.meshHintM,2,72);
        while(static_cast<std::size_t>(2*nt*nr)>maxTriangles && (nt>12||nr>2)){if(nt>=2*nr&&nt>18)--nt;else if(nr>2)--nr;else --nt;}
        const auto vertex=pointFromLocal(s,0,0,0);
        const double r1=s.radiusM/double(nr),z1=r1*r1/(4.0*s.focalLengthM);
        for(int i=0;i<nt&&out.size()<maxTriangles;++i)
        {
            const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
            out.push_back({vertex,pointFromLocal(s,r1*std::cos(a0),r1*std::sin(a0),z1),pointFromLocal(s,r1*std::cos(a1),r1*std::sin(a1),z1)});
        }
        for(int ir=1;ir<nr;++ir)
        {
            const double r0=s.radiusM*ir/nr,r2=s.radiusM*(ir+1)/nr,z0=r0*r0/(4.0*s.focalLengthM),z2=r2*r2/(4.0*s.focalLengthM);
            for(int i=0;i<nt;++i)
            {
                const double a0=2*Pi*i/nt,a1=2*Pi*(i+1)/nt;
                appendQuad(out,pointFromLocal(s,r0*std::cos(a0),r0*std::sin(a0),z0),pointFromLocal(s,r2*std::cos(a0),r2*std::sin(a0),z2),pointFromLocal(s,r2*std::cos(a1),r2*std::sin(a1),z2),pointFromLocal(s,r0*std::cos(a1),r0*std::sin(a1),z0),maxTriangles);
            }
        }
    }
    return out;
}

SurfaceMeshStats meshStats(const std::vector<SurfaceTriangle> &triangles)
{
    SurfaceMeshStats s; s.triangleCount=triangles.size();
    for(const auto&t:triangles)
    {
        const auto ab=sub(t.b,t.a),ac=sub(t.c,t.a),bc=sub(t.c,t.b);
        s.areaM2+=0.5*length(cross(ab,ac));
        s.maxEdgeM=std::max({s.maxEdgeM,length(ab),length(ac),length(bc)});
    }
    return s;
}

const char *kindName(SurfaceKind kind)
{
    switch(kind)
    {
        case SurfaceKind::Disk:return "Disk / annulus";
        case SurfaceKind::Cylinder:return "Cylindrical shell";
        case SurfaceKind::Cone:return "Conical shell";
        case SurfaceKind::Paraboloid:return "Parabolic reflector";
        case SurfaceKind::InsetPatch:return "Inset patch + microstrip";
        case SurfaceKind::Rectangle:
        default:return "Rectangular sheet";
    }
}

} // namespace AntennaSurface
