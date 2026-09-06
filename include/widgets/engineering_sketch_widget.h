#pragma once

#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cmath>

// Compact technical sketches used beside engineering calculators.  They are
// deliberately schematic: the goal is to make each numeric input immediately
// identifiable without consuming the vertical space of a large explanation.
class EngineeringSketchWidget final : public QWidget
{
public:
    enum class Kind {
        PlaneWave,
        HertzianDipole,
        LinkBudget,
        Solenoid,
        Capacitance,
        CoupledCoils,
        GeometricCoilCoupling,
        InductiveAntenna,
        PcbTrace,
        Filter,
        RfChain,
        Balun
    };

    explicit EngineeringSketchWidget(Kind kind, QWidget *parent=nullptr)
        : QWidget(parent), m_kind(kind)
    {
        setMinimumSize(250, 180);
        setMaximumHeight(300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setKind(Kind kind) { m_kind=kind; update(); }
    void setVariant(int variant) { m_variant=variant; update(); }
    void setValue(const QString &key,const QString &value) { m_values[key]=value; update(); }
    void setValues(const QMap<QString,QString> &values) { m_values=values; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing,true);
        const auto pal=palette();
        const QColor fg=pal.color(QPalette::Text);
        const QColor mid=pal.color(QPalette::Mid);
        QColor accent=pal.color(QPalette::Highlight);
        QColor soft=accent; soft.setAlpha(55);
        p.fillRect(rect(),pal.color(QPalette::Base));
        p.setPen(QPen(mid,1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect().adjusted(1,1,-2,-2),7,7);
        p.setPen(fg);

        const QRectF r=rect().adjusted(14,18,-14,-18);
        switch(m_kind) {
        case Kind::PlaneWave: drawPlaneWave(p,r,fg,accent,mid); break;
        case Kind::HertzianDipole: drawHertzian(p,r,fg,accent,mid); break;
        case Kind::LinkBudget: drawLink(p,r,fg,accent,mid); break;
        case Kind::Solenoid: drawSolenoid(p,r,fg,accent,mid); break;
        case Kind::Capacitance: drawCapacitance(p,r,fg,accent,mid); break;
        case Kind::CoupledCoils: drawCoupled(p,r,fg,accent,mid,false); break;
        case Kind::GeometricCoilCoupling: drawCoupled(p,r,fg,accent,mid,true); break;
        case Kind::InductiveAntenna: drawInductive(p,r,fg,accent,mid); break;
        case Kind::PcbTrace: drawPcb(p,r,fg,accent,mid,soft); break;
        case Kind::Filter: drawFilter(p,r,fg,accent,mid); break;
        case Kind::RfChain: drawRfChain(p,r,fg,accent,mid); break;
        case Kind::Balun: drawBalun(p,r,fg,accent,mid); break;
        }
    }

private:
    QString v(const QString &key,const QString &fallback=QString()) const
    { return m_values.contains(key)?m_values.value(key):fallback; }

    static void arrow(QPainter &p,QPointF a,QPointF b,const QColor &c,double width=1.5)
    {
        p.setPen(QPen(c,width,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(a,b);
        const double ang=std::atan2(b.y()-a.y(),b.x()-a.x());
        const double s=7.0;
        const QPointF q1=b-QPointF(std::cos(ang-0.55)*s,std::sin(ang-0.55)*s);
        const QPointF q2=b-QPointF(std::cos(ang+0.55)*s,std::sin(ang+0.55)*s);
        QPainterPath path;path.moveTo(b);path.lineTo(q1);path.lineTo(q2);path.closeSubpath();
        p.setBrush(c);p.drawPath(path);p.setBrush(Qt::NoBrush);
    }

    static void dimension(QPainter &p,QPointF a,QPointF b,const QString &text,const QColor &c,QPointF textOffset={0,-3})
    {
        arrow(p,a,b,c,1.0);arrow(p,b,a,c,1.0);
        p.setPen(c);
        const QPointF m=(a+b)*0.5+textOffset;
        p.drawText(QRectF(m.x()-75,m.y()-12,150,24),Qt::AlignCenter,text);
    }

    static void label(QPainter &p,const QPointF &pt,const QString &text,const QColor &c)
    { p.setPen(c);p.drawText(QRectF(pt.x()-65,pt.y()-11,130,22),Qt::AlignCenter,text); }

    void drawPlaneWave(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        const double y=r.center().y();
        QPainterPath wave; const double x0=r.left()+22,x1=r.right()-22;
        for(int i=0;i<=100;++i){const double t=i/100.0;QPointF pt(x0+t*(x1-x0),y-34*std::sin(t*4*3.14159265358979323846));if(i==0)wave.moveTo(pt);else wave.lineTo(pt);}p.setPen(QPen(a,2.2));p.drawPath(wave);
        dimension(p,QPointF(x0,y+52),QPointF(x0+(x1-x0)/2,y+52),QStringLiteral("λ  %1").arg(v("lambda")),fg,{0,12});
        arrow(p,QPointF(x0,y-62),QPointF(x0+70,y-62),a);label(p,QPointF(x0+35,y-78),QStringLiteral("k / propagation"),fg);
        arrow(p,QPointF(x0+25,y+5),QPointF(x0+25,y-45),a);label(p,QPointF(x0+65,y-30),QStringLiteral("E"),fg);
        arrow(p,QPointF(x0+25,y+5),QPointF(x0+65,y+28),mid);label(p,QPointF(x0+88,y+28),QStringLiteral("H"),fg);
    }

    void drawHertzian(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        const QPointF c(r.left()+78,r.center().y());const double h=110;
        p.setPen(QPen(a,4,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(c.x(),c.y()-h/2),QPointF(c.x(),c.y()+h/2));
        p.setPen(QPen(mid,1,Qt::DashLine));p.drawLine(QPointF(c.x(),c.y()-80),QPointF(c.x(),c.y()+80));
        dimension(p,QPointF(c.x()-28,c.y()-h/2),QPointF(c.x()-28,c.y()+h/2),QStringLiteral("ℓ  %1").arg(v("length")),fg,{-22,0});
        const QPointF obs(r.right()-45,r.top()+42);arrow(p,c,obs,a);label(p,(c+obs)*0.5+QPointF(5,-12),QStringLiteral("r"),fg);p.setBrush(a);p.drawEllipse(obs,4,4);p.setBrush(Qt::NoBrush);label(p,obs+QPointF(0,-17),QStringLiteral("M"),fg);
        p.setPen(QPen(mid,1,Qt::DashLine));p.drawLine(c,QPointF(c.x(),r.top()+15));label(p,QPointF(c.x()+24,c.y()-46),QStringLiteral("θ"),fg);
    }

    void drawLink(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        // Link-budget signal chain: generator -> TX line -> TX antenna -> free
        // space -> RX antenna -> RX line -> receiver.  This makes the reference
        // planes and the meaning of feeder losses visually explicit.
        const double y=r.center().y()+8;
        const double w=r.width();
        const double xGen=r.left()+0.06*w, xTxZ=r.left()+0.20*w, xTxAnt=r.left()+0.34*w;
        const double xRxAnt=r.left()+0.66*w, xRxZ=r.left()+0.80*w, xRcv=r.left()+0.94*w;

        // RF generator.
        p.setPen(QPen(fg,2));p.setBrush(Qt::NoBrush);p.drawEllipse(QPointF(xGen,y),18,18);
        QPainterPath sine; for(int i=0;i<=24;++i){const double t=i/24.0;const QPointF q(xGen-12+24*t,y-6*std::sin(2*3.14159265358979323846*t));if(i==0)sine.moveTo(q);else sine.lineTo(q);}p.setPen(QPen(a,1.5));p.drawPath(sine);
        p.setPen(QPen(fg,2));p.drawLine(QPointF(xGen+18,y),QPointF(xTxZ-20,y));
        resistorBox(p,QRectF(xTxZ-20,y-13,40,26),QStringLiteral("ZTX"),fg,a);
        p.drawLine(QPointF(xTxZ+20,y),QPointF(xTxAnt-13,y));

        auto antenna=[&](double x,bool faceRight){
            p.setPen(QPen(a,2.2));p.drawLine(QPointF(x,y+29),QPointF(x,y-30));
            const double s=faceRight?1.0:-1.0;
            p.drawLine(QPointF(x,y-30),QPointF(x+18*s,y-47));
            p.drawLine(QPointF(x,y-30),QPointF(x+18*s,y-13));
        };
        antenna(xTxAnt,true); antenna(xRxAnt,false);

        // A few propagating wave fronts between the antennas.
        p.setPen(QPen(mid,1.5));
        const double cx=(xTxAnt+xRxAnt)/2.0;
        for(int i=-2;i<=2;++i){const double xx=cx+i*16.0;p.drawArc(QRectF(xx-12,y-45,24,90),-70*16,140*16);}
        arrow(p,QPointF(xTxAnt+30,y-60),QPointF(xRxAnt-30,y-60),a,1.8);

        p.setPen(QPen(fg,2));p.drawLine(QPointF(xRxAnt+13,y),QPointF(xRxZ-20,y));
        resistorBox(p,QRectF(xRxZ-20,y-13,40,26),QStringLiteral("ZRX"),fg,a);
        p.drawLine(QPointF(xRxZ+20,y),QPointF(xRcv-20,y));
        resistorBox(p,QRectF(xRcv-20,y-20,40,40),QStringLiteral("RCV"),fg,a);

        label(p,QPointF(xGen,r.top()+17),QStringLiteral("generator"),fg);
        label(p,QPointF(xTxAnt,r.top()+17),QStringLiteral("TX antenna"),fg);
        label(p,QPointF(cx,r.top()+17),QStringLiteral("free space / FSPL"),fg);
        label(p,QPointF(xRxAnt,r.top()+17),QStringLiteral("RX antenna"),fg);
        dimension(p,QPointF(xTxAnt,r.bottom()-18),QPointF(xRxAnt,r.bottom()-18),QStringLiteral("d  %1").arg(v("distance")),fg,{0,-11});
        label(p,QPointF((xGen+xTxAnt)/2,y+48),QStringLiteral("LTX, GT"),mid);
        label(p,QPointF((xRxAnt+xRcv)/2,y+48),QStringLiteral("GR, LRX, Pr"),mid);
    }

    void drawSolenoid(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        const double x0=r.left()+45,x1=r.right()-45,y=r.center().y();const int turns=13;
        QPainterPath coil; for(int i=0;i<=260;++i){double t=i/260.0;double x=x0+t*(x1-x0);double yy=y+38*std::sin(t*turns*2*3.14159265358979323846);QPointF pt(x,yy);if(i==0)coil.moveTo(pt);else coil.lineTo(pt);}p.setPen(QPen(a,2.4));p.drawPath(coil);
        p.setPen(QPen(mid,1,Qt::DashLine));p.drawLine(QPointF(x0,y),QPointF(x1,y));
        dimension(p,QPointF(x0,r.bottom()-22),QPointF(x1,r.bottom()-22),QStringLiteral("winding length l  %1").arg(v("length")),fg,{0,-11});dimension(p,QPointF(x1+18,y),QPointF(x1+18,y-38),QStringLiteral("mean radius r  %1").arg(v("radius")),fg,{48,0});
        label(p,QPointF((x0+x1)/2,r.top()+14),QStringLiteral("N turns • pitch p • wire Ød"),fg);
    }

    void drawCapacitance(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        const QPointF c=r.center();
        QColor diel=a; diel.setAlpha(42);
        const QString mat=v("material",QStringLiteral("dielectric"));
        const QString er=v("epsilon",QStringLiteral("1"));
        const QString td=v("tand",QStringLiteral("0"));
        if(m_variant==0)
        {
            const QRectF region(c.x()-82,c.y()-62,164,124);
            p.fillRect(region,diel);
            p.setPen(QPen(a,6));
            p.drawLine(QPointF(region.left(),region.top()),QPointF(region.left(),region.bottom()));
            p.drawLine(QPointF(region.right(),region.top()),QPointF(region.right(),region.bottom()));
            dimension(p,QPointF(region.left(),region.bottom()+26),QPointF(region.right(),region.bottom()+26),QStringLiteral("d  %1").arg(v("d")),fg,{0,-12});
            dimension(p,QPointF(region.left()-24,region.top()),QPointF(region.left()-24,region.bottom()),QStringLiteral("plate size / area A"),fg,{-42,0});
            label(p,QPointF(c.x(),c.y()-86),QStringLiteral("%1 • εr=%2 • tanδ=%3").arg(mat,er,td),fg);
            label(p,QPointF(c.x(),c.y()),QStringLiteral("homogeneous dielectric"),mid);
        }
        else if(m_variant==1||m_variant==2)
        {
            if(m_variant==2){p.setBrush(diel);p.setPen(Qt::NoPen);p.drawEllipse(c,72,72);p.setBrush(palette().color(QPalette::Base));p.drawEllipse(c,35,35);p.setBrush(Qt::NoBrush);}
            p.setPen(QPen(a,3));p.drawEllipse(c,m_variant==1?55:35,m_variant==1?55:35);
            if(m_variant==2){p.setPen(QPen(mid,3));p.drawEllipse(c,72,72);}
            dimension(p,c,QPointF(c.x()+55,c.y()),QStringLiteral("a"),fg,{0,-12});if(m_variant==2)dimension(p,c,QPointF(c.x()+72,c.y()),QStringLiteral("b"),fg,{0,13});
            label(p,QPointF(c.x(),r.top()+12),QStringLiteral("%1 • εr=%2").arg(mat,er),fg);
        }
        else if(m_variant==3)
        {
            p.setBrush(diel);p.setPen(Qt::NoPen);p.drawEllipse(c,72,72);p.setBrush(palette().color(QPalette::Base));p.drawEllipse(c,34,34);p.setBrush(Qt::NoBrush);
            p.setPen(QPen(a,3));p.drawEllipse(c,34,34);p.setPen(QPen(mid,3));p.drawEllipse(c,72,72);
            dimension(p,c,QPointF(c.x()+34,c.y()),QStringLiteral("a"),fg,{0,-13});dimension(p,c,QPointF(c.x()+72,c.y()),QStringLiteral("b"),fg,{0,13});
            label(p,QPointF(c.x(),r.top()+12),QStringLiteral("%1 • εr=%2 • tanδ=%3").arg(mat,er,td),fg);label(p,QPointF(c.x(),r.bottom()-10),QStringLiteral("coaxial cylinders • length L"),fg);
        }
        else
        {
            p.fillRect(QRectF(c.x()-55,c.y()-18,110,36),diel);
            p.setPen(QPen(a,3));p.drawEllipse(QPointF(c.x()-55,c.y()),16,16);p.drawEllipse(QPointF(c.x()+55,c.y()),16,16);
            dimension(p,QPointF(c.x()-55,c.y()+45),QPointF(c.x()+55,c.y()+45),QStringLiteral("center spacing D"),fg,{0,12});dimension(p,QPointF(c.x()-55,c.y()),QPointF(c.x()-39,c.y()),QStringLiteral("a"),fg,{0,-13});
            label(p,QPointF(c.x(),r.top()+12),QStringLiteral("%1 • εr=%2").arg(mat,er),fg);
        }
    }

    void drawCoupled(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid,bool geometric)
    {
        // Keep the windings visually close. The previous full-width placement made
        // even a strongly coupled core device look like two unrelated coils.
        const double halfSep=std::min(125.0,std::max(62.0,r.width()*0.14));
        const QPointF center=r.center();
        const QPointF c1(center.x()-halfSep,center.y()),c2(center.x()+halfSep,center.y());
        if(m_variant>0)
        {
            QColor core=mid; core.setAlpha(95);
            p.setPen(QPen(mid,5));p.setBrush(core);
            if(m_variant==2)
            {
                // Common-mode choke / bifilar winding on a shared ferrite core.
                const QRectF outer(center.x()-halfSep-62,center.y()-82,2*halfSep+124,164);
                QPainterPath cp;cp.addRoundedRect(outer,28,28);cp.addRoundedRect(outer.adjusted(34,34,-34,-34),20,20);cp.setFillRule(Qt::OddEvenFill);p.drawPath(cp);
            }
            else
            {
                QPainterPath cp;cp.addRoundedRect(QRectF(c1.x()-52,c1.y()-82,c2.x()-c1.x()+104,164),12,12);cp.addRoundedRect(QRectF(c1.x()-15,c1.y()-46,c2.x()-c1.x()+30,92),8,8);cp.setFillRule(Qt::OddEvenFill);p.drawPath(cp);
            }
            p.setBrush(Qt::NoBrush);
            label(p,QPointF(center.x(),r.top()+18),m_variant==2?QStringLiteral("common-mode choke core • μeff %1").arg(v("mu")):QStringLiteral("magnetic core / μeff  %1").arg(v("mu")),fg);
        }
        if(m_variant==2)
        {
            // Two close, interleaved windings are a much closer visual match to a
            // real bifilar common-mode choke than two remote solenoids.
            for(int i=-4;i<=4;++i){p.setPen(QPen(a,2.2));p.drawEllipse(QPointF(center.x()-20+i*4,center.y()),28,50);p.setPen(QPen(mid,2.2));p.drawEllipse(QPointF(center.x()+20+i*4,center.y()),28,50);}
            label(p,QPointF(center.x()-55,center.y()+70),QStringLiteral("line 1 / N1"),fg);
            label(p,QPointF(center.x()+55,center.y()+70),QStringLiteral("line 2 / N2"),fg);
            label(p,QPointF(center.x(),center.y()-72),QStringLiteral("LCM≈L+M • LDM≈L−M"),fg);
        }
        else
        {
            for(int i=-3;i<=3;++i){p.setPen(QPen(a,2));p.drawEllipse(QPointF(c1.x()+i*4,c1.y()),28,52);p.setPen(QPen(mid,2));p.drawEllipse(QPointF(c2.x()+i*4,c2.y()+(geometric?i*2:0)),28,52);}
            arrow(p,QPointF(c1.x()+38,c1.y()-68),QPointF(c2.x()-38,c2.y()-68),fg);
            label(p,QPointF(center.x(),c1.y()-84),geometric?QStringLiteral("position / angle / distance"):QStringLiteral("M = k√(L1 L2)"),fg);
            label(p,c1+QPointF(0,72),QStringLiteral("N1, L1"),fg);label(p,c2+QPointF(0,72),QStringLiteral("N2, L2"),fg);
        }
    }

    void drawBalun(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        const QPointF c=r.center();
        // Unbalanced coax/source on the left, transformer/current-balun core in
        // the middle and the two balanced conductors on the right.
        p.setPen(QPen(fg,2));
        p.drawLine(QPointF(r.left()+25,c.y()),QPointF(c.x()-92,c.y()));
        p.drawLine(QPointF(r.left()+25,c.y()+28),QPointF(c.x()-92,c.y()+28));
        label(p,QPointF(r.left()+58,c.y()-28),QStringLiteral("UNBAL"),fg);
        QColor core=mid;core.setAlpha(95);p.setBrush(core);p.setPen(QPen(mid,4));
        QPainterPath cp;cp.addEllipse(QPointF(c.x()-18,c.y()+12),66,66);cp.addEllipse(QPointF(c.x()-18,c.y()+12),34,34);cp.setFillRule(Qt::OddEvenFill);p.drawPath(cp);p.setBrush(Qt::NoBrush);
        for(int i=-3;i<=3;++i){p.setPen(QPen(a,2));p.drawArc(QRectF(c.x()-73+i*4,c.y()-43,110,110),25*16,310*16);}
        p.setPen(QPen(fg,2));p.drawLine(QPointF(c.x()+50,c.y()-17),QPointF(r.right()-25,c.y()-17));p.drawLine(QPointF(c.x()+50,c.y()+41),QPointF(r.right()-25,c.y()+41));
        label(p,QPointF(r.right()-58,c.y()-48),QStringLiteral("BALANCED"),fg);
        label(p,QPointF(c.x()-18,r.top()+16),v("ratio",QStringLiteral("Z ratio")),fg);
        label(p,QPointF(c.x()-18,r.bottom()-16),QStringLiteral("Lm • Lleak • Cp"),fg);
    }

    void drawInductive(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        if(m_variant==2){ // planar spiral
            const QPointF c(r.center().x(),r.center().y());QPainterPath path;const int pts=500;for(int i=0;i<pts;++i){double t=i/double(pts-1),ang=t*10*3.14159265358979323846,rad=24+t*62;QPointF pt(c.x()+rad*std::cos(ang),c.y()+rad*std::sin(ang));if(i==0)path.moveTo(pt);else path.lineTo(pt);}p.setPen(QPen(a,3));p.drawPath(path);dimension(p,QPointF(c.x()-86,c.y()+95),QPointF(c.x()+86,c.y()+95),QStringLiteral("Dout"),fg,{0,-11});dimension(p,QPointF(c.x()-24,c.y()),QPointF(c.x()+24,c.y()),QStringLiteral("Din"),fg,{0,-14});label(p,QPointF(c.x(),r.top()+12),QStringLiteral("trace w • spacing s • N"),fg);
        }else if(m_variant==1){drawSolenoid(p,r,fg,a,mid);}else{const QPointF c=r.center();p.setPen(QPen(a,4));p.drawEllipse(c,70,70);dimension(p,c,QPointF(c.x()+70,c.y()),QStringLiteral("loop radius r"),fg,{0,-14});arrow(p,QPointF(c.x(),c.y()+92),QPointF(c.x(),c.y()+18),mid);label(p,QPointF(c.x()+50,c.y()+86),QStringLiteral("B axis"),fg);label(p,QPointF(c.x(),r.top()+10),QStringLiteral("N turns • magnetic moment m=NIA"),fg);}
    }

    void drawPcb(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid,const QColor&soft)
    {
        const double y=r.center().y()+22;const QRectF sub(r.left()+35,y-50,r.width()-70,75);p.fillRect(sub,soft);p.setPen(QPen(mid,1));p.drawRect(sub);
        const double tw=100;
        if(m_variant==0)
        {
            p.setPen(QPen(fg,4));p.drawLine(QPointF(sub.left(),sub.bottom()+5),QPointF(sub.right(),sub.bottom()+5));
            const QRectF tr(r.center().x()-tw/2,sub.top()-9,tw,9);p.fillRect(tr,a);dimension(p,QPointF(tr.left(),tr.top()-16),QPointF(tr.right(),tr.top()-16),QStringLiteral("trace width w"),fg,{0,-10});dimension(p,QPointF(sub.left()+18,sub.top()),QPointF(sub.left()+18,sub.bottom()),QStringLiteral("h"),fg,{-18,0});dimension(p,QPointF(tr.right()+16,tr.top()),QPointF(tr.right()+16,tr.bottom()),QStringLiteral("t"),fg,{18,0});label(p,QPointF(r.center().x(),r.bottom()-8),QStringLiteral("microstrip • ground plane • line length ℓ • ZL"),fg);
        }
        else
        {
            p.setPen(QPen(fg,4));p.drawLine(QPointF(sub.left(),sub.top()-7),QPointF(sub.right(),sub.top()-7));p.drawLine(QPointF(sub.left(),sub.bottom()+7),QPointF(sub.right(),sub.bottom()+7));
            const QRectF tr(r.center().x()-tw/2,sub.center().y()-4,tw,8);p.fillRect(tr,a);dimension(p,QPointF(tr.left(),tr.top()-17),QPointF(tr.right(),tr.top()-17),QStringLiteral("trace width w"),fg,{0,-10});dimension(p,QPointF(sub.left()+18,sub.top()),QPointF(sub.left()+18,sub.bottom()),QStringLiteral("ground spacing h"),fg,{-28,0});label(p,QPointF(r.center().x(),r.bottom()-8),QStringLiteral("symmetric stripline • two ground planes • ℓ • ZL"),fg);
        }
        label(p,QPointF(r.center().x(),sub.center().y()+22),QStringLiteral("εr, tanδ"),fg);
    }

    static void capacitor(QPainter&p,QPointF c,const QColor&color,bool vertical=true)
    {
        p.setPen(QPen(color,2));
        if(vertical){p.drawLine(QPointF(c.x()-12,c.y()-4),QPointF(c.x()+12,c.y()-4));p.drawLine(QPointF(c.x()-12,c.y()+4),QPointF(c.x()+12,c.y()+4));}
        else{p.drawLine(QPointF(c.x()-4,c.y()-12),QPointF(c.x()-4,c.y()+12));p.drawLine(QPointF(c.x()+4,c.y()-12),QPointF(c.x()+4,c.y()+12));}
    }
    static void inductor(QPainter&p,QPointF a0,QPointF b0,const QColor&color)
    {
        p.setPen(QPen(color,2));const double len=b0.x()-a0.x();for(int i=0;i<5;++i)p.drawArc(QRectF(a0.x()+i*len/5.2,a0.y()-10,len/4.5,20),0,180*16);
    }
    static void verticalInductor(QPainter&p,QPointF a0,QPointF b0,const QColor&color)
    {
        p.setPen(QPen(color,2));
        const double len=b0.y()-a0.y();
        for(int i=0;i<5;++i)
            p.drawArc(QRectF(a0.x()-10,a0.y()+i*len/5.2,20,len/4.5),-90*16,180*16);
    }

    static void resistorBox(QPainter&p,const QRectF &box,const QString &text,const QColor&fg,const QColor&accent)
    {
        p.setPen(QPen(accent,2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box,3,3);
        label(p,box.center(),text,fg);
    }

    void drawFilter(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        const double y=r.center().y()-4;const double x0=r.left()+28,x1=r.right()-28;label(p,QPointF(r.center().x(),r.top()+15),QStringLiteral("f0 / fc • Q • BW • |H(f)|"),fg);
        p.setPen(QPen(mid,2));p.drawLine(QPointF(x0,y+72),QPointF(x1,y+72));
        if(m_variant==0) // low-pass: series L, shunt C
        {
            p.drawLine(QPointF(x0,y),QPointF(x0+35,y));inductor(p,QPointF(x0+35,y),QPointF(x0+115,y),a);p.drawLine(QPointF(x0+115,y),QPointF(x1,y));const double xn=x0+145;p.drawLine(QPointF(xn,y),QPointF(xn,y+24));capacitor(p,QPointF(xn,y+34),a,true);p.drawLine(QPointF(xn,y+42),QPointF(xn,y+72));label(p,QPointF(x0+75,y-28),QStringLiteral("L"),fg);label(p,QPointF(xn+28,y+35),QStringLiteral("C"),fg);label(p,QPointF(r.center().x(),r.bottom()-7),QStringLiteral("2nd-order low-pass"),fg);
        }
        else if(m_variant==1) // high-pass: series C, shunt L
        {
            // Series C followed by a true shunt L.  Keep the inductor vertical so
            // the topology is visually unambiguous and electrically connected.
            const double xc=x0+58;
            p.drawLine(QPointF(x0,y),QPointF(xc-8,y));
            capacitor(p,QPointF(xc,y),a,false);
            p.drawLine(QPointF(xc+8,y),QPointF(x1,y));
            const double xn=x0+150;
            p.drawLine(QPointF(xn,y),QPointF(xn,y+14));
            verticalInductor(p,QPointF(xn,y+14),QPointF(xn,y+57),a);
            p.drawLine(QPointF(xn,y+57),QPointF(xn,y+72));
            label(p,QPointF(xc,y-28),QStringLiteral("C"),fg);
            label(p,QPointF(xn+30,y+37),QStringLiteral("L"),fg);
            label(p,QPointF(r.center().x(),r.bottom()-7),QStringLiteral("2nd-order high-pass: series C, shunt L"),fg);
        }
        else if(m_variant==4) // receive-line shunt series-LC trap
        {
            // Receive line loaded by a *series* L-C branch to ground.
            p.drawLine(QPointF(x0,y),QPointF(x1,y));arrow(p,QPointF(x0,y),QPointF(x1,y),fg);
            const double xn=r.center().x();
            p.drawLine(QPointF(xn,y),QPointF(xn,y+10));
            verticalInductor(p,QPointF(xn,y+10),QPointF(xn,y+40),a);
            p.drawLine(QPointF(xn,y+40),QPointF(xn,y+47));
            capacitor(p,QPointF(xn,y+52),a,true);
            p.drawLine(QPointF(xn,y+56),QPointF(xn,y+72));
            label(p,QPointF(xn+31,y+26),QStringLiteral("L"),fg);
            label(p,QPointF(xn+31,y+54),QStringLiteral("C"),fg);
            label(p,QPointF(x0+35,y-24),QStringLiteral("Rs"),mid);
            label(p,QPointF(x1-35,y-24),QStringLiteral("RL"),mid);
            label(p,QPointF(r.center().x(),r.bottom()-7),QStringLiteral("receive-line shunt series-LC trap"),fg);
        }
        else if(m_variant==2) // series RLC band-pass, output across R
        {
            // Series resonator with Vout taken across R: maximum at resonance.
            const double xc=x0+45, xl=x0+105, xn=x0+185;
            p.drawLine(QPointF(x0,y),QPointF(xc-8,y));capacitor(p,QPointF(xc,y),a,false);
            p.drawLine(QPointF(xc+8,y),QPointF(xl-22,y));inductor(p,QPointF(xl-22,y),QPointF(xl+35,y),a);
            p.drawLine(QPointF(xl+35,y),QPointF(xn,y));
            p.drawLine(QPointF(xn,y),QPointF(xn,y+20));
            resistorBox(p,QRectF(xn-16,y+20,32,31),QStringLiteral("R"),fg,a);
            p.drawLine(QPointF(xn,y+51),QPointF(xn,y+72));
            label(p,QPointF(xc,y-27),QStringLiteral("C"),fg);label(p,QPointF(xl+5,y-27),QStringLiteral("L"),fg);
            label(p,QPointF(xn+43,y+35),QStringLiteral("Vout"),fg);
            label(p,QPointF(r.center().x(),r.bottom()-7),QStringLiteral("series-RLC band-pass: output across R"),fg);
        }
        else // series-RLC notch prototype, output across L+C
        {
            // The transfer function is V(L+C)/Vin.  Draw that voltage across a
            // vertical series L-C branch instead of reusing the band-pass image.
            const double xr=x0+65, xn=r.center().x()+35;
            p.drawLine(QPointF(x0,y),QPointF(xr-24,y));
            resistorBox(p,QRectF(xr-24,y-12,48,24),QStringLiteral("R"),fg,a);
            p.drawLine(QPointF(xr+24,y),QPointF(xn,y));
            p.drawLine(QPointF(xn,y),QPointF(xn,y+8));
            verticalInductor(p,QPointF(xn,y+8),QPointF(xn,y+38),a);
            p.drawLine(QPointF(xn,y+38),QPointF(xn,y+46));
            capacitor(p,QPointF(xn,y+51),a,true);
            p.drawLine(QPointF(xn,y+55),QPointF(xn,y+72));
            label(p,QPointF(xn+32,y+24),QStringLiteral("L"),fg);label(p,QPointF(xn+32,y+52),QStringLiteral("C"),fg);
            label(p,QPointF(xn-2,y-23),QStringLiteral("Vout across L+C"),fg);
            label(p,QPointF(r.center().x(),r.bottom()-7),QStringLiteral("series-RLC notch prototype"),fg);
        }
    }


    void drawRfChain(QPainter&p,const QRectF&r,const QColor&fg,const QColor&a,const QColor&mid)
    {
        const double y=r.center().y();const double x0=r.left()+20,x1=r.right()-20;
        label(p,QPointF(r.center().x(),r.top()+12),QStringLiteral("source Z0 → PCB line → L-match / trap → antenna"),fg);
        p.setPen(QPen(mid,2));p.drawLine(QPointF(x0,y),QPointF(x1,y));
        p.setBrush(a);p.setPen(Qt::NoPen);p.drawEllipse(QPointF(x0+18,y),7,7);p.setBrush(Qt::NoBrush);label(p,QPointF(x0+18,y+28),QStringLiteral("port Z0"),fg);
        QRectF lineBox(x0+52,y-19,72,38);p.setPen(QPen(a,2));p.drawRoundedRect(lineBox,5,5);label(p,lineBox.center(),QStringLiteral("PCB line"),fg);
        const double xm=x0+160;inductor(p,QPointF(xm-22,y),QPointF(xm+35,y),a);label(p,QPointF(xm+5,y-27),QStringLiteral("series L/C"),fg);
        const double xs=x0+225;p.setPen(QPen(mid,2));p.drawLine(QPointF(xs,y),QPointF(xs,y+27));capacitor(p,QPointF(xs,y+37),a,true);p.drawLine(QPointF(xs,y+45),QPointF(xs,y+68));label(p,QPointF(xs+43,y+38),QStringLiteral("shunt C/L"),fg);
        p.setPen(QPen(fg,2));p.drawLine(QPointF(x1-28,y-42),QPointF(x1-28,y+42));p.drawLine(QPointF(x1-28,y-42),QPointF(x1-7,y-25));p.drawLine(QPointF(x1-28,y+42),QPointF(x1-7,y+25));label(p,QPointF(x1-35,y+66),QStringLiteral("Zant(f)"),fg);
        p.setPen(QPen(mid,1,Qt::DashLine));p.drawLine(QPointF(xs+35,y-55),QPointF(xs+35,y+65));label(p,QPointF(xs+56,y-56),QStringLiteral("optional LC trap"),mid);
        label(p,QPointF(r.center().x(),r.bottom()-8),QStringLiteral("Γsource(f), S11, VSWR, Smith, matching bandwidth"),fg);
    }

    Kind m_kind;
    int m_variant=0;
    QMap<QString,QString> m_values;
};
