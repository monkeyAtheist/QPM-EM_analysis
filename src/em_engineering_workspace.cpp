#include "widgets/em_engineering_workspace.h"

#include "em_engineering_model.h"
#include "widgets/antenna_designer_widget.h"
#include "widgets/field_profile_plot.h"
#include "widgets/engineering_sketch_widget.h"
#include "widgets/numerical_solvers_workspace.h"
#include "widgets/rf_smith_chart_widget.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <memory>
#include <limits>
#include <utility>

namespace
{
QDoubleSpinBox *spin(QWidget *parent, double value, double min, double max, int decimals = 6)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(decimals);
    s->setRange(min, max);
    s->setValue(value);
    s->setKeyboardTracking(false);
    return s;
}

QLabel *valueLabel(QWidget *parent)
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
    struct P { double scale; const char *s; };
    static const P p[] = {{1e12,"T"},{1e9,"G"},{1e6,"M"},{1e3,"k"},{1,""},{1e-3,"m"},{1e-6,"u"},{1e-9,"n"},{1e-12,"p"}};
    for (const auto &x : p)
        if (std::abs(value) >= x.scale*0.999 || x.scale == 1e-12)
            return QStringLiteral("%1 %2%3").arg(value/x.scale,0,'g',6).arg(QString::fromLatin1(x.s), unit);
    return QStringLiteral("%1 %2").arg(value,0,'g',6).arg(unit);
}

QString vec(const EmVec3 &v, const QString &unit)
{
    return QStringLiteral("(%1, %2, %3) %4")
        .arg(v.x,0,'g',6).arg(v.y,0,'g',6).arg(v.z,0,'g',6).arg(unit);
}

QString complexText(const std::complex<double> &z, const QString &unit = QStringLiteral("Ω"))
{
    return QStringLiteral("%1 %2 j%3 %4")
        .arg(z.real(),0,'g',7)
        .arg(z.imag() >= 0.0 ? QStringLiteral("+") : QStringLiteral("−"))
        .arg(std::abs(z.imag()),0,'g',7)
        .arg(unit);
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

EmVec3 axisFromCombo(int i)
{
    switch (i) { case 1: return {0,1,0}; case 2: return {0,0,1}; default: return {1,0,0}; }
}

QString antennaTypeName(EmEngineering::AntennaType t)
{
    using T=EmEngineering::AntennaType;
    switch(t)
    {
    case T::HertzianDipole: return QStringLiteral("Hertzian dipole");
    case T::ShortCenterFedDipole: return QStringLiteral("Short center-fed dipole");
    case T::ThinHalfWaveDipole: return QStringLiteral("Thin half-wave dipole");
    case T::QuarterWaveMonopole: return QStringLiteral("Quarter-wave monopole");
    case T::FoldedHalfWaveDipole: return QStringLiteral("Folded half-wave dipole");
    case T::SmallCircularLoop: return QStringLiteral("Small circular loop");
    }
    return QStringLiteral("Antenna");
}

struct ImportedAntennaSweep
{
    QVector<double> frequencyHz;
    QVector<double> resistanceOhm;
    QVector<double> reactanceOhm;
    double referenceOhm = 50.0;
    QString feedName;
    bool valid() const
    {
        return frequencyHz.size() >= 2 && frequencyHz.size() == resistanceOhm.size() &&
               frequencyHz.size() == reactanceOhm.size();
    }
};

std::complex<double> interpolateAntennaSweep(const ImportedAntennaSweep &sweep,double frequencyHz,bool *ok=nullptr)
{
    if(ok)*ok=false;
    if(!sweep.valid() || frequencyHz<sweep.frequencyHz.front() || frequencyHz>sweep.frequencyHz.back())
        return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
    auto it=std::lower_bound(sweep.frequencyHz.begin(),sweep.frequencyHz.end(),frequencyHz);
    if(it==sweep.frequencyHz.begin()){if(ok)*ok=true;return {sweep.resistanceOhm.front(),sweep.reactanceOhm.front()};}
    if(it==sweep.frequencyHz.end()){if(ok)*ok=true;return {sweep.resistanceOhm.back(),sweep.reactanceOhm.back()};}
    const int hi=int(it-sweep.frequencyHz.begin()),lo=hi-1;
    const double f0=sweep.frequencyHz[lo],f1=sweep.frequencyHz[hi];
    const double a=(frequencyHz-f0)/std::max(1e-30,f1-f0);
    if(ok)*ok=true;
    return {sweep.resistanceOhm[lo]+a*(sweep.resistanceOhm[hi]-sweep.resistanceOhm[lo]),
            sweep.reactanceOhm[lo]+a*(sweep.reactanceOhm[hi]-sweep.reactanceOhm[lo])};
}

QString reactiveComponentText(EmEngineering::ReactiveComponentType type,double value)
{
    using T=EmEngineering::ReactiveComponentType;
    if(type==T::Inductor)return QStringLiteral("L = %1").arg(eng(value,QStringLiteral("H")));
    if(type==T::Capacitor)return QStringLiteral("C = %1").arg(eng(value,QStringLiteral("F")));
    return QStringLiteral("none");
}
}

ElectromagneticEngineeringWorkspace::ElectromagneticEngineeringWorkspace(AntennaDesignerWidget *antennaDesigner, QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    // Shared advanced-loss presets. The dedicated Core / winding loss page
    // updates these lightweight engineering models; RF L/C, coupled-coil and
    // BALUN tools can reuse them without triggering a heavy field solve.
    auto sharedCoreLossModel = std::make_shared<EmEngineering::CoreLossInput>();
    auto sharedWindingLossModel = std::make_shared<EmEngineering::WindingLossInput>();

    // ------------------------------------------------------------------
    // Electromagnetic fields
    // ------------------------------------------------------------------
    auto *emTabs = new QTabWidget(tabs);
    tabs->addTab(emTabs, QStringLiteral("EM fields"));

    auto *planeContent = new QWidget(emTabs);
    auto *planeRoot = new QVBoxLayout(planeContent);
    auto *planeTop = new QHBoxLayout();
    auto *pwIn = new QGroupBox(QStringLiteral("Lossless plane wave"), planeContent);
    auto *pwi = new QFormLayout(pwIn);
    // Leave explicit breathing room around the form. With a large UI font the final
    // action row otherwise sits almost on top of the QGroupBox bottom frame.
    pwi->setContentsMargins(16, 18, 16, 16);
    pwi->setVerticalSpacing(6);
    auto *pwF = spin(pwIn, 100.0, 0.000001, 1e9, 6);
    auto *pwE = spin(pwIn, 1.0, 0.0, 1e12, 6);
    auto *pwEr = spin(pwIn, 1.0, 0.000001, 1e6, 6);
    auto *pwMr = spin(pwIn, 1.0, 0.000001, 1e6, 6);
    auto *pwPhase = spin(pwIn, 0.0, -3600, 3600, 3);
    auto *pwTime = spin(pwIn, 0.0, -1e9, 1e9, 6);
    auto *pwK = new QComboBox(pwIn); pwK->addItems({QStringLiteral("+X"),QStringLiteral("+Y"),QStringLiteral("+Z")});
    auto *pwPol = new QComboBox(pwIn); pwPol->addItems({QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")}); pwPol->setCurrentIndex(1);
    auto *pwX = spin(pwIn, 0,-1e6,1e6,6), *pwY=spin(pwIn,0,-1e6,1e6,6), *pwZ=spin(pwIn,0,-1e6,1e6,6);
    pwi->addRow(QStringLiteral("Frequency (MHz)"), pwF);
    pwi->addRow(QStringLiteral("Peak E0 (V/m)"), pwE);
    pwi->addRow(QStringLiteral("εr"), pwEr); pwi->addRow(QStringLiteral("μr"), pwMr);
    pwi->addRow(QStringLiteral("Propagation"), pwK); pwi->addRow(QStringLiteral("Requested polarization"), pwPol);
    pwi->addRow(QStringLiteral("Phase (deg)"), pwPhase); pwi->addRow(QStringLiteral("Time (ns)"), pwTime);
    pwi->addRow(QStringLiteral("Point x (m)"), pwX); pwi->addRow(QStringLiteral("Point y (m)"), pwY); pwi->addRow(QStringLiteral("Point z (m)"), pwZ);
    auto *pwCalc = new QPushButton(QStringLiteral("Calculate / refresh profile"), pwIn);
    auto *pwCalcRow = new QWidget(pwIn);
    auto *pwCalcLayout = new QVBoxLayout(pwCalcRow);
    pwCalcLayout->setContentsMargins(0, 4, 0, 6);
    pwCalcLayout->addWidget(pwCalc);
    pwi->addRow(pwCalcRow);
    planeTop->addWidget(pwIn);

    auto *pwOut = new QGroupBox(QStringLiteral("Wave quantities at point"), planeContent);
    auto *pwo = new QFormLayout(pwOut);
    auto *pwLambda=valueLabel(pwOut), *pwSpeed=valueLabel(pwOut), *pwEta=valueLabel(pwOut), *pwBeta=valueLabel(pwOut);
    auto *pwEv=valueLabel(pwOut), *pwHv=valueLabel(pwOut), *pwBv=valueLabel(pwOut), *pwS=valueLabel(pwOut), *pwSav=valueLabel(pwOut), *pwNote=valueLabel(pwOut);
    pwo->addRow(QStringLiteral("λ"),pwLambda); pwo->addRow(QStringLiteral("v"),pwSpeed); pwo->addRow(QStringLiteral("η"),pwEta); pwo->addRow(QStringLiteral("β"),pwBeta);
    pwo->addRow(QStringLiteral("E(r,t)"),pwEv); pwo->addRow(QStringLiteral("H(r,t)"),pwHv); pwo->addRow(QStringLiteral("B(r,t)"),pwBv);
    pwo->addRow(QStringLiteral("S instantaneous"),pwS); pwo->addRow(QStringLiteral("<S>"),pwSav); pwo->addRow(pwNote);
    planeTop->addWidget(pwOut,1);
    auto *pwSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::PlaneWave,planeContent);pwSketch->setMinimumWidth(290);planeTop->addWidget(pwSketch);
    planeRoot->addLayout(planeTop);
    auto *pwPlot = new FieldProfilePlot(planeContent); planeRoot->addWidget(pwPlot,1);

    auto refreshPlane = [=] {
        EmEngineering::PlaneWaveInput in;
        in.frequencyHz = pwF->value()*1e6; in.electricAmplitudeVpm=pwE->value(); in.epsilonR=pwEr->value(); in.muR=pwMr->value();
        in.phaseDeg=pwPhase->value(); in.timeSeconds=pwTime->value()*1e-9; in.propagation=axisFromCombo(pwK->currentIndex()); in.polarization=axisFromCombo(pwPol->currentIndex());
        in.point={pwX->value(),pwY->value(),pwZ->value()};
        const auto r=EmEngineering::planeWave(in);
        pwLambda->setText(eng(r.wavelength,QStringLiteral("m"))); pwSpeed->setText(eng(r.waveSpeed,QStringLiteral("m/s"))); pwEta->setText(eng(r.impedanceOhm,QStringLiteral("Ω"))); pwBeta->setText(eng(r.betaRadPerMeter,QStringLiteral("rad/m")));
        pwEv->setText(vec(r.electricField,QStringLiteral("V/m"))); pwHv->setText(vec(r.magneticField,QStringLiteral("A/m"))); pwBv->setText(vec(r.magneticFluxDensity,QStringLiteral("T")));
        pwS->setText(vec(r.poyntingInstant,QStringLiteral("W/m²"))); pwSav->setText(vec(r.poyntingAverage,QStringLiteral("W/m²"))); pwNote->setText(r.note);
        pwSketch->setValue(QStringLiteral("lambda"),eng(r.wavelength,QStringLiteral("m")));

        QVector<double> x,e,h;
        const int n=260; x.reserve(n); e.reserve(n); h.reserve(n);
        const EmVec3 k=emNormalized(in.propagation);
        EmVec3 requested=in.polarization - k*emDot(in.polarization,k);
        if(requested.norm()<1e-12) {
            const EmVec3 ref = std::abs(k.z) < 0.8 ? EmVec3{0,0,1} : EmVec3{0,1,0};
            requested=emCross(ref,k);
        }
        const EmVec3 ep=emNormalized(requested);
        const EmVec3 hp=emNormalized(emCross(k,ep));
        for(int i=0;i<n;++i){ double d=2.0*r.wavelength*double(i)/(n-1); auto sample=in; sample.point=k*d; const auto rr=EmEngineering::planeWave(sample); x.push_back(d); e.push_back(emDot(rr.electricField,ep)); h.push_back(emDot(rr.magneticField,hp)); }
        FieldProfileSeries se{x,e,QStringLiteral("E projection"),QStringLiteral("V/m"),false};
        FieldProfileSeries sh{x,h,QStringLiteral("H projection"),QStringLiteral("A/m"),false};
        pwPlot->setSeries({se,sh},QStringLiteral("Plane-wave spatial profile at selected time (2 λ)"));
    };
    QObject::connect(pwCalc,&QPushButton::clicked,planeContent,refreshPlane);
    for(auto *sp:{pwF,pwE,pwEr,pwMr,pwPhase,pwTime,pwX,pwY,pwZ}) QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),planeContent,[=](double){refreshPlane();});
    QObject::connect(pwK,qOverload<int>(&QComboBox::currentIndexChanged),planeContent,[=](int){refreshPlane();});
    QObject::connect(pwPol,qOverload<int>(&QComboBox::currentIndexChanged),planeContent,[=](int){refreshPlane();});
    refreshPlane();
    emTabs->addTab(planeContent,QStringLiteral("Plane wave"));

    auto *dipContent = new QWidget(emTabs); auto *dipRoot=new QVBoxLayout(dipContent); auto *dipTop=new QHBoxLayout();
    auto *dipIn=new QGroupBox(QStringLiteral("Hertzian dipole far field"),dipContent); auto *di=new QFormLayout(dipIn);
    auto *df=spin(dipIn,100,0.000001,1e9,6), *dI=spin(dipIn,1,-1e9,1e9,6), *dl=spin(dipIn,0.05,1e-9,1e6,6), *dPhase=spin(dipIn,0,-3600,3600,3);
    auto *dx=spin(dipIn,5,-1e9,1e9,6), *dy=spin(dipIn,0,-1e9,1e9,6), *dz=spin(dipIn,0,-1e9,1e9,6), *dt=spin(dipIn,0,-1e9,1e9,6);
    auto *dAxis=new QComboBox(dipIn); dAxis->addItems({QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")}); dAxis->setCurrentIndex(2);
    di->addRow(QStringLiteral("Frequency (MHz)"),df); di->addRow(QStringLiteral("Peak current (A)"),dI); di->addRow(QStringLiteral("Element length (m)"),dl); di->addRow(QStringLiteral("Axis"),dAxis); di->addRow(QStringLiteral("Phase (deg)"),dPhase); di->addRow(QStringLiteral("Time (ns)"),dt); di->addRow(QStringLiteral("Observation x (m)"),dx); di->addRow(QStringLiteral("Observation y (m)"),dy); di->addRow(QStringLiteral("Observation z (m)"),dz);
    auto *dcalc=new QPushButton(QStringLiteral("Calculate"),dipIn);di->addRow(dcalc); dipTop->addWidget(dipIn);
    auto *dipOut=new QGroupBox(QStringLiteral("Radiation-zone result"),dipContent);auto *doo=new QFormLayout(dipOut);
    auto *dLam=valueLabel(dipOut),*dR=valueLabel(dipOut),*dTheta=valueLabel(dipOut),*dRr=valueLabel(dipOut),*dEo=valueLabel(dipOut),*dHo=valueLabel(dipOut),*dBo=valueLabel(dipOut),*dSo=valueLabel(dipOut),*dNote=valueLabel(dipOut);
    doo->addRow(QStringLiteral("λ"),dLam);doo->addRow(QStringLiteral("r"),dR);doo->addRow(QStringLiteral("θ"),dTheta);doo->addRow(QStringLiteral("Radiation resistance"),dRr);doo->addRow(QStringLiteral("E far"),dEo);doo->addRow(QStringLiteral("H far"),dHo);doo->addRow(QStringLiteral("B far"),dBo);doo->addRow(QStringLiteral("<S>"),dSo);doo->addRow(dNote);dipTop->addWidget(dipOut,1);dipRoot->addLayout(dipTop);
    auto *dipSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::HertzianDipole,dipContent);dipSketch->setMinimumHeight(260);dipSketch->setMaximumHeight(350);dipRoot->addWidget(dipSketch);
    auto refreshDip=[=]{EmEngineering::HertzianDipoleInput in;in.frequencyHz=df->value()*1e6;in.currentAmplitudeA=dI->value();in.elementLengthM=dl->value();in.axis=axisFromCombo(dAxis->currentIndex());in.phaseDeg=dPhase->value();in.timeSeconds=dt->value()*1e-9;in.point={dx->value(),dy->value(),dz->value()};auto r=EmEngineering::hertzianDipoleFarField(in);dLam->setText(eng(r.wavelength,QStringLiteral("m")));dR->setText(eng(r.distanceM,QStringLiteral("m")));dTheta->setText(QStringLiteral("%1°").arg(r.thetaDeg,0,'g',6));dRr->setText(eng(r.radiationResistanceOhm,QStringLiteral("Ω")));dEo->setText(vec(r.electricFieldFar,QStringLiteral("V/m")));dHo->setText(vec(r.magneticFieldFar,QStringLiteral("A/m")));dBo->setText(vec(r.magneticFluxDensityFar,QStringLiteral("T")));dSo->setText(vec(r.poyntingAverage,QStringLiteral("W/m²")));dNote->setText(r.note);dipSketch->setValue(QStringLiteral("length"),eng(in.elementLengthM,QStringLiteral("m")));};
    QObject::connect(dcalc,&QPushButton::clicked,dipContent,refreshDip);
    for(auto *sp:{df,dI,dl,dPhase,dx,dy,dz,dt}) QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),dipContent,[=](double){refreshDip();});
    QObject::connect(dAxis,qOverload<int>(&QComboBox::currentIndexChanged),dipContent,[=](int){refreshDip();});
    refreshDip(); emTabs->addTab(dipContent,QStringLiteral("Hertzian radiator"));

    // ------------------------------------------------------------------
    // Antennas
    // ------------------------------------------------------------------
    auto *antContent=new QWidget(tabs);auto *antRoot=new QHBoxLayout(antContent);
    auto *antIn=new QGroupBox(QStringLiteral("Simple antenna geometry"),antContent);auto *ai=new QFormLayout(antIn);
    auto *atype=new QComboBox(antIn);for(int i=0;i<=int(EmEngineering::AntennaType::SmallCircularLoop);++i)atype->addItem(antennaTypeName(static_cast<EmEngineering::AntennaType>(i)));atype->setCurrentIndex(int(EmEngineering::AntennaType::ThinHalfWaveDipole));
    auto *af=spin(antIn,100,0.000001,1e9,6),*alen=spin(antIn,1.425,1e-9,1e9,6),*awr=spin(antIn,1,0.000001,1e6,6),*alr=spin(antIn,0.1,1e-9,1e9,6),*az0=spin(antIn,50,0.001,1e6,6);auto *aturns=new QSpinBox(antIn);aturns->setRange(1,1000000);aturns->setValue(1);
    ai->addRow(QStringLiteral("Model"),atype);ai->addRow(QStringLiteral("Frequency (MHz)"),af);ai->addRow(QStringLiteral("Element length (m)"),alen);ai->addRow(QStringLiteral("Wire radius (mm)"),awr);ai->addRow(QStringLiteral("Loop radius (m)"),alr);ai->addRow(QStringLiteral("Loop turns"),aturns);ai->addRow(QStringLiteral("Feed-line Z0 (Ω)"),az0);auto *acalc=new QPushButton(QStringLiteral("Estimate impedance"),antIn);ai->addRow(acalc);antRoot->addWidget(antIn);
    auto *antOut=new QGroupBox(QStringLiteral("Engineering estimate"),antContent);auto *ao=new QFormLayout(antOut);auto *aLam=valueLabel(antOut),*aEl=valueLabel(antOut),*aRr=valueLabel(antOut),*aZ=valueLabel(antOut),*aGam=valueLabel(antOut),*aRL=valueLabel(antOut),*aVswr=valueLabel(antOut),*aRec=valueLabel(antOut),*aVal=valueLabel(antOut);ao->addRow(QStringLiteral("λ"),aLam);ao->addRow(QStringLiteral("L/λ"),aEl);ao->addRow(QStringLiteral("Radiation R"),aRr);ao->addRow(QStringLiteral("Estimated Zin"),aZ);ao->addRow(QStringLiteral("|Γ|"),aGam);ao->addRow(QStringLiteral("Return loss"),aRL);ao->addRow(QStringLiteral("VSWR"),aVswr);ao->addRow(QStringLiteral("Reference/resonant length"),aRec);ao->addRow(aVal);antRoot->addWidget(antOut,1);
    auto refreshAnt=[=]{EmEngineering::AntennaInput in;in.type=static_cast<EmEngineering::AntennaType>(atype->currentIndex());in.frequencyHz=af->value()*1e6;in.lengthM=alen->value();in.wireRadiusM=awr->value()*1e-3;in.loopRadiusM=alr->value();in.turns=aturns->value();in.feedLineOhm=az0->value();auto r=EmEngineering::antennaEstimate(in);aLam->setText(eng(r.wavelengthM,QStringLiteral("m")));aEl->setText(QString::number(r.electricalLengthLambda,'g',7));aRr->setText(eng(r.radiationResistanceOhm,QStringLiteral("Ω")));aZ->setText(r.reactanceDefined?QStringLiteral("%1 %2 j%3 Ω").arg(r.resistanceOhm,0,'g',6).arg(r.reactanceOhm>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(r.reactanceOhm),0,'g',6):QStringLiteral("R ≈ %1 Ω; X not estimated").arg(r.resistanceOhm,0,'g',6));aGam->setText(QString::number(r.reflectionMagnitude,'g',6));aRL->setText(QStringLiteral("%1 dB").arg(r.returnLossDb,0,'g',6));aVswr->setText(QString::number(r.vswr,'g',6));aRec->setText(eng(r.recommendedLengthM,QStringLiteral("m")));aVal->setText(r.validity);};QObject::connect(acalc,&QPushButton::clicked,antContent,refreshAnt);
    for(auto *sp:{af,alen,awr,alr,az0}) QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),antContent,[=](double){refreshAnt();});
    QObject::connect(aturns,qOverload<int>(&QSpinBox::valueChanged),antContent,[=](int){refreshAnt();});
    QObject::connect(atype,qOverload<int>(&QComboBox::currentIndexChanged),antContent,[=](int){refreshAnt();});
    refreshAnt();tabs->addTab(antContent,QStringLiteral("Antennas / impedance"));

    // ------------------------------------------------------------------
    // RF link budget / Friis calculator
    // ------------------------------------------------------------------
    auto *linkContent = new QWidget(tabs);
    auto *linkRoot = new QVBoxLayout(linkContent);
    auto *linkTop = new QHBoxLayout();
    auto *linkIn = new QGroupBox(QStringLiteral("Link budget inputs"), linkContent);
    auto *li = new QFormLayout(linkIn);
    auto *lf = spin(linkIn, 868.0, 0.001, 1e9, 6);
    auto *ld = spin(linkIn, 1.0, 1e-9, 1e9, 6);
    auto *lPt = spin(linkIn, 20.0, -300.0, 300.0, 3);
    auto *lGt = spin(linkIn, 2.15, -100.0, 100.0, 3);
    auto *lGr = spin(linkIn, 2.15, -100.0, 100.0, 3);
    auto *lTxLoss = spin(linkIn, 1.0, 0.0, 300.0, 3);
    auto *lRxLoss = spin(linkIn, 1.0, 0.0, 300.0, 3);
    auto *lMiscLoss = spin(linkIn, 0.0, 0.0, 300.0, 3);
    auto *lBandwidth = spin(linkIn, 125000.0, 1.0, 1e12, 1);
    auto *lNoiseFigure = spin(linkIn, 5.0, 0.0, 100.0, 3);
    auto *lSensitivity = spin(linkIn, -100.0, -300.0, 100.0, 3);
    li->addRow(QStringLiteral("Frequency (MHz)"), lf);
    li->addRow(QStringLiteral("Distance (km)"), ld);
    li->addRow(QStringLiteral("TX power (dBm)"), lPt);
    li->addRow(QStringLiteral("TX antenna gain (dBi)"), lGt);
    li->addRow(QStringLiteral("RX antenna gain (dBi)"), lGr);
    li->addRow(QStringLiteral("TX cable/losses (dB)"), lTxLoss);
    li->addRow(QStringLiteral("RX cable/losses (dB)"), lRxLoss);
    li->addRow(QStringLiteral("Other path losses (dB)"), lMiscLoss);
    li->addRow(QStringLiteral("Noise bandwidth (Hz)"), lBandwidth);
    li->addRow(QStringLiteral("Receiver NF (dB)"), lNoiseFigure);
    li->addRow(QStringLiteral("Receiver sensitivity (dBm)"), lSensitivity);
    auto *linkCalc = new QPushButton(QStringLiteral("Calculate link budget"), linkIn);
    li->addRow(linkCalc);
    linkTop->addWidget(linkIn);

    auto *linkOut = new QGroupBox(QStringLiteral("Link budget results"), linkContent);
    auto *lo = new QFormLayout(linkOut);
    auto *lLambda=valueLabel(linkOut), *lFspl=valueLabel(linkOut), *lEirp=valueLabel(linkOut), *lPr=valueLabel(linkOut);
    auto *lNoiseDensity=valueLabel(linkOut), *lNoiseFloor=valueLabel(linkOut), *lSnr=valueLabel(linkOut), *lMargin=valueLabel(linkOut), *lNote=valueLabel(linkOut);
    lo->addRow(QStringLiteral("Wavelength λ"), lLambda);
    lo->addRow(QStringLiteral("Free-space path loss"), lFspl);
    lo->addRow(QStringLiteral("EIRP"), lEirp);
    lo->addRow(QStringLiteral("Received power"), lPr);
    lo->addRow(QStringLiteral("Thermal noise density @ 290 K"), lNoiseDensity);
    lo->addRow(QStringLiteral("Receiver noise floor"), lNoiseFloor);
    lo->addRow(QStringLiteral("Available SNR"), lSnr);
    lo->addRow(QStringLiteral("Link margin vs sensitivity"), lMargin);
    lo->addRow(lNote);
    linkTop->addWidget(linkOut, 1);
    linkRoot->addLayout(linkTop);
    auto *linkSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::LinkBudget,linkContent);
    linkSketch->setMinimumHeight(245);
    linkSketch->setMaximumHeight(340);
    linkRoot->addWidget(linkSketch);

    auto refreshLink = [=] {
        const double fHz = std::max(1.0, lf->value() * 1e6);
        const double distanceM = std::max(1e-9, ld->value() * 1000.0);
        const double lambda = EmEngineering::C0 / fHz;
        const double fsplDb = 20.0 * std::log10(4.0 * EmEngineering::Pi * distanceM / lambda);
        const double eirpDbm = lPt->value() - lTxLoss->value() + lGt->value();
        const double receivedDbm = eirpDbm - fsplDb - lMiscLoss->value() + lGr->value() - lRxLoss->value();
        const double thermalDensityDbmHz = -173.975;
        const double noiseFloorDbm = thermalDensityDbmHz + 10.0 * std::log10(std::max(1.0, lBandwidth->value())) + lNoiseFigure->value();
        const double snrDb = receivedDbm - noiseFloorDbm;
        const double marginDb = receivedDbm - lSensitivity->value();
        lLambda->setText(eng(lambda, QStringLiteral("m")));
        lFspl->setText(QStringLiteral("%1 dB").arg(fsplDb,0,'f',3));
        lEirp->setText(QStringLiteral("%1 dBm").arg(eirpDbm,0,'f',3));
        lPr->setText(QStringLiteral("%1 dBm").arg(receivedDbm,0,'f',3));
        lNoiseDensity->setText(QStringLiteral("%1 dBm/Hz").arg(thermalDensityDbmHz,0,'f',3));
        lNoiseFloor->setText(QStringLiteral("%1 dBm").arg(noiseFloorDbm,0,'f',3));
        lSnr->setText(QStringLiteral("%1 dB").arg(snrDb,0,'f',3));
        lMargin->setText(QStringLiteral("%1 dB").arg(marginDb,0,'f',3));
        linkSketch->setValue(QStringLiteral("distance"),QStringLiteral("%1 km").arg(ld->value(),0,'g',5));
        lNote->setText(QStringLiteral("Friis/free-space far-field budget. Put polarization mismatch, atmospheric, obstacle, fading and implementation losses in the explicit loss fields as appropriate."));
    };
    QObject::connect(linkCalc, &QPushButton::clicked, linkContent, refreshLink);
    for (auto *s : {lf,ld,lPt,lGt,lGr,lTxLoss,lRxLoss,lMiscLoss,lBandwidth,lNoiseFigure,lSensitivity})
        QObject::connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), linkContent, [=](double){ refreshLink(); });
    refreshLink();
    tabs->addTab(linkContent, QStringLiteral("Link budget"));

    // ------------------------------------------------------------------
    // Inductance and solenoids
    // ------------------------------------------------------------------
    auto *indContent=new QWidget(tabs);auto *indRoot=new QVBoxLayout(indContent);auto *indTop=new QHBoxLayout();
    auto *solIn=new QGroupBox(QStringLiteral("Detailed solenoid"),indContent);auto *si=new QFormLayout(solIn);auto *sN=new QSpinBox(solIn);sN->setRange(1,10000000);sN->setValue(200);auto *sR=spin(solIn,20,0.001,1e9,6),*sL=spin(solIn,80,0.001,1e9,6),*sWd=spin(solIn,0.8,0.000001,1e6,6),*sRb=spin(solIn,0.8,0.000001,1e6,6),*sMur=spin(solIn,1,0.000001,1e6,6),*sI=spin(solIn,1,-1e9,1e9,6);si->addRow(QStringLiteral("Turns N"),sN);si->addRow(QStringLiteral("Mean radius (mm)"),sR);si->addRow(QStringLiteral("Winding length (mm)"),sL);si->addRow(QStringLiteral("Wire diameter (mm)"),sWd);si->addRow(QStringLiteral("Radial build (mm)"),sRb);si->addRow(QStringLiteral("Core effective μr"),sMur);si->addRow(QStringLiteral("Current (A)"),sI);auto *scalc=new QPushButton(QStringLiteral("Calculate coil"),solIn);si->addRow(scalc);indTop->addWidget(solIn);
    auto *solOut=new QGroupBox(QStringLiteral("Solenoid results"),indContent);auto *so=new QFormLayout(solOut);auto *sLi=valueLabel(solOut),*sLw=valueLabel(solOut),*sLrec=valueLabel(solOut),*sWire=valueLabel(solOut),*sRes=valueLabel(solOut),*sB=valueLabel(solOut),*sEn=valueLabel(solOut),*sPitch=valueLabel(solOut),*sNote=valueLabel(solOut);so->addRow(QStringLiteral("Ideal μN²A/l"),sLi);so->addRow(QStringLiteral("Wheeler air-core"),sLw);so->addRow(QStringLiteral("Recommended L"),sLrec);so->addRow(QStringLiteral("Wire length"),sWire);so->addRow(QStringLiteral("Copper R @20°C"),sRes);so->addRow(QStringLiteral("B center estimate"),sB);so->addRow(QStringLiteral("Stored energy"),sEn);so->addRow(QStringLiteral("Turn pitch"),sPitch);so->addRow(sNote);indTop->addWidget(solOut,1);indRoot->addLayout(indTop);
    auto *loopGroup=new QGroupBox(QStringLiteral("Circular loop inductance"),indContent);auto *lg=new QFormLayout(loopGroup);auto *lr=spin(loopGroup,100,0.001,1e9,6),*lwr=spin(loopGroup,1,0.000001,1e6,6);auto *ln=new QSpinBox(loopGroup);ln->setRange(1,100000);ln->setValue(1);auto *lout=valueLabel(loopGroup);lg->addRow(QStringLiteral("Loop radius (mm)"),lr);lg->addRow(QStringLiteral("Wire radius (mm)"),lwr);lg->addRow(QStringLiteral("Turns"),ln);lg->addRow(QStringLiteral("L"),lout);indRoot->addWidget(loopGroup);
    auto *solSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::Solenoid,indContent);
    solSketch->setMinimumHeight(285);
    solSketch->setMaximumHeight(380);
    indRoot->addWidget(solSketch);
    auto lastL = std::make_shared<double>(0.0);
    auto refreshSol=[=]{EmEngineering::SolenoidInput in;in.turns=sN->value();in.radiusM=sR->value()*1e-3;in.lengthM=sL->value()*1e-3;in.wireDiameterM=sWd->value()*1e-3;in.radialBuildM=sRb->value()*1e-3;in.relativePermeability=sMur->value();in.currentA=sI->value();auto r=EmEngineering::solenoid(in);*lastL=r.recommendedInductanceH;solSketch->setValues({{QStringLiteral("radius"),QStringLiteral("%1 mm").arg(sR->value(),0,'g',5)},{QStringLiteral("length"),QStringLiteral("%1 mm").arg(sL->value(),0,'g',5)}});sLi->setText(eng(r.idealInductanceH,QStringLiteral("H")));sLw->setText(eng(r.wheelerAirCoreInductanceH,QStringLiteral("H")));sLrec->setText(eng(r.recommendedInductanceH,QStringLiteral("H")));sWire->setText(eng(r.wireLengthM,QStringLiteral("m")));sRes->setText(eng(r.wireResistanceOhm,QStringLiteral("Ω")));sB->setText(eng(r.centerFieldT,QStringLiteral("T")));sEn->setText(eng(r.storedEnergyJ,QStringLiteral("J")));sPitch->setText(eng(r.windingPitchM,QStringLiteral("m")));sNote->setText(r.note);lout->setText(eng(EmEngineering::circularLoopInductance(lr->value()*1e-3,lwr->value()*1e-3,ln->value()),QStringLiteral("H")));};QObject::connect(scalc,&QPushButton::clicked,indContent,refreshSol);
    for(auto *sp:{sR,sL,sWd,sRb,sMur,sI,lr,lwr}) QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),indContent,[=](double){refreshSol();});
    QObject::connect(sN,qOverload<int>(&QSpinBox::valueChanged),indContent,[=](int){refreshSol();});QObject::connect(ln,qOverload<int>(&QSpinBox::valueChanged),indContent,[=](int){refreshSol();});
    refreshSol();tabs->addTab(indContent,QStringLiteral("Inductance / coils"));

    // ------------------------------------------------------------------
    // Capacitance
    // ------------------------------------------------------------------
    auto *capContent=new QWidget(tabs);
    auto *capRoot=new QVBoxLayout(capContent);
    auto *capTop=new QHBoxLayout();
    auto *capIn=new QGroupBox(QStringLiteral("Canonical conductor geometry + dielectric"),capContent);
    auto *ci=new QFormLayout(capIn);
    auto *cg=new QComboBox(capIn);
    cg->addItems({QStringLiteral("Parallel plates"),QStringLiteral("Isolated sphere"),QStringLiteral("Concentric spheres"),QStringLiteral("Coaxial cylinders"),QStringLiteral("Two-wire line")});
    auto *cmat=new QComboBox(capIn);
    cmat->addItems({QStringLiteral("Air / vacuum"),QStringLiteral("PTFE"),QStringLiteral("Polyethylene"),QStringLiteral("Polyimide"),QStringLiteral("FR-4 (typical)"),QStringLiteral("Alumina ceramic"),QStringLiteral("Custom")});
    auto *cer=spin(capIn,1.0006,0.000001,1e6,6);
    auto *ctan=spin(capIn,0.0,0.0,10.0,8);
    auto *cfreq=spin(capIn,1.0,0.000001,1e9,6);
    auto *carea=spin(capIn,0.01,1e-18,1e12,9),*csep=spin(capIn,1,0.000001,1e12,6),*cia=spin(capIn,10,0.000001,1e12,6),*coa=spin(capIn,20,0.000001,1e12,6),*clen=spin(capIn,1,0.000001,1e12,6),*cwr=spin(capIn,1,0.000001,1e12,6),*cspacing=spin(capIn,10,0.000001,1e12,6);
    ci->addRow(QStringLiteral("Geometry"),cg);
    ci->addRow(QStringLiteral("Dielectric material"),cmat);
    ci->addRow(QStringLiteral("Relative permittivity εr"),cer);
    ci->addRow(QStringLiteral("Loss tangent tanδ"),ctan);
    ci->addRow(QStringLiteral("Evaluation frequency (MHz)"),cfreq);
    ci->addRow(QStringLiteral("Plate area (m²)"),carea);
    ci->addRow(QStringLiteral("Plate separation / dielectric d (mm)"),csep);
    ci->addRow(QStringLiteral("Inner / sphere radius (mm)"),cia);
    ci->addRow(QStringLiteral("Outer radius (mm)"),coa);
    ci->addRow(QStringLiteral("Length (m)"),clen);
    ci->addRow(QStringLiteral("Wire radius (mm)"),cwr);
    ci->addRow(QStringLiteral("Wire center spacing (mm)"),cspacing);
    auto *ccalc=new QPushButton(QStringLiteral("Calculate capacitance / dielectric impedance"),capIn);ci->addRow(ccalc);
    capTop->addWidget(capIn);

    auto *capOut=new QGroupBox(QStringLiteral("Capacitance / dielectric result"),capContent);
    auto *co=new QFormLayout(capOut);
    auto *cC=valueLabel(capOut),*cCp=valueLabel(capOut),*cZ=valueLabel(capOut),*cG=valueLabel(capOut),*cQ=valueLabel(capOut),*cFormula=valueLabel(capOut),*cNote=valueLabel(capOut);
    co->addRow(QStringLiteral("C"),cC);co->addRow(QStringLiteral("C'"),cCp);co->addRow(QStringLiteral("Z(f) with tanδ"),cZ);co->addRow(QStringLiteral("Dielectric loss conductance Gd"),cG);co->addRow(QStringLiteral("Dielectric Q ≈ 1/tanδ"),cQ);co->addRow(QStringLiteral("Formula"),cFormula);co->addRow(cNote);
    capTop->addWidget(capOut,1);capRoot->addLayout(capTop);
    auto *capSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::Capacitance,capContent);
    capSketch->setMinimumHeight(300);capSketch->setMaximumHeight(420);capRoot->addWidget(capSketch);

    auto applyCapMaterial=[=](int idx){
        struct Mat{double er,tanD;};
        static const Mat m[]={{1.0006,0.0},{2.10,0.0002},{2.25,0.0004},{3.40,0.002},{4.20,0.018},{9.80,0.0002}};
        if(idx>=0 && idx<6){cer->setValue(m[idx].er);ctan->setValue(m[idx].tanD);}
    };
    auto refreshCap=[=]{
        capSketch->setVariant(cg->currentIndex());
        capSketch->setValues({
            {QStringLiteral("d"),QStringLiteral("%1 mm").arg(csep->value(),0,'g',5)},
            {QStringLiteral("material"),cmat->currentText()},
            {QStringLiteral("epsilon"),QString::number(cer->value(),'g',5)},
            {QStringLiteral("tand"),QString::number(ctan->value(),'g',4)}
        });
        EmEngineering::CapacitanceInput in;
        in.geometry=static_cast<EmEngineering::CapacitanceGeometry>(cg->currentIndex());
        in.epsilonR=cer->value();in.lossTangent=ctan->value();in.frequencyHz=cfreq->value()*1e6;
        in.areaM2=carea->value();in.separationM=csep->value()*1e-3;in.innerRadiusM=cia->value()*1e-3;in.outerRadiusM=coa->value()*1e-3;in.lengthM=clen->value();in.wireRadiusM=cwr->value()*1e-3;in.wireCenterSpacingM=cspacing->value()*1e-3;
        auto r=EmEngineering::capacitance(in);
        cC->setText(r.valid?eng(r.capacitanceF,QStringLiteral("F")):QStringLiteral("n/a"));
        cCp->setText(r.capacitancePerMeterFpm>0?eng(r.capacitancePerMeterFpm,QStringLiteral("F/m")):QStringLiteral("—"));
        cZ->setText(r.valid?complexText(r.impedanceOhm):QStringLiteral("—"));
        cG->setText(r.valid?eng(r.dielectricConductanceS,QStringLiteral("S")):QStringLiteral("—"));
        cQ->setText(r.valid?(std::isfinite(r.dielectricQualityFactor)?QString::number(r.dielectricQualityFactor,'g',7):QStringLiteral("∞ (lossless)")):QStringLiteral("—"));
        cFormula->setText(r.formula);cNote->setText(r.note);
    };
    QObject::connect(ccalc,&QPushButton::clicked,capContent,refreshCap);
    QObject::connect(cg,qOverload<int>(&QComboBox::currentIndexChanged),capContent,[=](int){refreshCap();});
    QObject::connect(cmat,qOverload<int>(&QComboBox::currentIndexChanged),capContent,[=](int i){applyCapMaterial(i);refreshCap();});
    for(auto *sp:{cer,ctan,cfreq,carea,csep,cia,coa,clen,cwr,cspacing})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),capContent,[=](double){refreshCap();});
    applyCapMaterial(cmat->currentIndex());refreshCap();
    tabs->addTab(scrollPage(capContent,tabs),QStringLiteral("Capacitance"));

    // ------------------------------------------------------------------
    // Coupled coils / transformer
    // ------------------------------------------------------------------
    auto *coupledContent=new QWidget(tabs);auto *coupledRoot=new QVBoxLayout(coupledContent);auto *coupledTop=new QHBoxLayout();
    auto *coupledIn=new QGroupBox(QStringLiteral("Coupled-inductor / transformer model"),coupledContent);auto *cfi=new QFormLayout(coupledIn);
    auto *cModel=new QComboBox(coupledIn);cModel->addItems({QStringLiteral("Manual L1 / L2"),QStringLiteral("Shared magnetic core (Ae / le / μr / gap)")});
    auto *cApp=new QComboBox(coupledIn);cApp->addItems({QStringLiteral("Transformer / coupled inductor"),QStringLiteral("Common-mode choke (bifilar)"),QStringLiteral("Differential-mode coupled inductor")});
    auto *cCorePreset=new QComboBox(coupledIn);cCorePreset->addItems({QStringLiteral("Custom / datasheet values"),QStringLiteral("Ferrite toroid — generic start"),QStringLiteral("Ferrite E / EE — generic start"),QStringLiteral("Ferrite ETD / EER — generic start"),QStringLiteral("Powdered-iron toroid — generic start"),QStringLiteral("Nanocrystalline toroid — generic start"),QStringLiteral("Common-mode ferrite core — generic start")});
    auto *cf=spin(coupledIn,50,0.000001,1e12,6),*cL1=spin(coupledIn,100,0.000001,1e12,6),*cL2=spin(coupledIn,25,0.000001,1e12,6),*ck=spin(coupledIn,0.98,0,1,6),*cr1=spin(coupledIn,2,0,1e12,6),*cr2=spin(coupledIn,0.5,0,1e12,6),*clr=spin(coupledIn,10,-1e12,1e12,6),*clx=spin(coupledIn,0,-1e12,1e12,6);
    auto *cn1=new QSpinBox(coupledIn);cn1->setRange(1,100000000);cn1->setValue(1000);auto *cn2=new QSpinBox(coupledIn);cn2->setRange(1,100000000);cn2->setValue(500);
    auto *cMu=spin(coupledIn,2000,1.0,1e7,4),*cAe=spin(coupledIn,100,0.000001,1e12,6),*cLe=spin(coupledIn,100,0.000001,1e12,6),*cGap=spin(coupledIn,0,0,1e9,6),*cBsat=spin(coupledIn,0.30,0.001,100,4);
    auto *cCoreFreqOn=new QCheckBox(QStringLiteral("Frequency-dependent complex μ / core loss"),coupledIn);auto *cMuFc=spin(coupledIn,1.0,0.000001,1e9,6),*cMuRef=spin(coupledIn,0.1,0.000001,1e9,6),*cMuTan=spin(coupledIn,0.01,0.0,100.0,8),*cMuExp=spin(coupledIn,0.5,0.0,3.0,4);
    auto *cWindingOn=new QCheckBox(QStringLiteral("Round-wire skin / proximity AC resistance"),coupledIn);auto *cWire1=spin(coupledIn,0.50,0.000001,1e6,6),*cWire2=spin(coupledIn,0.50,0.000001,1e6,6),*cProx=spin(coupledIn,25.0,0.0,10000.0,4),*cProxRef=spin(coupledIn,0.1,0.000001,1e9,6),*cProxExp=spin(coupledIn,1.0,0.0,3.0,4);auto *cDowellOn=new QCheckBox(QStringLiteral("Dowell-like multilayer winding estimate"),coupledIn);auto *cLayers1=new QSpinBox(coupledIn),*cLayers2=new QSpinBox(coupledIn);cLayers1->setRange(1,100);cLayers2->setRange(1,100);cLayers1->setValue(1);cLayers2->setValue(1);auto *cCoreOpOn=new QCheckBox(QStringLiteral("Use operating-point core loss from Core / winding loss tab"),coupledIn);
    cfi->addRow(QStringLiteral("Inductance source"),cModel);cfi->addRow(QStringLiteral("Application / winding type"),cApp);cfi->addRow(QStringLiteral("Core preset"),cCorePreset);cfi->addRow(QStringLiteral("Frequency (Hz)"),cf);cfi->addRow(QStringLiteral("L1 manual (mH)"),cL1);cfi->addRow(QStringLiteral("L2 manual (mH)"),cL2);cfi->addRow(QStringLiteral("Coupling k"),ck);cfi->addRow(QStringLiteral("N1"),cn1);cfi->addRow(QStringLiteral("N2"),cn2);
    cfi->addRow(QStringLiteral("Core μr / low-frequency μs"),cMu);cfi->addRow(QStringLiteral("Core Ae (mm²)"),cAe);cfi->addRow(QStringLiteral("Core path le (mm)"),cLe);cfi->addRow(QStringLiteral("Air gap g (mm)"),cGap);cfi->addRow(QStringLiteral("Saturation Bsat (T)"),cBsat);
    cfi->addRow(cCoreFreqOn);cfi->addRow(QStringLiteral("μ relaxation fc (MHz)"),cMuFc);cfi->addRow(QStringLiteral("μ / loss reference f (MHz)"),cMuRef);cfi->addRow(QStringLiteral("Extra tanδμ @ fref"),cMuTan);cfi->addRow(QStringLiteral("Magnetic-loss exponent"),cMuExp);
    cfi->addRow(QStringLiteral("R1 DC (Ω)"),cr1);cfi->addRow(QStringLiteral("R2 DC (Ω)"),cr2);cfi->addRow(cWindingOn);cfi->addRow(QStringLiteral("Primary wire diameter (mm)"),cWire1);cfi->addRow(QStringLiteral("Secondary wire diameter (mm)"),cWire2);cfi->addRow(QStringLiteral("Extra proximity loss @ fref (%)"),cProx);cfi->addRow(QStringLiteral("Proximity fref (MHz)"),cProxRef);cfi->addRow(QStringLiteral("Proximity exponent"),cProxExp);cfi->addRow(cDowellOn);cfi->addRow(QStringLiteral("Primary winding layers"),cLayers1);cfi->addRow(QStringLiteral("Secondary winding layers"),cLayers2);cfi->addRow(cCoreOpOn);cfi->addRow(QStringLiteral("Load R (Ω)"),clr);cfi->addRow(QStringLiteral("Load X (Ω)"),clx);
    auto *copy1=new QPushButton(QStringLiteral("Use last solenoid L as L1"),coupledIn),*copy2=new QPushButton(QStringLiteral("Use last solenoid L as L2"),coupledIn),*cfcalc=new QPushButton(QStringLiteral("Calculate coupling / transformer"),coupledIn);cfi->addRow(copy1);cfi->addRow(copy2);cfi->addRow(cfcalc);coupledTop->addWidget(coupledIn);

    auto *coupledOut=new QGroupBox(QStringLiteral("Coupled-coil / transformer result"),coupledContent);auto *cfo=new QFormLayout(coupledOut);
    auto *cLeff1=valueLabel(coupledOut),*cLeff2=valueLabel(coupledOut),*cM=valueLabel(coupledOut),*cn=valueLabel(coupledOut),*clp=valueLabel(coupledOut),*cls=valueLabel(coupledOut),*cModalHi=valueLabel(coupledOut),*cModalLo=valueLabel(coupledOut),*cZcm=valueLabel(coupledOut),*cZdm=valueLabel(coupledOut),*cRel=valueLabel(coupledOut),*cAl=valueLabel(coupledOut),*cIsat=valueLabel(coupledOut),*cMuOut=valueLabel(coupledOut),*cRacOut=valueLabel(coupledOut),*cRcoreOut=valueLabel(coupledOut),*cCoreOpOut=valueLabel(coupledOut),*cref=valueLabel(coupledOut),*czin=valueLabel(coupledOut),*ci2=valueLabel(coupledOut),*cfnote=valueLabel(coupledOut);
    cfo->addRow(QStringLiteral("Effective L1"),cLeff1);cfo->addRow(QStringLiteral("Effective L2"),cLeff2);cfo->addRow(QStringLiteral("Mutual M"),cM);cfo->addRow(QStringLiteral("N2/N1"),cn);cfo->addRow(QStringLiteral("Primary leakage"),clp);cfo->addRow(QStringLiteral("Secondary leakage"),cls);cfo->addRow(QStringLiteral("High eigenmode L / CM if equal windings"),cModalHi);cfo->addRow(QStringLiteral("Low eigenmode L / DM if equal windings"),cModalLo);cfo->addRow(QStringLiteral("Z common-mode estimate"),cZcm);cfo->addRow(QStringLiteral("Z differential-mode estimate"),cZdm);cfo->addRow(QStringLiteral("Core reluctance ℜ"),cRel);cfo->addRow(QStringLiteral("Core AL"),cAl);cfo->addRow(QStringLiteral("Primary Isat estimate"),cIsat);cfo->addRow(QStringLiteral("Core μ′ / μ″ / tanδμ"),cMuOut);cfo->addRow(QStringLiteral("Winding Rac primary / secondary"),cRacOut);cfo->addRow(QStringLiteral("Core loss R primary / secondary"),cRcoreOut);cfo->addRow(QStringLiteral("Operating-point Pcore / Req(primary)"),cCoreOpOut);cfo->addRow(QStringLiteral("Reflected impedance"),cref);cfo->addRow(QStringLiteral("Input impedance"),czin);cfo->addRow(QStringLiteral("I2/I1"),ci2);cfo->addRow(cfnote);coupledTop->addWidget(coupledOut,1);coupledRoot->addLayout(coupledTop);
    auto *coupledSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::CoupledCoils,coupledContent);coupledSketch->setMinimumHeight(300);coupledSketch->setMaximumHeight(420);coupledRoot->addWidget(coupledSketch);

    auto updateCoupledUi=[=](int idx){const bool core=idx==1;cL1->setEnabled(!core);cL2->setEnabled(!core);copy1->setEnabled(!core);copy2->setEnabled(!core);cCorePreset->setEnabled(core);for(auto *w:{cMu,cAe,cLe,cGap,cBsat})w->setEnabled(core);cCoreFreqOn->setEnabled(core);for(auto *w:{cMuFc,cMuRef,cMuTan,cMuExp})w->setEnabled(core&&cCoreFreqOn->isChecked());for(auto *w:{cWire1,cWire2,cProx,cProxRef,cProxExp})w->setEnabled(cWindingOn->isChecked());cDowellOn->setEnabled(cWindingOn->isChecked());cLayers1->setEnabled(cWindingOn->isChecked()&&cDowellOn->isChecked());cLayers2->setEnabled(cWindingOn->isChecked()&&cDowellOn->isChecked());const bool cm=cApp->currentIndex()==1;coupledSketch->setVariant(cm?2:(core?1:0));};
    auto applyCorePreset=[=](int idx){
        // Generic starting points only. Real designs should use the selected core's datasheet Ae/le/μi/Bsat.
        struct P{double mu,ae,le,g,bs,fc,tan;};
        static const P pset[]={{0,0,0,0,0,1,.01},{2000,80,90,0,0.35,.3,.02},{2000,125,95,0.30,0.35,.5,.02},{2000,125,100,0.50,0.35,.5,.02},{75,80,90,0,0.80,50,.005},{20000,100,110,0,1.20,.05,.03},{5000,100,100,0,0.35,1,.03}};
        if(idx>0 && idx<int(sizeof(pset)/sizeof(pset[0]))){const auto &q=pset[idx];cMu->setValue(q.mu);cAe->setValue(q.ae);cLe->setValue(q.le);cGap->setValue(q.g);cBsat->setValue(q.bs);cMuFc->setValue(q.fc);cMuTan->setValue(q.tan);}
    };
    auto lastCoupledL=std::make_shared<double>(0.0);
    auto refreshCoupled=[=]{
        EmEngineering::CoupledCoilsInput in;in.frequencyHz=cf->value();in.primaryInductanceH=cL1->value()*1e-3;in.secondaryInductanceH=cL2->value()*1e-3;in.couplingCoefficient=ck->value();in.primaryTurns=cn1->value();in.secondaryTurns=cn2->value();in.primaryResistanceOhm=cr1->value();in.secondaryResistanceOhm=cr2->value();in.loadOhm={clr->value(),clx->value()};in.application=static_cast<EmEngineering::CoupledWindingApplication>(cApp->currentIndex());
        in.useSharedCore=cModel->currentIndex()==1;in.coreRelativePermeability=cMu->value();in.coreEffectiveAreaM2=cAe->value()*1e-6;in.corePathLengthM=cLe->value()*1e-3;in.airGapM=cGap->value()*1e-3;in.saturationFluxDensityT=cBsat->value();
        in.useFrequencyDependentCore=in.useSharedCore&&cCoreFreqOn->isChecked();in.coreMaterial.enabled=in.useFrequencyDependentCore;in.coreMaterial.lowFrequencyRelativePermeability=cMu->value();in.coreMaterial.relaxationFrequencyHz=cMuFc->value()*1e6;in.coreMaterial.referenceFrequencyHz=cMuRef->value()*1e6;in.coreMaterial.additionalLossTangentAtReference=cMuTan->value();in.coreMaterial.additionalLossExponent=cMuExp->value();
        in.useWindingAcLoss=cWindingOn->isChecked();in.primaryWireDiameterM=cWire1->value()*1e-3;in.secondaryWireDiameterM=cWire2->value()*1e-3;in.windingProximityExtraAtReference=cProx->value()/100.0;in.windingProximityReferenceFrequencyHz=cProxRef->value()*1e6;in.windingProximityExponent=cProxExp->value();in.useDowellMultilayerEstimate=cDowellOn->isChecked();in.primaryWindingLayers=cLayers1->value();in.secondaryWindingLayers=cLayers2->value();in.useOperatingPointCoreLoss=cCoreOpOn->isChecked();in.operatingPointCoreLoss=*sharedCoreLossModel;in.operatingPointCoreLoss.frequencyHz=in.frequencyHz;
        auto r=EmEngineering::coupledCoils(in);*lastCoupledL=r.effectivePrimaryInductanceH;
        const bool cm=in.application==EmEngineering::CoupledWindingApplication::CommonModeChoke;coupledSketch->setVariant(cm?2:(in.useSharedCore?1:0));coupledSketch->setValue(QStringLiteral("mu"),QString::number(cMu->value(),'g',5));
        cLeff1->setText(eng(r.effectivePrimaryInductanceH,QStringLiteral("H")));cLeff2->setText(eng(r.effectiveSecondaryInductanceH,QStringLiteral("H")));cM->setText(eng(r.mutualInductanceH,QStringLiteral("H")));cn->setText(QString::number(r.turnsRatioN2OverN1,'g',7));clp->setText(eng(r.primaryLeakageH,QStringLiteral("H")));cls->setText(eng(r.secondaryLeakageH,QStringLiteral("H")));
        cModalHi->setText(eng(r.modalHighInductanceH,QStringLiteral("H")));cModalLo->setText(eng(r.modalLowInductanceH,QStringLiteral("H")));cZcm->setText(complexText(r.commonModeImpedanceOhm));cZdm->setText(complexText(r.differentialModeImpedanceOhm));
        cRel->setText(in.useSharedCore?eng(r.magneticReluctanceAtPerH,QStringLiteral("A/Wb")):QStringLiteral("—"));cAl->setText(in.useSharedCore?eng(r.alValueHPerTurn2,QStringLiteral("H/turn²")):QStringLiteral("—"));cIsat->setText(in.useSharedCore?eng(r.estimatedPrimarySaturationCurrentA,QStringLiteral("A")):QStringLiteral("—"));
        cMuOut->setText(in.useFrequencyDependentCore?QStringLiteral("%1 / %2 / %3").arg(r.coreMuPrime,0,'g',6).arg(r.coreMuDoublePrime,0,'g',6).arg(r.coreLossTangent,0,'g',6):QStringLiteral("—"));cRacOut->setText(QStringLiteral("%1 / %2").arg(eng(r.primaryAcResistanceOhm,QStringLiteral("Ω"))).arg(eng(r.secondaryAcResistanceOhm,QStringLiteral("Ω"))));cRcoreOut->setText(in.useFrequencyDependentCore?QStringLiteral("%1 / %2").arg(eng(r.primaryCoreLossSeriesResistanceOhm,QStringLiteral("Ω"))).arg(eng(r.secondaryCoreLossSeriesResistanceOhm,QStringLiteral("Ω"))):QStringLiteral("—"));cCoreOpOut->setText(in.useOperatingPointCoreLoss?QStringLiteral("%1 / %2").arg(eng(r.operatingPointCoreLossW,QStringLiteral("W"))).arg(eng(r.operatingPointEquivalentPrimaryResistanceOhm,QStringLiteral("Ω"))):QStringLiteral("—"));
        cref->setText(complexText(r.reflectedImpedanceOhm));czin->setText(complexText(r.inputImpedanceOhm));ci2->setText(QStringLiteral("%1 %2 j%3").arg(r.secondaryCurrentPerPrimaryAperA.real(),0,'g',7).arg(r.secondaryCurrentPerPrimaryAperA.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(r.secondaryCurrentPerPrimaryAperA.imag()),0,'g',7));cfnote->setText(r.note);
    };
    QObject::connect(cfcalc,&QPushButton::clicked,coupledContent,refreshCoupled);QObject::connect(copy1,&QPushButton::clicked,coupledContent,[=]{if(*lastL>0)cL1->setValue(*lastL*1e3);});QObject::connect(copy2,&QPushButton::clicked,coupledContent,[=]{if(*lastL>0)cL2->setValue(*lastL*1e3);});QObject::connect(cModel,qOverload<int>(&QComboBox::currentIndexChanged),coupledContent,[=](int i){updateCoupledUi(i);refreshCoupled();});
    QObject::connect(cApp,qOverload<int>(&QComboBox::currentIndexChanged),coupledContent,[=](int){updateCoupledUi(cModel->currentIndex());refreshCoupled();});
    QObject::connect(cCorePreset,qOverload<int>(&QComboBox::currentIndexChanged),coupledContent,[=](int i){applyCorePreset(i);refreshCoupled();});
    // Legacy live-refresh coverage: for(auto *sp:{cf,cL1,cL2,ck,cr1,cr2,clr,clx,cMu,cAe,cLe,cGap,cBsat})
    QObject::connect(cCoreFreqOn,&QCheckBox::toggled,coupledContent,[=](bool){updateCoupledUi(cModel->currentIndex());refreshCoupled();});QObject::connect(cWindingOn,&QCheckBox::toggled,coupledContent,[=](bool){updateCoupledUi(cModel->currentIndex());refreshCoupled();});QObject::connect(cDowellOn,&QCheckBox::toggled,coupledContent,[=](bool){updateCoupledUi(cModel->currentIndex());refreshCoupled();});QObject::connect(cCoreOpOn,&QCheckBox::toggled,coupledContent,[=](bool){refreshCoupled();});QObject::connect(cLayers1,qOverload<int>(&QSpinBox::valueChanged),coupledContent,[=](int){refreshCoupled();});QObject::connect(cLayers2,qOverload<int>(&QSpinBox::valueChanged),coupledContent,[=](int){refreshCoupled();});
    for(auto *sp:{cf,cL1,cL2,ck,cr1,cr2,clr,clx,cMu,cAe,cLe,cGap,cBsat,cMuFc,cMuRef,cMuTan,cMuExp,cWire1,cWire2,cProx,cProxRef,cProxExp}) QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),coupledContent,[=](double){refreshCoupled();});
    QObject::connect(cn1,qOverload<int>(&QSpinBox::valueChanged),coupledContent,[=](int){refreshCoupled();});QObject::connect(cn2,qOverload<int>(&QSpinBox::valueChanged),coupledContent,[=](int){refreshCoupled();});
    updateCoupledUi(cModel->currentIndex());refreshCoupled();tabs->addTab(scrollPage(coupledContent,tabs),QStringLiteral("Coupled coils / transformer"));

    // ------------------------------------------------------------------
    // Balun / unun engineering calculator
    // ------------------------------------------------------------------
    auto *balContent=new QWidget(tabs);auto *balRoot=new QVBoxLayout(balContent);auto *balTop=new QHBoxLayout();
    auto *balIn=new QGroupBox(QStringLiteral("BALUN / UNUN first-order equivalent"),balContent);auto *bif=new QFormLayout(balIn);
    auto *balType=new QComboBox(balIn);balType->addItems({QStringLiteral("1:1 current balun"),QStringLiteral("4:1 Guanella current balun"),QStringLiteral("4:1 Ruthroff voltage balun"),QStringLiteral("Arbitrary transformer balun / unun")});
    auto *balF=spin(balIn,14.2,0.000001,1e9,6),*balZ0=spin(balIn,50,0.001,1e9,6),*balR=spin(balIn,50,0.001,1e9,6),*balX=spin(balIn,0,-1e9,1e9,6),*balN=spin(balIn,1,0.001,1000,6),*balLm=spin(balIn,100,0.000001,1e12,6),*balLl=spin(balIn,0.5,0,1e12,6),*balRw=spin(balIn,0.2,0,1e9,6),*balCp=spin(balIn,5,0,1e12,6);
    auto *balCoreOn=new QCheckBox(QStringLiteral("Complex-permeability ferrite loss"),balIn);auto *balMu=spin(balIn,250,1.0,1e7,4),*balMuFc=spin(balIn,15.0,0.000001,1e9,6),*balMuRef=spin(balIn,1.0,0.000001,1e9,6),*balMuTan=spin(balIn,0.01,0.0,100.0,8),*balMuExp=spin(balIn,0.5,0.0,3.0,4);
    auto *balWireOn=new QCheckBox(QStringLiteral("Round-wire skin / proximity loss"),balIn);auto *balWire=spin(balIn,0.50,0.000001,1e6,6),*balProx=spin(balIn,25.0,0.0,10000.0,4),*balProxRef=spin(balIn,14.2,0.000001,1e9,6),*balProxExp=spin(balIn,1.0,0.0,3.0,4);auto *balDowellOn=new QCheckBox(QStringLiteral("Dowell-like multilayer estimate"),balIn);auto *balLayers=new QSpinBox(balIn);balLayers->setRange(1,100);balLayers->setValue(1);auto *balCoreOpOn=new QCheckBox(QStringLiteral("Use operating-point core loss from Core / winding loss tab"),balIn);
    bif->addRow(QStringLiteral("Topology"),balType);bif->addRow(QStringLiteral("Frequency (MHz)"),balF);bif->addRow(QStringLiteral("Unbalanced source Z0 (Ω)"),balZ0);bif->addRow(QStringLiteral("Load R (Ω)"),balR);bif->addRow(QStringLiteral("Load X (Ω)"),balX);bif->addRow(QStringLiteral("N2/N1 (arbitrary mode)"),balN);bif->addRow(QStringLiteral("Magnetizing / choke Lm @ fref (µH)"),balLm);bif->addRow(QStringLiteral("Leakage L (µH)"),balLl);bif->addRow(QStringLiteral("Winding DC R (Ω)"),balRw);bif->addRow(QStringLiteral("Parasitic C (pF)"),balCp);bif->addRow(balCoreOn);bif->addRow(QStringLiteral("Low-frequency μs"),balMu);bif->addRow(QStringLiteral("μ relaxation fc (MHz)"),balMuFc);bif->addRow(QStringLiteral("Lm / loss reference f (MHz)"),balMuRef);bif->addRow(QStringLiteral("Extra tanδμ @ fref"),balMuTan);bif->addRow(QStringLiteral("Magnetic-loss exponent"),balMuExp);bif->addRow(balWireOn);bif->addRow(QStringLiteral("Wire diameter (mm)"),balWire);bif->addRow(QStringLiteral("Extra proximity loss @ fref (%)"),balProx);bif->addRow(QStringLiteral("Proximity fref (MHz)"),balProxRef);bif->addRow(QStringLiteral("Proximity exponent"),balProxExp);bif->addRow(balDowellOn);bif->addRow(QStringLiteral("Winding layers"),balLayers);bif->addRow(balCoreOpOn);
    auto *balUseCore=new QPushButton(QStringLiteral("Use current coupled-core L1 as Lm"),balIn);auto *balCalc=new QPushButton(QStringLiteral("Calculate BALUN / UNUN"),balIn);bif->addRow(balUseCore);bif->addRow(balCalc);balTop->addWidget(balIn);
    auto *balOut=new QGroupBox(QStringLiteral("BALUN / UNUN result"),balContent);auto *bof=new QFormLayout(balOut);auto *balVr=valueLabel(balOut),*balZr=valueLabel(balOut),*balIdeal=valueLabel(balOut),*balZin=valueLabel(balOut),*balGam=valueLabel(balOut),*balRl=valueLabel(balOut),*balVs=valueLabel(balOut),*balCm=valueLabel(balOut),*balSrf=valueLabel(balOut),*balLmEff=valueLabel(balOut),*balLoss=valueLabel(balOut),*balMuOut=valueLabel(balOut),*balCoreOp=valueLabel(balOut),*balNote=valueLabel(balOut);bof->addRow(QStringLiteral("V2/V1 ideal"),balVr);bof->addRow(QStringLiteral("Z2/Z1 ideal"),balZr);bof->addRow(QStringLiteral("Load referred to input"),balIdeal);bof->addRow(QStringLiteral("Non-ideal input Zin"),balZin);bof->addRow(QStringLiteral("|Γ|"),balGam);bof->addRow(QStringLiteral("Return loss"),balRl);bof->addRow(QStringLiteral("VSWR"),balVs);bof->addRow(QStringLiteral("Common-mode choking Z"),balCm);bof->addRow(QStringLiteral("Approx. Lm-Cp SRF"),balSrf);bof->addRow(QStringLiteral("Effective Lm(f)"),balLmEff);bof->addRow(QStringLiteral("Winding Rac / core Rloss"),balLoss);bof->addRow(QStringLiteral("Core μ′ / μ″ / tanδμ"),balMuOut);bof->addRow(QStringLiteral("Operating-point Pcore / Req"),balCoreOp);bof->addRow(balNote);balTop->addWidget(balOut,1);balRoot->addLayout(balTop);
    auto *balSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::Balun,balContent);balSketch->setMinimumHeight(230);balSketch->setMaximumHeight(320);balRoot->addWidget(balSketch);
    auto *balPlot=new FieldProfilePlot(balContent);balPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));balPlot->setXAxisLogarithmic(true);balRoot->addWidget(balPlot,1);
    auto updateBalLossUi=[=]{for(auto*w:{balMu,balMuFc,balMuRef,balMuTan,balMuExp})w->setEnabled(balCoreOn->isChecked());for(auto*w:{balWire,balProx,balProxRef,balProxExp})w->setEnabled(balWireOn->isChecked());balDowellOn->setEnabled(balWireOn->isChecked());balLayers->setEnabled(balWireOn->isChecked()&&balDowellOn->isChecked());};
    auto refreshBal=[=]{updateBalLossUi();EmEngineering::BalunInput in;in.topology=static_cast<EmEngineering::BalunTopology>(balType->currentIndex());in.frequencyHz=balF->value()*1e6;in.sourceResistanceOhm=balZ0->value();in.loadOhm={balR->value(),balX->value()};in.secondaryToPrimaryTurnsRatio=balN->value();in.magnetizingInductanceH=balLm->value()*1e-6;in.leakageInductanceH=balLl->value()*1e-6;in.windingResistanceOhm=balRw->value();in.parasiticCapacitanceF=balCp->value()*1e-12;in.useFrequencyDependentCore=balCoreOn->isChecked();in.coreMaterial.enabled=in.useFrequencyDependentCore;in.coreMaterial.lowFrequencyRelativePermeability=balMu->value();in.coreMaterial.relaxationFrequencyHz=balMuFc->value()*1e6;in.coreMaterial.referenceFrequencyHz=balMuRef->value()*1e6;in.coreMaterial.additionalLossTangentAtReference=balMuTan->value();in.coreMaterial.additionalLossExponent=balMuExp->value();in.useWindingAcLoss=balWireOn->isChecked();in.wireDiameterM=balWire->value()*1e-3;in.windingProximityExtraAtReference=balProx->value()/100.0;in.windingProximityReferenceFrequencyHz=balProxRef->value()*1e6;in.windingProximityExponent=balProxExp->value();in.useDowellMultilayerEstimate=balDowellOn->isChecked();in.windingLayers=balLayers->value();in.useOperatingPointCoreLoss=balCoreOpOn->isChecked();in.operatingPointCoreLoss=*sharedCoreLossModel;in.operatingPointCoreLoss.frequencyHz=in.frequencyHz;const auto r=EmEngineering::balun(in);balN->setEnabled(balType->currentIndex()==3);balVr->setText(QString::number(r.voltageRatioSecondaryOverPrimary,'g',7));balZr->setText(QString::number(r.impedanceRatioSecondaryOverPrimary,'g',7));balIdeal->setText(complexText(r.idealReferredLoadOhm));balZin->setText(complexText(r.inputImpedanceOhm));balGam->setText(QString::number(std::abs(r.reflectionCoefficient),'g',7));balRl->setText(QStringLiteral("%1 dB").arg(r.returnLossDb,0,'g',7));balVs->setText(QString::number(r.vswr,'g',7));balCm->setText(complexText(r.commonModeChokingImpedanceOhm));balSrf->setText(r.approximateSelfResonanceHz>0?eng(r.approximateSelfResonanceHz,QStringLiteral("Hz")):QStringLiteral("—"));balLmEff->setText(eng(r.effectiveMagnetizingInductanceH,QStringLiteral("H")));balLoss->setText(QStringLiteral("%1 / %2").arg(eng(r.windingAcResistanceOhm,QStringLiteral("Ω"))).arg(eng(r.coreLossSeriesResistanceOhm,QStringLiteral("Ω"))));balMuOut->setText(in.useFrequencyDependentCore?QStringLiteral("%1 / %2 / %3").arg(r.coreMuPrime,0,'g',6).arg(r.coreMuDoublePrime,0,'g',6).arg(r.coreLossTangent,0,'g',6):QStringLiteral("—"));balCoreOp->setText(in.useOperatingPointCoreLoss?QStringLiteral("%1 / %2").arg(eng(r.operatingPointCoreLossW,QStringLiteral("W"))).arg(eng(r.operatingPointEquivalentSeriesResistanceOhm,QStringLiteral("Ω"))):QStringLiteral("—"));balNote->setText(r.note);balSketch->setValue(QStringLiteral("ratio"),QStringLiteral("V ratio %1 : 1 • Z ratio %2 : 1").arg(r.voltageRatioSecondaryOverPrimary,0,'g',4).arg(r.impedanceRatioSecondaryOverPrimary,0,'g',4));QVector<double> ff,zin,s11;const int np=160;const double fmin=std::max(1.0,in.frequencyHz/10),fmax=in.frequencyHz*10;for(int i=0;i<np;++i){const double u=double(i)/(np-1),f=fmin*std::pow(fmax/fmin,u);auto si=in;si.frequencyHz=f;const auto sr=EmEngineering::balun(si);ff.push_back(f);zin.push_back(std::abs(sr.inputImpedanceOhm));s11.push_back(20*std::log10(std::max(1e-15,std::abs(sr.reflectionCoefficient))));}balPlot->setSeries({FieldProfileSeries{ff,zin,QStringLiteral("|Zin|"),QStringLiteral("Ω"),false},FieldProfileSeries{ff,s11,QStringLiteral("S11"),QStringLiteral("dB"),false}},QStringLiteral("BALUN / UNUN response with optional ferrite and winding loss"));};
    QObject::connect(balCalc,&QPushButton::clicked,balContent,refreshBal);QObject::connect(balUseCore,&QPushButton::clicked,balContent,[=]{if(*lastCoupledL>0)balLm->setValue(*lastCoupledL*1e6);});QObject::connect(balType,qOverload<int>(&QComboBox::currentIndexChanged),balContent,[=](int){refreshBal();});QObject::connect(balCoreOn,&QCheckBox::toggled,balContent,[=](bool){refreshBal();});QObject::connect(balWireOn,&QCheckBox::toggled,balContent,[=](bool){refreshBal();});QObject::connect(balDowellOn,&QCheckBox::toggled,balContent,[=](bool){refreshBal();});QObject::connect(balCoreOpOn,&QCheckBox::toggled,balContent,[=](bool){refreshBal();});QObject::connect(balLayers,qOverload<int>(&QSpinBox::valueChanged),balContent,[=](int){refreshBal();});for(auto *sp:{balF,balZ0,balR,balX,balN,balLm,balLl,balRw,balCp,balMu,balMuFc,balMuRef,balMuTan,balMuExp,balWire,balProx,balProxRef,balProxExp})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),balContent,[=](double){refreshBal();});refreshBal();tabs->addTab(scrollPage(balContent,tabs),QStringLiteral("BALUN / UNUN"));

    // ------------------------------------------------------------------
    // Magnetic-core / multilayer-winding loss explorer
    // ------------------------------------------------------------------
    auto *lossContent=new QWidget(tabs);auto *lossRoot=new QVBoxLayout(lossContent);auto *lossTop=new QHBoxLayout();lossRoot->addLayout(lossTop);
    auto *coreLossBox=new QGroupBox(QStringLiteral("Core loss — Steinmetz / manufacturer data"),lossContent);auto *clf=new QFormLayout(coreLossBox);
    auto *clMode=new QComboBox(coreLossBox);clMode->addItems({QStringLiteral("Steinmetz coefficients"),QStringLiteral("Manufacturer / measured f-B-Pv points")});
    auto *clF=spin(coreLossBox,100.0,0.000001,1e9,6),*clB=spin(coreLossBox,100.0,0.000001,1e6,6),*clVe=spin(coreLossBox,1.0,0.0,1e12,6),*clIrms=spin(coreLossBox,1.0,0.000001,1e12,6),*clK=spin(coreLossBox,1.0,0.0,1e30,10),*clA=spin(coreLossBox,1.5,0.0,5.0,6),*clBeta=spin(coreLossBox,2.5,0.0,8.0,6);
    auto *clData=new QPlainTextEdit(coreLossBox);clData->setMaximumHeight(110);clData->setPlaceholderText(QStringLiteral("f_kHz ; B_mT ; Pv_kW/m3\n100 ; 100 ; 80\n200 ; 100 ; 210"));auto *clLoad=new QPushButton(QStringLiteral("Load core-loss CSV…"),coreLossBox);
    clf->addRow(QStringLiteral("Loss model"),clMode);clf->addRow(QStringLiteral("Frequency (kHz)"),clF);clf->addRow(QStringLiteral("Bpeak (mT)"),clB);clf->addRow(QStringLiteral("Effective core volume Ve (cm³)"),clVe);clf->addRow(QStringLiteral("Operating Irms (A)"),clIrms);clf->addRow(QStringLiteral("Steinmetz k [SI: W/m³, Hz, T]"),clK);clf->addRow(QStringLiteral("Steinmetz α"),clA);clf->addRow(QStringLiteral("Steinmetz β"),clBeta);clf->addRow(QStringLiteral("Curve: f_kHz ; B_mT ; Pv_kW/m³"),clData);clf->addRow(clLoad);lossTop->addWidget(coreLossBox);
    auto *windLossBox=new QGroupBox(QStringLiteral("Winding loss — skin / proximity / multilayer"),lossContent);auto *wlf=new QFormLayout(windLossBox);auto *wlF=spin(windLossBox,100.0,0.000001,1e9,6),*wlRdc=spin(windLossBox,0.1,0.0,1e12,8),*wlDia=spin(windLossBox,0.5,0.000001,1e6,6),*wlProx=spin(windLossBox,0.0,0.0,100000.0,6),*wlProxRef=spin(windLossBox,100.0,0.000001,1e9,6),*wlProxExp=spin(windLossBox,1.0,0.0,3.0,6);auto *wlDowell=new QCheckBox(QStringLiteral("Enable Dowell-like multilayer estimate"),windLossBox);auto *wlLayers=new QSpinBox(windLossBox);wlLayers->setRange(1,100);wlLayers->setValue(1);wlf->addRow(QStringLiteral("Frequency (kHz)"),wlF);wlf->addRow(QStringLiteral("Rdc (Ω)"),wlRdc);wlf->addRow(QStringLiteral("Wire / effective conductor thickness (mm)"),wlDia);wlf->addRow(QStringLiteral("Extra empirical proximity @ fref (%)"),wlProx);wlf->addRow(QStringLiteral("Proximity fref (kHz)"),wlProxRef);wlf->addRow(QStringLiteral("Proximity exponent"),wlProxExp);wlf->addRow(wlDowell);wlf->addRow(QStringLiteral("Winding layers"),wlLayers);lossTop->addWidget(windLossBox);
    auto *lossOut=new QGroupBox(QStringLiteral("Loss result"),lossContent);auto *lof=new QFormLayout(lossOut);auto *loPv=valueLabel(lossOut),*loPc=valueLabel(lossOut),*loReq=valueLabel(lossOut),*loDelta=valueLabel(lossOut),*loSkin=valueLabel(lossOut),*loProx=valueLabel(lossOut),*loDow=valueLabel(lossOut),*loRac=valueLabel(lossOut),*loNote=valueLabel(lossOut);lof->addRow(QStringLiteral("Core loss density Pv"),loPv);lof->addRow(QStringLiteral("Total core loss"),loPc);lof->addRow(QStringLiteral("Equivalent core series R @ Irms"),loReq);lof->addRow(QStringLiteral("Skin depth δ"),loDelta);lof->addRow(QStringLiteral("Skin factor"),loSkin);lof->addRow(QStringLiteral("Empirical proximity factor"),loProx);lof->addRow(QStringLiteral("Dowell multilayer factor"),loDow);lof->addRow(QStringLiteral("Winding Rac"),loRac);lof->addRow(loNote);lossTop->addWidget(lossOut,1);
    auto *lossPlot=new FieldProfilePlot(lossContent);lossPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));lossPlot->setXAxisLogarithmic(true);lossRoot->addWidget(lossPlot,1);
    auto parseCoreLossCurve=[=](){std::vector<EmEngineering::CoreLossDatasheetPoint> pts;for(const auto &raw:clData->toPlainText().split('\n')){const auto line=raw.trimmed();if(line.isEmpty()||line.startsWith('#'))continue;const auto c=line.split(';');if(c.size()<3)continue;bool a=false,b=false,d=false;const double fk=c[0].trimmed().toDouble(&a),bm=c[1].trimmed().toDouble(&b),pv=c[2].trimmed().toDouble(&d);if(a&&b&&d&&fk>0&&bm>0&&pv>=0)pts.push_back({fk*1e3,bm*1e-3,pv*1e3});}return pts;};
    auto refreshLoss=[=]{EmEngineering::CoreLossInput ci;ci.frequencyHz=clF->value()*1e3;ci.fluxDensityPeakT=clB->value()*1e-3;ci.coreVolumeM3=clVe->value()*1e-6;ci.currentRmsA=clIrms->value();ci.useSteinmetz=clMode->currentIndex()==0;ci.steinmetzK=clK->value();ci.steinmetzAlpha=clA->value();ci.steinmetzBeta=clBeta->value();ci.useDatasheetCurve=clMode->currentIndex()==1;ci.datasheetCurve=parseCoreLossCurve();*sharedCoreLossModel=ci;EmEngineering::WindingLossInput wi;wi.frequencyHz=wlF->value()*1e3;wi.dcResistanceOhm=wlRdc->value();wi.wireDiameterM=wlDia->value()*1e-3;wi.proximityExtraAtReference=wlProx->value()/100.0;wi.proximityReferenceFrequencyHz=wlProxRef->value()*1e3;wi.proximityExponent=wlProxExp->value();wi.useDowellMultilayerEstimate=wlDowell->isChecked();wi.layerCount=wlLayers->value();*sharedWindingLossModel=wi;const auto cr=EmEngineering::coreLoss(ci);const auto wr=EmEngineering::windingLoss(wi);loPv->setText(cr.valid?QStringLiteral("%1 kW/m³").arg(cr.powerDensityWPerM3/1e3,0,'g',7):QStringLiteral("n/a"));loPc->setText(cr.valid?eng(cr.totalCoreLossW,QStringLiteral("W")):QStringLiteral("n/a"));loReq->setText(cr.valid?eng(cr.equivalentSeriesResistanceOhm,QStringLiteral("Ω")):QStringLiteral("n/a"));loDelta->setText(eng(wr.skinDepthM,QStringLiteral("m")));loSkin->setText(QString::number(wr.skinEffectFactor,'g',7));loProx->setText(QString::number(wr.proximityEffectFactor,'g',7));loDow->setText(QString::number(wr.dowellMultilayerFactor,'g',7));loRac->setText(eng(wr.acResistanceOhm,QStringLiteral("Ω")));loNote->setText(cr.note+QStringLiteral("  Dowell is a 1-D foil/layer model used here as an engineering approximation for tightly packed round wire; use measured winding impedance or a field solver for precision."));QVector<double> ff,pv,rr;const int np=180;const double f0=std::max(1.0,std::min(ci.frequencyHz,wi.frequencyHz)/20.0),f1=std::max(f0*1.001,std::max(ci.frequencyHz,wi.frequencyHz)*20.0);for(int i=0;i<np;++i){const double u=double(i)/(np-1),f=f0*std::pow(f1/f0,u);auto ci2=ci;ci2.frequencyHz=f;auto wi2=wi;wi2.frequencyHz=f;const auto c2=EmEngineering::coreLoss(ci2);const auto w2=EmEngineering::windingLoss(wi2);ff.push_back(f);pv.push_back(c2.valid?c2.powerDensityWPerM3/1e3:std::numeric_limits<double>::quiet_NaN());rr.push_back(wi.dcResistanceOhm>0?w2.acResistanceOhm/wi.dcResistanceOhm:0.0);}lossPlot->setSeries({FieldProfileSeries{ff,pv,QStringLiteral("Pv"),QStringLiteral("kW/m³"),false},FieldProfileSeries{ff,rr,QStringLiteral("Rac/Rdc"),QString(),true}},QStringLiteral("Core-loss density and winding AC-resistance growth"));};
    auto updateLossUi=[=]{const bool ds=clMode->currentIndex()==1;clK->setEnabled(!ds);clA->setEnabled(!ds);clBeta->setEnabled(!ds);clData->setEnabled(ds);clLoad->setEnabled(ds);wlLayers->setEnabled(wlDowell->isChecked());};
    QObject::connect(clLoad,&QPushButton::clicked,lossContent,[=]{const auto fn=QFileDialog::getOpenFileName(lossContent,QStringLiteral("Load core-loss CSV"),QString(),QStringLiteral("CSV / text (*.csv *.txt);;All files (*)"));if(fn.isEmpty())return;QFile f(fn);if(f.open(QIODevice::ReadOnly|QIODevice::Text)){QTextStream ts(&f);clData->setPlainText(ts.readAll());clMode->setCurrentIndex(1);}});QObject::connect(clMode,qOverload<int>(&QComboBox::currentIndexChanged),lossContent,[=](int){updateLossUi();refreshLoss();});QObject::connect(clData,&QPlainTextEdit::textChanged,lossContent,refreshLoss);QObject::connect(wlDowell,&QCheckBox::toggled,lossContent,[=](bool){updateLossUi();refreshLoss();});for(auto *sp:{clF,clB,clVe,clIrms,clK,clA,clBeta,wlF,wlRdc,wlDia,wlProx,wlProxRef,wlProxExp})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),lossContent,[=](double){refreshLoss();});QObject::connect(wlLayers,qOverload<int>(&QSpinBox::valueChanged),lossContent,[=](int){refreshLoss();});updateLossUi();refreshLoss();tabs->addTab(scrollPage(lossContent,tabs),QStringLiteral("Core / winding loss"));

    // ------------------------------------------------------------------
    // Inductive / magnetic-loop antennas
    // ------------------------------------------------------------------
    auto *indAntContent=new QWidget(tabs);auto *indAntRoot=new QVBoxLayout(indAntContent);auto *indAntTop=new QHBoxLayout();indAntRoot->addLayout(indAntTop);
    auto *indAntIn=new QGroupBox(QStringLiteral("Inductive / loop antenna geometry"),indAntContent);auto *iaf=new QFormLayout(indAntIn);
    auto *iaGeom=new QComboBox(indAntIn);iaGeom->addItems({QStringLiteral("Circular wire loop"),QStringLiteral("Solenoidal coil"),QStringLiteral("Planar circular spiral / PCB")});
    auto *iaFreq=spin(indAntIn,13.56,0.000001,1e9,6);auto *iaTurns=new QSpinBox(indAntIn);iaTurns->setRange(1,100000);iaTurns->setValue(3);
    auto *iaRadius=spin(indAntIn,25,0.001,1e9,6),*iaSolLen=spin(indAntIn,30,0.001,1e9,6),*iaWire=spin(indAntIn,1,0.000001,1e6,6),*iaDin=spin(indAntIn,20,0.001,1e9,6),*iaTw=spin(indAntIn,1,0.000001,1e6,6),*iaSpace=spin(indAntIn,0.5,0,1e6,6),*iaCu=spin(indAntIn,35,0.001,1e6,4),*iaCurrent=spin(indAntIn,1,-1e6,1e6,6),*iaZ=spin(indAntIn,20,-1e9,1e9,6),*iaCp=spin(indAntIn,5,0,1e9,6),*iaZ0=spin(indAntIn,50,0.001,1e6,6);
    iaf->addRow(QStringLiteral("Geometry"),iaGeom);iaf->addRow(QStringLiteral("Frequency (MHz)"),iaFreq);iaf->addRow(QStringLiteral("Turns N"),iaTurns);iaf->addRow(QStringLiteral("Loop / solenoid radius (mm)"),iaRadius);iaf->addRow(QStringLiteral("Solenoid length (mm)"),iaSolLen);iaf->addRow(QStringLiteral("Round-wire diameter (mm)"),iaWire);iaf->addRow(QStringLiteral("Spiral inner diameter (mm)"),iaDin);iaf->addRow(QStringLiteral("PCB trace width (mm)"),iaTw);iaf->addRow(QStringLiteral("PCB trace spacing (mm)"),iaSpace);iaf->addRow(QStringLiteral("Copper thickness (µm)"),iaCu);iaf->addRow(QStringLiteral("Peak current (A)"),iaCurrent);iaf->addRow(QStringLiteral("Axial observation z (mm)"),iaZ);iaf->addRow(QStringLiteral("Estimated parasitic C (pF)"),iaCp);iaf->addRow(QStringLiteral("Reference Z0 (Ω)"),iaZ0);auto *iaCalc=new QPushButton(QStringLiteral("Calculate inductive antenna"),indAntIn);iaf->addRow(iaCalc);indAntTop->addWidget(indAntIn);
    auto *iaOut=new QGroupBox(QStringLiteral("Inductive antenna result"),indAntContent);auto *iao=new QFormLayout(iaOut);auto *iaLam=valueLabel(iaOut),*iaDout=valueLabel(iaOut),*iaArea=valueLabel(iaOut),*iaM=valueLabel(iaOut),*iaL=valueLabel(iaOut),*iaLen=valueLabel(iaOut),*iaRdc=valueLabel(iaOut),*iaRac=valueLabel(iaOut),*iaSkin=valueLabel(iaOut),*iaRr=valueLabel(iaOut),*iaZin=valueLabel(iaOut),*iaQ=valueLabel(iaOut),*iaSrf=valueLabel(iaOut),*iaB=valueLabel(iaOut),*iaNear=valueLabel(iaOut),*iaEl=valueLabel(iaOut),*iaGamma=valueLabel(iaOut),*iaNote=valueLabel(iaOut);iao->addRow(QStringLiteral("λ"),iaLam);iao->addRow(QStringLiteral("Outer diameter"),iaDout);iao->addRow(QStringLiteral("N·A effective"),iaArea);iao->addRow(QStringLiteral("Magnetic moment m=NIA"),iaM);iao->addRow(QStringLiteral("Estimated L"),iaL);iao->addRow(QStringLiteral("Conductor length"),iaLen);iao->addRow(QStringLiteral("Rdc"),iaRdc);iao->addRow(QStringLiteral("Rac / skin estimate"),iaRac);iao->addRow(QStringLiteral("Skin depth δ"),iaSkin);iao->addRow(QStringLiteral("Radiation resistance"),iaRr);iao->addRow(QStringLiteral("Input impedance"),iaZin);iao->addRow(QStringLiteral("Unloaded Q"),iaQ);iao->addRow(QStringLiteral("LC self-resonance"),iaSrf);iao->addRow(QStringLiteral("B on axis"),iaB);iao->addRow(QStringLiteral("Reactive near-field scale λ/2π"),iaNear);iao->addRow(QStringLiteral("Circumference / λ"),iaEl);iao->addRow(QStringLiteral("|Γ| vs Z0"),iaGamma);iao->addRow(iaNote);indAntTop->addWidget(iaOut,1);
    auto *iaSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::InductiveAntenna,indAntContent);iaSketch->setMinimumWidth(310);indAntTop->addWidget(iaSketch);
    auto *iaPlot=new FieldProfilePlot(indAntContent);iaPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));iaPlot->setXAxisLogarithmic(true);indAntRoot->addWidget(iaPlot,1);
    auto updateIndAntUi=[=](int idx){const bool planar=idx==2,solenoid=idx==1;iaRadius->setEnabled(!planar);iaSolLen->setEnabled(solenoid);iaWire->setEnabled(!planar);iaDin->setEnabled(planar);iaTw->setEnabled(planar);iaSpace->setEnabled(planar);iaCu->setEnabled(planar);};
    auto refreshIndAnt=[=]{EmEngineering::InductiveAntennaInput in;in.geometry=static_cast<EmEngineering::InductiveAntennaGeometry>(iaGeom->currentIndex());in.frequencyHz=iaFreq->value()*1e6;in.turns=iaTurns->value();in.radiusM=iaRadius->value()*1e-3;in.solenoidLengthM=iaSolLen->value()*1e-3;in.wireDiameterM=iaWire->value()*1e-3;in.innerDiameterM=iaDin->value()*1e-3;in.traceWidthM=iaTw->value()*1e-3;in.traceSpacingM=iaSpace->value()*1e-3;in.copperThicknessM=iaCu->value()*1e-6;in.currentA=iaCurrent->value();in.axialObservationM=iaZ->value()*1e-3;in.parasiticCapacitanceF=iaCp->value()*1e-12;in.feedLineOhm=iaZ0->value();auto r=EmEngineering::inductiveAntenna(in);iaSketch->setVariant(iaGeom->currentIndex());iaLam->setText(eng(r.wavelengthM,QStringLiteral("m")));iaDout->setText(eng(r.outerDiameterM,QStringLiteral("m")));iaArea->setText(eng(r.effectiveAreaM2Turns,QStringLiteral("m²-turn")));iaM->setText(eng(r.magneticMomentAm2,QStringLiteral("A·m²")));iaL->setText(eng(r.inductanceH,QStringLiteral("H")));iaLen->setText(eng(r.wireLengthM,QStringLiteral("m")));iaRdc->setText(eng(r.dcResistanceOhm,QStringLiteral("Ω")));iaRac->setText(eng(r.acResistanceOhm,QStringLiteral("Ω")));iaSkin->setText(eng(r.skinDepthM,QStringLiteral("m")));iaRr->setText(eng(r.radiationResistanceOhm,QStringLiteral("Ω")));iaZin->setText(complexText(r.inputImpedanceOhm));iaQ->setText(QString::number(r.unloadedQ,'g',7));iaSrf->setText(r.selfResonanceHz>0?eng(r.selfResonanceHz,QStringLiteral("Hz")):QStringLiteral("—"));iaB->setText(eng(r.axialFieldT,QStringLiteral("T")));iaNear->setText(eng(r.reactiveNearFieldScaleM,QStringLiteral("m")));iaEl->setText(QString::number(r.circumferenceLambda,'g',7));const std::complex<double> z0(iaZ0->value(),0);const auto gam=(r.inputImpedanceOhm-z0)/(r.inputImpedanceOhm+z0);iaGamma->setText(QString::number(std::abs(gam),'g',7));iaNote->setText(r.note);
        QVector<double> ff,zm,zp;const int np=180;const double fmin=std::max(1.0,in.frequencyHz/10.0),fmax=in.frequencyHz*10.0;for(int i=0;i<np;++i){const double u=double(i)/(np-1),f=fmin*std::pow(fmax/fmin,u);auto si=in;si.frequencyHz=f;const auto sr=EmEngineering::inductiveAntenna(si);ff.push_back(f);zm.push_back(std::abs(sr.inputImpedanceOhm));zp.push_back(std::arg(sr.inputImpedanceOhm)*180.0/EmEngineering::Pi);}iaPlot->setSeries({FieldProfileSeries{ff,zm,QStringLiteral("|Zin|"),QStringLiteral("Ω"),false},FieldProfileSeries{ff,zp,QStringLiteral("phase Zin"),QStringLiteral("deg"),false}},QStringLiteral("Inductive antenna impedance vs frequency — lumped/parasitic model"));};QObject::connect(iaCalc,&QPushButton::clicked,indAntContent,refreshIndAnt);QObject::connect(iaGeom,qOverload<int>(&QComboBox::currentIndexChanged),indAntContent,[=](int idx){updateIndAntUi(idx);refreshIndAnt();});
    for(auto *sp:{iaFreq,iaRadius,iaSolLen,iaWire,iaDin,iaTw,iaSpace,iaCu,iaCurrent,iaZ,iaCp,iaZ0}) QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),indAntContent,[=](double){refreshIndAnt();});
    QObject::connect(iaTurns,qOverload<int>(&QSpinBox::valueChanged),indAntContent,[=](int){refreshIndAnt();});
    updateIndAntUi(iaGeom->currentIndex());refreshIndAnt();tabs->addTab(scrollPage(indAntContent,tabs),QStringLiteral("Inductive antennas"));

    // ------------------------------------------------------------------
    // PCB transmission-line / trace impedance calculator
    // ------------------------------------------------------------------
    auto *tlContent=new QWidget(tabs);auto *tlRoot=new QVBoxLayout(tlContent);auto *tlTop=new QHBoxLayout();tlRoot->addLayout(tlTop);auto *tlIn=new QGroupBox(QStringLiteral("PCB trace / transmission-line stack-up"),tlContent);auto *tlf=new QFormLayout(tlIn);auto *tlGeom=new QComboBox(tlIn);tlGeom->addItems({QStringLiteral("Microstrip"),QStringLiteral("Symmetric stripline")});auto *tlFreq=spin(tlIn,1000,0.001,1e9,6),*tlW=spin(tlIn,1.5,0.000001,1e6,6),*tlT=spin(tlIn,35,0.001,1e6,4),*tlH=spin(tlIn,0.8,0.000001,1e6,6),*tlEr=spin(tlIn,4.2,1.000001,1000,6),*tlTan=spin(tlIn,0.018,0,1,8),*tlLen=spin(tlIn,50,0,1e9,6),*tlR=spin(tlIn,50,-1e9,1e9,6),*tlX=spin(tlIn,0,-1e9,1e9,6);tlf->addRow(QStringLiteral("Geometry"),tlGeom);tlf->addRow(QStringLiteral("Frequency (MHz)"),tlFreq);tlf->addRow(QStringLiteral("Trace width w (mm)"),tlW);tlf->addRow(QStringLiteral("Copper thickness t (µm)"),tlT);tlf->addRow(QStringLiteral("Dielectric height / ground spacing h (mm)"),tlH);tlf->addRow(QStringLiteral("εr"),tlEr);tlf->addRow(QStringLiteral("tanδ"),tlTan);tlf->addRow(QStringLiteral("Line length ℓ (mm)"),tlLen);tlf->addRow(QStringLiteral("Load R (Ω)"),tlR);tlf->addRow(QStringLiteral("Load X (Ω)"),tlX);auto *tlCalc=new QPushButton(QStringLiteral("Calculate trace impedance"),tlIn);tlf->addRow(tlCalc);tlTop->addWidget(tlIn);
    auto *tlOut=new QGroupBox(QStringLiteral("Transmission-line result"),tlContent);auto *tlo=new QFormLayout(tlOut);auto *tlZ0=valueLabel(tlOut),*tlEeff=valueLabel(tlOut),*tlWg=valueLabel(tlOut),*tlVp=valueLabel(tlOut),*tlBeta=valueLabel(tlOut),*tlTheta=valueLabel(tlOut),*tlSkin=valueLabel(tlOut),*tlRs=valueLabel(tlOut),*tlAc=valueLabel(tlOut),*tlAd=valueLabel(tlOut),*tlLoss=valueLabel(tlOut),*tlZin=valueLabel(tlOut),*tlNote=valueLabel(tlOut);tlo->addRow(QStringLiteral("Characteristic Z0"),tlZ0);tlo->addRow(QStringLiteral("Effective ε"),tlEeff);tlo->addRow(QStringLiteral("Guided λg"),tlWg);tlo->addRow(QStringLiteral("Phase velocity"),tlVp);tlo->addRow(QStringLiteral("β"),tlBeta);tlo->addRow(QStringLiteral("Electrical length"),tlTheta);tlo->addRow(QStringLiteral("Skin depth"),tlSkin);tlo->addRow(QStringLiteral("Surface resistance"),tlRs);tlo->addRow(QStringLiteral("Conductor loss"),tlAc);tlo->addRow(QStringLiteral("Dielectric loss"),tlAd);tlo->addRow(QStringLiteral("Total line loss"),tlLoss);tlo->addRow(QStringLiteral("Zin at this frequency"),tlZin);tlo->addRow(tlNote);tlTop->addWidget(tlOut,1);auto *tlSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::PcbTrace,tlContent);tlSketch->setMinimumWidth(300);tlTop->addWidget(tlSketch);
    auto *tlPlot=new FieldProfilePlot(tlContent);tlPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));tlPlot->setXAxisLogarithmic(true);tlRoot->addWidget(tlPlot,1);auto refreshTl=[=]{tlSketch->setVariant(tlGeom->currentIndex());EmEngineering::TransmissionLineInput in;in.geometry=static_cast<EmEngineering::TransmissionLineGeometry>(tlGeom->currentIndex());in.frequencyHz=tlFreq->value()*1e6;in.traceWidthM=tlW->value()*1e-3;in.copperThicknessM=tlT->value()*1e-6;in.dielectricHeightM=tlH->value()*1e-3;in.epsilonR=tlEr->value();in.lossTangent=tlTan->value();in.lineLengthM=tlLen->value()*1e-3;in.loadOhm={tlR->value(),tlX->value()};const auto r=EmEngineering::transmissionLine(in);if(!r.valid){tlZ0->setText(QStringLiteral("n/a"));tlNote->setText(r.note);tlPlot->clearData();return;}tlZ0->setText(eng(r.characteristicImpedanceOhm,QStringLiteral("Ω")));tlEeff->setText(QString::number(r.effectivePermittivity,'g',7));tlWg->setText(eng(r.guidedWavelengthM,QStringLiteral("m")));tlVp->setText(eng(r.phaseVelocityMps,QStringLiteral("m/s")));tlBeta->setText(eng(r.phaseConstantRadPerM,QStringLiteral("rad/m")));tlTheta->setText(QStringLiteral("%1°").arg(r.electricalLengthDeg,0,'g',7));tlSkin->setText(eng(r.skinDepthM,QStringLiteral("m")));tlRs->setText(eng(r.surfaceResistanceOhm,QStringLiteral("Ω/sq")));tlAc->setText(QStringLiteral("%1 dB/m").arg(r.conductorLossDbPerM,0,'g',7));tlAd->setText(QStringLiteral("%1 dB/m").arg(r.dielectricLossDbPerM,0,'g',7));tlLoss->setText(QStringLiteral("%1 dB").arg(r.totalLossDb,0,'g',7));tlZin->setText(complexText(r.inputImpedanceOhm));tlNote->setText(r.note);QVector<double> ff,mag,z0curve,phase;const int np=180;const double fmin=std::max(1.0,in.frequencyHz/20.0),fmax=in.frequencyHz*20.0;for(int i=0;i<np;++i){double u=double(i)/(np-1),f=fmin*std::pow(fmax/fmin,u);auto si=in;si.frequencyHz=f;auto sr=EmEngineering::transmissionLine(si);ff.push_back(f);mag.push_back(sr.valid?std::abs(sr.inputImpedanceOhm):std::numeric_limits<double>::quiet_NaN());z0curve.push_back(sr.valid?sr.characteristicImpedanceOhm:std::numeric_limits<double>::quiet_NaN());phase.push_back(sr.valid?std::arg(sr.inputImpedanceOhm)*180.0/EmEngineering::Pi:std::numeric_limits<double>::quiet_NaN());}tlPlot->setSeries({FieldProfileSeries{ff,mag,QStringLiteral("|Zin|"),QStringLiteral("Ω"),false},FieldProfileSeries{ff,z0curve,QStringLiteral("Z0 quasi-static"),QStringLiteral("Ω"),true},FieldProfileSeries{ff,phase,QStringLiteral("phase Zin"),QStringLiteral("deg"),false}},QStringLiteral("Trace impedance vs frequency — geometry Z0 and terminated lossy-line Zin"));};QObject::connect(tlCalc,&QPushButton::clicked,tlContent,refreshTl);for(auto*s:{tlFreq,tlW,tlT,tlH,tlEr,tlTan,tlLen,tlR,tlX})QObject::connect(s,qOverload<double>(&QDoubleSpinBox::valueChanged),tlContent,[=](double){refreshTl();});QObject::connect(tlGeom,qOverload<int>(&QComboBox::currentIndexChanged),tlContent,[=](int){refreshTl();});refreshTl();tabs->addTab(scrollPage(tlContent,tabs),QStringLiteral("PCB trace impedance"));

    // ------------------------------------------------------------------
    // RF non-ideal L/C component explorer
    // ------------------------------------------------------------------
    auto *rcContent=new QWidget(tabs);auto *rcRoot=new QVBoxLayout(rcContent);
    auto *rcTop=new QHBoxLayout();rcRoot->addLayout(rcTop);
    auto *rcIn=new QGroupBox(QStringLiteral("RF L/C non-ideal model"),rcContent);auto *rcf=new QFormLayout(rcIn);
    auto *rcType=new QComboBox(rcIn);rcType->addItems({QStringLiteral("Inductor"),QStringLiteral("Capacitor")});
    auto *rcValue=spin(rcIn,100.0,0.000001,1e15,7),*rcFreq=spin(rcIn,100.0,0.000001,1e9,7),*rcRs=spin(rcIn,0.10,0.0,1e9,7),*rcQref=spin(rcIn,80.0,0.0,1e9,6),*rcFref=spin(rcIn,100.0,0.000001,1e9,6),*rcExp=spin(rcIn,0.5,0.0,2.0,4),*rcCp=spin(rcIn,0.30,0.0,1e12,7),*rcEsl=spin(rcIn,0.50,0.0,1e12,7),*rcTan=spin(rcIn,0.002,0.0,10.0,8),*rcRp=spin(rcIn,100.0,0.0,1e12,6);
    auto *rcFmin=spin(rcIn,1.0,0.000001,1e9,6),*rcFmax=spin(rcIn,3000.0,0.000001,1e9,6);auto *rcLog=new QCheckBox(QStringLiteral("Logarithmic frequency axis"),rcIn);rcLog->setChecked(true);
    rcf->addRow(QStringLiteral("Component"),rcType);rcf->addRow(QStringLiteral("Nominal value (nH or pF)"),rcValue);rcf->addRow(QStringLiteral("Evaluation frequency (MHz)"),rcFreq);rcf->addRow(QStringLiteral("Series R / ESR (Ω)"),rcRs);rcf->addRow(QStringLiteral("Additional Q @ fref (0=off)"),rcQref);rcf->addRow(QStringLiteral("Q reference f (MHz)"),rcFref);rcf->addRow(QStringLiteral("Loss R exponent vs f"),rcExp);rcf->addRow(QStringLiteral("Inductor self-Cp (pF)"),rcCp);rcf->addRow(QStringLiteral("Capacitor ESL (nH)"),rcEsl);rcf->addRow(QStringLiteral("Capacitor tanδ"),rcTan);rcf->addRow(QStringLiteral("Inductor core-loss Rp (kΩ, 0=off)"),rcRp);rcf->addRow(QStringLiteral("Sweep start (MHz)"),rcFmin);rcf->addRow(QStringLiteral("Sweep stop (MHz)"),rcFmax);rcf->addRow(rcLog);rcTop->addWidget(rcIn);

    auto *rcAdvanced=new QGroupBox(QStringLiteral("Physical winding / ferrite / datasheet model"),rcContent);auto *rca=new QFormLayout(rcAdvanced);
    auto *rcWireOn=new QCheckBox(QStringLiteral("Use round-wire skin + proximity estimate"),rcAdvanced);
    auto *rcWireDia=spin(rcAdvanced,0.50,0.000001,1e6,6),*rcProx=spin(rcAdvanced,25.0,0.0,10000.0,4),*rcProxRef=spin(rcAdvanced,100.0,0.000001,1e9,6),*rcProxExp=spin(rcAdvanced,1.0,0.0,3.0,4);auto *rcDowellOn=new QCheckBox(QStringLiteral("Use Dowell-like multilayer estimate"),rcAdvanced);auto *rcLayers=new QSpinBox(rcAdvanced);rcLayers->setRange(1,100);rcLayers->setValue(1);auto *rcCoreOpOn=new QCheckBox(QStringLiteral("Use operating-point core loss from Core / winding loss tab"),rcAdvanced);
    auto *rcMuOn=new QCheckBox(QStringLiteral("Use single-pole complex-permeability core model"),rcAdvanced);
    auto *rcMuPreset=new QComboBox(rcAdvanced);rcMuPreset->addItems({QStringLiteral("Custom / datasheet μ data"),QStringLiteral("MnZn ferrite — generic LF start"),QStringLiteral("NiZn ferrite — generic RF start"),QStringLiteral("Powdered iron — generic start"),QStringLiteral("Nanocrystalline — generic LF start")});
    auto *rcMuStatic=spin(rcAdvanced,2000.0,1.0,1e7,4),*rcMuFc=spin(rcAdvanced,1.0,0.000001,1e9,6),*rcMuRef=spin(rcAdvanced,0.1,0.000001,1e9,6),*rcMuTan=spin(rcAdvanced,0.01,0.0,100.0,8),*rcMuExp=spin(rcAdvanced,0.5,0.0,3.0,4);
    auto *rcDataOn=new QCheckBox(QStringLiteral("Use measured / datasheet complex-Z curve (overrides lumped model)"),rcAdvanced);
    auto *rcDataText=new QPlainTextEdit(rcAdvanced);rcDataText->setPlaceholderText(QStringLiteral("f_MHz; R_ohm; X_ohm\n10; 0.20; 6.1\n100; 0.85; 61.8\n500; 4.2; -18.0"));rcDataText->setMaximumHeight(115);
    auto *rcDataLoad=new QPushButton(QStringLiteral("Load semicolon CSV…"),rcAdvanced);auto *rcDataStatus=valueLabel(rcAdvanced);rcDataStatus->setText(QStringLiteral("No datasheet points loaded."));
    rca->addRow(rcWireOn);rca->addRow(QStringLiteral("Round-wire diameter (mm)"),rcWireDia);rca->addRow(QStringLiteral("Extra proximity loss @ fref (%)"),rcProx);rca->addRow(QStringLiteral("Proximity fref (MHz)"),rcProxRef);rca->addRow(QStringLiteral("Proximity exponent"),rcProxExp);rca->addRow(rcDowellOn);rca->addRow(QStringLiteral("Winding layers"),rcLayers);rca->addRow(rcCoreOpOn);
    rca->addRow(rcMuOn);rca->addRow(QStringLiteral("Magnetic material preset"),rcMuPreset);rca->addRow(QStringLiteral("Low-frequency μs"),rcMuStatic);rca->addRow(QStringLiteral("μ relaxation fc (MHz)"),rcMuFc);rca->addRow(QStringLiteral("Nominal-L / loss fref (MHz)"),rcMuRef);rca->addRow(QStringLiteral("Extra tanδμ @ fref"),rcMuTan);rca->addRow(QStringLiteral("Extra magnetic-loss exponent"),rcMuExp);
    rca->addRow(rcDataOn);rca->addRow(QStringLiteral("Curve: f_MHz ; R ; X"),rcDataText);rca->addRow(rcDataLoad);rca->addRow(rcDataStatus);rcTop->addWidget(rcAdvanced);

    auto *rcOut=new QGroupBox(QStringLiteral("RF component result"),rcContent);auto *rco=new QFormLayout(rcOut);
    auto *rcZ=valueLabel(rcOut),*rcMag=valueLabel(rcOut),*rcPhase=valueLabel(rcOut),*rcQ=valueLabel(rcOut),*rcReff=valueLabel(rcOut),*rcLeff=valueLabel(rcOut),*rcMuResult=valueLabel(rcOut),*rcSkin=valueLabel(rcOut),*rcCoreOp=valueLabel(rcOut),*rcSrf=valueLabel(rcOut),*rcNote=valueLabel(rcOut);
    rco->addRow(QStringLiteral("Z(f)"),rcZ);rco->addRow(QStringLiteral("|Z|"),rcMag);rco->addRow(QStringLiteral("Phase"),rcPhase);rco->addRow(QStringLiteral("Effective Q=|Im Z|/|Re Z|"),rcQ);rco->addRow(QStringLiteral("Effective series-loss R"),rcReff);rco->addRow(QStringLiteral("Effective L(f)"),rcLeff);rco->addRow(QStringLiteral("Core μ′ / μ″ / tanδμ"),rcMuResult);rco->addRow(QStringLiteral("Skin / proximity / Dowell factors"),rcSkin);rco->addRow(QStringLiteral("Operating-point core loss"),rcCoreOp);rco->addRow(QStringLiteral("Approx. SRF"),rcSrf);rco->addRow(rcNote);rcTop->addWidget(rcOut,1);
    auto *rcPlot=new FieldProfilePlot(rcContent);rcPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));rcRoot->addWidget(rcPlot,1);

    auto parseRcCurve=[=](){std::vector<EmEngineering::ReactiveDatasheetPoint> pts;const auto lines=rcDataText->toPlainText().split('\n');for(const auto &raw:lines){const auto line=raw.trimmed();if(line.isEmpty()||line.startsWith('#'))continue;const auto cols=line.split(';');if(cols.size()<3)continue;bool okF=false,okR=false,okX=false;const double fm=cols[0].trimmed().toDouble(&okF),rr=cols[1].trimmed().toDouble(&okR),xx=cols[2].trimmed().toDouble(&okX);if(okF&&okR&&okX&&fm>0.0)pts.push_back({fm*1e6,rr,xx});}std::sort(pts.begin(),pts.end(),[](const auto&a,const auto&b){return a.frequencyHz<b.frequencyHz;});return pts;};
    auto rcParasitics=[=](){EmEngineering::ReactiveComponentParasitics p;p.enabled=true;p.seriesResistanceOhm=rcRs->value();p.qualityFactorAtReference=rcQref->value();p.referenceFrequencyHz=rcFref->value()*1e6;p.resistanceFrequencyExponent=rcExp->value();p.parasiticCapacitanceF=rcCp->value()*1e-12;p.parasiticInductanceH=rcEsl->value()*1e-9;p.lossTangent=rcTan->value();p.parallelResistanceOhm=rcRp->value()>0.0?rcRp->value()*1e3:0.0;p.usePhysicalWindingLoss=rcWireOn->isChecked();p.wireDiameterM=rcWireDia->value()*1e-3;p.proximityExtraAtReference=rcProx->value()/100.0;p.proximityReferenceFrequencyHz=rcProxRef->value()*1e6;p.proximityExponent=rcProxExp->value();p.useDowellMultilayerEstimate=rcDowellOn->isChecked();p.windingLayers=rcLayers->value();p.useOperatingPointCoreLoss=rcCoreOpOn->isChecked();p.operatingPointCoreLoss=*sharedCoreLossModel;p.useMagneticMaterialModel=rcMuOn->isChecked();p.magneticMaterial.enabled=p.useMagneticMaterialModel;p.magneticMaterial.lowFrequencyRelativePermeability=rcMuStatic->value();p.magneticMaterial.relaxationFrequencyHz=rcMuFc->value()*1e6;p.magneticMaterial.referenceFrequencyHz=rcMuRef->value()*1e6;p.magneticMaterial.additionalLossTangentAtReference=rcMuTan->value();p.magneticMaterial.additionalLossExponent=rcMuExp->value();p.useDatasheetCurve=rcDataOn->isChecked();p.datasheetCurve=parseRcCurve();return p;};
    auto rcNominal=[=](){return rcType->currentIndex()==0?rcValue->value()*1e-9:rcValue->value()*1e-12;};
    auto lastRfInductorModel=std::make_shared<EmEngineering::ReactiveComponentParasitics>();auto lastRfCapacitorModel=std::make_shared<EmEngineering::ReactiveComponentParasitics>();
    auto applyRcMuPreset=[=](int i){struct P{double mu,fc,ref,tan,e;};static const P pset[]={{2000,1.0,0.1,.01,.5},{2000,.3,.05,.02,.6},{250,15.0,1.0,.01,.5},{12,80.0,1.0,.005,.5},{15000,.05,.01,.03,.7}};if(i>0&&i<=4){const auto&q=pset[i];rcMuStatic->setValue(q.mu);rcMuFc->setValue(q.fc);rcMuRef->setValue(q.ref);rcMuTan->setValue(q.tan);rcMuExp->setValue(q.e);}};
    auto updateRcUi=[=](){const bool ind=rcType->currentIndex()==0;rcCp->setEnabled(ind);rcRp->setEnabled(ind);rcEsl->setEnabled(!ind);rcTan->setEnabled(!ind);rcWireOn->setEnabled(ind);for(auto*w:{rcWireDia,rcProx,rcProxRef,rcProxExp,rcMuStatic,rcMuFc,rcMuRef,rcMuTan,rcMuExp})w->setEnabled(ind);rcMuOn->setEnabled(ind);rcMuPreset->setEnabled(ind);rcDowellOn->setEnabled(ind&&rcWireOn->isChecked());rcLayers->setEnabled(ind&&rcWireOn->isChecked()&&rcDowellOn->isChecked());rcCoreOpOn->setEnabled(ind);};
    auto refreshRc=[=]{updateRcUi();const auto type=rcType->currentIndex()==0?EmEngineering::ReactiveComponentType::Inductor:EmEngineering::ReactiveComponentType::Capacitor;const auto p=rcParasitics();if(type==EmEngineering::ReactiveComponentType::Inductor)*lastRfInductorModel=p;else *lastRfCapacitorModel=p;rcDataStatus->setText(QStringLiteral("%1 valid complex-Z point(s)%2").arg(p.datasheetCurve.size()).arg(p.useDatasheetCurve?QStringLiteral(" — curve active"):QString()));const auto r=EmEngineering::reactiveComponent(type,rcNominal(),rcFreq->value()*1e6,p);rcZ->setText(complexText(r.impedanceOhm));rcMag->setText(eng(std::abs(r.impedanceOhm),QStringLiteral("Ω")));rcPhase->setText(QStringLiteral("%1°").arg(std::arg(r.impedanceOhm)*180.0/EmEngineering::Pi,0,'g',6));rcQ->setText(std::isfinite(r.effectiveQualityFactor)?QString::number(r.effectiveQualityFactor,'g',7):QStringLiteral("∞"));rcReff->setText(eng(r.effectiveSeriesResistanceOhm,QStringLiteral("Ω")));rcLeff->setText(type==EmEngineering::ReactiveComponentType::Inductor?eng(r.effectiveInductanceH,QStringLiteral("H")):QStringLiteral("—"));rcMuResult->setText(type==EmEngineering::ReactiveComponentType::Inductor&&p.useMagneticMaterialModel?QStringLiteral("%1 / %2 / %3").arg(r.magneticMuPrime,0,'g',6).arg(r.magneticMuDoublePrime,0,'g',6).arg(r.magneticLossTangent,0,'g',6):QStringLiteral("—"));rcSkin->setText(type==EmEngineering::ReactiveComponentType::Inductor&&p.usePhysicalWindingLoss?QStringLiteral("δ=%1 ; Fskin=%2 ; Fprox=%3 ; Fdowell=%4").arg(eng(r.skinDepthM,QStringLiteral("m"))).arg(r.skinEffectFactor,0,'g',5).arg(r.proximityEffectFactor,0,'g',5).arg(r.dowellMultilayerFactor,0,'g',5):QStringLiteral("—"));rcCoreOp->setText(type==EmEngineering::ReactiveComponentType::Inductor&&p.useOperatingPointCoreLoss?eng(r.operatingPointCoreLossW,QStringLiteral("W")):QStringLiteral("—"));rcSrf->setText(r.approximateSelfResonanceHz>0.0?eng(r.approximateSelfResonanceHz,QStringLiteral("Hz")):QStringLiteral("—"));rcNote->setText(r.note);QVector<double> ff,mag,qv,ph;const int np=241;const double f0=std::max(1.0,rcFmin->value()*1e6),f1=std::max(f0*1.000001,rcFmax->value()*1e6);for(int i=0;i<np;++i){const double u=double(i)/(np-1);const double f=rcLog->isChecked()?f0*std::pow(f1/f0,u):f0+(f1-f0)*u;const auto x=EmEngineering::reactiveComponent(type,rcNominal(),f,p);ff.push_back(f);mag.push_back(std::abs(x.impedanceOhm));qv.push_back(std::isfinite(x.effectiveQualityFactor)?std::min(x.effectiveQualityFactor,1000.0):1000.0);ph.push_back(std::arg(x.impedanceOhm)*180.0/EmEngineering::Pi);}rcPlot->setXAxisLogarithmic(rcLog->isChecked());rcPlot->setSeries({FieldProfileSeries{ff,mag,QStringLiteral("|Z|"),QStringLiteral("Ω"),false},FieldProfileSeries{ff,qv,QStringLiteral("Q (clipped 1000)"),QString(),true},FieldProfileSeries{ff,ph,QStringLiteral("phase"),QStringLiteral("deg"),true}},QStringLiteral("RF component impedance — lumped, complex-μ/winding, or datasheet model"));QVector<FieldPlotMarker> mk{{QStringLiteral("eval"),rcFreq->value()*1e6,true,false,false}};if(r.approximateSelfResonanceHz>0)mk.push_back({QStringLiteral("SRF"),r.approximateSelfResonanceHz,true,true,false});rcPlot->setMarkers(mk);};
    QObject::connect(rcDataLoad,&QPushButton::clicked,rcContent,[=]{const auto fn=QFileDialog::getOpenFileName(rcContent,QStringLiteral("Load component complex-Z CSV"),QString(),QStringLiteral("CSV / text (*.csv *.txt);;All files (*)"));if(fn.isEmpty())return;QFile f(fn);if(f.open(QIODevice::ReadOnly|QIODevice::Text)){QTextStream ts(&f);rcDataText->setPlainText(ts.readAll());rcDataOn->setChecked(true);refreshRc();}});
    QObject::connect(rcDataText,&QPlainTextEdit::textChanged,rcContent,refreshRc);QObject::connect(rcDataOn,&QCheckBox::toggled,rcContent,[=](bool){refreshRc();});QObject::connect(rcWireOn,&QCheckBox::toggled,rcContent,[=](bool){refreshRc();});QObject::connect(rcDowellOn,&QCheckBox::toggled,rcContent,[=](bool){refreshRc();});QObject::connect(rcCoreOpOn,&QCheckBox::toggled,rcContent,[=](bool){refreshRc();});QObject::connect(rcLayers,qOverload<int>(&QSpinBox::valueChanged),rcContent,[=](int){refreshRc();});QObject::connect(rcMuOn,&QCheckBox::toggled,rcContent,[=](bool){refreshRc();});QObject::connect(rcMuPreset,qOverload<int>(&QComboBox::currentIndexChanged),rcContent,[=](int i){applyRcMuPreset(i);refreshRc();});QObject::connect(rcType,qOverload<int>(&QComboBox::currentIndexChanged),rcContent,[=](int){refreshRc();});QObject::connect(rcLog,&QCheckBox::toggled,rcContent,[=](bool){refreshRc();});for(auto *sp:{rcValue,rcFreq,rcRs,rcQref,rcFref,rcExp,rcCp,rcEsl,rcTan,rcRp,rcFmin,rcFmax,rcWireDia,rcProx,rcProxRef,rcProxExp,rcMuStatic,rcMuFc,rcMuRef,rcMuTan,rcMuExp})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),rcContent,[=](double){refreshRc();});refreshRc();tabs->addTab(scrollPage(rcContent,tabs),QStringLiteral("RF L/C models"));

    // ------------------------------------------------------------------
    // Passive RLC filter synthesis / response
    // ------------------------------------------------------------------
    auto *fltContent=new QWidget(tabs);auto *fltRoot=new QVBoxLayout(fltContent);auto *fltTop=new QHBoxLayout();fltRoot->addLayout(fltTop);auto *fltIn=new QGroupBox(QStringLiteral("2nd-order RLC / antenna trap synthesis"),fltContent);auto *flf=new QFormLayout(fltIn);auto *fltType=new QComboBox(fltIn);fltType->addItems({QStringLiteral("Low-pass RLC"),QStringLiteral("High-pass RLC"),QStringLiteral("Band-pass series RLC"),QStringLiteral("Notch RLC prototype"),QStringLiteral("Shunt series-LC antenna trap")});fltType->setCurrentIndex(4);auto *fltF0=spin(fltIn,100,0.000001,1e9,6),*fltQ=spin(fltIn,10,0.05,1e6,6),*fltR=spin(fltIn,50,0.001,1e9,6),*fltLpref=spin(fltIn,100,0.000001,1e12,6),*fltRs=spin(fltIn,50,0.001,1e9,6),*fltRl=spin(fltIn,50,0.001,1e9,6),*fltFmin=spin(fltIn,10,0.000001,1e9,6),*fltFmax=spin(fltIn,1000,0.000001,1e9,6);auto *fltPts=new QSpinBox(fltIn);fltPts->setRange(51,2001);fltPts->setValue(401);auto *fltLog=new QCheckBox(QStringLiteral("Logarithmic frequency axis"),fltIn);fltLog->setChecked(true);flf->addRow(QStringLiteral("Topology"),fltType);flf->addRow(QStringLiteral("Target f0 / cutoff (MHz)"),fltF0);flf->addRow(QStringLiteral("Target Q"),fltQ);flf->addRow(QStringLiteral("Reference / damping R (Ω)"),fltR);flf->addRow(QStringLiteral("Preferred trap L (nH)"),fltLpref);flf->addRow(QStringLiteral("Source R for shunt trap (Ω)"),fltRs);flf->addRow(QStringLiteral("Load R for shunt trap (Ω)"),fltRl);flf->addRow(QStringLiteral("Sweep start (MHz)"),fltFmin);flf->addRow(QStringLiteral("Sweep stop (MHz)"),fltFmax);flf->addRow(QStringLiteral("Sweep points"),fltPts);flf->addRow(fltLog);auto *fltCalc=new QPushButton(QStringLiteral("Synthesize / plot response"),fltIn);flf->addRow(fltCalc);fltTop->addWidget(fltIn);
    auto *fltOut=new QGroupBox(QStringLiteral("Filter design result"),fltContent);auto *flo=new QFormLayout(fltOut);auto *fltDesc=valueLabel(fltOut),*fltL=valueLabel(fltOut),*fltC=valueLabel(fltOut),*fltLossR=valueLabel(fltOut),*fltFr=valueLabel(fltOut),*fltBw=valueLabel(fltOut),*fltMeasured=valueLabel(fltOut),*fltDepth=valueLabel(fltOut),*fltNote=valueLabel(fltOut);flo->addRow(QStringLiteral("Topology"),fltDesc);flo->addRow(QStringLiteral("L"),fltL);flo->addRow(QStringLiteral("C"),fltC);flo->addRow(QStringLiteral("Damping / resonator ESR"),fltLossR);flo->addRow(QStringLiteral("Calculated resonance"),fltFr);flo->addRow(QStringLiteral("Nominal BW=f0/Q"),fltBw);flo->addRow(QStringLiteral("Measured -3 dB region"),fltMeasured);flo->addRow(QStringLiteral("Peak / notch depth"),fltDepth);flo->addRow(fltNote);fltTop->addWidget(fltOut,1);auto *fltSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::Filter,fltContent);fltSketch->setMinimumWidth(300);fltTop->addWidget(fltSketch);
    auto *fltPlot=new FieldProfilePlot(fltContent);fltPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));fltRoot->addWidget(fltPlot,1);
    auto updateFilterUi=[=](int idx){const bool trap=idx==int(EmEngineering::FilterTopology::ShuntSeriesLcTrap);fltLpref->setEnabled(trap);fltRs->setEnabled(trap);fltRl->setEnabled(trap);fltR->setEnabled(!trap);};
    auto refreshFilter=[=]{fltSketch->setVariant(fltType->currentIndex());EmEngineering::FilterDesignInput in;in.topology=static_cast<EmEngineering::FilterTopology>(fltType->currentIndex());in.centerFrequencyHz=fltF0->value()*1e6;in.qualityFactor=fltQ->value();in.referenceResistanceOhm=fltR->value();in.preferredInductanceH=fltLpref->value()*1e-9;in.sourceResistanceOhm=fltRs->value();in.loadResistanceOhm=fltRl->value();const auto d=EmEngineering::designFilter(in);fltDesc->setText(d.topologyDescription);fltL->setText(eng(d.inductanceH,QStringLiteral("H")));fltC->setText(eng(d.capacitanceF,QStringLiteral("F")));fltLossR->setText(eng(d.dampingOrLossResistanceOhm,QStringLiteral("Ω")));fltFr->setText(eng(d.resonantFrequencyHz,QStringLiteral("Hz")));fltBw->setText(eng(d.nominalBandwidthHz,QStringLiteral("Hz")));fltNote->setText(d.note);const int np=fltPts->value();const double fmin=std::max(1.0,fltFmin->value()*1e6),fmax=std::max(fmin*1.000001,fltFmax->value()*1e6);QVector<double> ff,db,ph;ff.reserve(np);db.reserve(np);ph.reserve(np);double maxDb=-1e300,minDb=1e300;int bestMax=0,bestMin=0;for(int i=0;i<np;++i){double u=double(i)/(np-1);double f=fltLog->isChecked()?fmin*std::pow(fmax/fmin,u):fmin+(fmax-fmin)*u;const auto h=EmEngineering::filterTransfer(in,f);double md=20.0*std::log10(std::max(1e-12,std::abs(h)));ff.push_back(f);db.push_back(md);ph.push_back(std::arg(h)*180.0/EmEngineering::Pi);if(md>maxDb){maxDb=md;bestMax=i;}if(md<minDb){minDb=md;bestMin=i;}}fltPlot->setXAxisLogarithmic(fltLog->isChecked());fltPlot->setSeries({FieldProfileSeries{ff,db,QStringLiteral("|H|"),QStringLiteral("dB"),false},FieldProfileSeries{ff,ph,QStringLiteral("phase H"),QStringLiteral("deg"),false}},QStringLiteral("Passive-filter transfer function"));const bool notch=in.topology==EmEngineering::FilterTopology::NotchSeriesRlc||in.topology==EmEngineering::FilterTopology::ShuntSeriesLcTrap;const double threshold=notch?maxDb-3.0:maxDb-3.0;int left=-1,right=-1;const int center=notch?bestMin:bestMax;for(int i=center;i>=0;--i)if(db[i]>=threshold){left=i;break;}for(int i=center;i<np;++i)if(db[i]>=threshold){right=i;break;}if(!notch){left=-1;right=-1;for(int i=0;i<np;++i)if(db[i]>=threshold){if(left<0)left=i;right=i;}}QVector<FieldPlotMarker> filterMarkers;filterMarkers.push_back({QStringLiteral("target"),in.centerFrequencyHz,true,true,false});if(left>=0&&right>=0&&right>left){const double bw=ff[right]-ff[left];fltMeasured->setText(QStringLiteral("%1 … %2  (BW %3, Q≈%4)").arg(eng(ff[left],QStringLiteral("Hz"))).arg(eng(ff[right],QStringLiteral("Hz"))).arg(eng(bw,QStringLiteral("Hz"))).arg(in.centerFrequencyHz/std::max(1.0,bw),0,'g',5));filterMarkers.push_back({QStringLiteral("fL"),ff[left],true,false,false});filterMarkers.push_back({QStringLiteral("fH"),ff[right],true,false,false});}else fltMeasured->setText(QStringLiteral("Not fully bracketed by current sweep"));fltPlot->setMarkers(filterMarkers);fltDepth->setText(notch?QStringLiteral("notch %1 dB @ %2").arg(minDb,0,'f',2).arg(eng(ff[bestMin],QStringLiteral("Hz"))):QStringLiteral("peak %1 dB @ %2").arg(maxDb,0,'f',2).arg(eng(ff[bestMax],QStringLiteral("Hz"))));};QObject::connect(fltCalc,&QPushButton::clicked,fltContent,refreshFilter);QObject::connect(fltLog,&QCheckBox::toggled,fltContent,[=](bool){refreshFilter();});QObject::connect(fltType,qOverload<int>(&QComboBox::currentIndexChanged),fltContent,[=](int idx){updateFilterUi(idx);refreshFilter();});
    for(auto *sp:{fltF0,fltQ,fltR,fltLpref,fltRs,fltRl,fltFmin,fltFmax}) QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),fltContent,[=](double){refreshFilter();});
    QObject::connect(fltPts,qOverload<int>(&QSpinBox::valueChanged),fltContent,[=](int){refreshFilter();});
    updateFilterUi(fltType->currentIndex());refreshFilter();tabs->addTab(scrollPage(fltContent,tabs),QStringLiteral("Filter designer"));


    // ------------------------------------------------------------------
    // Cascaded RF chain: antenna -> trap / L-match -> PCB line -> source port
    // ------------------------------------------------------------------
    auto *chainContent=new QWidget(tabs);
    auto *chainRoot=new QVBoxLayout(chainContent);
    auto *chainTop=new QGridLayout();
    chainRoot->addLayout(chainTop);

    auto importedSweep=std::make_shared<ImportedAntennaSweep>();
    auto matchSolutions=std::make_shared<std::vector<EmEngineering::LMatchSolution>>();
    auto widebandTuned=std::make_shared<bool>(false);

    auto *chainLoadBox=new QGroupBox(QStringLiteral("Antenna / load source"),chainContent);
    auto *chainLoadForm=new QFormLayout(chainLoadBox);
    chainLoadForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    chainLoadForm->setHorizontalSpacing(12);
    chainLoadForm->setVerticalSpacing(6);
    chainLoadBox->setMinimumWidth(430);
    auto *chainSource=new QComboBox(chainLoadBox);
    chainSource->addItems({QStringLiteral("Manual complex Zin"),QStringLiteral("Imported Antenna Designer sweep")});
    auto *chainTarget=spin(chainLoadBox,100.0,0.000001,1e9,6);
    auto *chainR=spin(chainLoadBox,50.0,0.000001,1e9,7);
    auto *chainX=spin(chainLoadBox,0.0,-1e9,1e9,7);
    auto *chainZref=spin(chainLoadBox,50.0,0.001,1e9,6);
    auto *chainImportedInfo=valueLabel(chainLoadBox);
    chainImportedInfo->setText(QStringLiteral("No Antenna Designer data imported yet."));
    auto *chainUseSimple=new QPushButton(QStringLiteral("Use simple antenna model"),chainLoadBox);
    auto *chainUseInductive=new QPushButton(QStringLiteral("Use inductive-antenna model"),chainLoadBox);
    chainLoadForm->addRow(QStringLiteral("Load data"),chainSource);
    chainLoadForm->addRow(QStringLiteral("Design / match frequency (MHz)"),chainTarget);
    chainLoadForm->addRow(QStringLiteral("Manual antenna R (Ω)"),chainR);
    chainLoadForm->addRow(QStringLiteral("Manual antenna X (Ω)"),chainX);
    chainLoadForm->addRow(QStringLiteral("Source reference Z0 (Ω)"),chainZref);
    chainLoadForm->addRow(chainUseSimple);
    chainLoadForm->addRow(chainUseInductive);
    chainLoadForm->addRow(chainImportedInfo);
    chainTop->addWidget(chainLoadBox,0,0);

    auto *chainNetworkBox=new QGroupBox(QStringLiteral("Matching / receive trap"),chainContent);
    auto *chainNetworkForm=new QFormLayout(chainNetworkBox);
    chainNetworkForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    chainNetworkForm->setHorizontalSpacing(12);
    chainNetworkForm->setVerticalSpacing(6);
    chainNetworkBox->setMinimumWidth(430);
    auto *chainTrapOn=new QCheckBox(QStringLiteral("Enable shunt series-LC trap at antenna terminal"),chainNetworkBox);
    auto *chainTrapF=spin(chainNetworkBox,100.0,0.000001,1e9,6);
    auto *chainTrapL=spin(chainNetworkBox,100.0,0.000001,1e12,6);
    auto *chainTrapQ=spin(chainNetworkBox,50.0,0.05,1e9,6);
    auto *chainTrapC=valueLabel(chainNetworkBox);
    auto *chainCopyTrap=new QPushButton(QStringLiteral("Use Filter designer trap settings"),chainNetworkBox);
    auto *chainMatchOn=new QCheckBox(QStringLiteral("Enable synthesized 2-element L-match"),chainNetworkBox);
    chainMatchOn->setChecked(true);
    auto *chainSynthesize=new QPushButton(QStringLiteral("Synthesize L-match at design frequency"),chainNetworkBox);
    auto *chainBranch=new QComboBox(chainNetworkBox);
    auto *chainSeries=valueLabel(chainNetworkBox),*chainShunt=valueLabel(chainNetworkBox),*chainOrder=valueLabel(chainNetworkBox);
    chainNetworkForm->addRow(chainTrapOn);
    chainNetworkForm->addRow(QStringLiteral("Trap f0 (MHz)"),chainTrapF);
    chainNetworkForm->addRow(QStringLiteral("Trap L (nH)"),chainTrapL);
    chainNetworkForm->addRow(QStringLiteral("Trap Q"),chainTrapQ);
    chainNetworkForm->addRow(QStringLiteral("Calculated trap C"),chainTrapC);
    chainNetworkForm->addRow(chainCopyTrap);
    chainNetworkForm->addRow(chainMatchOn);
    chainNetworkForm->addRow(chainSynthesize);
    chainNetworkForm->addRow(QStringLiteral("L-match solution"),chainBranch);
    chainNetworkForm->addRow(QStringLiteral("Series element"),chainSeries);
    chainNetworkForm->addRow(QStringLiteral("Shunt element"),chainShunt);
    chainNetworkForm->addRow(QStringLiteral("Order"),chainOrder);
    chainTop->addWidget(chainNetworkBox,0,1);

    auto *chainLineBox=new QGroupBox(QStringLiteral("Optional PCB feed line"),chainContent);
    auto *chainLineForm=new QFormLayout(chainLineBox);
    chainLineForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    chainLineForm->setHorizontalSpacing(12);
    chainLineForm->setVerticalSpacing(6);
    chainLineBox->setMinimumWidth(430);
    auto *chainLineOn=new QCheckBox(QStringLiteral("Enable PCB transmission line before matching network"),chainLineBox);
    auto *chainLineGeom=new QComboBox(chainLineBox);chainLineGeom->addItems({QStringLiteral("Microstrip"),QStringLiteral("Symmetric stripline")});
    auto *chainLineW=spin(chainLineBox,1.5,0.000001,1e6,6),*chainLineT=spin(chainLineBox,35,0.001,1e6,4),*chainLineH=spin(chainLineBox,0.8,0.000001,1e6,6);
    auto *chainLineEr=spin(chainLineBox,4.2,1.000001,1000,6),*chainLineTan=spin(chainLineBox,0.018,0,1,8),*chainLineLen=spin(chainLineBox,50,0,1e9,6);
    auto *chainCopyLine=new QPushButton(QStringLiteral("Use PCB trace calculator settings"),chainLineBox);
    chainLineForm->addRow(chainLineOn);
    chainLineForm->addRow(QStringLiteral("Geometry"),chainLineGeom);
    chainLineForm->addRow(QStringLiteral("Trace width w (mm)"),chainLineW);
    chainLineForm->addRow(QStringLiteral("Copper t (µm)"),chainLineT);
    chainLineForm->addRow(QStringLiteral("Dielectric h (mm)"),chainLineH);
    chainLineForm->addRow(QStringLiteral("εr"),chainLineEr);
    chainLineForm->addRow(QStringLiteral("tanδ"),chainLineTan);
    chainLineForm->addRow(QStringLiteral("Line length ℓ (mm)"),chainLineLen);
    chainLineForm->addRow(chainCopyLine);
    chainTop->addWidget(chainLineBox,1,0);

    auto *chainSketch=new EngineeringSketchWidget(EngineeringSketchWidget::Kind::RfChain,chainContent);
    chainSketch->setMinimumWidth(430);
    chainSketch->setMinimumHeight(215);
    chainSketch->setMaximumHeight(300);
    chainTop->addWidget(chainSketch,1,1);

    auto *chainParBox=new QGroupBox(QStringLiteral("Non-ideal RF L/C components"),chainContent);auto *chainParGrid=new QGridLayout(chainParBox);
    auto *chainNonIdeal=new QCheckBox(QStringLiteral("Use ESR / Q / SRF models in matching and trap"),chainParBox);
    auto *chainParFref=spin(chainParBox,100.0,0.000001,1e9,6),*chainLrs=spin(chainParBox,0.10,0.0,1e9,6),*chainLq=spin(chainParBox,80.0,0.0,1e9,6),*chainLcp=spin(chainParBox,0.30,0.0,1e12,6),*chainLrp=spin(chainParBox,100.0,0.0,1e12,6),*chainCesr=spin(chainParBox,0.05,0.0,1e9,6),*chainCesl=spin(chainParBox,0.50,0.0,1e12,6),*chainCtan=spin(chainParBox,0.002,0.0,10.0,8);
    auto *chainWireOn=new QCheckBox(QStringLiteral("Physical round-wire skin / proximity for inductors"),chainParBox);auto *chainWireDia=spin(chainParBox,0.50,0.000001,1e6,6),*chainProx=spin(chainParBox,25.0,0.0,10000.0,4),*chainProxExp=spin(chainParBox,1.0,0.0,3.0,4);
    auto *chainMuOn=new QCheckBox(QStringLiteral("Complex-permeability inductor core model"),chainParBox);auto *chainMu=spin(chainParBox,250.0,1.0,1e7,4),*chainMuFc=spin(chainParBox,15.0,0.000001,1e9,6),*chainMuTan=spin(chainParBox,0.01,0.0,100.0,8),*chainMuExp=spin(chainParBox,0.5,0.0,3.0,4);
    auto *chainUseCopiedL=new QCheckBox(QStringLiteral("Use copied RF L/C inductor model / datasheet curve"),chainParBox),*chainUseCopiedC=new QCheckBox(QStringLiteral("Use copied RF L/C capacitor model / datasheet curve"),chainParBox);auto *chainCopyL=new QPushButton(QStringLiteral("Copy current RF L/C inductor model"),chainParBox),*chainCopyC=new QPushButton(QStringLiteral("Copy current RF L/C capacitor model"),chainParBox);auto chainCopiedL=std::make_shared<EmEngineering::ReactiveComponentParasitics>();auto chainCopiedC=std::make_shared<EmEngineering::ReactiveComponentParasitics>();
    chainParGrid->addWidget(chainNonIdeal,0,0,1,4);chainParGrid->addWidget(new QLabel(QStringLiteral("Q reference f (MHz)"),chainParBox),1,0);chainParGrid->addWidget(chainParFref,1,1);chainParGrid->addWidget(new QLabel(QStringLiteral("Inductor series R (Ω)"),chainParBox),1,2);chainParGrid->addWidget(chainLrs,1,3);
    chainParGrid->addWidget(new QLabel(QStringLiteral("Inductor Q @ fref"),chainParBox),2,0);chainParGrid->addWidget(chainLq,2,1);chainParGrid->addWidget(new QLabel(QStringLiteral("Inductor self-Cp (pF)"),chainParBox),2,2);chainParGrid->addWidget(chainLcp,2,3);
    chainParGrid->addWidget(new QLabel(QStringLiteral("Inductor core Rp (kΩ)"),chainParBox),3,0);chainParGrid->addWidget(chainLrp,3,1);chainParGrid->addWidget(new QLabel(QStringLiteral("Capacitor ESR (Ω)"),chainParBox),3,2);chainParGrid->addWidget(chainCesr,3,3);
    chainParGrid->addWidget(new QLabel(QStringLiteral("Capacitor ESL (nH)"),chainParBox),4,0);chainParGrid->addWidget(chainCesl,4,1);chainParGrid->addWidget(new QLabel(QStringLiteral("Capacitor tanδ"),chainParBox),4,2);chainParGrid->addWidget(chainCtan,4,3);
    chainParGrid->addWidget(chainWireOn,5,0,1,2);chainParGrid->addWidget(new QLabel(QStringLiteral("Wire diameter (mm)"),chainParBox),5,2);chainParGrid->addWidget(chainWireDia,5,3);
    chainParGrid->addWidget(new QLabel(QStringLiteral("Proximity extra @ fref (%)"),chainParBox),6,0);chainParGrid->addWidget(chainProx,6,1);chainParGrid->addWidget(new QLabel(QStringLiteral("Proximity exponent"),chainParBox),6,2);chainParGrid->addWidget(chainProxExp,6,3);
    chainParGrid->addWidget(chainMuOn,7,0,1,2);chainParGrid->addWidget(new QLabel(QStringLiteral("Low-frequency μs"),chainParBox),7,2);chainParGrid->addWidget(chainMu,7,3);
    chainParGrid->addWidget(new QLabel(QStringLiteral("μ relaxation fc (MHz)"),chainParBox),8,0);chainParGrid->addWidget(chainMuFc,8,1);chainParGrid->addWidget(new QLabel(QStringLiteral("Extra tanδμ @ fref"),chainParBox),8,2);chainParGrid->addWidget(chainMuTan,8,3);
    chainParGrid->addWidget(new QLabel(QStringLiteral("Magnetic-loss exponent"),chainParBox),9,0);chainParGrid->addWidget(chainMuExp,9,1);chainParGrid->addWidget(chainUseCopiedL,9,2,1,2);
    chainParGrid->addWidget(chainCopyL,10,0,1,2);chainParGrid->addWidget(chainUseCopiedC,10,2,1,2);chainParGrid->addWidget(chainCopyC,11,2,1,2);
    auto *chainParNote=valueLabel(chainParBox);chainParNote->setText(QStringLiteral("Non-ideal matching can use lumped ESR/Q/SRF, round-wire skin/proximity, single-pole complex μ, or a complex-Z curve copied from RF L/C models. The wideband optimizer reuses the same component model for every candidate."));chainParGrid->addWidget(chainParNote,12,0,1,4);chainTop->addWidget(chainParBox,2,0,1,2);
    chainTop->setColumnStretch(0,1);
    chainTop->setColumnStretch(1,1);
    chainTop->setHorizontalSpacing(12);
    chainTop->setVerticalSpacing(10);

    auto *chainSweepBox=new QGroupBox(QStringLiteral("Frequency sweep / matching bandwidth"),chainContent);
    auto *chainSweepForm=new QGridLayout(chainSweepBox);
    auto *chainFmin=spin(chainSweepBox,70,0.000001,1e9,6),*chainFmax=spin(chainSweepBox,130,0.000001,1e9,6);
    auto *chainPoints=new QSpinBox(chainSweepBox);chainPoints->setRange(11,4001);chainPoints->setValue(401);
    auto *chainLog=new QCheckBox(QStringLiteral("Log frequency axis"),chainSweepBox);
    auto *chainVswrLimit=spin(chainSweepBox,2.0,1.0001,1000,4),*chainS11Limit=spin(chainSweepBox,-10.0,-300,0,3);
    auto *chainRun=new QPushButton(QStringLiteral("Synthesize + run cascaded sweep"),chainSweepBox);
    auto *chainOptObjective=new QComboBox(chainSweepBox);chainOptObjective->addItems({QStringLiteral("Worst |Γ| over band"),QStringLiteral("Mean |Γ| over band")});
    auto *chainOptSamples=new QSpinBox(chainSweepBox);chainOptSamples->setRange(5,101);chainOptSamples->setValue(31);
    auto *chainOptPasses=new QSpinBox(chainSweepBox);chainOptPasses->setRange(1,10);chainOptPasses->setValue(4);
    auto *chainOptSpan=spin(chainSweepBox,50.0,1.0,200.0,2);
    auto *chainOptimize=new QPushButton(QStringLiteral("Optimize current L-match over band"),chainSweepBox);
    auto *chainOptResult=valueLabel(chainSweepBox);chainOptResult->setText(QStringLiteral("Wideband optimization not run."));
    chainSweepForm->addWidget(new QLabel(QStringLiteral("Start (MHz)"),chainSweepBox),0,0);chainSweepForm->addWidget(chainFmin,0,1);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("Stop (MHz)"),chainSweepBox),0,2);chainSweepForm->addWidget(chainFmax,0,3);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("Points"),chainSweepBox),0,4);chainSweepForm->addWidget(chainPoints,0,5);
    chainSweepForm->addWidget(chainLog,0,6);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("VSWR band ≤"),chainSweepBox),1,0);chainSweepForm->addWidget(chainVswrLimit,1,1);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("S11 band ≤ dB"),chainSweepBox),1,2);chainSweepForm->addWidget(chainS11Limit,1,3);
    chainSweepForm->addWidget(chainRun,1,4,1,3);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("Wideband objective"),chainSweepBox),2,0);chainSweepForm->addWidget(chainOptObjective,2,1);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("Optimization samples"),chainSweepBox),2,2);chainSweepForm->addWidget(chainOptSamples,2,3);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("Passes"),chainSweepBox),2,4);chainSweepForm->addWidget(chainOptPasses,2,5);
    chainSweepForm->addWidget(new QLabel(QStringLiteral("Initial span ± %"),chainSweepBox),3,0);chainSweepForm->addWidget(chainOptSpan,3,1);chainSweepForm->addWidget(chainOptimize,3,2,1,2);chainSweepForm->addWidget(chainOptResult,3,4,1,3);
    chainRoot->addWidget(chainSweepBox);

    auto *chainOut=new QGroupBox(QStringLiteral("Design-frequency result"),chainContent);
    auto *chainOutGrid=new QGridLayout(chainOut);
    auto *chainZant=valueLabel(chainOut),*chainZtrap=valueLabel(chainOut),*chainZmatch=valueLabel(chainOut),*chainZsource=valueLabel(chainOut);
    auto *chainS11=valueLabel(chainOut),*chainVswr=valueLabel(chainOut),*chainLineResult=valueLabel(chainOut),*chainBandwidth=valueLabel(chainOut),*chainNote=valueLabel(chainOut);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("Antenna Zin"),chainOut),0,0);chainOutGrid->addWidget(chainZant,0,1);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("After optional trap"),chainOut),0,2);chainOutGrid->addWidget(chainZtrap,0,3);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("After L-match"),chainOut),1,0);chainOutGrid->addWidget(chainZmatch,1,1);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("Source-plane Zin"),chainOut),1,2);chainOutGrid->addWidget(chainZsource,1,3);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("S11 / return loss"),chainOut),2,0);chainOutGrid->addWidget(chainS11,2,1);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("VSWR"),chainOut),2,2);chainOutGrid->addWidget(chainVswr,2,3);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("PCB line"),chainOut),3,0);chainOutGrid->addWidget(chainLineResult,3,1);
    chainOutGrid->addWidget(new QLabel(QStringLiteral("Sweep bands"),chainOut),3,2);chainOutGrid->addWidget(chainBandwidth,3,3);
    chainOutGrid->addWidget(chainNote,4,0,1,4);
    chainRoot->addWidget(chainOut);

    auto *chainResults=new QTabWidget(chainContent);
    chainResults->setMinimumHeight(260);
    auto *chainS11Plot=new FieldProfilePlot(chainResults);chainS11Plot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    auto *chainZPlot=new FieldProfilePlot(chainResults);chainZPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    auto *chainVswrPlot=new FieldProfilePlot(chainResults);chainVswrPlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    auto *chainStagePlot=new FieldProfilePlot(chainResults);chainStagePlot->setXAxis(QStringLiteral("Frequency"),QStringLiteral("Hz"));
    auto *chainSmith=new RfSmithChartWidget(chainResults);
    chainResults->addTab(chainS11Plot,QStringLiteral("S11 comparison"));
    chainResults->addTab(chainZPlot,QStringLiteral("Source Zin"));
    chainResults->addTab(chainVswrPlot,QStringLiteral("VSWR"));
    chainResults->addTab(chainStagePlot,QStringLiteral("Impedance stages"));
    chainResults->addTab(chainSmith,QStringLiteral("Smith"));

    auto trapCapacitance=[=](){const double w=2.0*EmEngineering::Pi*std::max(1.0,chainTrapF->value()*1e6);return 1.0/(w*w*std::max(1e-18,chainTrapL->value()*1e-9));};
    auto antennaAt=[=](double frequencyHz,bool *ok)->std::complex<double>{
        if(chainSource->currentIndex()==1)return interpolateAntennaSweep(*importedSweep,frequencyHz,ok);
        if(ok)*ok=true;return {chainR->value(),chainX->value()};
    };
    auto selectedMatch=[=](){EmEngineering::LMatchSolution s;const int i=chainBranch->currentIndex();if(i>=0&&i<int(matchSolutions->size()))s=(*matchSolutions)[static_cast<std::size_t>(i)];return s;};
    auto matchingReference=[=](double frequencyHz){if(!chainLineOn->isChecked())return chainZref->value();EmEngineering::TransmissionLineInput line;line.geometry=static_cast<EmEngineering::TransmissionLineGeometry>(chainLineGeom->currentIndex());line.frequencyHz=frequencyHz;line.traceWidthM=chainLineW->value()*1e-3;line.copperThicknessM=chainLineT->value()*1e-6;line.dielectricHeightM=chainLineH->value()*1e-3;line.epsilonR=chainLineEr->value();line.lossTangent=chainLineTan->value();line.lineLengthM=chainLineLen->value()*1e-3;line.loadOhm={50,0};const auto r=EmEngineering::transmissionLine(line);return r.valid?r.characteristicImpedanceOhm:chainZref->value();};
    auto chainInductorParasitics=[=](){if(chainUseCopiedL->isChecked()){auto p=*chainCopiedL;p.enabled=chainNonIdeal->isChecked();return p;}EmEngineering::ReactiveComponentParasitics p;p.enabled=chainNonIdeal->isChecked();p.seriesResistanceOhm=chainLrs->value();p.qualityFactorAtReference=chainLq->value();p.referenceFrequencyHz=chainParFref->value()*1e6;p.resistanceFrequencyExponent=0.5;p.parasiticCapacitanceF=chainLcp->value()*1e-12;p.parallelResistanceOhm=chainLrp->value()>0.0?chainLrp->value()*1e3:0.0;p.usePhysicalWindingLoss=chainWireOn->isChecked();p.wireDiameterM=chainWireDia->value()*1e-3;p.proximityExtraAtReference=chainProx->value()/100.0;p.proximityReferenceFrequencyHz=chainParFref->value()*1e6;p.proximityExponent=chainProxExp->value();p.useMagneticMaterialModel=chainMuOn->isChecked();p.magneticMaterial.enabled=p.useMagneticMaterialModel;p.magneticMaterial.lowFrequencyRelativePermeability=chainMu->value();p.magneticMaterial.relaxationFrequencyHz=chainMuFc->value()*1e6;p.magneticMaterial.referenceFrequencyHz=chainParFref->value()*1e6;p.magneticMaterial.additionalLossTangentAtReference=chainMuTan->value();p.magneticMaterial.additionalLossExponent=chainMuExp->value();return p;};
    auto chainCapacitorParasitics=[=](){if(chainUseCopiedC->isChecked()){auto p=*chainCopiedC;p.enabled=chainNonIdeal->isChecked();return p;}EmEngineering::ReactiveComponentParasitics p;p.enabled=chainNonIdeal->isChecked();p.seriesResistanceOhm=chainCesr->value();p.referenceFrequencyHz=chainParFref->value()*1e6;p.parasiticInductanceH=chainCesl->value()*1e-9;p.lossTangent=chainCtan->value();return p;};
    auto makeChainInput=[=](double frequencyHz,const std::complex<double>&zant){
        EmEngineering::RfChainInput in;in.frequencyHz=frequencyHz;in.referenceOhm=chainZref->value();in.antennaImpedanceOhm=zant;
        in.trapEnabled=chainTrapOn->isChecked();in.trapInductanceH=chainTrapL->value()*1e-9;in.trapCapacitanceF=trapCapacitance();in.trapQualityFactor=chainTrapQ->value();in.trapReferenceFrequencyHz=chainTrapF->value()*1e6;
        in.nonIdealTrapComponents=chainNonIdeal->isChecked();in.trapInductorParasitics=chainInductorParasitics();in.trapCapacitorParasitics=chainCapacitorParasitics();
        in.matchingEnabled=chainMatchOn->isChecked()&&!matchSolutions->empty();in.matching=selectedMatch();in.nonIdealMatchingComponents=chainNonIdeal->isChecked();in.matchingInductorParasitics=chainInductorParasitics();in.matchingCapacitorParasitics=chainCapacitorParasitics();
        in.lineEnabled=chainLineOn->isChecked();in.line.geometry=static_cast<EmEngineering::TransmissionLineGeometry>(chainLineGeom->currentIndex());in.line.traceWidthM=chainLineW->value()*1e-3;in.line.copperThicknessM=chainLineT->value()*1e-6;in.line.dielectricHeightM=chainLineH->value()*1e-3;in.line.epsilonR=chainLineEr->value();in.line.lossTangent=chainLineTan->value();in.line.lineLengthM=chainLineLen->value()*1e-3;
        return in;
    };

    std::function<void()> updateMatchLabels;
    std::function<void()> synthesizeMatch;
    std::function<void()> refreshChainTarget;
    updateMatchLabels=[=](){
        chainTrapC->setText(eng(trapCapacitance(),QStringLiteral("F")));
        const int i=chainBranch->currentIndex();
        if(!chainMatchOn->isChecked()){chainSeries->setText(QStringLiteral("disabled"));chainShunt->setText(QStringLiteral("disabled"));chainOrder->setText(QStringLiteral("No matching network"));return;}
        if(i<0||i>=int(matchSolutions->size())){chainSeries->setText(QStringLiteral("—"));chainShunt->setText(QStringLiteral("—"));chainOrder->setText(QStringLiteral("Synthesize a solution"));return;}
        const auto &m=(*matchSolutions)[static_cast<std::size_t>(i)];chainSeries->setText(reactiveComponentText(m.seriesComponent,m.seriesValue));chainShunt->setText(reactiveComponentText(m.shuntComponent,m.shuntValue));QString mode=chainNonIdeal->isChecked()?QStringLiteral("non-ideal ESR/Q/SRF model"):QStringLiteral("ideal reactances");chainOrder->setText(m.description+QStringLiteral(" | ")+mode+QStringLiteral(" | match reference ≈ ")+eng(matchingReference(chainTarget->value()*1e6),QStringLiteral("Ω"))+QStringLiteral(" | synthesis Zin = ")+complexText(m.matchedInputOhm));
    };
    synthesizeMatch=[=](){
        *widebandTuned=false;chainOptResult->setText(QStringLiteral("Wideband optimization not run."));matchSolutions->clear();chainBranch->clear();chainTrapC->setText(eng(trapCapacitance(),QStringLiteral("F")));
        if(!chainMatchOn->isChecked()){updateMatchLabels();return;}
        bool ok=false;const double f0=chainTarget->value()*1e6;const auto zant=antennaAt(f0,&ok);if(!ok){chainOrder->setText(QStringLiteral("Target frequency is outside imported antenna sweep."));updateMatchLabels();return;}
        auto pre=makeChainInput(f0,zant);pre.matchingEnabled=false;pre.lineEnabled=false;const auto pr=EmEngineering::rfChain(pre);if(!pr.valid){chainOrder->setText(pr.note);updateMatchLabels();return;}
        *matchSolutions=EmEngineering::synthesizeLMatch(pr.antennaWithTrapOhm,matchingReference(f0),f0);
        for(int i=0;i<int(matchSolutions->size());++i){const auto &m=(*matchSolutions)[static_cast<std::size_t>(i)];chainBranch->addItem(QStringLiteral("Solution %1 — %2 / %3").arg(i+1).arg(reactiveComponentText(m.seriesComponent,m.seriesValue),reactiveComponentText(m.shuntComponent,m.shuntValue)));}
        if(chainBranch->count()>0)chainBranch->setCurrentIndex(0);updateMatchLabels();
    };
    refreshChainTarget=[=](){
        updateMatchLabels();bool ok=false;const double f0=chainTarget->value()*1e6;const auto zant=antennaAt(f0,&ok);if(!ok){chainZant->setText(QStringLiteral("n/a — target outside imported sweep"));chainNote->setText(QStringLiteral("Choose a target frequency inside the Antenna Designer sweep or switch to manual load data."));return;}
        const auto r=EmEngineering::rfChain(makeChainInput(f0,zant));chainZant->setText(complexText(r.rawAntennaImpedanceOhm));chainZtrap->setText(complexText(r.antennaWithTrapOhm));chainZmatch->setText(complexText(r.afterMatchingOhm));chainZsource->setText(complexText(r.sourceInputOhm));
        const double s11db=20.0*std::log10(std::max(1e-15,std::abs(r.reflectionCoefficient)));chainS11->setText(QStringLiteral("%1 dB  (RL %2 dB, |Γ|=%3)").arg(s11db,0,'g',6).arg(r.returnLossDb,0,'g',6).arg(std::abs(r.reflectionCoefficient),0,'g',6));chainVswr->setText(std::isfinite(r.vswr)?QString::number(r.vswr,'g',7):QStringLiteral("∞"));chainLineResult->setText(chainLineOn->isChecked()?QStringLiteral("Z0≈%1 Ω | loss≈%2 dB").arg(r.lineCharacteristicImpedanceOhm,0,'g',6).arg(r.lineLossDb,0,'g',5):QStringLiteral("disabled"));chainNote->setText(r.note);
    };

    auto runChainSweep=[=](){
        if(!*widebandTuned)synthesizeMatch();refreshChainTarget();
        const double fmin=std::min(chainFmin->value(),chainFmax->value())*1e6,fmax=std::max(chainFmin->value(),chainFmax->value())*1e6;const int n=chainPoints->value();
        QVector<double> ff,rawDb,chainDb,rr,xx,swr,zaMag,zmMag,zsMag;QVector<std::complex<double>> gg;ff.reserve(n);gg.reserve(n);
        for(int i=0;i<n;++i){const double u=double(i)/(n-1);const double f=chainLog->isChecked()?fmin*std::pow(fmax/fmin,u):fmin+(fmax-fmin)*u;bool ok=false;const auto zant=antennaAt(f,&ok);if(!ok)continue;const auto r=EmEngineering::rfChain(makeChainInput(f,zant));if(!r.valid)continue;const std::complex<double> zref(chainZref->value(),0);const auto gr=(zant-zref)/(zant+zref);ff.push_back(f);rawDb.push_back(20*std::log10(std::max(1e-15,std::abs(gr))));chainDb.push_back(20*std::log10(std::max(1e-15,std::abs(r.reflectionCoefficient))));rr.push_back(r.sourceInputOhm.real());xx.push_back(r.sourceInputOhm.imag());swr.push_back(std::isfinite(r.vswr)?std::min(r.vswr,50.0):50.0);zaMag.push_back(std::abs(r.rawAntennaImpedanceOhm));zmMag.push_back(std::abs(r.afterMatchingOhm));zsMag.push_back(std::abs(r.sourceInputOhm));gg.push_back(r.reflectionCoefficient);}
        if(ff.size()<3){chainBandwidth->setText(QStringLiteral("Sweep has fewer than 3 valid points. For imported data, keep the chain sweep inside the Antenna Designer sweep range."));chainSmith->clearSweep();return;}
        int best=0,target=0;const double ft=chainTarget->value()*1e6;for(int i=1;i<ff.size();++i){if(std::abs(gg[i])<std::abs(gg[best]))best=i;if(std::abs(ff[i]-ft)<std::abs(ff[target]-ft))target=i;}
        auto bandAround=[&](auto predicate){int lo=-1,hi=-1;if(!predicate(best))return std::pair<int,int>{lo,hi};lo=hi=best;while(lo>0&&predicate(lo-1))--lo;while(hi+1<ff.size()&&predicate(hi+1))++hi;return std::pair<int,int>{lo,hi};};
        const auto vb=bandAround([&](int i){return swr[i]<=chainVswrLimit->value();});const auto sb=bandAround([&](int i){return chainDb[i]<=chainS11Limit->value();});
        auto bandText=[&](std::pair<int,int>b,const QString&name){if(b.first<0)return name+QStringLiteral(": none in sampled span");const double lo=ff[b.first],hi=ff[b.second];return QStringLiteral("%1: %2 … %3 (BW %4)").arg(name,eng(lo,QStringLiteral("Hz")),eng(hi,QStringLiteral("Hz")),eng(hi-lo,QStringLiteral("Hz")));};
        chainBandwidth->setText(bandText(vb,QStringLiteral("VSWR band"))+QStringLiteral(" | ")+bandText(sb,QStringLiteral("S11 band")));
        QVector<FieldPlotMarker> marks{{QStringLiteral("target"),ff[target],true,false,false},{QStringLiteral("best"),ff[best],true,true,false}};
        for(auto *pl:{chainS11Plot,chainZPlot,chainVswrPlot,chainStagePlot}){pl->setXAxisLogarithmic(chainLog->isChecked());pl->setMarkers(marks);}
        chainS11Plot->setSeries({FieldProfileSeries{ff,rawDb,QStringLiteral("raw antenna S11"),QStringLiteral("dB"),true},FieldProfileSeries{ff,chainDb,QStringLiteral("source-plane S11"),QStringLiteral("dB"),false}},QStringLiteral("Antenna versus complete RF-chain reflection coefficient"));
        chainZPlot->setSeries({FieldProfileSeries{ff,rr,QStringLiteral("Rin source"),QStringLiteral("Ω"),false},FieldProfileSeries{ff,xx,QStringLiteral("Xin source"),QStringLiteral("Ω"),true}},QStringLiteral("Complex input impedance at source reference plane"));
        chainVswrPlot->setSeries({FieldProfileSeries{ff,swr,QStringLiteral("VSWR (clipped at 50)"),QString(),false}},QStringLiteral("Source-plane VSWR"));
        chainStagePlot->setSeries({FieldProfileSeries{ff,zaMag,QStringLiteral("|Z antenna|"),QStringLiteral("Ω"),true},FieldProfileSeries{ff,zmMag,QStringLiteral("|Z after matching|"),QStringLiteral("Ω"),false},FieldProfileSeries{ff,zsMag,QStringLiteral("|Z source plane|"),QStringLiteral("Ω"),false}},QStringLiteral("Impedance magnitude through the cascaded RF chain"));
        chainSmith->setSweep(ff,gg,best,target);chainResults->setCurrentWidget(chainS11Plot);refreshChainTarget();
    };

    auto optimizeChainMatch=[=](){
        synthesizeMatch();
        const int idx=chainBranch->currentIndex();
        if(idx<0 || idx>=int(matchSolutions->size())){chainOptResult->setText(QStringLiteral("No valid L-match solution to optimize."));return;}
        const double fmin=std::min(chainFmin->value(),chainFmax->value())*1e6,fmax=std::max(chainFmin->value(),chainFmax->value())*1e6;
        EmEngineering::WidebandMatchOptimizationInput oi;
        bool ok0=false;const auto z0=antennaAt(chainTarget->value()*1e6,&ok0);if(!ok0){chainOptResult->setText(QStringLiteral("Target is outside the imported antenna sweep."));return;}
        oi.chainTemplate=makeChainInput(chainTarget->value()*1e6,z0);oi.chainTemplate.matching=(*matchSolutions)[static_cast<std::size_t>(idx)];oi.chainTemplate.matchingEnabled=true;
        oi.objective=chainOptObjective->currentIndex()==0?EmEngineering::WidebandMatchObjective::WorstReflectionMagnitude:EmEngineering::WidebandMatchObjective::MeanReflectionMagnitude;
        oi.initialRelativeSpan=chainOptSpan->value()/100.0;oi.samplesPerCoordinate=9;oi.passes=chainOptPasses->value();
        const int nf=chainOptSamples->value();oi.samples.reserve(static_cast<std::size_t>(nf));
        for(int i=0;i<nf;++i){const double u=double(i)/(nf-1);const double f=chainLog->isChecked()?fmin*std::pow(fmax/fmin,u):fmin+(fmax-fmin)*u;bool ok=false;const auto z=antennaAt(f,&ok);if(ok)oi.samples.push_back({f,z});}
        const auto ro=EmEngineering::optimizeWidebandLMatch(oi);if(!ro.valid){chainOptResult->setText(ro.note);return;}
        auto best=ro.optimizedMatching;best.description+=QStringLiteral(" | wideband tuned");
        auto targetInput=makeChainInput(chainTarget->value()*1e6,z0);targetInput.matching=best;targetInput.matchingEnabled=true;const auto tr=EmEngineering::rfChain(targetInput);if(tr.valid)best.matchedInputOhm=tr.afterMatchingOhm;
        (*matchSolutions)[static_cast<std::size_t>(idx)]=best;*widebandTuned=true;
        const QString metric=oi.objective==EmEngineering::WidebandMatchObjective::WorstReflectionMagnitude?QStringLiteral("worst |Γ|"):QStringLiteral("mean |Γ|");
        chainOptResult->setText(QStringLiteral("%1: %2 → %3 | series %4 | shunt %5").arg(metric).arg(ro.initialObjective,0,'g',6).arg(ro.optimizedObjective,0,'g',6).arg(reactiveComponentText(best.seriesComponent,best.seriesValue),reactiveComponentText(best.shuntComponent,best.shuntValue)));
        updateMatchLabels();refreshChainTarget();runChainSweep();
    };

    QObject::connect(chainSynthesize,&QPushButton::clicked,chainContent,[=]{synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainBranch,qOverload<int>(&QComboBox::currentIndexChanged),chainContent,[=](int){updateMatchLabels();refreshChainTarget();});
    QObject::connect(chainRun,&QPushButton::clicked,chainContent,runChainSweep);
    QObject::connect(chainOptimize,&QPushButton::clicked,chainContent,optimizeChainMatch);
    QObject::connect(chainCopyLine,&QPushButton::clicked,chainContent,[=]{chainLineGeom->setCurrentIndex(tlGeom->currentIndex());chainLineW->setValue(tlW->value());chainLineT->setValue(tlT->value());chainLineH->setValue(tlH->value());chainLineEr->setValue(tlEr->value());chainLineTan->setValue(tlTan->value());chainLineLen->setValue(tlLen->value());chainLineOn->setChecked(true);refreshChainTarget();});
    QObject::connect(chainCopyTrap,&QPushButton::clicked,chainContent,[=]{chainTrapF->setValue(fltF0->value());chainTrapL->setValue(fltLpref->value());chainTrapQ->setValue(fltQ->value());chainTrapOn->setChecked(true);synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainUseSimple,&QPushButton::clicked,chainContent,[=]{EmEngineering::AntennaInput in;in.type=static_cast<EmEngineering::AntennaType>(atype->currentIndex());in.frequencyHz=chainTarget->value()*1e6;in.lengthM=alen->value();in.wireRadiusM=awr->value()*1e-3;in.loopRadiusM=alr->value();in.turns=aturns->value();in.feedLineOhm=chainZref->value();const auto r=EmEngineering::antennaEstimate(in);if(!r.reactanceDefined){chainImportedInfo->setText(QStringLiteral("The selected simple antenna model does not provide reactance; use the MoM designer or manual Zin for matching synthesis."));return;}chainR->setValue(r.resistanceOhm);chainX->setValue(r.reactanceOhm);chainSource->setCurrentIndex(0);chainImportedInfo->setText(QStringLiteral("Loaded from simple antenna engineering model at target frequency."));synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainUseInductive,&QPushButton::clicked,chainContent,[=]{EmEngineering::InductiveAntennaInput in;in.geometry=static_cast<EmEngineering::InductiveAntennaGeometry>(iaGeom->currentIndex());in.frequencyHz=chainTarget->value()*1e6;in.turns=iaTurns->value();in.radiusM=iaRadius->value()*1e-3;in.solenoidLengthM=iaSolLen->value()*1e-3;in.wireDiameterM=iaWire->value()*1e-3;in.innerDiameterM=iaDin->value()*1e-3;in.traceWidthM=iaTw->value()*1e-3;in.traceSpacingM=iaSpace->value()*1e-3;in.copperThicknessM=iaCu->value()*1e-6;in.currentA=iaCurrent->value();in.axialObservationM=iaZ->value()*1e-3;in.parasiticCapacitanceF=iaCp->value()*1e-12;in.feedLineOhm=chainZref->value();const auto r=EmEngineering::inductiveAntenna(in);chainR->setValue(std::max(1e-6,r.inputImpedanceOhm.real()));chainX->setValue(r.inputImpedanceOhm.imag());chainSource->setCurrentIndex(0);chainImportedInfo->setText(QStringLiteral("Loaded complex Zin from Inductive antennas at target frequency."));synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainSource,qOverload<int>(&QComboBox::currentIndexChanged),chainContent,[=](int idx){chainR->setEnabled(idx==0);chainX->setEnabled(idx==0);synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainMatchOn,&QCheckBox::toggled,chainContent,[=](bool){synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainTrapOn,&QCheckBox::toggled,chainContent,[=](bool){synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainLineOn,&QCheckBox::toggled,chainContent,[=](bool){synthesizeMatch();refreshChainTarget();});
    for(auto *sp:{chainTarget,chainR,chainX,chainZref,chainTrapF,chainTrapL,chainTrapQ})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),chainContent,[=](double){synthesizeMatch();refreshChainTarget();});
    for(auto *sp:{chainLineW,chainLineT,chainLineH,chainLineEr,chainLineTan,chainLineLen})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),chainContent,[=](double){synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainLineGeom,qOverload<int>(&QComboBox::currentIndexChanged),chainContent,[=](int){synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainNonIdeal,&QCheckBox::toggled,chainContent,[=](bool){synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainCopyL,&QPushButton::clicked,chainContent,[=]{*chainCopiedL=*lastRfInductorModel;chainUseCopiedL->setChecked(true);synthesizeMatch();refreshChainTarget();});
    QObject::connect(chainCopyC,&QPushButton::clicked,chainContent,[=]{*chainCopiedC=*lastRfCapacitorModel;chainUseCopiedC->setChecked(true);synthesizeMatch();refreshChainTarget();});
    for(auto *cb:{chainUseCopiedL,chainUseCopiedC,chainWireOn,chainMuOn})QObject::connect(cb,&QCheckBox::toggled,chainContent,[=](bool){if(chainNonIdeal->isChecked()){synthesizeMatch();refreshChainTarget();}});
    for(auto *sp:{chainParFref,chainLrs,chainLq,chainLcp,chainLrp,chainCesr,chainCesl,chainCtan,chainWireDia,chainProx,chainProxExp,chainMu,chainMuFc,chainMuTan,chainMuExp})QObject::connect(sp,qOverload<double>(&QDoubleSpinBox::valueChanged),chainContent,[=](double){if(chainNonIdeal->isChecked()){synthesizeMatch();refreshChainTarget();}});

    QObject::connect(antennaDesigner,&AntennaDesignerWidget::antennaImpedanceAvailable,chainContent,[=](double f,double r,double x,double z0,const QString&feed){chainTarget->setValue(f/1e6);chainR->setValue(std::max(1e-6,r));chainX->setValue(x);chainZref->setValue(z0);chainSource->setCurrentIndex(0);chainImportedInfo->setText(QStringLiteral("Latest MoM point imported from %1 at %2.").arg(feed,eng(f,QStringLiteral("Hz"))));synthesizeMatch();refreshChainTarget();});
    QObject::connect(antennaDesigner,&AntennaDesignerWidget::antennaSweepAvailable,chainContent,[=](const QVector<double>&f,const QVector<double>&r,const QVector<double>&x,double z0,const QString&feed){importedSweep->frequencyHz=f;importedSweep->resistanceOhm=r;importedSweep->reactanceOhm=x;importedSweep->referenceOhm=z0;importedSweep->feedName=feed;if(importedSweep->valid()){chainSource->setCurrentIndex(1);chainZref->setValue(z0);chainFmin->setValue(f.front()/1e6);chainFmax->setValue(f.back()/1e6);chainPoints->setValue(std::clamp(int(f.size()),11,4001));if(chainTarget->value()*1e6<f.front()||chainTarget->value()*1e6>f.back())chainTarget->setValue(0.5*(f.front()+f.back())/1e6);chainImportedInfo->setText(QStringLiteral("Imported %1-point complex sweep from %2: %3 … %4").arg(f.size()).arg(feed,eng(f.front(),QStringLiteral("Hz")),eng(f.back(),QStringLiteral("Hz"))));synthesizeMatch();refreshChainTarget();}});

    synthesizeMatch();refreshChainTarget();

    // Keep the dense RF-chain controls at their natural form height and put them
    // in their own scrollable upper pane. The previous single resizable content
    // widget let the result plots consume vertical space and Qt compressed the
    // QFormLayout rows down to a few pixels. A splitter now makes the allocation
    // explicit and lets the user resize controls versus plots without clipping.
    for (auto *box : {chainLoadBox,chainNetworkBox,chainLineBox,chainParBox})
    {
        box->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);
        box->setMinimumHeight(box->sizeHint().height());
    }
    chainSweepBox->setMinimumHeight(chainSweepBox->sizeHint().height());
    chainOut->setMinimumHeight(chainOut->sizeHint().height());
    chainContent->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);
    chainContent->setMinimumHeight(chainContent->sizeHint().height());

    auto *chainPage=new QWidget(tabs);
    auto *chainPageLayout=new QVBoxLayout(chainPage);chainPageLayout->setContentsMargins(0,0,0,0);
    auto *chainSplit=new QSplitter(Qt::Vertical,chainPage);chainSplit->setChildrenCollapsible(false);chainSplit->setHandleWidth(6);
    auto *chainControlScroll=new QScrollArea(chainSplit);chainControlScroll->setWidgetResizable(true);chainControlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);chainControlScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);chainControlScroll->setWidget(chainContent);
    chainSplit->addWidget(chainControlScroll);chainSplit->addWidget(chainResults);chainSplit->setStretchFactor(0,0);chainSplit->setStretchFactor(1,1);chainSplit->setSizes({590,330});
    chainPageLayout->addWidget(chainSplit);
    tabs->addTab(chainPage,QStringLiteral("RF chain / matching"));

    auto *numerical = new NumericalSolversWorkspace(tabs);
    const int numericalTab = tabs->addTab(numerical, QStringLiteral("Numerical solvers  ⓘ"));
    tabs->setTabToolTip(numericalTab, QStringLiteral(
        "<qt><b>Numerical solvers</b><br/><br/>"
        "These solvers make discretization and validity assumptions explicit: 2D TMz/TEz FDTD with CPML, "
        "multiple sources/probes and RF analysis; 2D electrostatic BEM for infinitely long conductor sections; "
        "MoM/Pocklington for thin-wire dipoles; and the Neumann integral for loop coupling. "
        "For quantitative work, repeat calculations with several grid steps, meshes or segment counts to verify convergence."
        "</qt>"));
}
