#include "widgets/fdtd_workspace.h"

#include "fdtd_analysis.h"
#include "fdtd_solver.h"
#include "vna_time_domain.h"
#include "widgets/field_profile_plot.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QProgressDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace
{
QDoubleSpinBox *dspin(QWidget *parent, double value, double min, double max, int decimals = 6)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(decimals);
    s->setRange(min, max);
    s->setValue(value);
    s->setKeyboardTracking(false);
    return s;
}

QLabel *outLabel(QWidget *parent)
{
    auto *l = new QLabel(QStringLiteral("—"), parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    return l;
}

QString eng(double value, const QString &unit)
{
    if (!std::isfinite(value)) return QStringLiteral("n/a");
    if (std::abs(value) < 1e-30) return QStringLiteral("0 %1").arg(unit);
    struct P { double s; const char *p; };
    static const P p[] = {{1e12,"T"},{1e9,"G"},{1e6,"M"},{1e3,"k"},{1,""},{1e-3,"m"},{1e-6,"u"},{1e-9,"n"},{1e-12,"p"},{1e-15,"f"}};
    for (const auto &x : p)
        if (std::abs(value) >= 0.999 * x.s || x.s == 1e-15)
            return QStringLiteral("%1 %2%3").arg(value / x.s, 0, 'g', 7).arg(QString::fromLatin1(x.p), unit);
    return QStringLiteral("%1 %2").arg(value, 0, 'g', 7).arg(unit);
}

QString complexText(const std::complex<double> &v)
{
    return QStringLiteral("%1 %2 j%3")
        .arg(v.real(), 0, 'g', 6)
        .arg(v.imag() < 0.0 ? QStringLiteral("-") : QStringLiteral("+"))
        .arg(std::abs(v.imag()), 0, 'g', 6);
}

QVector<double> qvec(const std::vector<double> &v)
{
    QVector<double> q;
    q.reserve(static_cast<qsizetype>(v.size()));
    for (double x : v) q.push_back(x);
    return q;
}

struct ProbeDef
{
    QString name;
    int x = 100;
    int y = 60;
    bool enabled = true;
};

struct ProbeHistory
{
    std::vector<double> time;
    std::vector<double> scalar;
    std::vector<double> companion;
    std::vector<double> eTangential;
    std::vector<double> hTangential;
};

struct RfSweepData
{
    QString title;
    std::vector<double> frequencyHz;
    std::vector<std::complex<double>> s11;
    std::vector<std::complex<double>> s21;
};

std::size_t nearestFrequencyIndex(const std::vector<double> &frequencyHz, double targetHz)
{
    if (frequencyHz.empty()) return 0;
    auto it = std::lower_bound(frequencyHz.begin(), frequencyHz.end(), targetHz);
    if (it == frequencyHz.begin()) return 0;
    if (it == frequencyHz.end()) return frequencyHz.size()-1;
    const std::size_t hi = static_cast<std::size_t>(std::distance(frequencyHz.begin(), it));
    const std::size_t lo = hi-1;
    return std::abs(frequencyHz[hi]-targetHz) < std::abs(frequencyHz[lo]-targetHz) ? hi : lo;
}

double magnitudeDb(const std::complex<double> &v)
{
    return 20.0*std::log10(std::max(1e-15, std::abs(v)));
}

double phaseDeg(const std::complex<double> &v)
{
    return std::atan2(v.imag(), v.real())*180.0/FDTDAnalysis::Pi;
}

QString waveformName(FDTD::SourceWaveform w)
{
    switch (w)
    {
        case FDTD::SourceWaveform::ContinuousSine: return QStringLiteral("Sine");
        case FDTD::SourceWaveform::GaussianModulatedSine: return QStringLiteral("Gaussian sine");
        case FDTD::SourceWaveform::Ricker: return QStringLiteral("Ricker");
    }
    return QStringLiteral("?");
}

class SmithChartWidget final : public QWidget
{
public:
    explicit SmithChartWidget(QWidget *parent=nullptr) : QWidget(parent)
    {
        setMinimumSize(360,300);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setSweep(const std::vector<double> &frequencyHz, const std::vector<std::complex<double>> &gamma)
    {
        m_frequencyHz=frequencyHz;
        m_gamma=gamma;
        update();
    }
    void setFrozenSweeps(const std::vector<RfSweepData> &sweeps){m_frozen=sweeps;update();}
    void setGatedSweep(const std::vector<std::complex<double>> &gamma){m_gatedGamma=gamma;update();}
    void clearGatedSweep(){m_gatedGamma.clear();update();}
    void setMarkers(const QVector<FieldPlotMarker> &markers){m_markers=markers;update();}
    void clearSweep(){m_frequencyHz.clear();m_gamma.clear();m_gatedGamma.clear();m_frozen.clear();m_markers.clear();update();}

    std::function<void(double)> markerPositionRequested;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing,true);
        const auto pal=palette();p.fillRect(rect(),pal.color(QPalette::Base));
        const double side=std::max(80.0,std::min(width()-36.0,height()-48.0));
        const QPointF c(width()*0.5,height()*0.5+8.0);const double R=side*0.5;
        auto map=[&](std::complex<double> g){return QPointF(c.x()+R*g.real(),c.y()-R*g.imag());};
        const QRectF unit(c.x()-R,c.y()-R,2*R,2*R);
        p.setPen(QPen(pal.color(QPalette::Mid),1));p.drawEllipse(unit);p.drawLine(map({-1,0}),map({1,0}));
        p.save();QPainterPath clip;clip.addEllipse(unit);p.setClipPath(clip);
        QColor grid=pal.color(QPalette::Mid);grid.setAlpha(150);p.setPen(QPen(grid,1,Qt::DotLine));
        for(double r:{0.2,0.5,1.0,2.0,5.0})
        {
            const double cx=r/(1.0+r),rr=1.0/(1.0+r);
            p.drawEllipse(QRectF(c.x()+R*(cx-rr),c.y()-R*rr,2*R*rr,2*R*rr));
        }
        for(double x:{0.2,0.5,1.0,2.0,5.0})
            for(double sign:{-1.0,1.0})
            {
                const double cy=sign/x,rr=1.0/std::abs(x);
                p.drawEllipse(QRectF(c.x()+R*(1.0-rr),c.y()-R*(cy+rr),2*R*rr,2*R*rr));
            }
        p.restore();

        // Frozen sweeps first, as dashed comparison traces.
        for(std::size_t fi=0;fi<m_frozen.size();++fi)
        {
            const auto &sw=m_frozen[fi];
            if(sw.s11.empty())continue;
            QPainterPath path;bool started=false;
            for(const auto &g:sw.s11){if(!std::isfinite(g.real())||!std::isfinite(g.imag()))continue;const QPointF q=map(g);if(!started){path.moveTo(q);started=true;}else path.lineTo(q);}
            QColor fc=pal.color(QPalette::Highlight);int h=fc.hsvHue();if(h<0)h=205;fc.setHsv((h+70+int(fi)*53)%360,std::clamp(fc.hsvSaturation()+15,70,220),std::clamp(fc.value(),145,235),150);
            p.setPen(QPen(fc,1.4,Qt::DashLine));p.drawPath(path);
        }

        if(!m_gamma.empty())
        {
            QPainterPath path;bool started=false;
            for(const auto &g:m_gamma)
            {
                if(!std::isfinite(g.real())||!std::isfinite(g.imag()))continue;
                const QPointF q=map(g);if(!started){path.moveTo(q);started=true;}else path.lineTo(q);
            }
            p.setPen(QPen(pal.color(QPalette::Highlight),2.4));p.drawPath(path);
            if(started){p.setBrush(pal.color(QPalette::Highlight));p.setPen(Qt::NoPen);p.drawEllipse(map(m_gamma.front()),4,4);p.setBrush(pal.color(QPalette::Text));p.drawEllipse(map(m_gamma.back()),3,3);}
        }

        if(!m_gatedGamma.empty())
        {
            QPainterPath path;bool started=false;
            for(const auto &g:m_gatedGamma)
            {
                if(!std::isfinite(g.real())||!std::isfinite(g.imag()))continue;
                const QPointF q=map(g);if(!started){path.moveTo(q);started=true;}else path.lineTo(q);
            }
            QColor gc=pal.color(QPalette::Highlight);int h=gc.hsvHue();if(h<0)h=205;
            gc.setHsv((h+145)%360,std::clamp(gc.hsvSaturation()+20,90,230),std::clamp(gc.value(),155,240));
            p.setPen(QPen(gc,2.0,Qt::DotLine));p.drawPath(path);
        }

        for(int mi=0;mi<int(m_markers.size());++mi)
        {
            const auto &m=m_markers[mi];if(!m.enabled||m_frequencyHz.empty()||m_gamma.empty())continue;
            const auto idx=nearestFrequencyIndex(m_frequencyHz,m.xValue);if(idx>=m_gamma.size())continue;
            const QPointF q=map(m_gamma[idx]);
            QColor mc=pal.color(QPalette::Highlight);int h=mc.hsvHue();if(h<0)h=205;mc.setHsv((h+mi*71)%360,210,235);
            p.setPen(QPen(pal.color(QPalette::Base),m.active?2.4:1.4));p.setBrush(mc);p.drawEllipse(q,m.active?7.0:5.3,m.active?7.0:5.3);
            p.setPen(mc);QFont f=p.font();f.setBold(m.active);p.setFont(f);p.drawText(QRectF(q.x()+7,q.y()-12,52,22),Qt::AlignLeft|Qt::AlignVCenter,m.name);p.setFont(font());
        }
        if(m_hoverIndex>=0&&m_hoverIndex<static_cast<int>(m_gamma.size())&&m_hoverIndex<static_cast<int>(m_frequencyHz.size()))
        {
            const auto g=m_gamma[static_cast<std::size_t>(m_hoverIndex)];const QPointF q=map(g);const double gm=std::abs(g);
            const auto den=std::complex<double>(1.0,0.0)-g;const auto z=std::abs(den)>1e-12?(std::complex<double>(1.0,0.0)+g)/den:std::complex<double>(1e12,0.0);
            const double rl=gm>1e-12?-20.0*std::log10(gm):200.0;const double vswr=gm<0.999999?(1.0+gm)/std::max(1e-12,1.0-gm):std::numeric_limits<double>::infinity();
            auto ftxt=[](double f){if(f>=1e9)return QStringLiteral("%1 GHz").arg(f/1e9,0,'g',6);if(f>=1e6)return QStringLiteral("%1 MHz").arg(f/1e6,0,'g',6);if(f>=1e3)return QStringLiteral("%1 kHz").arg(f/1e3,0,'g',6);return QStringLiteral("%1 Hz").arg(f,0,'g',6);};
            p.setPen(QPen(pal.color(QPalette::Base),1.2));p.setBrush(pal.color(QPalette::Highlight));p.drawEllipse(q,5.0,5.0);
            QStringList info{ftxt(m_frequencyHz[static_cast<std::size_t>(m_hoverIndex)]),QStringLiteral("S11 = %1 %2 j").arg(g.real(),0,'g',5).arg(g.imag()>=0?QStringLiteral("+")+QString::number(g.imag(),'g',5):QString::number(g.imag(),'g',5)),QStringLiteral("|S11| = %1").arg(gm,0,'g',5),QStringLiteral("z/Z0 = %1 %2 j").arg(z.real(),0,'g',5).arg(z.imag()>=0?QStringLiteral("+")+QString::number(z.imag(),'g',5):QString::number(z.imag(),'g',5)),QStringLiteral("RL = %1 dB | VSWR = %2").arg(rl,0,'g',5).arg(std::isfinite(vswr)?QString::number(vswr,'g',5):QStringLiteral("∞"))};
            const QFontMetrics fm(p.font());int bw=0;for(const auto &s:info)bw=std::max(bw,fm.horizontalAdvance(s));const double lh=fm.height()+2.0,bh=10.0+lh*info.size(),ww=std::min(width()*0.48,double(bw)+18.0);double bx=q.x()+12.0;if(bx+ww>width()-8)bx=q.x()-ww-12.0;bx=std::clamp(bx,8.0,std::max(8.0,width()-ww-8.0));double by=q.y()-bh-8.0;if(by<30)by=q.y()+10.0;by=std::clamp(by,30.0,std::max(30.0,height()-bh-28.0));QRectF box(bx,by,ww,bh);QColor fill=pal.color(QPalette::Base);fill.setAlpha(232);p.setBrush(fill);p.setPen(QPen(pal.color(QPalette::Mid),1.0));p.drawRoundedRect(box,6,6);p.setPen(pal.color(QPalette::Text));for(int i=0;i<info.size();++i)p.drawText(QRectF(box.left()+8,box.top()+6+i*lh,box.width()-12,lh),Qt::AlignLeft|Qt::AlignVCenter,info[i]);
        }
        p.setPen(pal.color(QPalette::Text));p.drawText(QRectF(8,5,width()-16,24),Qt::AlignCenter,QStringLiteral("Smith chart — S11 / normalized impedance"));
        p.setPen(pal.color(QPalette::PlaceholderText));p.drawText(QRectF(8,height()-24,width()-16,18),Qt::AlignCenter,m_gamma.empty()?QStringLiteral("Run an S-parameter sweep to populate the chart."):(m_gatedGamma.empty()?QStringLiteral("Hover for frequency/S11/impedance/RL/VSWR; drag/click the current trace to move the active VNA marker."):QStringLiteral("Solid: original S11 | dotted: time-gated S11 | hover original trace for values | click/drag for marker.")));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton && markerPositionRequested && requestNearest(event->position())){m_dragging=true;event->accept();return;}
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(m_dragging && (event->buttons()&Qt::LeftButton) && markerPositionRequested){requestNearest(event->position());event->accept();return;}
        const int hover=nearestIndexAt(event->position(),14.0);
        if(hover!=m_hoverIndex){m_hoverIndex=hover;update();}
        QWidget::mouseMoveEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        if(m_hoverIndex!=-1){m_hoverIndex=-1;update();}
        QWidget::leaveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton&&m_dragging){m_dragging=false;event->accept();return;}QWidget::mouseReleaseEvent(event);
    }
private:
    int nearestIndexAt(const QPointF &position,double maxPixels) const
    {
        if(m_frequencyHz.empty()||m_gamma.empty())return -1;
        const double side=std::max(80.0,std::min(width()-36.0,height()-48.0));const QPointF c(width()*0.5,height()*0.5+8.0);const double R=side*0.5;
        double best=maxPixels*maxPixels;int bestIdx=-1;
        const std::size_t n=std::min(m_frequencyHz.size(),m_gamma.size());
        for(std::size_t i=0;i<n;++i){const auto &g=m_gamma[i];if(!std::isfinite(g.real())||!std::isfinite(g.imag()))continue;const QPointF q(c.x()+R*g.real(),c.y()-R*g.imag());const double dx=q.x()-position.x(),dy=q.y()-position.y(),d=dx*dx+dy*dy;if(d<=best){best=d;bestIdx=static_cast<int>(i);}}
        return bestIdx;
    }
    bool requestNearest(const QPointF &position)
    {
        if(!markerPositionRequested)return false;
        const int bestIdx=nearestIndexAt(position,30.0);if(bestIdx<0)return false;markerPositionRequested(m_frequencyHz[static_cast<std::size_t>(bestIdx)]);return true;
    }
    std::vector<double> m_frequencyHz;
    std::vector<std::complex<double>> m_gamma;
    std::vector<std::complex<double>> m_gatedGamma;
    std::vector<RfSweepData> m_frozen;
    QVector<FieldPlotMarker> m_markers;
    bool m_dragging=false;
    int m_hoverIndex=-1;
};

class FdtdCanvas final : public QWidget
{
public:
    enum class Display { Primary, EMag, HMag, Ex, Ey, Ez, Hx, Hy, Hz, Energy };
    enum class Tool { Source, Probe, Paint };

    explicit FdtdCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        // The canvas must be able to yield vertical space to FFT/S-parameter plots.
        // A 500 px hard minimum made the lower analysis pane clip on laptop displays.
        setMinimumSize(480, 300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setSolver(FDTD::Solver2D *solver) { m_solver = solver; update(); }
    void setProbes(const std::vector<ProbeDef> *probes) { m_probes = probes; update(); }
    void setDisplay(Display display) { m_display = display; update(); }
    void setTool(Tool tool) { m_tool = tool; update(); }
    void setActiveSource(std::size_t index) { m_activeSource = index; update(); }
    void setActiveProbe(std::size_t index) { m_activeProbe = index; update(); }
    void setBrushRadius(int radius) { m_brushRadius = std::max(0, radius); }
    void setShowMaterials(bool show) { m_showMaterials = show; update(); }
    void setGuidePortOverlay(int inputX, int outputX, int yStart, int yEnd, bool show = true) { m_guideInputX=inputX; m_guideOutputX=outputX; m_guideYStart=yStart; m_guideYEnd=yEnd; m_showGuidePorts=show; update(); }

    std::function<void(std::size_t,int,int)> sourceMoved;
    std::function<void(std::size_t,int,int)> probeMoved;
    std::function<void(int,int,bool,int)> paintRequested;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), palette().color(QPalette::Base));
        if (!m_solver || m_solver->nx() <= 0 || m_solver->ny() <= 0)
        {
            p.setPen(palette().color(QPalette::PlaceholderText));
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("FDTD grid not configured."));
            return;
        }

        const QRectF fieldRect = drawingRect();
        const int nx = m_solver->nx(), ny = m_solver->ny();
        QImage image(nx, ny, QImage::Format_RGB32);
        std::vector<double> values(static_cast<std::size_t>(nx * ny), 0.0);
        const auto &ex=m_solver->ex(), &ey=m_solver->ey(), &ez=m_solver->ez();
        const auto &hx=m_solver->hx(), &hy=m_solver->hy(), &hz=m_solver->hz();
        auto valueAt=[&](int x,int y){
            const auto k=static_cast<std::size_t>(y*nx+x);
            switch(m_display)
            {
                case Display::Primary: return m_solver->polarization()==FDTD::Polarization::TMz?ez[k]:hz[k];
                case Display::EMag: return std::sqrt(ex[k]*ex[k]+ey[k]*ey[k]+ez[k]*ez[k]);
                case Display::HMag: return std::sqrt(hx[k]*hx[k]+hy[k]*hy[k]+hz[k]*hz[k]);
                case Display::Ex: return ex[k]; case Display::Ey: return ey[k]; case Display::Ez: return ez[k];
                case Display::Hx: return hx[k]; case Display::Hy: return hy[k]; case Display::Hz: return hz[k];
                case Display::Energy: return m_solver->sample(x,y).energyDensityJPerM3;
            }
            return 0.0;
        };
        for(int y=0;y<ny;++y)for(int x=0;x<nx;++x)values[static_cast<std::size_t>(y*nx+x)]=valueAt(x,y);

        const bool signedField = m_display==Display::Primary || m_display==Display::Ex || m_display==Display::Ey || m_display==Display::Ez || m_display==Display::Hx || m_display==Display::Hy || m_display==Display::Hz;
        std::vector<double> magnitudes; magnitudes.reserve(values.size());
        for(double v:values) if(std::isfinite(v)) magnitudes.push_back(std::abs(v));
        double scale=1.0;
        if(!magnitudes.empty())
        {
            const std::size_t idx=static_cast<std::size_t>(0.985*double(magnitudes.size()-1));
            std::nth_element(magnitudes.begin(),magnitudes.begin()+static_cast<std::ptrdiff_t>(idx),magnitudes.end());
            scale=std::max(1e-30,magnitudes[idx]);
        }
        auto color=[&](double value){
            if(signedField)
            {
                const double u=std::clamp(value/scale,-1.0,1.0),a=std::abs(u);
                return u>=0.0?QColor(int(35+220*a),int(30+85*(1-a)),int(40+40*(1-a))):QColor(int(35+35*(1-a)),int(35+85*(1-a)),int(45+210*a));
            }
            const double a=std::clamp(std::sqrt(std::abs(value)/scale),0.0,1.0);
            return QColor(int(15+235*a),int(25+185*a),int(75+40*(1-a)));
        };
        for(int y=0;y<ny;++y)for(int x=0;x<nx;++x)
        {
            const auto k=static_cast<std::size_t>(y*nx+x); QColor c=color(values[k]);
            if(m_showMaterials)
            {
                const auto m=m_solver->materialAt(x,y);
                if(m.pec)c=QColor(115,115,125);
                else if(m.conductivitySPerM>0.0){const QColor t(245,175,45);c=QColor((3*c.red()+t.red())/4,(3*c.green()+t.green())/4,(3*c.blue()+t.blue())/4);}
                else if(m.epsilonR>1.000001||m.muR>1.000001){const QColor t(70,210,125);c=QColor((4*c.red()+t.red())/5,(4*c.green()+t.green())/5,(4*c.blue()+t.blue())/5);}
            }
            image.setPixelColor(x,ny-1-y,c);
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform,false); p.drawImage(fieldRect,image);
        p.setPen(QPen(palette().color(QPalette::Mid),1.0));p.drawRect(fieldRect);

        auto mapCell=[&](int x,int y){return QPointF(fieldRect.left()+(double(x)+0.5)/nx*fieldRect.width(),fieldRect.bottom()-(double(y)+0.5)/ny*fieldRect.height());};
        const auto &sources=m_solver->sources();
        for(std::size_t i=0;i<sources.size();++i)
        {
            const auto &s=sources[i]; if(!s.enabled) continue; const QPointF q=mapCell(s.x,s.y);
            const QColor col=i==m_activeSource?QColor(255,225,45):QColor(235,175,55);
            p.setPen(QPen(col,i==m_activeSource?2.8:1.5));p.setBrush(QColor(col.red(),col.green(),col.blue(),55));p.drawEllipse(q,7,7);p.drawLine(q+QPointF(-10,0),q+QPointF(10,0));p.drawLine(q+QPointF(0,-10),q+QPointF(0,10));p.drawText(q+QPointF(8,-9),QString::fromStdString(s.name));
        }
        if(m_probes)
            for(std::size_t i=0;i<m_probes->size();++i)
            {
                const auto &pr=(*m_probes)[i];if(!pr.enabled)continue;const QPointF q=mapCell(pr.x,pr.y);const QColor col=i==m_activeProbe?QColor(65,245,190):QColor(65,180,210);
                p.setPen(QPen(col,i==m_activeProbe?2.7:1.4));p.setBrush(Qt::NoBrush);p.drawEllipse(q,6,6);p.drawText(q+QPointF(7,14),pr.name);
            }

        const auto gs=m_solver->guideModeSource();
        if(gs.enabled)
        {
            const QPointF a=mapCell(gs.x,gs.yStart),b=mapCell(gs.x,gs.yEnd);
            p.setPen(QPen(QColor(255,205,70),2.0));p.drawLine(a,b);p.drawText(a+QPointF(6,-5),QStringLiteral("MODAL SRC m=%1").arg(gs.modeIndex));
        }
        if(m_showGuidePorts)
        {
            const QPointF ia=mapCell(m_guideInputX,m_guideYStart),ib=mapCell(m_guideInputX,m_guideYEnd);
            const QPointF oa=mapCell(m_guideOutputX,m_guideYStart),ob=mapCell(m_guideOutputX,m_guideYEnd);
            p.setPen(QPen(QColor(90,220,255,180),1.4,Qt::DashLine));p.drawLine(ia,ib);p.drawLine(oa,ob);
            p.drawText(ia+QPointF(4,14),QStringLiteral("PORT 1"));p.drawText(oa+QPointF(4,14),QStringLiteral("PORT 2"));
        }

        if(m_solver->boundaryCondition()==FDTD::BoundaryCondition::Cpml)
        {
            const int n=m_solver->cpmlCells();
            const double wx=fieldRect.width()*double(n)/nx, wy=fieldRect.height()*double(n)/ny;
            p.setPen(QPen(QColor(180,90,235,130),1.2,Qt::DashLine));
            p.drawRect(fieldRect.adjusted(wx,wy,-wx,-wy));
        }

        const QString pol=m_solver->polarization()==FDTD::Polarization::TMz?QStringLiteral("TMz (Ez,Hx,Hy)"):QStringLiteral("TEz (Hz,Ex,Ey)");
        const QString mode=m_tool==Tool::Source?QStringLiteral("move selected source"):m_tool==Tool::Probe?QStringLiteral("move selected probe"):QStringLiteral("material brush");
        p.setPen(palette().color(QPalette::Text));p.drawText(QRectF(12,4,width()-24,22),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("2D FDTD — %1 — %2 × %3 — %4").arg(pol).arg(nx).arg(ny).arg(mode));
        p.drawText(QRectF(12,height()-28,width()-24,20),Qt::AlignCenter,QStringLiteral("Domain %1 m × %2 m | left: active tool | right: erase material | dashed box: CPML interface").arg(nx*m_solver->dxM(),0,'g',4).arg(ny*m_solver->dyM(),0,'g',4));
    }

    void mousePressEvent(QMouseEvent *event) override { handleMouse(event); }
    void mouseMoveEvent(QMouseEvent *event) override { if(event->buttons()!=Qt::NoButton)handleMouse(event); }

private:
    QRectF drawingRect() const { return QRectF(12,30,std::max(10,width()-24),std::max(10,height()-62)); }
    bool pointToCell(const QPointF &p,int &x,int &y) const
    {
        if(!m_solver)return false;const QRectF r=drawingRect();if(!r.contains(p))return false;
        x=std::clamp(int((p.x()-r.left())/r.width()*m_solver->nx()),0,m_solver->nx()-1);y=std::clamp(int((r.bottom()-p.y())/r.height()*m_solver->ny()),0,m_solver->ny()-1);return true;
    }
    void handleMouse(QMouseEvent *event)
    {
        int x=0,y=0;if(!pointToCell(event->position(),x,y))return;
        const bool left=(event->buttons()&Qt::LeftButton)||event->button()==Qt::LeftButton;const bool right=(event->buttons()&Qt::RightButton)||event->button()==Qt::RightButton;
        if(m_tool==Tool::Source&&left){if(sourceMoved)sourceMoved(m_activeSource,x,y);}
        else if(m_tool==Tool::Probe&&left){if(probeMoved)probeMoved(m_activeProbe,x,y);}
        else if(m_tool==Tool::Paint&&(left||right)&&paintRequested)paintRequested(x,y,right,m_brushRadius);
    }

    FDTD::Solver2D *m_solver=nullptr; const std::vector<ProbeDef> *m_probes=nullptr;
    Display m_display=Display::Primary;Tool m_tool=Tool::Source;std::size_t m_activeSource=0,m_activeProbe=0;int m_brushRadius=1;bool m_showMaterials=true;
    int m_guideInputX=38,m_guideOutputX=130,m_guideYStart=42,m_guideYEnd=78;bool m_showGuidePorts=true;
};
}

FdtdWorkspace::FdtdWorkspace(QWidget *parent)
    : QWidget(parent)
{
    auto solver=std::make_shared<FDTD::Solver2D>();
    auto probes=std::make_shared<std::vector<ProbeDef>>();
    probes->push_back({QStringLiteral("P1"),100,60,true});
    probes->push_back({QStringLiteral("P2"),130,60,true});
    auto histories=std::make_shared<std::vector<ProbeHistory>>(probes->size());
    auto accumulator=std::make_shared<FDTDAnalysis::HarmonicFieldAccumulator>();
    auto rfSweep=std::make_shared<RfSweepData>();
    auto frozenRfSweeps=std::make_shared<std::vector<RfSweepData>>();
    auto timeDomainResult=std::make_shared<VnaTimeDomain::TransformResult>();
    auto gatedRfSweep=std::make_shared<RfSweepData>();
    auto hasGatedSweep=std::make_shared<bool>(false);
    auto gatedParameter=std::make_shared<int>(0); // 0=S11, 1=S21
    auto rfMarkers=std::make_shared<QVector<FieldPlotMarker>>();
    rfMarkers->push_back({QStringLiteral("M1"),0.0,true,true});
    rfMarkers->push_back({QStringLiteral("M2"),0.0,true,false});
    rfMarkers->push_back({QStringLiteral("M3"),0.0,true,false});
    auto activeRfMarker=std::make_shared<int>(0);
    auto rfMarkersInitialized=std::make_shared<bool>(false);

    auto *root=new QVBoxLayout(this);
    auto *mainSplit=new QSplitter(Qt::Horizontal,this);root->addWidget(mainSplit,1);

    auto *controlScroll=new QScrollArea(mainSplit);controlScroll->setWidgetResizable(true);controlScroll->setMinimumWidth(370);auto *controls=new QWidget(controlScroll);auto *cl=new QVBoxLayout(controls);controlScroll->setWidget(controls);

    auto *runBox=new QGroupBox(QStringLiteral("Simulation"),controls);auto *runLayout=new QVBoxLayout(runBox);auto *buttons=new QHBoxLayout();auto *runButton=new QPushButton(QStringLiteral("Run"),runBox);auto *pauseButton=new QPushButton(QStringLiteral("Pause"),runBox);auto *stepButton=new QPushButton(QStringLiteral("Step"),runBox);auto *resetButton=new QPushButton(QStringLiteral("Reset fields"),runBox);auto *snapshotButton=new QPushButton(QStringLiteral("Snapshot PNG"),runBox);buttons->addWidget(runButton);buttons->addWidget(pauseButton);buttons->addWidget(stepButton);buttons->addWidget(resetButton);buttons->addWidget(snapshotButton);runLayout->addLayout(buttons);auto *stepsPerFrame=new QSpinBox(runBox);stepsPerFrame->setRange(1,100);stepsPerFrame->setValue(6);auto *fps=new QSpinBox(runBox);fps->setRange(5,60);fps->setValue(30);auto *rf=new QFormLayout();rf->addRow(QStringLiteral("FDTD steps / frame"),stepsPerFrame);rf->addRow(QStringLiteral("Display FPS"),fps);runLayout->addLayout(rf);cl->addWidget(runBox);

    auto *gridBox=new QGroupBox(QStringLiteral("Grid / polarization / boundary"),controls);auto *gridForm=new QFormLayout(gridBox);auto *polarization=new QComboBox(gridBox);polarization->addItems({QStringLiteral("TMz — Ez, Hx, Hy"),QStringLiteral("TEz — Hz, Ex, Ey")});auto *nxSpin=new QSpinBox(gridBox);nxSpin->setRange(40,500);nxSpin->setValue(161);auto *nySpin=new QSpinBox(gridBox);nySpin->setRange(40,500);nySpin->setValue(121);auto *dxMm=dspin(gridBox,5.0,0.01,1000.0,4);auto *dyMm=dspin(gridBox,5.0,0.01,1000.0,4);auto *courant=dspin(gridBox,0.95,0.05,0.999,4);auto *boundary=new QComboBox(gridBox);boundary->addItems({QStringLiteral("CPML absorbing"),QStringLiteral("Mur 1st-order"),QStringLiteral("PEC box")});auto *cpmlCells=new QSpinBox(gridBox);cpmlCells->setRange(4,40);cpmlCells->setValue(solver->cpmlCells());auto *cpmlReflection=dspin(gridBox,1e-7,1e-12,1e-2,10);auto *applyGrid=new QPushButton(QStringLiteral("Apply grid (clears geometry)"),gridBox);gridForm->addRow(QStringLiteral("Polarization"),polarization);gridForm->addRow(QStringLiteral("Nx"),nxSpin);gridForm->addRow(QStringLiteral("Ny"),nySpin);gridForm->addRow(QStringLiteral("dx (mm)"),dxMm);gridForm->addRow(QStringLiteral("dy (mm)"),dyMm);gridForm->addRow(QStringLiteral("Courant factor"),courant);gridForm->addRow(QStringLiteral("Boundary"),boundary);gridForm->addRow(QStringLiteral("CPML thickness (cells)"),cpmlCells);gridForm->addRow(QStringLiteral("CPML target R"),cpmlReflection);gridForm->addRow(applyGrid);cl->addWidget(gridBox);

    auto *sourceBox=new QGroupBox(QStringLiteral("Multiple RF point sources"),controls);auto *sourceLayout=new QVBoxLayout(sourceBox);auto *sourceTable=new QTableWidget(0,5,sourceBox);sourceTable->setHorizontalHeaderLabels({QStringLiteral("Name"),QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("f GHz"),QStringLiteral("On")});sourceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);sourceTable->setSelectionBehavior(QAbstractItemView::SelectRows);sourceTable->setSelectionMode(QAbstractItemView::SingleSelection);sourceTable->setMaximumHeight(135);sourceLayout->addWidget(sourceTable);auto *sourceButtons=new QHBoxLayout();auto *addSource=new QPushButton(QStringLiteral("+ Source"),sourceBox);auto *removeSource=new QPushButton(QStringLiteral("Remove"),sourceBox);sourceButtons->addWidget(addSource);sourceButtons->addWidget(removeSource);sourceLayout->addLayout(sourceButtons);auto *sourceForm=new QFormLayout();auto *sourceName=new QLineEdit(sourceBox);auto *waveform=new QComboBox(sourceBox);waveform->addItems({QStringLiteral("Continuous sine"),QStringLiteral("Gaussian-modulated sine"),QStringLiteral("Ricker pulse")});auto *injection=new QComboBox(sourceBox);injection->addItems({QStringLiteral("Soft (additive)"),QStringLiteral("Hard (overwrite)")});auto *frequencyGHz=dspin(sourceBox,1.0,0.001,100.0,6);auto *amplitude=dspin(sourceBox,1.0,-1e6,1e6,6);auto *sourcePhaseDeg=dspin(sourceBox,0.0,-3600.0,3600.0,3);auto *sourceX=new QSpinBox(sourceBox);sourceX->setRange(1,159);auto *sourceY=new QSpinBox(sourceBox);sourceY->setRange(1,119);auto *sourceEnabled=new QCheckBox(QStringLiteral("Enabled"),sourceBox);sourceEnabled->setChecked(true);sourceForm->addRow(QStringLiteral("Selected name"),sourceName);sourceForm->addRow(QStringLiteral("Waveform"),waveform);sourceForm->addRow(QStringLiteral("Injection"),injection);sourceForm->addRow(QStringLiteral("Frequency (GHz)"),frequencyGHz);sourceForm->addRow(QStringLiteral("Amplitude (Ez V/m or Hz A/m)"),amplitude);sourceForm->addRow(QStringLiteral("Phase (deg)"),sourcePhaseDeg);sourceForm->addRow(QStringLiteral("Cell X"),sourceX);sourceForm->addRow(QStringLiteral("Cell Y"),sourceY);sourceForm->addRow(sourceEnabled);sourceLayout->addLayout(sourceForm);cl->addWidget(sourceBox);

    auto *probeBox=new QGroupBox(QStringLiteral("Multiple probes / local ports"),controls);auto *probeLayout=new QVBoxLayout(probeBox);auto *probeTable=new QTableWidget(0,4,probeBox);probeTable->setHorizontalHeaderLabels({QStringLiteral("Name"),QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("On")});probeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);probeTable->setSelectionBehavior(QAbstractItemView::SelectRows);probeTable->setSelectionMode(QAbstractItemView::SingleSelection);probeTable->setMaximumHeight(120);probeLayout->addWidget(probeTable);auto *probeButtons=new QHBoxLayout();auto *addProbe=new QPushButton(QStringLiteral("+ Probe"),probeBox);auto *removeProbe=new QPushButton(QStringLiteral("Remove"),probeBox);probeButtons->addWidget(addProbe);probeButtons->addWidget(removeProbe);probeLayout->addLayout(probeButtons);auto *probeForm=new QFormLayout();auto *probeName=new QLineEdit(probeBox);auto *probeX=new QSpinBox(probeBox);probeX->setRange(0,160);auto *probeY=new QSpinBox(probeBox);probeY->setRange(0,120);auto *probeEnabled=new QCheckBox(QStringLiteral("Enabled"),probeBox);probeEnabled->setChecked(true);probeForm->addRow(QStringLiteral("Selected name"),probeName);probeForm->addRow(QStringLiteral("Cell X"),probeX);probeForm->addRow(QStringLiteral("Cell Y"),probeY);probeForm->addRow(probeEnabled);probeLayout->addLayout(probeForm);cl->addWidget(probeBox);

    auto *paintBox=new QGroupBox(QStringLiteral("Geometry / material brush"),controls);auto *paintForm=new QFormLayout(paintBox);auto *tool=new QComboBox(paintBox);tool->addItems({QStringLiteral("Move selected source"),QStringLiteral("Move selected probe"),QStringLiteral("Paint material")});auto *material=new QComboBox(paintBox);material->addItems({QStringLiteral("Free space / erase"),QStringLiteral("Dielectric"),QStringLiteral("Lossy dielectric"),QStringLiteral("PEC")});auto *epsR=dspin(paintBox,4.0,1.0,1000.0,4);auto *muR=dspin(paintBox,1.0,1.0,1000.0,4);auto *sigma=dspin(paintBox,0.02,0.0,1e7,6);auto *brushRadius=new QSpinBox(paintBox);brushRadius->setRange(0,20);brushRadius->setValue(1);auto *showMaterials=new QCheckBox(QStringLiteral("Overlay material regions"),paintBox);showMaterials->setChecked(true);paintForm->addRow(QStringLiteral("Canvas tool"),tool);paintForm->addRow(QStringLiteral("Material"),material);paintForm->addRow(QStringLiteral("εr"),epsR);paintForm->addRow(QStringLiteral("μr"),muR);paintForm->addRow(QStringLiteral("σ (S/m)"),sigma);paintForm->addRow(QStringLiteral("Brush radius"),brushRadius);paintForm->addRow(showMaterials);cl->addWidget(paintBox);

    auto *presetBox=new QGroupBox(QStringLiteral("Geometry presets"),controls);auto *presetLayout=new QVBoxLayout(presetBox);auto *preset=new QComboBox(presetBox);preset->addItems({QStringLiteral("Free space"),QStringLiteral("Dielectric slab"),QStringLiteral("PEC single slit"),QStringLiteral("PEC double slit"),QStringLiteral("PEC cavity"),QStringLiteral("Parallel-plate guide")});auto *applyPreset=new QPushButton(QStringLiteral("Apply preset"),presetBox);auto *clearMaterials=new QPushButton(QStringLiteral("Clear materials"),presetBox);presetLayout->addWidget(preset);presetLayout->addWidget(applyPreset);presetLayout->addWidget(clearMaterials);cl->addWidget(presetBox);

    auto *analysisBox=new QGroupBox(QStringLiteral("Frequency / port / far-field analysis"),controls);auto *analysisForm=new QFormLayout(analysisBox);auto *analysisFreqGHz=dspin(analysisBox,1.0,0.001,100.0,6);auto *inputPort=new QComboBox(analysisBox);auto *outputPort=new QComboBox(analysisBox);auto *accumulateFar=new QCheckBox(QStringLiteral("Accumulate harmonic field for NF→FF"),analysisBox);accumulateFar->setChecked(true);auto *contourMargin=new QSpinBox(analysisBox);contourMargin->setRange(3,100);contourMargin->setValue(18);auto *fftButton=new QPushButton(QStringLiteral("FFT selected probe"),analysisBox);auto *sButton=new QPushButton(QStringLiteral("S11/S21 @ analysis f"),analysisBox);auto *sSweepButton=new QPushButton(QStringLiteral("Broadband S from histories"),analysisBox);auto *farButton=new QPushButton(QStringLiteral("2D Huygens far-field"),analysisBox);auto *s11Label=outLabel(analysisBox),*s21Label=outLabel(analysisBox),*vswrLabel=outLabel(analysisBox),*farDLabel=outLabel(analysisBox),*analysisNote=outLabel(analysisBox);analysisForm->addRow(QStringLiteral("Analysis frequency (GHz)"),analysisFreqGHz);analysisForm->addRow(QStringLiteral("Input port probe"),inputPort);analysisForm->addRow(QStringLiteral("Output port probe"),outputPort);analysisForm->addRow(accumulateFar);analysisForm->addRow(QStringLiteral("Huygens contour margin"),contourMargin);analysisForm->addRow(fftButton);analysisForm->addRow(sButton);analysisForm->addRow(sSweepButton);analysisForm->addRow(farButton);analysisForm->addRow(QStringLiteral("S11"),s11Label);analysisForm->addRow(QStringLiteral("S21"),s21Label);analysisForm->addRow(QStringLiteral("VSWR"),vswrLabel);analysisForm->addRow(QStringLiteral("2D directivity"),farDLabel);analysisForm->addRow(QStringLiteral("Validity"),analysisNote);cl->addWidget(analysisBox);


    auto *guideBox=new QGroupBox(QStringLiteral("2D PEC guide modal ports / CW sweep"),controls);
    auto *guideForm=new QFormLayout(guideBox);
    auto *guideEnableSource=new QCheckBox(QStringLiteral("Enable distributed modal line source"),guideBox);guideEnableSource->setChecked(false);
    auto *guideSourceX=new QSpinBox(guideBox);guideSourceX->setRange(2,158);guideSourceX->setValue(24);
    auto *guideInputX=new QSpinBox(guideBox);guideInputX->setRange(2,158);guideInputX->setValue(38);
    auto *guideOutputX=new QSpinBox(guideBox);guideOutputX->setRange(2,158);guideOutputX->setValue(130);
    auto *guideYStart=new QSpinBox(guideBox);guideYStart->setRange(2,118);guideYStart->setValue(42);
    auto *guideYEnd=new QSpinBox(guideBox);guideYEnd->setRange(3,119);guideYEnd->setValue(78);
    auto *guideMode=new QSpinBox(guideBox);guideMode->setRange(0,20);guideMode->setValue(1);
    auto *guideSep=new QSpinBox(guideBox);guideSep->setRange(1,12);guideSep->setValue(2);
    auto *guideEpsR=dspin(guideBox,1.0,1.0,1000.0,5);
    auto *guideMuR=dspin(guideBox,1.0,1.0,1000.0,5);
    auto *guideAmplitude=dspin(guideBox,0.04,-1e4,1e4,6);
    auto *guidePresetButton=new QPushButton(QStringLiteral("Build parallel-plate guide preset"),guideBox);
    auto *guideApplySourceButton=new QPushButton(QStringLiteral("Apply modal source"),guideBox);
    auto *guideSolveButton=new QPushButton(QStringLiteral("Modal S11/S21 @ analysis f"),guideBox);
    auto *sweepStartGHz=dspin(guideBox,0.6,0.001,100.0,6);
    auto *sweepStopGHz=dspin(guideBox,3.0,0.001,100.0,6);
    auto *sweepPoints=new QSpinBox(guideBox);sweepPoints->setRange(3,81);sweepPoints->setValue(13);
    auto *settlePeriods=new QSpinBox(guideBox);settlePeriods->setRange(2,80);settlePeriods->setValue(10);
    auto *samplePeriods=new QSpinBox(guideBox);samplePeriods->setRange(2,40);samplePeriods->setValue(6);
    auto *guideSweepButton=new QPushButton(QStringLiteral("Run modal CW frequency sweep"),guideBox);
    auto *guideCutoffLabel=outLabel(guideBox),*guideBetaLabel=outLabel(guideBox),*guideS11Label=outLabel(guideBox),*guideS21Label=outLabel(guideBox),*guidePurityLabel=outLabel(guideBox),*guideNote=outLabel(guideBox);
    guideForm->addRow(guideEnableSource);
    guideForm->addRow(QStringLiteral("Modal source X cell"),guideSourceX);
    guideForm->addRow(QStringLiteral("Input modal port X"),guideInputX);
    guideForm->addRow(QStringLiteral("Output modal port X"),guideOutputX);
    guideForm->addRow(QStringLiteral("Aperture Y start"),guideYStart);
    guideForm->addRow(QStringLiteral("Aperture Y end"),guideYEnd);
    guideForm->addRow(QStringLiteral("Mode index m"),guideMode);
    guideForm->addRow(QStringLiteral("Port plane separation"),guideSep);
    guideForm->addRow(QStringLiteral("Guide εr"),guideEpsR);
    guideForm->addRow(QStringLiteral("Guide μr"),guideMuR);
    guideForm->addRow(QStringLiteral("Modal source amplitude"),guideAmplitude);
    guideForm->addRow(guidePresetButton);
    guideForm->addRow(guideApplySourceButton);
    guideForm->addRow(guideSolveButton);
    guideForm->addRow(QStringLiteral("Sweep start (GHz)"),sweepStartGHz);
    guideForm->addRow(QStringLiteral("Sweep stop (GHz)"),sweepStopGHz);
    guideForm->addRow(QStringLiteral("Sweep points"),sweepPoints);
    guideForm->addRow(QStringLiteral("Settle periods / point"),settlePeriods);
    guideForm->addRow(QStringLiteral("Sample periods / point"),samplePeriods);
    guideForm->addRow(guideSweepButton);
    guideForm->addRow(QStringLiteral("Mode cutoff"),guideCutoffLabel);
    guideForm->addRow(QStringLiteral("β / λg"),guideBetaLabel);
    guideForm->addRow(QStringLiteral("Modal S11"),guideS11Label);
    guideForm->addRow(QStringLiteral("Modal S21"),guideS21Label);
    guideForm->addRow(QStringLiteral("Mode purity"),guidePurityLabel);
    guideForm->addRow(QStringLiteral("Modal validity"),guideNote);
    cl->addWidget(guideBox);

    auto *rfBox=new QGroupBox(QStringLiteral("RF network analyzer / export"),controls);
    auto *rfForm=new QFormLayout(rfBox);
    auto *referenceZ0=dspin(rfBox,50.0,0.001,1e6,4);
    auto *analyzeSweepButton=new QPushButton(QStringLiteral("Analyze last S sweep"),rfBox);
    auto *exportS1pButton=new QPushButton(QStringLiteral("Export S11 Touchstone .s1p"),rfBox);
    auto *exportS2pButton=new QPushButton(QStringLiteral("Export .s2p (symmetric reciprocal assumption)"),rfBox);
    auto *exportCsvButton=new QPushButton(QStringLiteral("Export sweep CSV"),rfBox);
    auto *rfResonanceLabel=outLabel(rfBox),*rfImpedanceLabel=outLabel(rfBox),*rfReturnLossLabel=outLabel(rfBox),*rfBwLabel=outLabel(rfBox),*rfVswrBandLabel=outLabel(rfBox),*rfDelayLabel=outLabel(rfBox),*rfCandidatesLabel=outLabel(rfBox);
    rfForm->addRow(QStringLiteral("Reference Z0 (Ω)"),referenceZ0);
    rfForm->addRow(analyzeSweepButton);
    rfForm->addRow(QStringLiteral("Best match / resonance"),rfResonanceLabel);
    rfForm->addRow(QStringLiteral("Zin @ best match"),rfImpedanceLabel);
    rfForm->addRow(QStringLiteral("Return loss / VSWR"),rfReturnLossLabel);
    rfForm->addRow(QStringLiteral("S21 -3 dB bandwidth"),rfBwLabel);
    rfForm->addRow(QStringLiteral("VSWR ≤ 2 bandwidth"),rfVswrBandLabel);
    rfForm->addRow(QStringLiteral("Group delay @ S21 peak"),rfDelayLabel);
    rfForm->addRow(QStringLiteral("Detected resonances"),rfCandidatesLabel);
    rfForm->addRow(exportS1pButton);
    rfForm->addRow(exportS2pButton);
    rfForm->addRow(exportCsvButton);
    cl->addWidget(rfBox);

    auto *markerBox=new QGroupBox(QStringLiteral("VNA markers / sweep comparison"),controls);
    auto *markerLayout=new QVBoxLayout(markerBox);
    auto *markerForm=new QFormLayout();
    auto *markerSelect=new QComboBox(markerBox);
    auto *markerFreqGHz=dspin(markerBox,1.0,0.000001,1000.0,9);
    auto *markerEnabled=new QCheckBox(QStringLiteral("Enabled"),markerBox);markerEnabled->setChecked(true);
    auto *deltaReference=new QComboBox(markerBox);
    auto *markerReadout=outLabel(markerBox);auto *markerDelta=outLabel(markerBox);
    markerForm->addRow(QStringLiteral("Active marker"),markerSelect);
    markerForm->addRow(QStringLiteral("Frequency (GHz)"),markerFreqGHz);
    markerForm->addRow(markerEnabled);
    markerForm->addRow(QStringLiteral("Delta reference"),deltaReference);
    markerForm->addRow(QStringLiteral("Marker readout"),markerReadout);
    markerForm->addRow(QStringLiteral("Delta"),markerDelta);
    markerLayout->addLayout(markerForm);
    auto *markerButtons=new QGridLayout();
    auto *addMarkerButton=new QPushButton(QStringLiteral("+ Marker"),markerBox);
    auto *removeMarkerButton=new QPushButton(QStringLiteral("Remove marker"),markerBox);
    auto *snapS11Button=new QPushButton(QStringLiteral("Snap to best S11"),markerBox);
    auto *snapS21Button=new QPushButton(QStringLiteral("Snap to S21 peak"),markerBox);
    markerButtons->addWidget(addMarkerButton,0,0);markerButtons->addWidget(removeMarkerButton,0,1);
    markerButtons->addWidget(snapS11Button,1,0);markerButtons->addWidget(snapS21Button,1,1);
    markerLayout->addLayout(markerButtons);
    auto *freezeSweepButton=new QPushButton(QStringLiteral("Freeze current sweep"),markerBox);
    auto *clearFrozenButton=new QPushButton(QStringLiteral("Clear frozen sweeps"),markerBox);
    auto *frozenList=new QListWidget(markerBox);frozenList->setMaximumHeight(110);
    markerLayout->addWidget(freezeSweepButton);markerLayout->addWidget(clearFrozenButton);markerLayout->addWidget(frozenList);
    cl->addWidget(markerBox);

    auto *statsBox=new QGroupBox(QStringLiteral("Solver status"),controls);auto *statsForm=new QFormLayout(statsBox);auto *dtLabel=outLabel(statsBox),*timeLabel=outLabel(statsBox),*lambdaLabel=outLabel(statsBox),*cellsLambdaLabel=outLabel(statsBox),*cellsMinLambdaLabel=outLabel(statsBox),*maxFieldLabel=outLabel(statsBox),*energyLabel=outLabel(statsBox),*probeFieldLabel=outLabel(statsBox),*statusLabel=outLabel(statsBox);statsForm->addRow(QStringLiteral("Δt"),dtLabel);statsForm->addRow(QStringLiteral("Time"),timeLabel);statsForm->addRow(QStringLiteral("λ0"),lambdaLabel);statsForm->addRow(QStringLiteral("Cells / λ0"),cellsLambdaLabel);statsForm->addRow(QStringLiteral("Cells / λmin(material)"),cellsMinLambdaLabel);statsForm->addRow(QStringLiteral("max primary field"),maxFieldLabel);statsForm->addRow(QStringLiteral("Energy / z-length"),energyLabel);statsForm->addRow(QStringLiteral("Selected probe"),probeFieldLabel);statsForm->addRow(QStringLiteral("Status"),statusLabel);cl->addWidget(statsBox);cl->addStretch(1);

    auto *right=new QWidget(mainSplit);
    auto *rightLayout=new QVBoxLayout(right);
    auto *visualBar=new QHBoxLayout();
    auto *display=new QComboBox(right);
    display->addItems({QStringLiteral("Primary scalar Ez/Hz"),QStringLiteral("|E|"),QStringLiteral("|H|"),QStringLiteral("Ex"),QStringLiteral("Ey"),QStringLiteral("Ez"),QStringLiteral("Hx"),QStringLiteral("Hy"),QStringLiteral("Hz"),QStringLiteral("Energy density")});
    visualBar->addWidget(new QLabel(QStringLiteral("Display:"),right));
    visualBar->addWidget(display);
    visualBar->addStretch(1);
    rightLayout->addLayout(visualBar);
    // Use a user-resizable vertical splitter between the field canvas and analysis.
    // This prevents the tab bar/plot area from appearing to cut into the simulation
    // and lets FFT/time-domain views claim more height when required.
    auto *viewAnalysisSplit=new QSplitter(Qt::Vertical,right);
    viewAnalysisSplit->setChildrenCollapsible(false);
    viewAnalysisSplit->setHandleWidth(6);
    // Do not continuously repaint both the FDTD raster and the analysis plot while
    // the handle is moving. Apart from being cheaper, non-opaque resizing avoids a
    // transient geometry where the QTabWidget can visually lag one resize event
    // behind the canvas on Windows/Fusion.
    viewAnalysisSplit->setOpaqueResize(false);
    viewAnalysisSplit->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    rightLayout->addWidget(viewAnalysisSplit,1);

    // Put each splitter child in its own pane. QTabWidget paints its tab bar around
    // its frame; using a dedicated pane makes the splitter allocate the complete
    // tab widget (tab bar + page) instead of letting the canvas and tab frame appear
    // to touch/overlap during the first layout pass.
    auto *canvasPane=new QWidget();
    auto *canvasPaneLayout=new QVBoxLayout(canvasPane);
    canvasPaneLayout->setContentsMargins(0,0,0,0);
    canvasPaneLayout->setSpacing(0);
    auto *canvas=new FdtdCanvas(canvasPane);canvas->setSolver(solver.get());canvas->setProbes(probes.get());
    canvasPaneLayout->addWidget(canvas,1);
    canvasPane->setMinimumHeight(240);
    canvasPane->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);
    viewAnalysisSplit->addWidget(canvasPane);

    auto *analysisPane=new QWidget();
    auto *analysisPaneLayout=new QVBoxLayout(analysisPane);
    analysisPaneLayout->setContentsMargins(0,0,0,0);
    analysisPaneLayout->setSpacing(0);
    auto *analysisTabs=new QTabWidget(analysisPane);
    auto *timePlot=new FieldProfilePlot(analysisTabs);timePlot->setXAxis(QStringLiteral("Time"),QStringLiteral("s"));
    auto *fftPlot=new FieldProfilePlot(analysisTabs);fftPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    auto *sPlot=new FieldProfilePlot(analysisTabs);sPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    auto *rfPhasePlot=new FieldProfilePlot(analysisTabs);rfPhasePlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    auto *farPlot=new FieldProfilePlot(analysisTabs);farPlot->setXAxis(QStringLiteral("Azimuth φ"),QStringLiteral("deg"));
    auto *smithPage=new QWidget(analysisTabs);auto *smithLayout=new QHBoxLayout(smithPage);auto *smithChart=new SmithChartWidget(smithPage);smithLayout->addWidget(smithChart,2);
    auto *smithInfo=new QGroupBox(QStringLiteral("Last sweep summary"),smithPage);auto *smithInfoForm=new QFormLayout(smithInfo);auto *smithSource=outLabel(smithInfo),*smithBest=outLabel(smithInfo),*smithZ=outLabel(smithInfo),*smithPeak=outLabel(smithInfo),*smithBandwidth=outLabel(smithInfo);smithInfoForm->addRow(QStringLiteral("Sweep"),smithSource);smithInfoForm->addRow(QStringLiteral("Best S11"),smithBest);smithInfoForm->addRow(QStringLiteral("Equivalent Zin"),smithZ);smithInfoForm->addRow(QStringLiteral("S21 peak"),smithPeak);smithInfoForm->addRow(QStringLiteral("Bandwidth"),smithBandwidth);smithLayout->addWidget(smithInfo,1);

    // VNA frequency -> time transform and time gating (2.2.4).
    auto *timeDomainPage=new QWidget(analysisTabs);auto *tdPageLayout=new QVBoxLayout(timeDomainPage);tdPageLayout->setContentsMargins(0,0,0,0);
    auto *tdScroll=new QScrollArea(timeDomainPage);tdScroll->setWidgetResizable(true);auto *tdContent=new QWidget(tdScroll);auto *tdRoot=new QVBoxLayout(tdContent);tdScroll->setWidget(tdContent);tdPageLayout->addWidget(tdScroll);
    auto *tdControls=new QGroupBox(QStringLiteral("VNA time domain / gating"),tdContent);auto *tdGrid=new QGridLayout(tdControls);
    auto *tdParameter=new QComboBox(tdControls);tdParameter->addItems({QStringLiteral("S11 — reflection / TDR"),QStringLiteral("S21 — transmission")});tdParameter->setMinimumWidth(190);
    auto *tdWindow=new QComboBox(tdControls);tdWindow->addItems({QStringLiteral("Rectangular"),QStringLiteral("Hann"),QStringLiteral("Hamming"),QStringLiteral("Blackman")});tdWindow->setCurrentIndex(1);tdWindow->setMinimumWidth(125);
    auto *tdAxis=new QComboBox(tdControls);tdAxis->addItems({QStringLiteral("Time (ns)"),QStringLiteral("Distance (m)")});tdAxis->setMinimumWidth(120);
    auto *tdPropagation=new QComboBox(tdControls);tdPropagation->addItems({QStringLiteral("Velocity factor"),QStringLiteral("Effective ε / μ")});tdPropagation->setMinimumWidth(145);
    auto *tdVelocityFactor=dspin(tdControls,0.66,0.001,2.0,6);auto *tdEpsEff=dspin(tdControls,1.0,0.000001,1e6,6);auto *tdMuEff=dspin(tdControls,1.0,0.000001,1e6,6);
    auto *tdGateMode=new QComboBox(tdControls);tdGateMode->addItems({QStringLiteral("Keep inside"),QStringLiteral("Reject inside")});
    auto *tdGateEdge=new QComboBox(tdControls);tdGateEdge->addItems({QStringLiteral("Raised cosine"),QStringLiteral("Rectangular")});
    auto *tdGateStartNs=dspin(tdControls,0.0,0.0,1e12,6);auto *tdGateStopNs=dspin(tdControls,100.0,0.0,1e12,6);auto *tdTransitionNs=dspin(tdControls,2.0,0.0,1e12,6);
    auto *tdRefreshButton=new QPushButton(QStringLiteral("Transform sweep"),tdControls);auto *tdApplyGateButton=new QPushButton(QStringLiteral("Apply gate → frequency"),tdControls);auto *tdClearGateButton=new QPushButton(QStringLiteral("Clear gate result"),tdControls);auto *tdExportGatedButton=new QPushButton(QStringLiteral("Export gated S11 .s1p"),tdControls);
    auto *tdSampling=outLabel(tdControls),*tdDistanceInfo=outLabel(tdControls),*tdPeaks=outLabel(tdControls),*tdStatus=outLabel(tdControls);
    tdGrid->addWidget(new QLabel(QStringLiteral("Parameter:"),tdControls),0,0);tdGrid->addWidget(tdParameter,0,1);tdGrid->addWidget(new QLabel(QStringLiteral("Frequency window:"),tdControls),0,2);tdGrid->addWidget(tdWindow,0,3);tdGrid->addWidget(new QLabel(QStringLiteral("X axis:"),tdControls),0,4);tdGrid->addWidget(tdAxis,0,5);
    tdGrid->addWidget(new QLabel(QStringLiteral("Propagation:"),tdControls),1,0);tdGrid->addWidget(tdPropagation,1,1);tdGrid->addWidget(new QLabel(QStringLiteral("Velocity factor:"),tdControls),1,2);tdGrid->addWidget(tdVelocityFactor,1,3);tdGrid->addWidget(new QLabel(QStringLiteral("εeff / μeff:"),tdControls),1,4);auto *tdMediumRow=new QWidget(tdControls);auto *tdMediumLayout=new QHBoxLayout(tdMediumRow);tdMediumLayout->setContentsMargins(0,0,0,0);tdMediumLayout->addWidget(tdEpsEff);tdMediumLayout->addWidget(tdMuEff);tdGrid->addWidget(tdMediumRow,1,5);
    tdGrid->addWidget(new QLabel(QStringLiteral("Gate mode:"),tdControls),2,0);tdGrid->addWidget(tdGateMode,2,1);tdGrid->addWidget(new QLabel(QStringLiteral("Edge:"),tdControls),2,2);tdGrid->addWidget(tdGateEdge,2,3);tdGrid->addWidget(new QLabel(QStringLiteral("Transition (ns):"),tdControls),2,4);tdGrid->addWidget(tdTransitionNs,2,5);
    tdGrid->addWidget(new QLabel(QStringLiteral("Gate start (ns):"),tdControls),3,0);tdGrid->addWidget(tdGateStartNs,3,1);tdGrid->addWidget(new QLabel(QStringLiteral("Gate stop (ns):"),tdControls),3,2);tdGrid->addWidget(tdGateStopNs,3,3);auto *tdButtons=new QHBoxLayout();tdButtons->addWidget(tdRefreshButton);tdButtons->addWidget(tdApplyGateButton);tdButtons->addWidget(tdClearGateButton);tdButtons->addWidget(tdExportGatedButton);tdGrid->addLayout(tdButtons,3,4,1,2);
    tdGrid->addWidget(new QLabel(QStringLiteral("Sampling:"),tdControls),4,0);tdGrid->addWidget(tdSampling,4,1,1,2);tdGrid->addWidget(new QLabel(QStringLiteral("Distance:"),tdControls),4,3);tdGrid->addWidget(tdDistanceInfo,4,4,1,2);
    tdGrid->addWidget(new QLabel(QStringLiteral("Peaks:"),tdControls),5,0);tdGrid->addWidget(tdPeaks,5,1,1,5);tdGrid->addWidget(new QLabel(QStringLiteral("Status:"),tdControls),6,0);tdGrid->addWidget(tdStatus,6,1,1,5);
    tdRoot->addWidget(tdControls);
    auto *tdResponsePlot=new FieldProfilePlot(tdContent);tdResponsePlot->setMinimumHeight(180);
    auto *tdFrequencyPlot=new FieldProfilePlot(tdContent);tdFrequencyPlot->setMinimumHeight(160);tdFrequencyPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    tdRoot->addWidget(tdResponsePlot,1);tdRoot->addWidget(tdFrequencyPlot,1);
    analysisTabs->addTab(timePlot,QStringLiteral("Probe time"));
    analysisTabs->addTab(fftPlot,QStringLiteral("FFT"));
    analysisTabs->addTab(sPlot,QStringLiteral("S-parameters"));
    analysisTabs->addTab(rfPhasePlot,QStringLiteral("RF phase / delay"));
    analysisTabs->addTab(smithPage,QStringLiteral("Smith chart"));
    analysisTabs->addTab(timeDomainPage,QStringLiteral("Time domain / gating"));
    analysisTabs->addTab(farPlot,QStringLiteral("Far field"));
    analysisTabs->setTabBarAutoHide(false);
    analysisTabs->setMinimumHeight(240);
    // Ignore the current page's sizeHint vertically: FFT, Smith and time-domain pages
    // have very different preferred heights. The splitter geometry must not jump or
    // clip simply because the user changes measurement tab.
    analysisTabs->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);
    analysisPaneLayout->addWidget(analysisTabs,1);
    analysisPane->setMinimumHeight(240);
    analysisPane->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);
    viewAnalysisSplit->addWidget(analysisPane);
    viewAnalysisSplit->setStretchFactor(0,3);
    viewAnalysisSplit->setStretchFactor(1,2);
    // Initial splitter sizes are intentionally applied after the widget has a real
    // height (see stabilizeViewSplit below). Calling setSizes() here, while the
    // workspace is still being constructed, is the source of the intermittent
    // first-frame clipping that disappears after a manual resize.
    mainSplit->addWidget(controlScroll);mainSplit->addWidget(right);mainSplit->setStretchFactor(1,1);

    auto *timer=new QTimer(this);

    // The splitter is created before its final geometry is known. On Windows this
    // can leave QTabWidget one layout pass behind, so the tab bar/page appears
    // clipped until the user moves the splitter. Apply the initial ratio only after
    // Qt has completed layout/polish, and explicitly activate both child layouts.
    auto stabilizeViewSplit=[=]{
        if(viewAnalysisSplit->height()<=0)return;
        canvasPaneLayout->activate();
        analysisPaneLayout->activate();
        rightLayout->activate();
        const int handle=viewAnalysisSplit->handleWidth();
        const int available=std::max(0,viewAnalysisSplit->height()-handle);
        if(available<=0)return;
        const int minTop=canvasPane->minimumHeight();
        const int minBottom=analysisPane->minimumHeight();
        if(available>=minTop+minBottom){
            int bottom=std::clamp(int(std::lround(available*0.42)),minBottom,available-minTop);
            int top=available-bottom;
            viewAnalysisSplit->setSizes({top,bottom});
        }
        canvasPane->updateGeometry();
        analysisPane->updateGeometry();
        canvas->updateGeometry();
        analysisTabs->updateGeometry();
        if(QWidget *page=analysisTabs->currentWidget())page->updateGeometry();
        canvas->update();
        analysisTabs->update();
    };
    QTimer::singleShot(0,this,stabilizeViewSplit);
    // A second queued pass handles the style/font polish performed by the main
    // window just after construction. It runs only at startup and never fights the
    // user's later splitter position.
    QTimer::singleShot(40,this,stabilizeViewSplit);
    QObject::connect(analysisTabs,&QTabWidget::currentChanged,this,[=](int){
        QTimer::singleShot(0,this,[=]{
            analysisPaneLayout->activate();
            analysisTabs->updateGeometry();
            if(QWidget *page=analysisTabs->currentWidget()){page->updateGeometry();page->update();}
        });
    });

    auto syncingSource=std::make_shared<bool>(false), syncingProbe=std::make_shared<bool>(false), syncingRfMarker=std::make_shared<bool>(false);

    auto refreshMarkerCombos=[=]{
        *syncingRfMarker=true;
        const int keepActive=std::clamp(*activeRfMarker,0,std::max(0,int(rfMarkers->size())-1));
        const int keepDelta=std::max(0,deltaReference->currentIndex());
        markerSelect->clear();deltaReference->clear();
        for(const auto &m:*rfMarkers){markerSelect->addItem(m.name);deltaReference->addItem(m.name);}
        if(!rfMarkers->isEmpty()){
            *activeRfMarker=std::clamp(keepActive,0,int(rfMarkers->size())-1);
            markerSelect->setCurrentIndex(*activeRfMarker);
            deltaReference->setCurrentIndex(std::clamp(keepDelta,0,int(rfMarkers->size())-1));
        }
        *syncingRfMarker=false;
    };

    auto refreshFrozenList=[=]{
        frozenList->clear();
        for(const auto &sw:*frozenRfSweeps)frozenList->addItem(sw.title);
        if(frozenRfSweeps->empty())frozenList->addItem(QStringLiteral("No frozen sweep"));
    };

    auto clampMarkerFrequencies=[=]{
        if(rfSweep->frequencyHz.empty())return;
        const double f0=rfSweep->frequencyHz.front(),f1=rfSweep->frequencyHz.back();
        if(!*rfMarkersInitialized && rfMarkers->size()>=3){
            (*rfMarkers)[0].xValue=f0;
            (*rfMarkers)[1].xValue=rfSweep->frequencyHz[rfSweep->frequencyHz.size()/2];
            (*rfMarkers)[2].xValue=f1;
        }
        for(auto &m:*rfMarkers)m.xValue=std::clamp(m.xValue,std::min(f0,f1),std::max(f0,f1));
    };

    auto refreshMarkerReadout=[=]{
        if(rfMarkers->isEmpty()){markerReadout->setText(QStringLiteral("No marker"));markerDelta->setText(QStringLiteral("—"));return;}
        *activeRfMarker=std::clamp(*activeRfMarker,0,int(rfMarkers->size())-1);
        for(int i=0;i<int(rfMarkers->size());++i)(*rfMarkers)[i].active=(i==*activeRfMarker);
        const auto &m=(*rfMarkers)[*activeRfMarker];
        *syncingRfMarker=true;markerSelect->setCurrentIndex(*activeRfMarker);markerFreqGHz->setValue(m.xValue/1e9);markerEnabled->setChecked(m.enabled);*syncingRfMarker=false;
        sPlot->setMarkers(*rfMarkers);rfPhasePlot->setMarkers(*rfMarkers);smithChart->setMarkers(*rfMarkers);
        if(rfSweep->frequencyHz.empty()||rfSweep->s11.empty()||rfSweep->s21.empty()){
            markerReadout->setText(QStringLiteral("Run an S sweep first."));markerDelta->setText(QStringLiteral("—"));return;
        }
        const auto i=nearestFrequencyIndex(rfSweep->frequencyHz,m.xValue);
        if(i>=rfSweep->s11.size()||i>=rfSweep->s21.size())return;
        const auto g=rfSweep->s11[i],t=rfSweep->s21[i];const double gm=std::abs(g);
        std::complex<double> z{std::numeric_limits<double>::quiet_NaN(),0.0};
        if(std::abs(std::complex<double>(1.0,0.0)-g)>1e-12)z=referenceZ0->value()*(std::complex<double>(1.0,0.0)+g)/(std::complex<double>(1.0,0.0)-g);
        const double vswr=gm<1.0?(1.0+gm)/std::max(1e-15,1.0-gm):std::numeric_limits<double>::infinity();
        markerReadout->setText(QStringLiteral("%1 | S11=%2 dB ∠%3° | S21=%4 dB ∠%5° | VSWR=%6 | Zin=%7 Ω")
            .arg(eng(rfSweep->frequencyHz[i],QStringLiteral("Hz")))
            .arg(magnitudeDb(g),0,'g',6).arg(phaseDeg(g),0,'g',6)
            .arg(magnitudeDb(t),0,'g',6).arg(phaseDeg(t),0,'g',6)
            .arg(vswr,0,'g',6).arg(complexText(z)));
        if(deltaReference->currentIndex()>=0 && deltaReference->currentIndex()<int(rfMarkers->size()) && deltaReference->currentIndex()!=*activeRfMarker){
            const auto &r=(*rfMarkers)[deltaReference->currentIndex()];const auto j=nearestFrequencyIndex(rfSweep->frequencyHz,r.xValue);
            if(j<rfSweep->s11.size()&&j<rfSweep->s21.size()){
                auto wrap=[](double d){while(d>180.0)d-=360.0;while(d<=-180.0)d+=360.0;return d;};
                markerDelta->setText(QStringLiteral("Δ to %1: Δf=%2 | ΔS11=%3 dB | ΔS21=%4 dB | Δφ21=%5°")
                    .arg(r.name).arg(eng(rfSweep->frequencyHz[i]-rfSweep->frequencyHz[j],QStringLiteral("Hz")))
                    .arg(magnitudeDb(g)-magnitudeDb(rfSweep->s11[j]),0,'g',6)
                    .arg(magnitudeDb(t)-magnitudeDb(rfSweep->s21[j]),0,'g',6)
                    .arg(wrap(phaseDeg(t)-phaseDeg(rfSweep->s21[j])),0,'g',6));
            }
        }else markerDelta->setText(QStringLiteral("Select another marker as delta reference."));
    };

    auto refreshSweepPlot=[=]{
        if(rfSweep->frequencyHz.size()<2)return;
        QVector<FieldProfileSeries> series;
        std::vector<double> currentS11,currentS21;currentS11.reserve(rfSweep->s11.size());currentS21.reserve(rfSweep->s21.size());
        for(const auto &v:rfSweep->s11)currentS11.push_back(std::max(-160.0,magnitudeDb(v)));
        for(const auto &v:rfSweep->s21)currentS21.push_back(std::max(-160.0,magnitudeDb(v)));
        series.push_back({qvec(rfSweep->frequencyHz),qvec(currentS11),QStringLiteral("S11 current"),QStringLiteral("dB"),false});
        series.push_back({qvec(rfSweep->frequencyHz),qvec(currentS21),QStringLiteral("S21 current"),QStringLiteral("dB"),false});
        for(std::size_t si=0;si<frozenRfSweeps->size();++si){const auto &sw=(*frozenRfSweeps)[si];std::vector<double>a,b;for(const auto &v:sw.s11)a.push_back(std::max(-160.0,magnitudeDb(v)));for(const auto &v:sw.s21)b.push_back(std::max(-160.0,magnitudeDb(v)));const QString tag=QStringLiteral("F%1").arg(si+1);series.push_back({qvec(sw.frequencyHz),qvec(a),QStringLiteral("S11 %1").arg(tag),QStringLiteral("dB"),true});series.push_back({qvec(sw.frequencyHz),qvec(b),QStringLiteral("S21 %1").arg(tag),QStringLiteral("dB"),true});}
        if(*hasGatedSweep && gatedRfSweep->frequencyHz.size()==rfSweep->frequencyHz.size())
        {
            const auto &values=*gatedParameter==0?gatedRfSweep->s11:gatedRfSweep->s21;std::vector<double> gatedDb;gatedDb.reserve(values.size());
            for(const auto &v:values)gatedDb.push_back(std::max(-160.0,magnitudeDb(v)));
            series.push_back({qvec(gatedRfSweep->frequencyHz),qvec(gatedDb),*gatedParameter==0?QStringLiteral("S11 gated"):QStringLiteral("S21 gated"),QStringLiteral("dB"),true});
        }
        sPlot->setSeries(series,QStringLiteral("S-parameter comparison — %1").arg(rfSweep->title));sPlot->setMarkers(*rfMarkers);
        smithChart->setFrozenSweeps(*frozenRfSweeps);
        if(*hasGatedSweep && *gatedParameter==0)smithChart->setGatedSweep(gatedRfSweep->s11);else smithChart->clearGatedSweep();
    };

    auto refreshRfAnalysis=[=](bool showSmith){
        if(rfSweep->frequencyHz.size()<2){smithChart->clearSweep();rfResonanceLabel->setText(QStringLiteral("Run a broadband or modal sweep first."));refreshMarkerReadout();return;}
        const auto a=FDTDAnalysis::analyzeSParameterSweep(rfSweep->frequencyHz,rfSweep->s11,rfSweep->s21,referenceZ0->value());
        if(!a.valid){rfResonanceLabel->setText(QStringLiteral("Analysis failed: %1").arg(QString::fromStdString(a.error)));return;}
        clampMarkerFrequencies();
        if(rfMarkers->size()>=3 && !rfSweep->frequencyHz.empty() && !*rfMarkersInitialized){
            (*rfMarkers)[1].xValue=a.resonanceHz;
            *rfMarkersInitialized=true;
        }
        smithChart->setSweep(a.frequencyHz,a.s11);smithChart->setFrozenSweeps(*frozenRfSweeps);if(*hasGatedSweep&&*gatedParameter==0)smithChart->setGatedSweep(gatedRfSweep->s11);else smithChart->clearGatedSweep();smithSource->setText(rfSweep->title);
        const auto zin=a.resonanceInputImpedanceOhm;
        const QString ztxt=std::isfinite(zin.real())&&std::isfinite(zin.imag())?QStringLiteral("%1 %2 j%3 Ω").arg(zin.real(),0,'g',7).arg(zin.imag()<0?QStringLiteral("-"):QStringLiteral("+")).arg(std::abs(zin.imag()),0,'g',7):QStringLiteral("open / singular");
        rfResonanceLabel->setText(QStringLiteral("%1 | S11=%2 dB").arg(eng(a.resonanceHz,QStringLiteral("Hz"))).arg(a.resonanceS11Db,0,'g',6));
        rfImpedanceLabel->setText(ztxt);
        rfReturnLossLabel->setText(QStringLiteral("RL=%1 dB | VSWR=%2").arg(a.resonanceReturnLossDb,0,'g',6).arg(a.resonanceVswr,0,'g',6));
        rfBwLabel->setText(a.has3dBBandwidth?QStringLiteral("%1 … %2 | BW=%3 | Q=%4").arg(eng(a.lower3dBHz,QStringLiteral("Hz")),eng(a.upper3dBHz,QStringLiteral("Hz")),eng(a.bandwidth3dBHz,QStringLiteral("Hz"))).arg(a.loadedQ,0,'g',6):QStringLiteral("Not bounded inside sweep"));
        rfVswrBandLabel->setText(a.hasVswr2Bandwidth?QStringLiteral("%1 … %2 | BW=%3").arg(eng(a.lowerVswr2Hz,QStringLiteral("Hz")),eng(a.upperVswr2Hz,QStringLiteral("Hz")),eng(a.bandwidthVswr2Hz,QStringLiteral("Hz"))):QStringLiteral("No VSWR≤2 interval around best match"));
        rfDelayLabel->setText(eng(a.groupDelayS[a.transmissionPeakIndex],QStringLiteral("s")));
        QStringList candidates;for(auto i:a.resonanceCandidates)candidates<<QStringLiteral("%1 (%2 dB)").arg(eng(a.frequencyHz[i],QStringLiteral("Hz"))).arg(a.s11Db[i],0,'g',5);rfCandidatesLabel->setText(candidates.join(QStringLiteral("; ")));
        smithBest->setText(rfResonanceLabel->text());smithZ->setText(ztxt);smithPeak->setText(QStringLiteral("%1 @ %2").arg(a.transmissionPeakDb,0,'g',5).arg(eng(a.transmissionPeakHz,QStringLiteral("Hz"))));smithBandwidth->setText(rfBwLabel->text());
        rfPhasePlot->setSeries({FieldProfileSeries{qvec(a.frequencyHz),qvec(a.s11PhaseDeg),QStringLiteral("phase S11"),QStringLiteral("deg"),false},FieldProfileSeries{qvec(a.frequencyHz),qvec(a.s21PhaseDeg),QStringLiteral("phase S21"),QStringLiteral("deg"),false},FieldProfileSeries{qvec(a.frequencyHz),qvec(a.groupDelayS),QStringLiteral("S21 group delay"),QStringLiteral("s"),false}},QStringLiteral("Unwrapped phase and τg = -dφ21/dω — %1").arg(rfSweep->title));
        refreshSweepPlot();refreshMarkerReadout();
        if(showSmith)analysisTabs->setCurrentWidget(smithPage);
    };
    auto tdPropagationSpeed=[=]{
        return tdPropagation->currentIndex()==0
            ? VnaTimeDomain::propagationSpeedFromVelocityFactor(tdVelocityFactor->value())
            : VnaTimeDomain::propagationSpeedFromEffectiveMedium(tdEpsEff->value(),tdMuEff->value());
    };
    auto tdGateSpec=[=]{
        VnaTimeDomain::Gate gate;
        gate.mode=tdGateMode->currentIndex()==0?VnaTimeDomain::GateMode::KeepInside:VnaTimeDomain::GateMode::RejectInside;
        gate.edge=tdGateEdge->currentIndex()==0?VnaTimeDomain::GateEdge::RaisedCosine:VnaTimeDomain::GateEdge::Rectangular;
        gate.startS=tdGateStartNs->value()*1e-9;gate.stopS=tdGateStopNs->value()*1e-9;gate.transitionS=tdTransitionNs->value()*1e-9;
        return gate;
    };
    auto renderTimeDomain=[=]{
        if(!timeDomainResult->valid||timeDomainResult->timeResponse.empty()){
            tdResponsePlot->clearData();tdFrequencyPlot->clearData();tdSampling->setText(QStringLiteral("—"));tdDistanceInfo->setText(QStringLiteral("—"));tdPeaks->setText(QStringLiteral("Run a uniform S-parameter sweep first."));tdExportGatedButton->setEnabled(false);return;
        }
        const bool reflection=tdParameter->currentIndex()==0;const double vp=tdPropagationSpeed();const bool distanceAxis=tdAxis->currentIndex()==1;
        if(!(vp>0.0)||!std::isfinite(vp)){tdStatus->setText(QStringLiteral("Invalid propagation velocity."));return;}
        const auto &tr=*timeDomainResult;const auto gate=tdGateSpec();const auto gatedTime=VnaTimeDomain::applyGate(tr.timeS,tr.timeResponse,gate);
        double refMag=0.0;for(const auto &v:tr.timeResponse)refMag=std::max(refMag,std::abs(v));refMag=std::max(1e-15,refMag);
        std::vector<double>x,originalDb,previewDb;x.reserve(tr.timeS.size());originalDb.reserve(tr.timeS.size());previewDb.reserve(tr.timeS.size());
        for(std::size_t i=0;i<tr.timeS.size();++i){x.push_back(distanceAxis?VnaTimeDomain::distanceFromDelay(tr.timeS[i],vp,reflection):tr.timeS[i]*1e9);originalDb.push_back(20.0*std::log10(std::max(1e-15,std::abs(tr.timeResponse[i])/refMag)));previewDb.push_back(20.0*std::log10(std::max(1e-15,std::abs(gatedTime[i])/refMag)));}
        tdResponsePlot->setXAxis(distanceAxis?QStringLiteral("Distance from reference plane"):QStringLiteral("Delay"),distanceAxis?QStringLiteral("m"):QStringLiteral("ns"));
        tdResponsePlot->setSeries({FieldProfileSeries{qvec(x),qvec(originalDb),QStringLiteral("Original time response"),QStringLiteral("dB"),false},FieldProfileSeries{qvec(x),qvec(previewDb),QStringLiteral("Gate preview"),QStringLiteral("dB"),true}},QStringLiteral("%1 time-domain response — normalized magnitude").arg(reflection?QStringLiteral("S11"):QStringLiteral("S21")));
        auto xFromTime=[=](double t){return distanceAxis?VnaTimeDomain::distanceFromDelay(t,vp,reflection):t*1e9;};
        QVector<FieldPlotMarker> tdMarkers;tdMarkers.push_back({QStringLiteral("G1"),xFromTime(gate.startS),true,true,true});tdMarkers.push_back({QStringLiteral("G2"),xFromTime(gate.stopS),true,true,true});
        const auto peaks=VnaTimeDomain::findPeaks(tr,vp,reflection,3,2);QStringList peakText;int pn=1;for(const auto &peak:peaks){tdMarkers.push_back({QStringLiteral("P%1").arg(pn),xFromTime(peak.timeS),true,false,false});peakText<<QStringLiteral("P%1: %2 ns | %3 m | %4 dB").arg(pn++).arg(peak.timeS*1e9,0,'g',6).arg(peak.distanceM,0,'g',6).arg(peak.magnitudeDb,0,'g',5);}tdResponsePlot->setMarkers(tdMarkers);tdPeaks->setText(peakText.isEmpty()?QStringLiteral("No local peak detected."):peakText.join(QStringLiteral(" ; ")));
        tdSampling->setText(QStringLiteral("Δf=%1 | Δt=%2 | Tnon-amb=%3").arg(eng(tr.frequencyStepHz,QStringLiteral("Hz")),eng(tr.timeStepS,QStringLiteral("s")),eng(tr.unambiguousTimeS,QStringLiteral("s"))));
        tdDistanceInfo->setText(QStringLiteral("vp=%1 | Δd≈%2 | dmax≈%3").arg(eng(vp,QStringLiteral("m/s")),eng(VnaTimeDomain::nominalDistanceResolution(tr.frequencySpanHz,vp,reflection),QStringLiteral("m")),eng(VnaTimeDomain::unambiguousDistance(tr.frequencyStepHz,vp,reflection),QStringLiteral("m"))));
        const auto &original=reflection?rfSweep->s11:rfSweep->s21;std::vector<double>origDb;origDb.reserve(original.size());for(const auto &v:original)origDb.push_back(std::max(-180.0,magnitudeDb(v)));QVector<FieldProfileSeries> freqSeries;freqSeries.push_back({qvec(rfSweep->frequencyHz),qvec(origDb),reflection?QStringLiteral("Original S11"):QStringLiteral("Original S21"),QStringLiteral("dB"),false});
        if(*hasGatedSweep&&*gatedParameter==tdParameter->currentIndex()){const auto &gv=reflection?gatedRfSweep->s11:gatedRfSweep->s21;std::vector<double>gdb;gdb.reserve(gv.size());for(const auto &v:gv)gdb.push_back(std::max(-180.0,magnitudeDb(v)));freqSeries.push_back({qvec(gatedRfSweep->frequencyHz),qvec(gdb),reflection?QStringLiteral("Gated S11"):QStringLiteral("Gated S21"),QStringLiteral("dB"),true});}
        tdFrequencyPlot->setSeries(freqSeries,QStringLiteral("Original vs gated frequency response"));tdExportGatedButton->setEnabled(*hasGatedSweep&&*gatedParameter==0);
    };
    auto refreshTimeDomain=[=](bool resetGate,bool invalidateGateResult){
        if(invalidateGateResult){*hasGatedSweep=false;gatedRfSweep->frequencyHz.clear();gatedRfSweep->s11.clear();gatedRfSweep->s21.clear();smithChart->clearGatedSweep();if(rfSweep->frequencyHz.size()>=2)refreshSweepPlot();}
        if(rfSweep->frequencyHz.size()<2){*timeDomainResult={};tdStatus->setText(QStringLiteral("Run a broadband or modal S sweep first."));renderTimeDomain();return;}
        const auto &samples=tdParameter->currentIndex()==0?rfSweep->s11:rfSweep->s21;
        *timeDomainResult=VnaTimeDomain::toTimeDomain(rfSweep->frequencyHz,samples,static_cast<VnaTimeDomain::Window>(std::clamp(tdWindow->currentIndex(),0,3)));
        if(!timeDomainResult->valid){tdStatus->setText(QStringLiteral("Time transform unavailable: %1").arg(QString::fromStdString(timeDomainResult->error)));renderTimeDomain();return;}
        const double maxNs=timeDomainResult->timeS.empty()?0.0:timeDomainResult->timeS.back()*1e9;tdGateStartNs->setRange(0.0,std::max(0.0,maxNs));tdGateStopNs->setRange(0.0,std::max(0.0,maxNs));tdTransitionNs->setRange(0.0,std::max(0.0,maxNs));
        if(resetGate||tdGateStopNs->value()<=tdGateStartNs->value()||tdGateStopNs->value()>maxNs){std::size_t peak=0;double best=-1.0;for(std::size_t i=0;i<timeDomainResult->timeResponse.size();++i){const double m=std::abs(timeDomainResult->timeResponse[i]);if(m>best){best=m;peak=i;}}const double center=timeDomainResult->timeS[peak]*1e9;const double half=std::max(3.0*timeDomainResult->timeStepS*1e9,0.02*timeDomainResult->unambiguousTimeS*1e9);tdGateStartNs->setValue(std::max(0.0,center-half));tdGateStopNs->setValue(std::min(maxNs,center+half));tdTransitionNs->setValue(std::min(2.0*timeDomainResult->timeStepS*1e9,half));}
        tdStatus->setText(QStringLiteral("Complex inverse DFT ready. Windowing trades time resolution for sidelobe rejection; gate preview is applied in the time domain before transforming back to frequency."));renderTimeDomain();
    };
    auto applyTimeGate=[=]{
        if(!timeDomainResult->valid){refreshTimeDomain(false,true);if(!timeDomainResult->valid)return;}
        const auto gatedTime=VnaTimeDomain::applyGate(timeDomainResult->timeS,timeDomainResult->timeResponse,tdGateSpec());const auto gatedFrequency=VnaTimeDomain::toFrequencyDomain(gatedTime);
        if(gatedFrequency.size()!=rfSweep->frequencyHz.size()){tdStatus->setText(QStringLiteral("Gate transform failed: unexpected transform size."));return;}
        *gatedRfSweep=*rfSweep;gatedRfSweep->title=QStringLiteral("Time gated — %1").arg(rfSweep->title);*gatedParameter=tdParameter->currentIndex();if(*gatedParameter==0)gatedRfSweep->s11=gatedFrequency;else gatedRfSweep->s21=gatedFrequency;*hasGatedSweep=true;
        refreshSweepPlot();if(*gatedParameter==0)smithChart->setGatedSweep(gatedRfSweep->s11);else smithChart->clearGatedSweep();renderTimeDomain();tdStatus->setText(QStringLiteral("Gate applied and transformed back to frequency. The gated trace is now available in S-parameters%1.").arg(*gatedParameter==0?QStringLiteral(", Smith chart and .s1p export"):QString()));
    };
    auto setRfSweep=[=](const QString &title,const std::vector<double> &frequencyHz,const std::vector<std::complex<double>> &s11,const std::vector<std::complex<double>> &s21,bool showSmith){rfSweep->title=title;rfSweep->frequencyHz=frequencyHz;rfSweep->s11=s11;rfSweep->s21=s21;*hasGatedSweep=false;gatedRfSweep->frequencyHz.clear();smithChart->clearGatedSweep();clampMarkerFrequencies();refreshRfAnalysis(showSmith);refreshTimeDomain(true,false);};
    auto moveActiveMarkerToFrequency=[=](double frequencyHz){if(rfMarkers->isEmpty()||rfSweep->frequencyHz.empty())return;*activeRfMarker=std::clamp(*activeRfMarker,0,int(rfMarkers->size())-1);const auto idx=nearestFrequencyIndex(rfSweep->frequencyHz,frequencyHz);(*rfMarkers)[*activeRfMarker].xValue=rfSweep->frequencyHz[idx];refreshMarkerReadout();};

    auto refreshPortCombos=[=]{const int ai=inputPort->currentIndex(),ao=outputPort->currentIndex();inputPort->clear();outputPort->clear();for(const auto &p:*probes){inputPort->addItem(p.name);outputPort->addItem(p.name);}if(!probes->empty()){inputPort->setCurrentIndex(std::clamp(ai,0,int(probes->size())-1));outputPort->setCurrentIndex(std::clamp(ao<0?1:ao,0,int(probes->size())-1));}};
    auto refreshSourceTable=[=]{*syncingSource=true;sourceTable->setRowCount(int(solver->sourceCount()));for(int r=0;r<int(solver->sourceCount());++r){const auto s=solver->source(std::size_t(r));const QStringList vals={QString::fromStdString(s.name),QString::number(s.x),QString::number(s.y),QString::number(s.frequencyHz/1e9,'g',6),s.enabled?QStringLiteral("yes"):QStringLiteral("no")};for(int c=0;c<vals.size();++c){auto *item=new QTableWidgetItem(vals[c]);item->setFlags(item->flags()&~Qt::ItemIsEditable);sourceTable->setItem(r,c,item);}}*syncingSource=false;if(sourceTable->rowCount()>0&&sourceTable->currentRow()<0)sourceTable->selectRow(0);};
    auto refreshProbeTable=[=]{*syncingProbe=true;probeTable->setRowCount(int(probes->size()));for(int r=0;r<int(probes->size());++r){const auto &p=(*probes)[std::size_t(r)];const QStringList vals={p.name,QString::number(p.x),QString::number(p.y),p.enabled?QStringLiteral("yes"):QStringLiteral("no")};for(int c=0;c<vals.size();++c){auto *item=new QTableWidgetItem(vals[c]);item->setFlags(item->flags()&~Qt::ItemIsEditable);probeTable->setItem(r,c,item);}}*syncingProbe=false;if(probeTable->rowCount()>0&&probeTable->currentRow()<0)probeTable->selectRow(0);refreshPortCombos();canvas->update();};

    auto loadSelectedSource=[=]{const int r=std::clamp(sourceTable->currentRow(),0,std::max(0,int(solver->sourceCount())-1));if(solver->sourceCount()==0)return;*syncingSource=true;const auto s=solver->source(std::size_t(r));sourceName->setText(QString::fromStdString(s.name));waveform->setCurrentIndex(int(s.waveform));injection->setCurrentIndex(int(s.injection));frequencyGHz->setValue(s.frequencyHz/1e9);amplitude->setValue(s.amplitude);sourcePhaseDeg->setValue(s.phaseDeg);sourceX->setValue(s.x);sourceY->setValue(s.y);sourceEnabled->setChecked(s.enabled);canvas->setActiveSource(std::size_t(r));*syncingSource=false;};
    auto saveSelectedSource=[=]{if(*syncingSource||solver->sourceCount()==0)return;const int r=std::clamp(sourceTable->currentRow(),0,int(solver->sourceCount())-1);auto s=solver->source(std::size_t(r));s.name=sourceName->text().trimmed().isEmpty()?QStringLiteral("S%1").arg(r+1).toStdString():sourceName->text().toStdString();s.waveform=static_cast<FDTD::SourceWaveform>(std::clamp(waveform->currentIndex(),0,2));s.injection=static_cast<FDTD::SourceInjection>(std::clamp(injection->currentIndex(),0,1));s.frequencyHz=frequencyGHz->value()*1e9;s.amplitude=amplitude->value();s.phaseDeg=sourcePhaseDeg->value();s.x=sourceX->value();s.y=sourceY->value();s.enabled=sourceEnabled->isChecked();solver->setSource(std::size_t(r),s);refreshSourceTable();sourceTable->selectRow(r);canvas->setActiveSource(std::size_t(r));canvas->update();};
    auto loadSelectedProbe=[=]{if(probes->empty())return;const int r=std::clamp(probeTable->currentRow(),0,int(probes->size())-1);*syncingProbe=true;const auto &p=(*probes)[std::size_t(r)];probeName->setText(p.name);probeX->setValue(p.x);probeY->setValue(p.y);probeEnabled->setChecked(p.enabled);canvas->setActiveProbe(std::size_t(r));*syncingProbe=false;};
    auto saveSelectedProbe=[=]{if(*syncingProbe||probes->empty())return;const int r=std::clamp(probeTable->currentRow(),0,int(probes->size())-1);auto &p=(*probes)[std::size_t(r)];p.name=probeName->text().trimmed().isEmpty()?QStringLiteral("P%1").arg(r+1):probeName->text();p.x=std::clamp(probeX->value(),0,solver->nx()-1);p.y=std::clamp(probeY->value(),0,solver->ny()-1);p.enabled=probeEnabled->isChecked();refreshProbeTable();probeTable->selectRow(r);canvas->setActiveProbe(std::size_t(r));canvas->update();};

    auto clearHistories=[=]{for(auto &h:*histories){h.time.clear();h.scalar.clear();h.companion.clear();h.eTangential.clear();h.hTangential.clear();}timePlot->clearData();fftPlot->clearData();sPlot->clearData();farPlot->clearData();s11Label->setText(QStringLiteral("—"));s21Label->setText(QStringLiteral("—"));vswrLabel->setText(QStringLiteral("—"));farDLabel->setText(QStringLiteral("—"));};
    auto resetAccumulator=[=]{accumulator->configure(static_cast<std::size_t>(solver->nx()*solver->ny()),analysisFreqGHz->value()*1e9,solver->dtS());};
    auto resetAll=[=]{solver->resetFields();clearHistories();resetAccumulator();canvas->update();};
    auto appendHistories=[=]{if(histories->size()!=probes->size())histories->resize(probes->size());for(std::size_t i=0;i<probes->size();++i){const auto &p=(*probes)[i];if(!p.enabled)continue;auto &h=(*histories)[i];const auto s=solver->sample(std::clamp(p.x,0,solver->nx()-1),std::clamp(p.y,0,solver->ny()-1));h.time.push_back(solver->timeS());if(solver->polarization()==FDTD::Polarization::TMz){h.scalar.push_back(s.ezVPerM);h.companion.push_back(s.magneticMagnitudeAPerM);h.eTangential.push_back(s.ezVPerM);h.hTangential.push_back(-s.hyAPerM);}else{h.scalar.push_back(s.hzAPerM);h.companion.push_back(s.electricMagnitudeVPerM);h.eTangential.push_back(s.eyVPerM);h.hTangential.push_back(s.hzAPerM);}constexpr std::size_t maxN=8192;if(h.time.size()>maxN){const std::size_t n=h.time.size()-maxN;h.time.erase(h.time.begin(),h.time.begin()+static_cast<std::ptrdiff_t>(n));h.scalar.erase(h.scalar.begin(),h.scalar.begin()+static_cast<std::ptrdiff_t>(n));h.companion.erase(h.companion.begin(),h.companion.begin()+static_cast<std::ptrdiff_t>(n));h.eTangential.erase(h.eTangential.begin(),h.eTangential.begin()+static_cast<std::ptrdiff_t>(n));h.hTangential.erase(h.hTangential.begin(),h.hTangential.begin()+static_cast<std::ptrdiff_t>(n));}}const double f=analysisFreqGHz->value()*1e9;if(accumulateFar->isChecked()&&solver->timeS()>5.0/std::max(1.0,f))accumulator->add(solver->primaryScalarField(),solver->timeS());};

    auto updateTimePlot=[=]{if(probes->empty()||histories->empty())return;const int r=std::clamp(probeTable->currentRow(),0,int(probes->size())-1);const auto &h=(*histories)[std::size_t(r)];if(h.time.size()<2)return;const bool tm=solver->polarization()==FDTD::Polarization::TMz;QVector<FieldProfileSeries> series;series.push_back({qvec(h.time),qvec(h.scalar),tm?QStringLiteral("Ez"):QStringLiteral("Hz"),tm?QStringLiteral("V/m"):QStringLiteral("A/m"),false});series.push_back({qvec(h.time),qvec(h.companion),tm?QStringLiteral("|H|"):QStringLiteral("|E|"),tm?QStringLiteral("A/m"):QStringLiteral("V/m"),false});timePlot->setSeries(series,QStringLiteral("%1 time history").arg((*probes)[std::size_t(r)].name));};
    auto updateStats=[=]{dtLabel->setText(eng(solver->dtS(),QStringLiteral("s")));timeLabel->setText(eng(solver->timeS(),QStringLiteral("s")));lambdaLabel->setText(eng(solver->wavelengthInVacuumM(),QStringLiteral("m")));cellsLambdaLabel->setText(QString::number(solver->cellsPerVacuumWavelength(),'f',1));cellsMinLambdaLabel->setText(QString::number(solver->cellsPerMinimumWavelength(),'f',1));maxFieldLabel->setText(eng(solver->maxAbsPrimaryField(),solver->polarization()==FDTD::Polarization::TMz?QStringLiteral("V/m"):QStringLiteral("A/m")));energyLabel->setText(eng(solver->totalEnergyPerMeterJ(),QStringLiteral("J/m")));if(!probes->empty()){const int r=std::clamp(probeTable->currentRow(),0,int(probes->size())-1);const auto &p=(*probes)[std::size_t(r)];const auto s=solver->sample(p.x,p.y);probeFieldLabel->setText(solver->polarization()==FDTD::Polarization::TMz?QStringLiteral("Ez %1 | |H| %2").arg(eng(s.ezVPerM,QStringLiteral("V/m")),eng(s.magneticMagnitudeAPerM,QStringLiteral("A/m"))):QStringLiteral("Hz %1 | |E| %2").arg(eng(s.hzAPerM,QStringLiteral("A/m")),eng(s.electricMagnitudeVPerM,QStringLiteral("V/m"))));}const double cpw=solver->cellsPerMinimumWavelength();QString status=cpw>=20?QStringLiteral("Good spatial sampling (≥20 cells/shortest λ)"):cpw>=10?QStringLiteral("Usable sampling; numerical dispersion visible"):QStringLiteral("WARNING: <10 cells/shortest λ");if(solver->boundaryCondition()==FDTD::BoundaryCondition::Cpml)status+=QStringLiteral(" | CPML %1 cells").arg(solver->cpmlCells());statusLabel->setText(status);canvas->update();};


    auto guideBoundaryMode=[=]{return solver->polarization()==FDTD::Polarization::TMz
        ? FDTDAnalysis::GuideScalarBoundary::DirichletSine
        : FDTDAnalysis::GuideScalarBoundary::NeumannCosine;};
    auto guideHeight=[=]{return double(std::max(1,guideYEnd->value()-guideYStart->value()+2))*solver->dyM();};
    auto refreshGuideInfo=[=](double frequencyHz){
        const auto info=FDTDAnalysis::guideModeInfo(frequencyHz,guideHeight(),guideMode->value(),guideBoundaryMode(),guideEpsR->value(),guideMuR->value());
        guideCutoffLabel->setText(eng(info.cutoffHz,QStringLiteral("Hz")));
        if(info.propagating){guideBetaLabel->setText(QStringLiteral("β=%1 rad/m | λg=%2").arg(info.betaRadPerM,0,'g',6).arg(eng(info.guideWavelengthM,QStringLiteral("m"))));guideNote->setText(QStringLiteral("Uniform 2D PEC parallel-plate modal projection. TMz uses sine modes m≥1; TEz uses cosine modes m≥0."));}
        else{guideBetaLabel->setText(QStringLiteral("below cutoff"));guideNote->setText(QString::fromStdString(info.error));}
    };
    auto applyGuideSource=[=](double frequencyHz){
        FDTD::GuideModeSource gs=solver->guideModeSource();
        gs.enabled=guideEnableSource->isChecked();gs.x=guideSourceX->value();gs.yStart=guideYStart->value();gs.yEnd=guideYEnd->value();gs.modeIndex=guideMode->value();gs.frequencyHz=frequencyHz;gs.amplitude=guideAmplitude->value();gs.waveform=FDTD::SourceWaveform::ContinuousSine;gs.injection=FDTD::SourceInjection::Soft;solver->setGuideModeSource(gs);
    };
    auto modalEstimateFromPhasor=[=](const std::vector<std::complex<double>> &phasor,double frequencyHz){
        return FDTDAnalysis::estimateParallelPlateGuideModeSParameters(phasor,solver->nx(),solver->ny(),solver->dxM(),solver->dyM(),frequencyHz,guideInputX->value(),guideOutputX->value(),guideYStart->value(),guideYEnd->value(),guideMode->value(),guideBoundaryMode(),guideSep->value(),guideEpsR->value(),guideMuR->value());
    };
    auto showModalResult=[=](const FDTDAnalysis::GuideModalSParameterEstimate &r){
        if(!r.valid){guideS11Label->setText(QStringLiteral("—"));guideS21Label->setText(QStringLiteral("—"));guidePurityLabel->setText(QStringLiteral("—"));guideNote->setText(QStringLiteral("Modal solve failed: %1").arg(QString::fromStdString(r.error)));return;}
        guideS11Label->setText(QStringLiteral("%1 | %2 dB").arg(complexText(r.s11)).arg(r.s11Db,0,'g',5));
        guideS21Label->setText(QStringLiteral("%1 | %2 dB").arg(complexText(r.s21)).arg(r.s21Db,0,'g',5));
        guidePurityLabel->setText(QStringLiteral("input %1% | output %2%").arg(100.0*r.inputModePurity,0,'f',1).arg(100.0*r.outputModePurity,0,'f',1));
        guideCutoffLabel->setText(eng(r.mode.cutoffHz,QStringLiteral("Hz")));
        guideBetaLabel->setText(QStringLiteral("β=%1 rad/m | λg=%2").arg(r.mode.betaRadPerM,0,'g',6).arg(eng(r.mode.guideWavelengthM,QStringLiteral("m"))));
        QString quality=(r.inputModePurity>0.8&&r.outputModePurity>0.8)?QStringLiteral("good selected-mode purity"):QStringLiteral("WARNING: strong higher-mode/evanescent content at one or both ports");
        guideNote->setText(QStringLiteral("Transverse modal projection + two-plane forward/backward decomposition; %1. Same aperture/material is assumed at both ports.").arg(quality));
    };

    auto runModalFrequency=[=](double frequencyHz){
        FDTDAnalysis::GuideModalSParameterEstimate failed;
        const auto info=FDTDAnalysis::guideModeInfo(frequencyHz,guideHeight(),guideMode->value(),guideBoundaryMode(),guideEpsR->value(),guideMuR->value());
        if(!info.propagating){failed.error=info.error;failed.mode=info;return failed;}
        const double stepsExact=1.0/(frequencyHz*solver->dtS());
        const int stepsPerPeriod=std::max(4,int(std::ceil(stepsExact)));
        const long long totalSteps=1LL*stepsPerPeriod*(settlePeriods->value()+samplePeriods->value());
        if(totalSteps>750000){failed.error="Requested frequency needs too many FDTD steps for the interactive sweep; increase frequency/coarsen the grid or reduce periods.";failed.mode=info;return failed;}

        std::vector<FDTD::Source> savedSources=solver->sources();
        const FDTD::GuideModeSource savedGuide=solver->guideModeSource();
        for(std::size_t i=0;i<solver->sourceCount();++i){auto src=solver->source(i);src.enabled=false;solver->setSource(i,src);}
        FDTD::GuideModeSource gs=savedGuide;gs.enabled=true;gs.x=guideSourceX->value();gs.yStart=guideYStart->value();gs.yEnd=guideYEnd->value();gs.modeIndex=guideMode->value();gs.frequencyHz=frequencyHz;gs.amplitude=guideAmplitude->value();gs.waveform=FDTD::SourceWaveform::ContinuousSine;gs.injection=FDTD::SourceInjection::Soft;solver->setGuideModeSource(gs);
        solver->resetFields();
        solver->step(stepsPerPeriod*settlePeriods->value());
        FDTDAnalysis::HarmonicFieldAccumulator local;
        local.configure(static_cast<std::size_t>(solver->nx()*solver->ny()),frequencyHz,solver->dtS());
        const int sampleSteps=stepsPerPeriod*samplePeriods->value();
        for(int i=0;i<sampleSteps;++i){solver->step(1);local.add(solver->primaryScalarField(),solver->timeS());}
        auto result=modalEstimateFromPhasor(local.phasorField(),frequencyHz);
        for(std::size_t i=0;i<savedSources.size()&&i<solver->sourceCount();++i)solver->setSource(i,savedSources[i]);
        solver->setGuideModeSource(savedGuide);
        canvas->update();updateStats();
        return result;
    };

    QObject::connect(timer,&QTimer::timeout,this,[=]{for(int i=0;i<stepsPerFrame->value();++i){solver->step(1);appendHistories();}updateTimePlot();updateStats();});
    QObject::connect(runButton,&QPushButton::clicked,this,[=]{timer->start(std::max(1,1000/fps->value()));});QObject::connect(pauseButton,&QPushButton::clicked,timer,&QTimer::stop);QObject::connect(stepButton,&QPushButton::clicked,this,[=]{timer->stop();solver->step(1);appendHistories();updateTimePlot();updateStats();});QObject::connect(resetButton,&QPushButton::clicked,this,[=]{timer->stop();resetAll();updateStats();});QObject::connect(snapshotButton,&QPushButton::clicked,this,[=]{const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("Save FDTD snapshot"),QStringLiteral("fdtd_snapshot.png"),QStringLiteral("PNG image (*.png)"));if(!path.isEmpty())canvas->grab().save(path,"PNG");});QObject::connect(fps,qOverload<int>(&QSpinBox::valueChanged),this,[=](int v){if(timer->isActive())timer->start(std::max(1,1000/v));});

    QObject::connect(sourceTable,&QTableWidget::itemSelectionChanged,this,loadSelectedSource);
    QObject::connect(sourceName,&QLineEdit::editingFinished,this,saveSelectedSource);QObject::connect(waveform,qOverload<int>(&QComboBox::currentIndexChanged),this,[=]{saveSelectedSource();});QObject::connect(injection,qOverload<int>(&QComboBox::currentIndexChanged),this,[=]{saveSelectedSource();});QObject::connect(frequencyGHz,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=]{saveSelectedSource();});QObject::connect(amplitude,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=]{saveSelectedSource();});QObject::connect(sourcePhaseDeg,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=]{saveSelectedSource();});QObject::connect(sourceX,qOverload<int>(&QSpinBox::valueChanged),this,[=]{saveSelectedSource();});QObject::connect(sourceY,qOverload<int>(&QSpinBox::valueChanged),this,[=]{saveSelectedSource();});QObject::connect(sourceEnabled,&QCheckBox::toggled,this,[=]{saveSelectedSource();});
    QObject::connect(addSource,&QPushButton::clicked,this,[=]{FDTD::Source s;s.name=QStringLiteral("S%1").arg(solver->sourceCount()+1).toStdString();s.x=std::max(2,solver->nx()/6+int(solver->sourceCount())*4);s.y=solver->ny()/2;s.frequencyHz=analysisFreqGHz->value()*1e9;const int row=int(solver->addSource(s));refreshSourceTable();sourceTable->selectRow(row);resetAll();});QObject::connect(removeSource,&QPushButton::clicked,this,[=]{if(solver->sourceCount()<=1){QMessageBox::information(this,QStringLiteral("FDTD sources"),QStringLiteral("Keep at least one source; disable it if you want a source-free field decay."));return;}const int r=std::clamp(sourceTable->currentRow(),0,int(solver->sourceCount())-1);solver->removeSource(std::size_t(r));refreshSourceTable();sourceTable->selectRow(std::min(r,sourceTable->rowCount()-1));resetAll();});

    QObject::connect(probeTable,&QTableWidget::itemSelectionChanged,this,[=]{loadSelectedProbe();updateTimePlot();updateStats();});QObject::connect(probeName,&QLineEdit::editingFinished,this,saveSelectedProbe);QObject::connect(probeX,qOverload<int>(&QSpinBox::valueChanged),this,[=]{saveSelectedProbe();});QObject::connect(probeY,qOverload<int>(&QSpinBox::valueChanged),this,[=]{saveSelectedProbe();});QObject::connect(probeEnabled,&QCheckBox::toggled,this,[=]{saveSelectedProbe();});QObject::connect(addProbe,&QPushButton::clicked,this,[=]{const int n=int(probes->size())+1;probes->push_back({QStringLiteral("P%1").arg(n),std::clamp(2*solver->nx()/3+(n-2)*5,0,solver->nx()-1),solver->ny()/2,true});histories->resize(probes->size());refreshProbeTable();probeTable->selectRow(int(probes->size())-1);canvas->update();});QObject::connect(removeProbe,&QPushButton::clicked,this,[=]{if(probes->size()<=1)return;const int r=std::clamp(probeTable->currentRow(),0,int(probes->size())-1);probes->erase(probes->begin()+r);histories->erase(histories->begin()+r);refreshProbeTable();probeTable->selectRow(std::min(r,probeTable->rowCount()-1));});

    QObject::connect(display,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int i){canvas->setDisplay(static_cast<FdtdCanvas::Display>(std::clamp(i,0,9)));});QObject::connect(tool,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int i){canvas->setTool(static_cast<FdtdCanvas::Tool>(std::clamp(i,0,2)));});QObject::connect(brushRadius,qOverload<int>(&QSpinBox::valueChanged),canvas,&FdtdCanvas::setBrushRadius);QObject::connect(showMaterials,&QCheckBox::toggled,canvas,&FdtdCanvas::setShowMaterials);
    canvas->sourceMoved=[=](std::size_t i,int x,int y){if(i>=solver->sourceCount())return;auto s=solver->source(i);s.x=x;s.y=y;solver->setSource(i,s);refreshSourceTable();sourceTable->selectRow(int(i));loadSelectedSource();canvas->update();};canvas->probeMoved=[=](std::size_t i,int x,int y){if(i>=probes->size())return;(*probes)[i].x=x;(*probes)[i].y=y;refreshProbeTable();probeTable->selectRow(int(i));loadSelectedProbe();};canvas->paintRequested=[=](int cx,int cy,bool erase,int radius){timer->stop();FDTD::Material m;if(!erase){const int type=material->currentIndex();if(type==1){m.epsilonR=epsR->value();m.muR=muR->value();}else if(type==2){m.epsilonR=epsR->value();m.muR=muR->value();m.conductivitySPerM=sigma->value();}else if(type==3)m.pec=true;}for(int y=cy-radius;y<=cy+radius;++y)for(int x=cx-radius;x<=cx+radius;++x)if((x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius)solver->setMaterialCell(x,y,m);resetAll();updateStats();};

    auto applyPresetGeometry=[=]{timer->stop();solver->clearMaterials();const int nx=solver->nx(),ny=solver->ny(),edge=solver->boundaryCondition()==FDTD::BoundaryCondition::Cpml?solver->cpmlCells()+3:4;const FDTD::Material pec{1,1,0,true};FDTD::Material diel;diel.epsilonR=epsR->value();diel.muR=muR->value();const FDTD::Material free{};switch(preset->currentIndex()){case 1:solver->setMaterialRect(nx/2-10,edge,nx/2+10,ny-edge-1,diel);break;case 2:{const int x=nx/2,ap=std::max(3,ny/12);solver->setMaterialRect(x-1,edge,x+1,ny-edge-1,pec);solver->setMaterialRect(x-1,ny/2-ap,x+1,ny/2+ap,free);break;}case 3:{const int x=nx/2,w=std::max(2,ny/22),sep=std::max(6,ny/9);solver->setMaterialRect(x-1,edge,x+1,ny-edge-1,pec);solver->setMaterialRect(x-1,ny/2-sep-w,x+1,ny/2-sep+w,free);solver->setMaterialRect(x-1,ny/2+sep-w,x+1,ny/2+sep+w,free);break;}case 4:{const int m=edge+3,t=2;solver->setMaterialRect(m,m,nx-m-1,m+t,pec);solver->setMaterialRect(m,ny-m-t-1,nx-m-1,ny-m-1,pec);solver->setMaterialRect(m,m,m+t,ny-m-1,pec);solver->setMaterialRect(nx-m-t-1,m,nx-m-1,ny-m-1,pec);break;}case 5:{const int y1=ny/3,y2=2*ny/3;solver->setMaterialRect(edge+2,y1-1,nx-edge-3,y1+1,pec);solver->setMaterialRect(edge+2,y2-1,nx-edge-3,y2+1,pec);break;}default:break;}resetAll();updateStats();};QObject::connect(applyPreset,&QPushButton::clicked,this,applyPresetGeometry);QObject::connect(clearMaterials,&QPushButton::clicked,this,[=]{timer->stop();solver->clearMaterials();resetAll();updateStats();});

    QObject::connect(applyGrid,&QPushButton::clicked,this,[=]{timer->stop();const auto pol=polarization->currentIndex()==0?FDTD::Polarization::TMz:FDTD::Polarization::TEz;const auto bc=boundary->currentIndex()==0?FDTD::BoundaryCondition::Cpml:boundary->currentIndex()==1?FDTD::BoundaryCondition::MurFirstOrder:FDTD::BoundaryCondition::Pec;solver->configure(nxSpin->value(),nySpin->value(),dxMm->value()*1e-3,dyMm->value()*1e-3,courant->value(),bc);solver->setPolarization(pol);solver->setCpmlCells(cpmlCells->value());solver->setCpmlTargetReflection(cpmlReflection->value());sourceX->setRange(1,solver->nx()-2);sourceY->setRange(1,solver->ny()-2);probeX->setRange(0,solver->nx()-1);probeY->setRange(0,solver->ny()-1);guideSourceX->setRange(2,solver->nx()-3);guideInputX->setRange(2,solver->nx()-3);guideOutputX->setRange(2,solver->nx()-3);guideYStart->setRange(2,solver->ny()-4);guideYEnd->setRange(3,solver->ny()-3);for(auto &p:*probes){p.x=std::clamp(p.x,0,solver->nx()-1);p.y=std::clamp(p.y,0,solver->ny()-1);}histories->resize(probes->size());applyGuideSource(analysisFreqGHz->value()*1e9);refreshSourceTable();refreshProbeTable();resetAll();refreshGuideInfo(analysisFreqGHz->value()*1e9);updateStats();});
    QObject::connect(polarization,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int i){timer->stop();solver->setPolarization(i==0?FDTD::Polarization::TMz:FDTD::Polarization::TEz);applyGuideSource(analysisFreqGHz->value()*1e9);resetAll();refreshGuideInfo(analysisFreqGHz->value()*1e9);updateStats();canvas->update();});QObject::connect(boundary,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int i){solver->setBoundaryCondition(i==0?FDTD::BoundaryCondition::Cpml:i==1?FDTD::BoundaryCondition::MurFirstOrder:FDTD::BoundaryCondition::Pec);resetAll();updateStats();});QObject::connect(cpmlCells,qOverload<int>(&QSpinBox::valueChanged),this,[=](int v){solver->setCpmlCells(v);contourMargin->setValue(std::max(contourMargin->value(),v+3));resetAll();});QObject::connect(cpmlReflection,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double v){solver->setCpmlTargetReflection(v);resetAll();});

    QObject::connect(analysisFreqGHz,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double fGHz){resetAccumulator();applyGuideSource(fGHz*1e9);refreshGuideInfo(fGHz*1e9);});
    QObject::connect(fftButton,&QPushButton::clicked,this,[=]{if(probes->empty())return;const int r=std::clamp(probeTable->currentRow(),0,int(probes->size())-1);const auto &h=(*histories)[std::size_t(r)];const auto sp=FDTDAnalysis::realSpectrum(h.scalar,solver->dtS(),360,true);if(sp.frequencyHz.size()<2){analysisNote->setText(QStringLiteral("Need more probe samples for FFT."));return;}QVector<FieldProfileSeries> series;series.push_back({qvec(sp.frequencyHz),qvec(sp.magnitudeDb),QStringLiteral("Magnitude"),QStringLiteral("dB rel. 1 unit"),false});series.push_back({qvec(sp.frequencyHz),qvec(sp.phaseDeg),QStringLiteral("Phase"),QStringLiteral("deg"),false});fftPlot->setSeries(series,QStringLiteral("%1 FFT (%2)").arg((*probes)[std::size_t(r)].name,solver->polarization()==FDTD::Polarization::TMz?QStringLiteral("Ez"):QStringLiteral("Hz")));analysisTabs->setCurrentWidget(fftPlot);analysisNote->setText(QStringLiteral("FFT uses a Hann window. Absolute dB is referenced to 1 field unit."));});

    auto portHistory=[=](int idx){FDTDAnalysis::PortTimeSeries p;if(idx<0||idx>=int(histories->size()))return p;p.electricTangentialVPerM=(*histories)[std::size_t(idx)].eTangential;p.magneticTangentialAPerM=(*histories)[std::size_t(idx)].hTangential;return p;};
    auto etaAtProbe=[=](int idx){if(idx<0||idx>=int(probes->size()))return 376.730313668;const auto &p=(*probes)[std::size_t(idx)];const auto m=solver->materialAt(p.x,p.y);return std::sqrt(FDTD::mu0*std::max(1e-9,m.muR)/(FDTD::epsilon0*std::max(1e-9,m.epsilonR)));};
    QObject::connect(sButton,&QPushButton::clicked,this,[=]{const int pi=inputPort->currentIndex(),po=outputPort->currentIndex();const auto r=FDTDAnalysis::estimateLocalPlaneWaveSParameters(portHistory(pi),portHistory(po),solver->dtS(),analysisFreqGHz->value()*1e9,etaAtProbe(pi),etaAtProbe(po),true);if(!r.valid){analysisNote->setText(QStringLiteral("S-parameter estimate failed: %1").arg(QString::fromStdString(r.error)));return;}s11Label->setText(QStringLiteral("%1  |%2|  %3 dB").arg(complexText(r.s11)).arg(std::abs(r.s11),0,'g',5).arg(r.s11Db,0,'g',5));s21Label->setText(QStringLiteral("%1  |%2|  %3 dB").arg(complexText(r.s21)).arg(std::abs(r.s21),0,'g',5).arg(r.s21Db,0,'g',5));vswrLabel->setText(QString::number(r.vswr,'g',6));analysisNote->setText(QStringLiteral("Local plane-wave decomposition: valid when each probe lies in a homogeneous, approximately single-mode region normal to +X."));});
    QObject::connect(sSweepButton,&QPushButton::clicked,this,[=]{const int pi=inputPort->currentIndex(),po=outputPort->currentIndex();if(pi<0||po<0)return;const auto in=portHistory(pi),out=portHistory(po);const auto base=FDTDAnalysis::realSpectrum(in.electricTangentialVPerM,solver->dtS(),180,true);std::vector<double> f,s11db,s21db;std::vector<std::complex<double>> s11c,s21c;const double etaI=etaAtProbe(pi),etaO=etaAtProbe(po);for(double freq:base.frequencyHz){if(freq<=0.0)continue;const auto r=FDTDAnalysis::estimateLocalPlaneWaveSParameters(in,out,solver->dtS(),freq,etaI,etaO,false);if(r.valid&&std::isfinite(r.s11Db)&&std::isfinite(r.s21Db)){f.push_back(freq);s11db.push_back(std::max(-120.0,r.s11Db));s21db.push_back(std::max(-120.0,r.s21Db));s11c.push_back(r.s11);s21c.push_back(r.s21);}}if(f.size()<3){analysisNote->setText(QStringLiteral("Not enough broadband energy/history for S sweep. Use Gaussian/Ricker excitation and record until the pulse crosses both ports."));return;}sPlot->setSeries({FieldProfileSeries{qvec(f),qvec(s11db),QStringLiteral("S11"),QStringLiteral("dB"),false},FieldProfileSeries{qvec(f),qvec(s21db),QStringLiteral("S21"),QStringLiteral("dB"),false}},QStringLiteral("Approximate local-plane-wave S-parameters"));setRfSweep(QStringLiteral("Broadband local-plane-wave probe sweep"),f,s11c,s21c,false);analysisTabs->setCurrentWidget(sPlot);analysisNote->setText(QStringLiteral("Broadband S extraction is an educational local plane-wave estimator, not a calibrated modal port solver. RF metrics/Smith chart are available from the same sweep."));});

    QObject::connect(guidePresetButton,&QPushButton::clicked,this,[=]{
        timer->stop();solver->clearMaterials();const int nx=solver->nx(),ny=solver->ny();const int edge=solver->boundaryCondition()==FDTD::BoundaryCondition::Cpml?solver->cpmlCells()+3:4;const FDTD::Material pec{1,1,0,true};
        const int y1=std::max(edge+5,ny/3),y2=std::min(ny-edge-6,2*ny/3);solver->setMaterialRect(edge+1,y1-1,nx-edge-2,y1+1,pec);solver->setMaterialRect(edge+1,y2-1,nx-edge-2,y2+1,pec);
        guideYStart->setValue(y1+2);guideYEnd->setValue(y2-2);guideSourceX->setValue(std::min(nx-4,edge+10));guideInputX->setValue(std::min(nx-4,edge+24));guideOutputX->setValue(std::max(guideInputX->value()+guideSep->value()+4,nx-edge-24));guideEnableSource->setChecked(true);applyGuideSource(analysisFreqGHz->value()*1e9);resetAll();refreshGuideInfo(analysisFreqGHz->value()*1e9);updateStats();
    });
    QObject::connect(guideApplySourceButton,&QPushButton::clicked,this,[=]{timer->stop();applyGuideSource(analysisFreqGHz->value()*1e9);resetAll();refreshGuideInfo(analysisFreqGHz->value()*1e9);guideNote->setText(QStringLiteral("Distributed modal source applied. Regular point sources remain available for non-modal experiments."));});
    QObject::connect(guideEnableSource,&QCheckBox::toggled,this,[=](bool){applyGuideSource(analysisFreqGHz->value()*1e9);resetAll();});
    for(auto *spin:{guideSourceX,guideInputX,guideOutputX,guideYStart,guideYEnd,guideMode,guideSep})QObject::connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,[=](int){canvas->setGuidePortOverlay(guideInputX->value(),guideOutputX->value(),guideYStart->value(),guideYEnd->value(),true);refreshGuideInfo(analysisFreqGHz->value()*1e9);});
    QObject::connect(guideEpsR,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double){refreshGuideInfo(analysisFreqGHz->value()*1e9);});
    QObject::connect(guideMuR,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double){refreshGuideInfo(analysisFreqGHz->value()*1e9);});
    QObject::connect(guideSolveButton,&QPushButton::clicked,this,[=]{timer->stop();const double f=analysisFreqGHz->value()*1e9;QApplication::setOverrideCursor(Qt::WaitCursor);const auto r=runModalFrequency(f);QApplication::restoreOverrideCursor();showModalResult(r);analysisTabs->setCurrentWidget(sPlot);if(r.valid)sPlot->setSeries({FieldProfileSeries{QVector<double>{f},QVector<double>{r.s11Db},QStringLiteral("S11 modal"),QStringLiteral("dB"),false},FieldProfileSeries{QVector<double>{f},QVector<double>{r.s21Db},QStringLiteral("S21 modal"),QStringLiteral("dB"),false}},QStringLiteral("2D PEC guide modal port @ %1").arg(eng(f,QStringLiteral("Hz"))));});
    QObject::connect(guideSweepButton,&QPushButton::clicked,this,[=]{
        timer->stop();double f0=sweepStartGHz->value()*1e9,f1=sweepStopGHz->value()*1e9;if(f1<f0)std::swap(f0,f1);const int n=sweepPoints->value();std::vector<double> freq,s11v,s21v;std::vector<std::complex<double>> s11c,s21c;freq.reserve(n);s11v.reserve(n);s21v.reserve(n);s11c.reserve(n);s21c.reserve(n);
        QProgressDialog progress(QStringLiteral("Running modal CW frequency sweep..."),QStringLiteral("Cancel"),0,n,this);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);
        QString lastError;
        for(int i=0;i<n;++i){if(progress.wasCanceled())break;const double f=n==1?f0:f0+(f1-f0)*double(i)/double(n-1);progress.setValue(i);progress.setLabelText(QStringLiteral("Modal sweep %1 / %2 — %3").arg(i+1).arg(n).arg(eng(f,QStringLiteral("Hz"))));QApplication::processEvents();const auto r=runModalFrequency(f);if(r.valid){freq.push_back(f);s11v.push_back(std::max(-140.0,r.s11Db));s21v.push_back(std::max(-140.0,r.s21Db));s11c.push_back(r.s11);s21c.push_back(r.s21);showModalResult(r);}else lastError=QString::fromStdString(r.error);}
        progress.setValue(n);if(freq.size()>=2){sPlot->setSeries({FieldProfileSeries{qvec(freq),qvec(s11v),QStringLiteral("S11 modal"),QStringLiteral("dB"),false},FieldProfileSeries{qvec(freq),qvec(s21v),QStringLiteral("S21 modal"),QStringLiteral("dB"),false}},QStringLiteral("2D PEC guide modal CW sweep — mode m=%1").arg(guideMode->value()));setRfSweep(QStringLiteral("2D guide modal CW sweep — mode m=%1").arg(guideMode->value()),freq,s11c,s21c,false);analysisTabs->setCurrentWidget(sPlot);guideNote->setText(QStringLiteral("Modal sweep completed at %1 valid frequency points. Smith chart, impedance, group delay and bandwidth metrics are now available. Below-cutoff/ill-conditioned points are omitted. %2").arg(freq.size()).arg(lastError));}else guideNote->setText(QStringLiteral("Modal sweep produced too few valid points. %1").arg(lastError));
    });

    QObject::connect(analyzeSweepButton,&QPushButton::clicked,this,[=]{refreshRfAnalysis(true);});
    QObject::connect(referenceZ0,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double){if(rfSweep->frequencyHz.size()>=2)refreshRfAnalysis(false);});

    QObject::connect(markerSelect,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int index){if(*syncingRfMarker||index<0||index>=int(rfMarkers->size()))return;*activeRfMarker=index;refreshMarkerReadout();});
    QObject::connect(markerFreqGHz,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double ghz){if(*syncingRfMarker)return;moveActiveMarkerToFrequency(ghz*1e9);});
    QObject::connect(markerEnabled,&QCheckBox::toggled,this,[=](bool enabled){if(*syncingRfMarker||rfMarkers->isEmpty())return;(*rfMarkers)[std::clamp(*activeRfMarker,0,int(rfMarkers->size())-1)].enabled=enabled;refreshMarkerReadout();});
    QObject::connect(deltaReference,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int){if(!*syncingRfMarker)refreshMarkerReadout();});
    QObject::connect(addMarkerButton,&QPushButton::clicked,this,[=]{const int n=int(rfMarkers->size())+1;double f=rfSweep->frequencyHz.empty()?analysisFreqGHz->value()*1e9:rfSweep->frequencyHz[rfSweep->frequencyHz.size()/2];rfMarkers->push_back({QStringLiteral("M%1").arg(n),f,true,false});*activeRfMarker=int(rfMarkers->size())-1;refreshMarkerCombos();refreshMarkerReadout();});
    QObject::connect(removeMarkerButton,&QPushButton::clicked,this,[=]{if(rfMarkers->size()<=1){QMessageBox::information(this,QStringLiteral("VNA markers"),QStringLiteral("Keep at least one marker."));return;}rfMarkers->removeAt(std::clamp(*activeRfMarker,0,int(rfMarkers->size())-1));*activeRfMarker=std::clamp(*activeRfMarker,0,int(rfMarkers->size())-1);for(int i=0;i<int(rfMarkers->size());++i)(*rfMarkers)[i].name=QStringLiteral("M%1").arg(i+1);refreshMarkerCombos();refreshMarkerReadout();});
    QObject::connect(snapS11Button,&QPushButton::clicked,this,[=]{if(rfSweep->frequencyHz.empty())return;std::size_t best=0;double v=std::numeric_limits<double>::infinity();for(std::size_t i=0;i<rfSweep->s11.size();++i){const double m=std::abs(rfSweep->s11[i]);if(m<v){v=m;best=i;}}if(best<rfSweep->frequencyHz.size())moveActiveMarkerToFrequency(rfSweep->frequencyHz[best]);});
    QObject::connect(snapS21Button,&QPushButton::clicked,this,[=]{if(rfSweep->frequencyHz.empty())return;std::size_t best=0;double v=-1.0;for(std::size_t i=0;i<rfSweep->s21.size();++i){const double m=std::abs(rfSweep->s21[i]);if(m>v){v=m;best=i;}}if(best<rfSweep->frequencyHz.size())moveActiveMarkerToFrequency(rfSweep->frequencyHz[best]);});
    QObject::connect(freezeSweepButton,&QPushButton::clicked,this,[=]{if(rfSweep->frequencyHz.size()<2){QMessageBox::information(this,QStringLiteral("Sweep comparison"),QStringLiteral("Run an S-parameter sweep first."));return;}RfSweepData copy=*rfSweep;copy.title=QStringLiteral("Snapshot %1 — %2").arg(int(frozenRfSweeps->size())+1).arg(rfSweep->title);frozenRfSweeps->push_back(std::move(copy));if(frozenRfSweeps->size()>8)frozenRfSweeps->erase(frozenRfSweeps->begin());refreshFrozenList();refreshSweepPlot();smithChart->setFrozenSweeps(*frozenRfSweeps);});
    QObject::connect(clearFrozenButton,&QPushButton::clicked,this,[=]{frozenRfSweeps->clear();refreshFrozenList();if(rfSweep->frequencyHz.size()>=2){refreshSweepPlot();smithChart->setFrozenSweeps(*frozenRfSweeps);}else smithChart->clearSweep();});
    sPlot->markerPositionRequested=moveActiveMarkerToFrequency;
    rfPhasePlot->markerPositionRequested=moveActiveMarkerToFrequency;
    smithChart->markerPositionRequested=moveActiveMarkerToFrequency;
    tdExportGatedButton->setEnabled(false);
    QObject::connect(tdRefreshButton,&QPushButton::clicked,this,[=]{refreshTimeDomain(false,true);analysisTabs->setCurrentWidget(timeDomainPage);});
    QObject::connect(tdApplyGateButton,&QPushButton::clicked,this,[=]{applyTimeGate();analysisTabs->setCurrentWidget(timeDomainPage);});
    QObject::connect(tdClearGateButton,&QPushButton::clicked,this,[=]{*hasGatedSweep=false;gatedRfSweep->frequencyHz.clear();gatedRfSweep->s11.clear();gatedRfSweep->s21.clear();smithChart->clearGatedSweep();if(rfSweep->frequencyHz.size()>=2)refreshSweepPlot();renderTimeDomain();tdStatus->setText(QStringLiteral("Gated frequency result cleared; the gate cursors remain as a preview."));});
    QObject::connect(tdParameter,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int){refreshTimeDomain(true,true);});
    QObject::connect(tdWindow,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int){refreshTimeDomain(false,true);});
    QObject::connect(tdAxis,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int){renderTimeDomain();});
    QObject::connect(tdPropagation,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int){renderTimeDomain();});
    for(auto *sp:{tdVelocityFactor,tdEpsEff,tdMuEff})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double){renderTimeDomain();});
    auto invalidateGateFromControls=[=]{if(*hasGatedSweep){*hasGatedSweep=false;gatedRfSweep->frequencyHz.clear();smithChart->clearGatedSweep();if(rfSweep->frequencyHz.size()>=2)refreshSweepPlot();}renderTimeDomain();};
    QObject::connect(tdGateMode,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int){invalidateGateFromControls();});
    QObject::connect(tdGateEdge,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int){invalidateGateFromControls();});
    for(auto *sp:{tdGateStartNs,tdGateStopNs,tdTransitionNs})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double){invalidateGateFromControls();});
    tdResponsePlot->markerPositionRequestedIndexed=[=](int markerIndex,double xValue){if(markerIndex<0||markerIndex>1||!timeDomainResult->valid)return;const bool reflection=tdParameter->currentIndex()==0;const double vp=tdPropagationSpeed();if(!(vp>0.0))return;const double timeS=tdAxis->currentIndex()==1?xValue*(reflection?2.0:1.0)/vp:xValue*1e-9;const double ns=std::clamp(timeS*1e9,0.0,timeDomainResult->timeS.back()*1e9);if(markerIndex==0)tdGateStartNs->setValue(ns);else tdGateStopNs->setValue(ns);};
    QObject::connect(tdExportGatedButton,&QPushButton::clicked,this,[=]{
        if(!*hasGatedSweep||*gatedParameter!=0||gatedRfSweep->s11.size()!=gatedRfSweep->frequencyHz.size()){QMessageBox::information(this,QStringLiteral("Gated Touchstone export"),QStringLiteral("Apply a gate to S11 first."));return;}
        QString path=QFileDialog::getSaveFileName(this,QStringLiteral("Export gated Touchstone S1P"),QStringLiteral("qtsignal_gated_s11.s1p"),QStringLiteral("Touchstone S1P (*.s1p)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".s1p"),Qt::CaseInsensitive))path+=QStringLiteral(".s1p");
        QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,QStringLiteral("Gated Touchstone export"),file.errorString());return;}QTextStream ts(&file);ts<<"! QTsignalApp 2.2.4 — time-gated S11 — "<<rfSweep->title<<"\n";ts<<"# Hz S RI R "<<QString::number(referenceZ0->value(),'g',12)<<"\n";for(std::size_t i=0;i<gatedRfSweep->frequencyHz.size();++i)ts<<QString::number(gatedRfSweep->frequencyHz[i],'g',16)<<" "<<QString::number(gatedRfSweep->s11[i].real(),'g',16)<<" "<<QString::number(gatedRfSweep->s11[i].imag(),'g',16)<<"\n";
    });
    QObject::connect(exportS1pButton,&QPushButton::clicked,this,[=]{
        if(rfSweep->frequencyHz.empty()||rfSweep->s11.size()!=rfSweep->frequencyHz.size()){QMessageBox::information(this,QStringLiteral("Touchstone export"),QStringLiteral("Run an S-parameter sweep first."));return;}
        QString path=QFileDialog::getSaveFileName(this,QStringLiteral("Export Touchstone S1P"),QStringLiteral("qtsignal_sweep.s1p"),QStringLiteral("Touchstone S1P (*.s1p)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".s1p"),Qt::CaseInsensitive))path+=QStringLiteral(".s1p");
        QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,QStringLiteral("Touchstone export"),file.errorString());return;}QTextStream ts(&file);ts<<"! QTsignalApp 2.2.4 — "<<rfSweep->title<<"\n";ts<<"# Hz S RI R "<<QString::number(referenceZ0->value(),'g',12)<<"\n";for(std::size_t i=0;i<rfSweep->frequencyHz.size();++i)ts<<QString::number(rfSweep->frequencyHz[i],'g',16)<<" "<<QString::number(rfSweep->s11[i].real(),'g',16)<<" "<<QString::number(rfSweep->s11[i].imag(),'g',16)<<"\n";
    });
    QObject::connect(exportS2pButton,&QPushButton::clicked,this,[=]{
        if(rfSweep->frequencyHz.empty()||rfSweep->s11.size()!=rfSweep->frequencyHz.size()||rfSweep->s21.size()!=rfSweep->frequencyHz.size()){QMessageBox::information(this,QStringLiteral("Touchstone export"),QStringLiteral("Run an S-parameter sweep first."));return;}
        const auto answer=QMessageBox::warning(this,QStringLiteral("S2P symmetry assumption"),QStringLiteral("The current 2D forward sweep solves S11 and S21 only. Exporting .s2p will assume a reciprocal symmetric network: S12 = S21 and S22 = S11. Use this only when that assumption is physically justified."),QMessageBox::Ok|QMessageBox::Cancel,QMessageBox::Cancel);if(answer!=QMessageBox::Ok)return;
        QString path=QFileDialog::getSaveFileName(this,QStringLiteral("Export Touchstone S2P"),QStringLiteral("qtsignal_symmetric.s2p"),QStringLiteral("Touchstone S2P (*.s2p)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".s2p"),Qt::CaseInsensitive))path+=QStringLiteral(".s2p");
        QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,QStringLiteral("Touchstone export"),file.errorString());return;}QTextStream ts(&file);ts<<"! QTsignalApp 2.2.4 — symmetric reciprocal assumption: S12=S21, S22=S11\n";ts<<"! "<<rfSweep->title<<"\n";ts<<"# Hz S RI R "<<QString::number(referenceZ0->value(),'g',12)<<"\n";for(std::size_t i=0;i<rfSweep->frequencyHz.size();++i){const auto a=rfSweep->s11[i],b=rfSweep->s21[i];ts<<QString::number(rfSweep->frequencyHz[i],'g',16)<<" "<<QString::number(a.real(),'g',16)<<" "<<QString::number(a.imag(),'g',16)<<" "<<QString::number(b.real(),'g',16)<<" "<<QString::number(b.imag(),'g',16)<<" "<<QString::number(b.real(),'g',16)<<" "<<QString::number(b.imag(),'g',16)<<" "<<QString::number(a.real(),'g',16)<<" "<<QString::number(a.imag(),'g',16)<<"\n";}
    });
    QObject::connect(exportCsvButton,&QPushButton::clicked,this,[=]{
        if(rfSweep->frequencyHz.size()<2){QMessageBox::information(this,QStringLiteral("RF CSV export"),QStringLiteral("Run an S-parameter sweep first."));return;}const auto a=FDTDAnalysis::analyzeSParameterSweep(rfSweep->frequencyHz,rfSweep->s11,rfSweep->s21,referenceZ0->value());if(!a.valid)return;
        QString path=QFileDialog::getSaveFileName(this,QStringLiteral("Export RF sweep CSV"),QStringLiteral("qtsignal_rf_sweep.csv"),QStringLiteral("CSV (*.csv)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".csv"),Qt::CaseInsensitive))path+=QStringLiteral(".csv");QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,QStringLiteral("RF CSV export"),file.errorString());return;}QTextStream ts(&file);ts<<"frequency_Hz,S11_re,S11_im,S11_dB,S11_phase_deg,S21_re,S21_im,S21_dB,S21_phase_deg,group_delay_s,VSWR,Zin_re_ohm,Zin_im_ohm\n";for(std::size_t i=0;i<a.frequencyHz.size();++i)ts<<QString::number(a.frequencyHz[i],'g',16)<<","<<QString::number(a.s11[i].real(),'g',16)<<","<<QString::number(a.s11[i].imag(),'g',16)<<","<<QString::number(a.s11Db[i],'g',16)<<","<<QString::number(a.s11PhaseDeg[i],'g',16)<<","<<QString::number(a.s21[i].real(),'g',16)<<","<<QString::number(a.s21[i].imag(),'g',16)<<","<<QString::number(a.s21Db[i],'g',16)<<","<<QString::number(a.s21PhaseDeg[i],'g',16)<<","<<QString::number(a.groupDelayS[i],'g',16)<<","<<QString::number(a.vswr[i],'g',16)<<","<<QString::number(a.inputImpedanceOhm[i].real(),'g',16)<<","<<QString::number(a.inputImpedanceOhm[i].imag(),'g',16)<<"\n";
    });

    QObject::connect(farButton,&QPushButton::clicked,this,[=]{const auto ph=accumulator->phasorField();if(accumulator->sampleCount()<32){analysisNote->setText(QStringLiteral("Need at least 32 harmonic accumulation samples. Use a continuous sine and run for several periods."));return;}const double f=analysisFreqGHz->value()*1e9;const double k=2.0*FDTDAnalysis::Pi*f/FDTD::c0;const auto ff=FDTDAnalysis::scalarHuygensFarField(ph,solver->nx(),solver->ny(),solver->dxM(),solver->dyM(),k,contourMargin->value(),361);if(!ff.valid){analysisNote->setText(QStringLiteral("Far-field failed: %1").arg(QString::fromStdString(ff.error)));return;}farPlot->setSeries({FieldProfileSeries{qvec(ff.angleDeg),qvec(ff.magnitudeDb),QStringLiteral("Normalized far field"),QStringLiteral("dB"),false}},QStringLiteral("2D scalar Huygens far-field estimate"));analysisTabs->setCurrentWidget(farPlot);farDLabel->setText(QStringLiteral("%1 linear / %2 dB (2D)").arg(ff.directivity2DLinear,0,'g',6).arg(ff.directivity2DDbi,0,'g',6));analysisNote->setText(QStringLiteral("Experimental 2D scalar Huygens transform. The contour must be homogeneous, enclose all scatterers, and remain inside the CPML interface."));});

    refreshMarkerCombos();refreshFrozenList();refreshMarkerReadout();refreshTimeDomain(false,false);
    refreshSourceTable();sourceTable->selectRow(0);loadSelectedSource();refreshProbeTable();probeTable->selectRow(0);loadSelectedProbe();refreshPortCombos();inputPort->setCurrentIndex(0);outputPort->setCurrentIndex(std::min(1,outputPort->count()-1));applyGuideSource(analysisFreqGHz->value()*1e9);canvas->setGuidePortOverlay(guideInputX->value(),guideOutputX->value(),guideYStart->value(),guideYEnd->value(),true);refreshGuideInfo(analysisFreqGHz->value()*1e9);resetAccumulator();updateStats();
}
