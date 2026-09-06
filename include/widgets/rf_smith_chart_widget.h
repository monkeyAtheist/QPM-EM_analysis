#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QMouseEvent>
#include <QLineF>
#include <QFontMetrics>
#include <QEvent>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

class RfSmithChartWidget final : public QWidget
{
public:
    explicit RfSmithChartWidget(QWidget *parent=nullptr) : QWidget(parent)
    {
        setMinimumSize(360,260);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setSweep(const QVector<double> &frequencyHz,const QVector<std::complex<double>> &gamma,
                  int bestIndex,int targetIndex)
    {
        m_frequencyHz=frequencyHz;m_gamma=gamma;m_bestIndex=bestIndex;m_targetIndex=targetIndex;update();
    }
    void clearSweep(){m_frequencyHz.clear();m_gamma.clear();m_bestIndex=-1;m_targetIndex=-1;update();}

protected:
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const double side=std::max(80.0,std::min(width()-42.0,height()-58.0));const QPointF c(width()*0.5,height()*0.5+8.0);const double R=side*0.5;
        int best=-1;double bestPx=14.0;
        const int n=static_cast<int>(std::min(m_frequencyHz.size(),m_gamma.size()));
        for(int i=0;i<n;++i){const auto &g=m_gamma[i];if(!std::isfinite(g.real())||!std::isfinite(g.imag()))continue;const QPointF q(c.x()+R*g.real(),c.y()-R*g.imag());const double d=QLineF(event->position(),q).length();if(d<=bestPx){bestPx=d;best=i;}}
        if(best!=m_hoverIndex){m_hoverIndex=best;update();}
        QWidget::mouseMoveEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        if(m_hoverIndex!=-1){m_hoverIndex=-1;update();}
        QWidget::leaveEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing,true);const auto pal=palette();p.fillRect(rect(),pal.color(QPalette::Base));
        const double side=std::max(80.0,std::min(width()-42.0,height()-58.0));const QPointF c(width()*0.5,height()*0.5+8.0);const double R=side*0.5;
        auto mapGamma=[&](const std::complex<double>&g){return QPointF(c.x()+R*g.real(),c.y()-R*g.imag());};
        const QRectF unit(c.x()-R,c.y()-R,2*R,2*R);p.setPen(QPen(pal.color(QPalette::Mid),1.0));p.drawEllipse(unit);p.drawLine(mapGamma({-1,0}),mapGamma({1,0}));
        p.save();QPainterPath clip;clip.addEllipse(unit);p.setClipPath(clip);QColor grid=pal.color(QPalette::Mid);grid.setAlpha(150);p.setPen(QPen(grid,1.0,Qt::DotLine));
        for(double r:{0.2,0.5,1.0,2.0,5.0}){const double cx=r/(1+r),rr=1/(1+r);p.drawEllipse(QRectF(c.x()+R*(cx-rr),c.y()-R*rr,2*R*rr,2*R*rr));}
        for(double x:{0.2,0.5,1.0,2.0,5.0})for(double sign:{-1.0,1.0}){const double cy=sign/x,rr=1/std::abs(x);p.drawEllipse(QRectF(c.x()+R*(1-rr),c.y()-R*(cy+rr),2*R*rr,2*R*rr));}p.restore();
        if(!m_gamma.isEmpty())
        {
            QPainterPath trace;bool started=false;for(const auto &g:m_gamma){if(!std::isfinite(g.real())||!std::isfinite(g.imag()))continue;const auto q=mapGamma(g);if(!started){trace.moveTo(q);started=true;}else trace.lineTo(q);}p.setPen(QPen(pal.color(QPalette::Highlight),2.4));p.drawPath(trace);
            if(started){p.setPen(Qt::NoPen);p.setBrush(pal.color(QPalette::Highlight));p.drawEllipse(mapGamma(m_gamma.front()),4.5,4.5);p.setBrush(pal.color(QPalette::Text));p.drawEllipse(mapGamma(m_gamma.back()),3.5,3.5);}
        }
        auto freqText=[&](int i){if(i<0||i>=m_frequencyHz.size())return QString();const double f=m_frequencyHz[i];if(f>=1e9)return QStringLiteral("%1 GHz").arg(f/1e9,0,'g',6);if(f>=1e6)return QStringLiteral("%1 MHz").arg(f/1e6,0,'g',6);if(f>=1e3)return QStringLiteral("%1 kHz").arg(f/1e3,0,'g',6);return QStringLiteral("%1 Hz").arg(f,0,'g',6);};
        auto marker=[&](int index,const QString&name,int hueShift){if(index<0||index>=m_gamma.size())return;const auto &g=m_gamma[index];if(!std::isfinite(g.real())||!std::isfinite(g.imag()))return;QColor mc=pal.color(QPalette::Highlight);int h=mc.hsvHue();if(h<0)h=205;mc.setHsv((h+hueShift)%360,210,235);const QPointF q=mapGamma(g);p.setPen(QPen(pal.color(QPalette::Base),1.5));p.setBrush(mc);p.drawEllipse(q,6,6);p.setPen(mc);p.drawText(QRectF(q.x()+8,q.y()-12,180,24),Qt::AlignLeft|Qt::AlignVCenter,name+QStringLiteral(" ")+freqText(index));};
        marker(m_bestIndex,QStringLiteral("Best"),0);if(m_targetIndex!=m_bestIndex)marker(m_targetIndex,QStringLiteral("Target"),110);
        if(m_hoverIndex>=0&&m_hoverIndex<static_cast<int>(m_gamma.size())&&m_hoverIndex<static_cast<int>(m_frequencyHz.size()))
        {
            const auto g=m_gamma[m_hoverIndex];const QPointF q=mapGamma(g);const double gm=std::abs(g);
            const auto den=std::complex<double>(1.0,0.0)-g;const auto z=std::abs(den)>1e-12?(std::complex<double>(1.0,0.0)+g)/den:std::complex<double>(1e12,0.0);
            const double rl=gm>1e-12?-20.0*std::log10(gm):200.0;const double vswr=gm<0.999999?(1.0+gm)/std::max(1e-12,1.0-gm):std::numeric_limits<double>::infinity();
            p.setPen(QPen(pal.color(QPalette::Base),1.2));p.setBrush(pal.color(QPalette::Highlight));p.drawEllipse(q,5.0,5.0);
            QStringList info{freqText(m_hoverIndex),QStringLiteral("Γ = %1 %2 j").arg(g.real(),0,'g',5).arg(g.imag()>=0?QStringLiteral("+")+QString::number(g.imag(),'g',5):QString::number(g.imag(),'g',5)),QStringLiteral("|Γ| = %1").arg(gm,0,'g',5),QStringLiteral("z/Z0 = %1 %2 j").arg(z.real(),0,'g',5).arg(z.imag()>=0?QStringLiteral("+")+QString::number(z.imag(),'g',5):QString::number(z.imag(),'g',5)),QStringLiteral("RL = %1 dB | VSWR = %2").arg(rl,0,'g',5).arg(std::isfinite(vswr)?QString::number(vswr,'g',5):QStringLiteral("∞"))};
            const QFontMetrics fm(p.font());int bw=0;for(const auto &s:info)bw=std::max(bw,fm.horizontalAdvance(s));const double lh=fm.height()+2.0,bh=10.0+lh*info.size(),ww=std::min(width()*0.48,double(bw)+18.0);double bx=q.x()+12.0;if(bx+ww>width()-8)bx=q.x()-ww-12.0;bx=std::clamp(bx,8.0,std::max(8.0,width()-ww-8.0));double by=q.y()-bh-8.0;if(by<30)by=q.y()+10.0;by=std::clamp(by,30.0,std::max(30.0,height()-bh-28.0));QRectF box(bx,by,ww,bh);QColor fill=pal.color(QPalette::Base);fill.setAlpha(232);p.setBrush(fill);p.setPen(QPen(pal.color(QPalette::Mid),1.0));p.drawRoundedRect(box,6,6);p.setPen(pal.color(QPalette::Text));for(int i=0;i<info.size();++i)p.drawText(QRectF(box.left()+8,box.top()+6+i*lh,box.width()-12,lh),Qt::AlignLeft|Qt::AlignVCenter,info[i]);
        }
        p.setPen(pal.color(QPalette::Text));p.drawText(QRectF(8,4,width()-16,24),Qt::AlignCenter,QStringLiteral("Cascaded RF-chain Smith chart — Γ at source reference plane"));
        p.setPen(pal.color(QPalette::PlaceholderText));p.drawText(QRectF(8,height()-23,width()-16,18),Qt::AlignCenter,m_gamma.isEmpty()?QStringLiteral("Run the RF-chain sweep to populate the Smith chart."):QStringLiteral("Hover the trace for frequency, Γ, normalized impedance, RL and VSWR. Start/end dots retain sweep direction."));
    }
private:
    QVector<double> m_frequencyHz;QVector<std::complex<double>> m_gamma;int m_bestIndex=-1,m_targetIndex=-1;int m_hoverIndex=-1;
};
