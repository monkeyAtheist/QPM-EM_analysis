#include "widgets/numerical_solvers_workspace.h"

#include "numerical_em_solvers.h"
#include "widgets/field_profile_plot.h"
#include "widgets/fdtd_workspace.h"
#include "widgets/engineering_sketch_widget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <complex>

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

QLabel *output(QWidget *parent)
{
    auto *l = new QLabel(QStringLiteral("—"), parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QString eng(double value, const QString &unit)
{
    if (!std::isfinite(value)) return QStringLiteral("n/a");
    if (std::abs(value) < 1e-30) return QStringLiteral("0 %1").arg(unit);
    struct Prefix { double scale; const char *symbol; };
    static const Prefix p[] = {{1e12,"T"},{1e9,"G"},{1e6,"M"},{1e3,"k"},{1,""},{1e-3,"m"},{1e-6,"u"},{1e-9,"n"},{1e-12,"p"},{1e-15,"f"}};
    for (const auto &x : p)
        if (std::abs(value) >= 0.999*x.scale || x.scale == 1e-15)
            return QStringLiteral("%1 %2%3").arg(value/x.scale,0,'g',7).arg(QString::fromLatin1(x.symbol),unit);
    return QStringLiteral("%1 %2").arg(value,0,'g',7).arg(unit);
}

QString complexOhm(const std::complex<double> &z)
{
    return QStringLiteral("%1 %2 j%3 Ω")
        .arg(z.real(),0,'g',8)
        .arg(z.imag() >= 0.0 ? QStringLiteral("+") : QStringLiteral("−"))
        .arg(std::abs(z.imag()),0,'g',8);
}

QWidget *scrollPage(QWidget *content, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0,0,0,0);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    return page;
}

class BemPreview final : public QWidget
{
public:
    explicit BemPreview(QWidget *parent=nullptr) : QWidget(parent)
    {
        setMinimumSize(620,420);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    }

    void setResult(const NumericalEM::BemResult2D &r)
    {
        m_result=r;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing,true);
        const auto pal=palette();
        p.fillRect(rect(),pal.color(QPalette::Base));
        p.setPen(pal.color(QPalette::Text));
        p.drawText(QRectF(10,8,width()-20,24),Qt::AlignCenter,QStringLiteral("BEM cross-section — boundary charge density"));
        if(!m_result.valid || m_result.panels.empty())
        {
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(rect(),Qt::AlignCenter,QStringLiteral("Solve the two-conductor problem to display panels and charge density."));
            return;
        }

        double xmin=1e100,xmax=-1e100,ymin=1e100,ymax=-1e100,maxSigma=0.0;
        for(const auto &q:m_result.panels)
        {
            xmin=std::min({xmin,q.p0.x,q.p1.x});xmax=std::max({xmax,q.p0.x,q.p1.x});
            ymin=std::min({ymin,q.p0.y,q.p1.y});ymax=std::max({ymax,q.p0.y,q.p1.y});
            maxSigma=std::max(maxSigma,std::abs(q.surfaceChargeDensityCpm2));
        }
        const double dx=std::max(xmax-xmin,1e-9),dy=std::max(ymax-ymin,1e-9);
        xmin-=0.18*dx;xmax+=0.18*dx;ymin-=0.18*dy;ymax+=0.18*dy;
        const QRectF pr(55,45,width()-85,height()-85);
        const double sx=pr.width()/(xmax-xmin),sy=pr.height()/(ymax-ymin),s=std::min(sx,sy);
        auto map=[&](NumericalEM::Vec2 v){return QPointF(pr.center().x()+(v.x-0.5*(xmin+xmax))*s,pr.center().y()-(v.y-0.5*(ymin+ymax))*s);};

        p.setPen(QPen(pal.color(QPalette::Mid),1,Qt::DotLine));
        if(xmin<0&&xmax>0){auto a=map({0,ymin}),b=map({0,ymax});p.drawLine(a,b);} if(ymin<0&&ymax>0){auto a=map({xmin,0}),b=map({xmax,0});p.drawLine(a,b);}

        for(const auto &q:m_result.panels)
        {
            const double t=maxSigma>0?std::clamp(std::abs(q.surfaceChargeDensityCpm2)/maxSigma,0.0,1.0):0.0;
            QColor c=q.surfaceChargeDensityCpm2>=0?QColor(230,70,60):QColor(65,135,240);
            c.setAlphaF(0.35+0.65*t);
            p.setPen(QPen(c,2.0+3.0*t,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(map(q.p0),map(q.p1));
        }
        p.setPen(pal.color(QPalette::Text));
        p.drawText(QRectF(10,height()-30,width()-20,20),Qt::AlignCenter,
                   QStringLiteral("Red: +σ   Blue: −σ   |σ|max = %1").arg(eng(maxSigma,QStringLiteral("C/m²"))));
    }
private:
    NumericalEM::BemResult2D m_result;
};

NumericalEM::ConductorShape shapeFromIndex(int i)
{
    return i==1?NumericalEM::ConductorShape::Rectangle:NumericalEM::ConductorShape::Circle;
}
}

NumericalSolversWorkspace::NumericalSolversWorkspace(QWidget *parent)
    : QWidget(parent)
{
    auto *root=new QVBoxLayout(this);
    auto *tabs=new QTabWidget(this);
    root->addWidget(tabs,1);

    auto *fdtd = new FdtdWorkspace(tabs);
    const int fdtdTab = tabs->addTab(fdtd, QStringLiteral("FDTD 2D time-domain  ⓘ"));
    tabs->setTabToolTip(fdtdTab, QStringLiteral(
        "<qt><b>FDTD 2D time-domain</b><br/><br/>"
        "Time-domain Maxwell solver on a 2D Yee grid with TMz/TEz polarizations, CPML/Mur/PEC boundaries, "
        "multiple sources and probes, FFT/port analysis and a scalar Huygens near-to-far transform. "
        "Probe-based S-parameters assume locally plane, predominantly single-mode propagation. "
        "Keep the far-field contour in homogeneous material, around all scatterers and inside the absorber."
        "</qt>"));

    // ------------------------------------------------------------------
    // 2D electrostatic BEM
    // ------------------------------------------------------------------
    auto *bemContent=new QWidget(tabs);auto *bemRoot=new QVBoxLayout(bemContent);
    auto *bemSplit=new QSplitter(Qt::Horizontal,bemContent);bemRoot->addWidget(bemSplit,1);
    // Keep the dense BEM parameter/result column independently scrollable. This
    // prevents large UI fonts or compact window heights from squeezing form rows.
    auto *bemControlScroll=new QScrollArea(bemSplit);bemControlScroll->setWidgetResizable(true);bemControlScroll->setMinimumWidth(500);
    auto *bemControls=new QWidget();auto *bc=new QVBoxLayout(bemControls);bc->setSpacing(10);bemControlScroll->setWidget(bemControls);
    auto *global=new QGroupBox(QStringLiteral("2D boundary-element settings"),bemControls);auto *gf=new QFormLayout(global);
    auto *epsr=dspin(global,1.0,0.000001,1e6,6);auto *panels=new QSpinBox(global);panels->setRange(8,200);panels->setValue(48);
    auto *va=dspin(global,0.5,-1e6,1e6,6),*vb=dspin(global,-0.5,-1e6,1e6,6);
    gf->addRow(QStringLiteral("Relative permittivity εr"),epsr);gf->addRow(QStringLiteral("Panels / conductor"),panels);gf->addRow(QStringLiteral("Conductor A potential (V)"),va);gf->addRow(QStringLiteral("Conductor B potential (V)"),vb);bc->addWidget(global);

    struct ConductorUi { QGroupBox *box; QComboBox *shape; QDoubleSpinBox *x,*y,*radius,*width,*height; };
    auto makeConductor=[&](const QString &name,double x0)->ConductorUi{
        ConductorUi u{};u.box=new QGroupBox(name,bemControls);auto *f=new QFormLayout(u.box);u.shape=new QComboBox(u.box);u.shape->addItems({QStringLiteral("Circle"),QStringLiteral("Rectangle")});
        u.x=dspin(u.box,x0,-1000,1000,6);u.y=dspin(u.box,0,-1000,1000,6);u.radius=dspin(u.box,0.010,1e-6,1000,6);u.width=dspin(u.box,0.020,1e-6,1000,6);u.height=dspin(u.box,0.020,1e-6,1000,6);
        f->addRow(QStringLiteral("Shape"),u.shape);f->addRow(QStringLiteral("Center X (m)"),u.x);f->addRow(QStringLiteral("Center Y (m)"),u.y);f->addRow(QStringLiteral("Radius (m)"),u.radius);f->addRow(QStringLiteral("Width (m)"),u.width);f->addRow(QStringLiteral("Height (m)"),u.height);
        auto update=[=](int idx){u.radius->setEnabled(idx==0);u.width->setEnabled(idx==1);u.height->setEnabled(idx==1);};QObject::connect(u.shape,qOverload<int>(&QComboBox::currentIndexChanged),u.box,update);update(0);return u;};
    const auto ca=makeConductor(QStringLiteral("Conductor A"),-0.025);const auto cb=makeConductor(QStringLiteral("Conductor B"),0.025);bc->addWidget(ca.box);bc->addWidget(cb.box);
    auto *solveBem=new QPushButton(QStringLiteral("Solve BEM capacitance"),bemControls);bc->addWidget(solveBem);
    auto *bemOut=new QGroupBox(QStringLiteral("Numerical result"),bemControls);auto *bof=new QFormLayout(bemOut);bof->setVerticalSpacing(8);auto *cap=output(bemOut),*qa=output(bemOut),*qb=output(bemOut),*res=output(bemOut),*conditioning=output(bemOut),*analytic=output(bemOut),*note=output(bemOut);bof->addRow(QStringLiteral("C'"),cap);bof->addRow(QStringLiteral("Q'A"),qa);bof->addRow(QStringLiteral("Q'B"),qb);bof->addRow(QStringLiteral("Boundary residual"),res);bof->addRow(QStringLiteral("Pivot span"),conditioning);bof->addRow(QStringLiteral("Analytic comparison"),analytic);bof->addRow(note);note->setMinimumHeight(58);bemOut->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::MinimumExpanding);bc->addWidget(bemOut);bc->addSpacing(8);
    auto *preview=new BemPreview(bemSplit);bemSplit->addWidget(bemControlScroll);bemSplit->addWidget(preview);bemSplit->setStretchFactor(0,0);bemSplit->setStretchFactor(1,1);bemSplit->setSizes({540,900});

    auto runBem=[=]{
        NumericalEM::BemInput2D in;in.epsilonR=epsr->value();in.panelsPerConductor=panels->value();
        auto fill=[](NumericalEM::BemConductor2D &c,const ConductorUi &u,double pot){c.shape=shapeFromIndex(u.shape->currentIndex());c.center={u.x->value(),u.y->value()};c.radiusM=u.radius->value();c.widthM=u.width->value();c.heightM=u.height->value();c.potentialV=pot;};
        fill(in.conductorA,ca,va->value());fill(in.conductorB,cb,vb->value());const auto r=NumericalEM::solveTwoConductorBem2D(in);preview->setResult(r);
        if(!r.valid){cap->setText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(r.error)));qa->setText(QStringLiteral("—"));qb->setText(QStringLiteral("—"));res->setText(QStringLiteral("—"));conditioning->setText(QStringLiteral("—"));analytic->setText(QStringLiteral("—"));note->setText(QStringLiteral("Check geometry and discretization."));return;}
        cap->setText(eng(r.capacitancePerLengthFpm,QStringLiteral("F/m")));qa->setText(eng(r.totalChargePerLengthCpm[0],QStringLiteral("C/m")));qb->setText(eng(r.totalChargePerLengthCpm[1],QStringLiteral("C/m")));res->setText(eng(r.maxBoundaryResidualV,QStringLiteral("V")));conditioning->setText(QStringLiteral("%1 … %2").arg(r.minPivotAbs,0,'g',4).arg(r.maxPivotAbs,0,'g',4));note->setText(QString::fromStdString(r.note));
        if(in.conductorA.shape==NumericalEM::ConductorShape::Circle && in.conductorB.shape==NumericalEM::ConductorShape::Circle && std::abs(in.conductorA.radiusM-in.conductorB.radiusM)<1e-12)
        {const double dx=in.conductorA.center.x-in.conductorB.center.x,dy=in.conductorA.center.y-in.conductorB.center.y,d=std::sqrt(dx*dx+dy*dy);const double exact=NumericalEM::analyticTwoWireCapacitancePerLength(in.conductorA.radiusM,d,in.epsilonR);const double err=exact>0?100.0*(r.capacitancePerLengthFpm-exact)/exact:0.0;analytic->setText(QStringLiteral("Two-wire closed form: %1  (error %2 %)").arg(eng(exact,QStringLiteral("F/m"))).arg(err,0,'g',5));}
        else analytic->setText(QStringLiteral("No closed-form comparison selected for this geometry."));
    };
    QObject::connect(solveBem,&QPushButton::clicked,bemContent,runBem);runBem();
    tabs->addTab(bemContent,QStringLiteral("Electrostatic BEM 2D"));

    // ------------------------------------------------------------------
    // Thin-wire Pocklington MoM
    // ------------------------------------------------------------------
    auto *momContent=new QWidget(tabs);auto *momRoot=new QVBoxLayout(momContent);auto *momTop=new QHBoxLayout();momRoot->addLayout(momTop);
    auto *momIn=new QGroupBox(QStringLiteral("Straight center-fed thin-wire dipole"),momContent);auto *mif=new QFormLayout(momIn);auto *mf=dspin(momIn,100.0,0.001,1e6,6);auto *ml=dspin(momIn,1.409,1e-6,1e6,6);auto *mr=dspin(momIn,15.0,0.001,1e6,6);auto *mn=new QSpinBox(momIn);mn->setRange(7,151);mn->setSingleStep(2);mn->setValue(41);auto *mv=dspin(momIn,1.0,1e-6,1e6,6);auto *momSolve=new QPushButton(QStringLiteral("Solve Pocklington MoM"),momIn);mif->addRow(QStringLiteral("Frequency (MHz)"),mf);mif->addRow(QStringLiteral("Total length (m)"),ml);mif->addRow(QStringLiteral("Wire radius (mm)"),mr);mif->addRow(QStringLiteral("Odd pulse segments"),mn);mif->addRow(QStringLiteral("Delta-gap voltage (V)"),mv);mif->addRow(momSolve);momTop->addWidget(momIn);
    auto *momOut=new QGroupBox(QStringLiteral("Solved feed / convergence"),momContent);auto *mof=new QFormLayout(momOut);auto *mLam=output(momOut),*mEl=output(momOut),*mDz=output(momOut),*mZ=output(momOut),*mI=output(momOut),*mD=output(momOut),*mRes=output(momOut),*mPivot=output(momOut),*mNote=output(momOut);mof->addRow(QStringLiteral("λ"),mLam);mof->addRow(QStringLiteral("L / λ"),mEl);mof->addRow(QStringLiteral("Segment Δz"),mDz);mof->addRow(QStringLiteral("Input impedance"),mZ);mof->addRow(QStringLiteral("|I feed|"),mI);mof->addRow(QStringLiteral("Directivity"),mD);mof->addRow(QStringLiteral("Linear residual"),mRes);mof->addRow(QStringLiteral("Pivot span"),mPivot);mof->addRow(mNote);momTop->addWidget(momOut,1);
    auto *momPlots=new QTabWidget(momContent);momRoot->addWidget(momPlots,1);auto *currentPlot=new FieldProfilePlot(momPlots);currentPlot->setXAxis(QStringLiteral("Position z along dipole"),QStringLiteral("m"));auto *patternPlot=new FieldProfilePlot(momPlots);patternPlot->setXAxis(QStringLiteral("Polar angle θ"),QStringLiteral("deg"));momPlots->addTab(currentPlot,QStringLiteral("Current distribution"));momPlots->addTab(patternPlot,QStringLiteral("Far-field pattern"));
    auto runMom=[=]{NumericalEM::ThinWireMomInput in;in.frequencyHz=mf->value()*1e6;in.lengthM=ml->value();in.radiusM=mr->value()*1e-3;in.segments=mn->value();in.feedVoltageV=mv->value();const auto r=NumericalEM::solveThinWireDipolePocklington(in);if(!r.valid){mZ->setText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(r.error)));currentPlot->clearData();patternPlot->clearData();return;}mLam->setText(eng(r.wavelengthM,QStringLiteral("m")));mEl->setText(QString::number(r.electricalLengthLambda,'g',7));mDz->setText(eng(r.segmentLengthM,QStringLiteral("m")));mZ->setText(complexOhm(r.inputImpedanceOhm));mI->setText(eng(r.feedCurrentMagnitudeA,QStringLiteral("A")));mD->setText(QStringLiteral("%1 linear / %2 dBi").arg(r.directivityLinear,0,'g',6).arg(r.directivityDbi,0,'g',6));mRes->setText(QString::number(r.residualRelative,'g',5));mPivot->setText(QStringLiteral("%1 … %2").arg(r.minPivotAbs,0,'g',4).arg(r.maxPivotAbs,0,'g',4));mNote->setText(QString::fromStdString(r.note));QVector<double> z,mag,phase;for(size_t i=0;i<r.zM.size();++i){z.push_back(r.zM[i]);mag.push_back(std::abs(r.currentA[i]));phase.push_back(std::arg(r.currentA[i])*180.0/NumericalEM::Pi);}currentPlot->setSeries({FieldProfileSeries{z,mag,QStringLiteral("|I(z)|"),QStringLiteral("A"),false},FieldProfileSeries{z,phase,QStringLiteral("phase(I)"),QStringLiteral("deg"),false}},QStringLiteral("MoM solved current — change segment count to check convergence"));QVector<double> th,db;for(size_t i=0;i<r.thetaDeg.size();++i){th.push_back(r.thetaDeg[i]);db.push_back(r.normalizedFarField[i]>1e-6?20.0*std::log10(r.normalizedFarField[i]):-120.0);}patternPlot->setSeries({FieldProfileSeries{th,db,QStringLiteral("Normalized |E far|"),QStringLiteral("dB"),false}},QStringLiteral("Axisymmetric far-field pattern from solved current"));};
    QObject::connect(momSolve,&QPushButton::clicked,momContent,runMom);runMom();tabs->addTab(momContent,QStringLiteral("Thin-wire MoM"));

    // ------------------------------------------------------------------
    // Numerical circular-loop mutual inductance
    // ------------------------------------------------------------------
    auto *loopContent=new QWidget(tabs);auto *loopRoot=new QVBoxLayout(loopContent);auto *loopTop=new QHBoxLayout();loopRoot->addLayout(loopTop);
    struct LoopUi{QGroupBox*box;QDoubleSpinBox*x,*y,*z,*r,*wire,*az,*el;QSpinBox*turns;};
    auto makeLoop=[&](const QString &name,double z0)->LoopUi{LoopUi u{};u.box=new QGroupBox(name,loopContent);auto*f=new QFormLayout(u.box);u.x=dspin(u.box,0,-1000,1000,6);u.y=dspin(u.box,0,-1000,1000,6);u.z=dspin(u.box,z0,-1000,1000,6);u.r=dspin(u.box,50,0.001,1e6,4);u.wire=dspin(u.box,0.5,0.001,1e6,4);u.turns=new QSpinBox(u.box);u.turns->setRange(1,100000);u.turns->setValue(1);u.az=dspin(u.box,0,-3600,3600,3);u.el=dspin(u.box,90,-3600,3600,3);f->addRow(QStringLiteral("Center X (m)"),u.x);f->addRow(QStringLiteral("Center Y (m)"),u.y);f->addRow(QStringLiteral("Center Z (m)"),u.z);f->addRow(QStringLiteral("Loop radius (mm)"),u.r);f->addRow(QStringLiteral("Wire radius (mm)"),u.wire);f->addRow(QStringLiteral("Turns"),u.turns);f->addRow(QStringLiteral("Axis azimuth (deg)"),u.az);f->addRow(QStringLiteral("Axis elevation (deg)"),u.el);return u;};
    const auto l1=makeLoop(QStringLiteral("Primary circular coil"),0.0),l2=makeLoop(QStringLiteral("Secondary circular coil"),0.03);loopTop->addWidget(l1.box);loopTop->addWidget(l2.box);
    auto *loopSettings=new QGroupBox(QStringLiteral("Neumann integration / magnetic medium"),loopContent);auto *lsf=new QFormLayout(loopSettings);auto *ln=new QSpinBox(loopSettings);ln->setRange(24,500);ln->setValue(120);auto *loopMedium=new QComboBox(loopSettings);loopMedium->addItems({QStringLiteral("Custom effective μr"),QStringLiteral("Air / vacuum"),QStringLiteral("Ferrite-like effective medium (μr=100)"),QStringLiteral("Ferrite-like effective medium (μr=1000)"),QStringLiteral("High-μ effective medium (μr=5000)")});auto *loopMu=dspin(loopSettings,1.0,0.000001,1e6,6);auto *loopSolve=new QPushButton(QStringLiteral("Calculate geometric coupling"),loopSettings);auto *lm=output(loopSettings),*ll1=output(loopSettings),*ll2=output(loopSettings),*lk=output(loopSettings),*ld=output(loopSettings),*lnote=output(loopSettings);lsf->addRow(QStringLiteral("Segments / loop"),ln);lsf->addRow(QStringLiteral("Magnetic-medium preset"),loopMedium);lsf->addRow(QStringLiteral("Effective / uniform μr"),loopMu);lsf->addRow(loopSolve);lsf->addRow(QStringLiteral("Mutual M"),lm);lsf->addRow(QStringLiteral("Self L1"),ll1);lsf->addRow(QStringLiteral("Self L2"),ll2);lsf->addRow(QStringLiteral("k = M/√(L1L2)"),lk);lsf->addRow(QStringLiteral("Minimum filament distance"),ld);lsf->addRow(lnote);loopTop->addWidget(loopSettings,1);
    auto *loopSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::GeometricCoilCoupling,loopContent);loopSketch->setMinimumHeight(190);loopRoot->addWidget(loopSketch);
    auto runLoop=[=]{NumericalEM::LoopCouplingInput in;in.integrationSegments=ln->value();in.relativePermeability=loopMu->value();loopSketch->setVariant(loopMu->value()>1.000001?1:0);loopSketch->setValue(QStringLiteral("mu"),QString::number(loopMu->value(),'g',5));auto fill=[](NumericalEM::CircularLoopGeometry &g,const LoopUi &u){g.center={u.x->value(),u.y->value(),u.z->value()};g.radiusM=u.r->value()*1e-3;g.wireRadiusM=u.wire->value()*1e-3;g.turns=u.turns->value();g.axis=NumericalEM::axisFromAzimuthElevationDeg(u.az->value(),u.el->value());};fill(in.primary,l1);fill(in.secondary,l2);const auto r=NumericalEM::solveCircularLoopCoupling(in);if(!r.valid){lm->setText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(r.error)));return;}lm->setText(eng(r.mutualInductanceH,QStringLiteral("H")));ll1->setText(eng(r.primarySelfInductanceH,QStringLiteral("H")));ll2->setText(eng(r.secondarySelfInductanceH,QStringLiteral("H")));lk->setText(QStringLiteral("%1 (raw %2)").arg(r.couplingCoefficient,0,'g',7).arg(r.rawCouplingCoefficient,0,'g',7));ld->setText(eng(r.minimumSegmentDistanceM,QStringLiteral("m")));lnote->setText(QString::fromStdString(r.note));};
    QObject::connect(loopSolve,&QPushButton::clicked,loopContent,runLoop);
    QObject::connect(loopMedium,qOverload<int>(&QComboBox::currentIndexChanged),loopContent,[=](int i){static const double mu[]={1,1,100,1000,5000};if(i>0)loopMu->setValue(mu[i]);runLoop();});
    for(auto *sp:{l1.x,l1.y,l1.z,l1.r,l1.wire,l1.az,l1.el,l2.x,l2.y,l2.z,l2.r,l2.wire,l2.az,l2.el,loopMu})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),loopContent,[=](double){runLoop();});
    for(auto *ip:{l1.turns,l2.turns,ln})QObject::connect(ip,qOverload<int>(&QSpinBox::valueChanged),loopContent,[=](int){runLoop();});
    runLoop();
    auto *loopExplain=new QLabel(QStringLiteral("This tab computes M from actual relative position/orientation instead of asking for k. μr=1 is the air-core Neumann result. A value μr>1 applies a homogeneous/effective magnetic-medium scaling to M, L1 and L2; it is not a localized ferrite-core field solution and therefore does not model fringing, gaps or saturation."),loopContent);loopExplain->setWordWrap(true);loopRoot->addWidget(loopExplain);loopRoot->addStretch(1);tabs->addTab(scrollPage(loopContent,tabs),QStringLiteral("Geometric coil coupling"));
}
