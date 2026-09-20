#include "widgets/laplace_rail_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
QString number(double v, int precision = 8)
{
    return QString::number(v, 'g', precision);
}

QDoubleSpinBox *spin(QWidget *parent, double value, double min, double max, int decimals, const QString &suffix = {})
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setValue(value);
    s->setKeyboardTracking(false);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}

double stateValue(const LaplaceRail::State &s, int quantity)
{
    switch (quantity)
    {
    case 0: return s.positionM;
    case 1: return s.velocityMps;
    case 2: return s.currentA;
    case 3: return s.forceN;
    case 4: return s.motionalEmfV;
    case 5: return s.accelerationMps2;
    default: return s.positionM;
    }
}

QString quantityName(int quantity)
{
    switch (quantity)
    {
    case 0: return QStringLiteral("Position x(t) [m]");
    case 1: return QStringLiteral("Velocity v(t) [m/s]");
    case 2: return QStringLiteral("Current I(t) [A]");
    case 3: return QStringLiteral("Laplace force F(t) [N]");
    case 4: return QStringLiteral("Motional EMF e(t) [V]");
    case 5: return QStringLiteral("Acceleration a(t) [m/s²]");
    default: return QStringLiteral("Value");
    }
}
} // namespace

class LaplaceRailGraphWidget final : public QWidget
{
public:
    explicit LaplaceRailGraphWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(260);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setResult(const LaplaceRail::Result &result) { m_states = result.states; update(); }
    void setQuantity(int q) { m_quantity = q; update(); }
    void setSelectedTime(double t)
    {
        m_selectedTime = t;
        update();
    }
    void setHoverHandler(std::function<void(double)> handler) { m_hoverHandler = std::move(handler); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor fg = palette().color(QPalette::Text);
        const QColor grid = palette().color(QPalette::Mid);
        const QColor accent = palette().color(QPalette::Highlight);
        p.fillRect(rect(), palette().color(QPalette::Base));

        const QRectF plot(64.0, 24.0, std::max(20.0, width() - 86.0), std::max(40.0, height() - 70.0));
        p.setPen(QPen(grid, 1));
        for (int i = 0; i <= 5; ++i)
        {
            const double x = plot.left() + plot.width() * i / 5.0;
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (int i = 0; i <= 4; ++i)
        {
            const double y = plot.top() + plot.height() * i / 4.0;
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        p.setPen(fg);
        p.drawText(QRectF(plot.left(), 2.0, plot.width(), 20.0), Qt::AlignCenter, quantityName(m_quantity));
        p.drawText(QRectF(plot.left(), plot.bottom() + 8.0, plot.width(), 20.0), Qt::AlignCenter, QStringLiteral("time [s] — hover/click to inspect"));

        if (m_states.size() < 2) return;
        double ymin = std::numeric_limits<double>::infinity();
        double ymax = -std::numeric_limits<double>::infinity();
        for (const auto &s : m_states)
        {
            const double v = stateValue(s, m_quantity);
            ymin = std::min(ymin, v); ymax = std::max(ymax, v);
        }
        if (!(ymax > ymin)) { const double d = std::max(1.0, std::abs(ymax) * 0.1); ymin -= d; ymax += d; }
        const double pad = 0.05 * (ymax - ymin); ymin -= pad; ymax += pad;
        const double tmax = std::max(1e-12, m_states.back().timeS);
        auto mapPoint = [&](const LaplaceRail::State &s) {
            const double x = plot.left() + plot.width() * s.timeS / tmax;
            const double y = plot.bottom() - plot.height() * (stateValue(s, m_quantity) - ymin) / (ymax - ymin);
            return QPointF(x, y);
        };
        QPainterPath path;
        path.moveTo(mapPoint(m_states.front()));
        for (std::size_t i = 1; i < m_states.size(); ++i) path.lineTo(mapPoint(m_states[i]));
        p.setPen(QPen(accent, 2.2));
        p.drawPath(path);

        p.setPen(fg);
        p.drawText(QRectF(2.0, plot.top() - 7.0, 58.0, 18.0), Qt::AlignRight, number(ymax, 5));
        p.drawText(QRectF(2.0, plot.bottom() - 10.0, 58.0, 18.0), Qt::AlignRight, number(ymin, 5));
        p.drawText(QRectF(plot.left(), plot.bottom() + 25.0, 80.0, 18.0), Qt::AlignLeft, QStringLiteral("0"));
        p.drawText(QRectF(plot.right() - 100.0, plot.bottom() + 25.0, 100.0, 18.0), Qt::AlignRight, number(tmax, 5));

        const double sel = std::clamp(m_selectedTime, 0.0, tmax);
        const double sx = plot.left() + plot.width() * sel / tmax;
        p.setPen(QPen(fg, 1, Qt::DashLine));
        p.drawLine(QPointF(sx, plot.top()), QPointF(sx, plot.bottom()));
        const auto it = std::lower_bound(m_states.begin(), m_states.end(), sel,
                                         [](const LaplaceRail::State &s, double t){ return s.timeS < t; });
        const auto &s = (it == m_states.end()) ? m_states.back() : *it;
        const QPointF q = mapPoint(s);
        p.setBrush(accent); p.setPen(Qt::NoPen); p.drawEllipse(q, 5.0, 5.0);
        p.setPen(fg);
        const QString tip = QStringLiteral("t=%1 s   %2=%3")
            .arg(number(s.timeS, 5), quantityName(m_quantity).section(' ',0,0), number(stateValue(s,m_quantity), 7));
        p.drawText(QRectF(plot.left()+8, plot.top()+6, plot.width()-16, 22), Qt::AlignLeft, tip);
    }

    void mouseMoveEvent(QMouseEvent *event) override { selectFromMouse(event->position().x()); }
    void mousePressEvent(QMouseEvent *event) override { selectFromMouse(event->position().x()); }

private:
    void selectFromMouse(double x)
    {
        if (m_states.size() < 2) return;
        const double left = 64.0, right = std::max(left + 1.0, width() - 22.0);
        const double u = std::clamp((x - left) / (right - left), 0.0, 1.0);
        const double t = u * m_states.back().timeS;
        m_selectedTime = t;
        update();
        if (m_hoverHandler) m_hoverHandler(t);
    }

    std::vector<LaplaceRail::State> m_states;
    int m_quantity = 1;
    double m_selectedTime = 0.0;
    std::function<void(double)> m_hoverHandler;
};

class LaplaceRailSketchWidget final : public QWidget
{
public:
    explicit LaplaceRailSketchWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(220);
    }
    void setData(const LaplaceRail::Inputs &in, const LaplaceRail::State &state, double duration)
    { m_in=in; m_state=state; m_duration=duration; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), palette().color(QPalette::Base));
        const QColor fg=palette().color(QPalette::Text), mid=palette().color(QPalette::Mid), accent=palette().color(QPalette::Highlight);
        const QRectF r(24, 26, std::max(100.0,width()-48.0), std::max(120.0,height()-52.0));
        const double y1=r.top()+r.height()*0.30, y2=r.top()+r.height()*0.72;
        p.setPen(QPen(mid,5,Qt::SolidLine,Qt::RoundCap)); p.drawLine(QPointF(r.left(),y1),QPointF(r.right(),y1)); p.drawLine(QPointF(r.left(),y2),QPointF(r.right(),y2));
        const double k = m_in.magneticFieldT*m_in.railSeparationM*std::sin(m_in.fieldAngleDeg*3.14159265358979323846/180.0);
        const double scaleX = (m_duration>0.0) ? std::clamp(m_state.timeS/m_duration,0.0,1.0) : 0.0;
        const double rodX=r.left()+r.width()*(0.12+0.76*scaleX);
        p.setPen(QPen(accent,8,Qt::SolidLine,Qt::RoundCap)); p.drawLine(QPointF(rodX,y1),QPointF(rodX,y2));
        p.setPen(fg);
        p.drawText(QRectF(r.left(),2,r.width(),22),Qt::AlignCenter,QStringLiteral("Laplace rail — time-cursor animation (schematic / not to scale)"));

        // Magnetic-field symbols. Cross = into screen for positive B convention.
        p.setPen(QPen(mid,1.2));
        for(int ix=0;ix<8;++ix) for(int iy=0;iy<3;++iy){const double x=r.left()+30+ix*(r.width()-60)/7.0;const double y=y1+20+iy*(y2-y1-40)/2.0;p.drawEllipse(QPointF(x,y),6,6);p.drawLine(QPointF(x-4,y-4),QPointF(x+4,y+4));p.drawLine(QPointF(x-4,y+4),QPointF(x+4,y-4));}

        auto arrow=[&](QPointF a,QPointF b,const QString&label){p.setPen(QPen(fg,2));p.drawLine(a,b);const QPointF d=b-a;const double len=std::hypot(d.x(),d.y());if(len>1){QPointF u=d/len;QPointF n(-u.y(),u.x());p.drawLine(b,b-u*12+n*5);p.drawLine(b,b-u*12-n*5);}p.drawText(QRectF(b.x()-60,b.y()-24,120,20),Qt::AlignCenter,label);};
        const double currentSign = (m_state.currentA>=0.0)?1.0:-1.0;
        arrow(QPointF(rodX,y2-12),QPointF(rodX,y1+12),QStringLiteral("I=%1 A").arg(number(m_state.currentA,4)));
        const double forceSign = (m_state.forceN>=0.0)?1.0:-1.0;
        arrow(QPointF(rodX,y1-10),QPointF(rodX+forceSign*70,y1-10),QStringLiteral("F=%1 N").arg(number(m_state.forceN,4)));
        Q_UNUSED(currentSign);
        p.drawText(QRectF(r.left()+8,r.bottom()-24,r.width()-16,20),Qt::AlignLeft,
                   QStringLiteral("B=%1 T   L=%2 m   k=B·L·sinθ=%3 N/A   e=k·v=%4 V")
                   .arg(number(m_in.magneticFieldT,4),number(m_in.railSeparationM,4),number(k,5),number(m_state.motionalEmfV,5)));
    }
private:
    LaplaceRail::Inputs m_in{}; LaplaceRail::State m_state{}; double m_duration=1.0;
};

LaplaceRailWidget::LaplaceRailWidget(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto *body = new QWidget(scroll); scroll->setWidget(body);
    auto *root = new QVBoxLayout(body);

    auto *intro = new QLabel(QStringLiteral(
        "Educational Laplace-rail calculator. The moving conductor is assumed rigid, frictionless and perpendicular to the rails. "
        "The reciprocal electromechanical coupling is represented by k = B L sin(theta) as an effective geometry factor. Voltage/generator modes neglect circuit inductance; use them to study the coupling between mechanics and motional EMF."), body);
    intro->setWordWrap(true); root->addWidget(intro);

    auto *inputBox = new QGroupBox(QStringLiteral("Model and parameters"), body);
    auto *form = new QFormLayout(inputBox);
    m_mode = new QComboBox(inputBox);
    m_mode->addItem(QStringLiteral("Constant current — F = B I L sin(theta)"), static_cast<int>(LaplaceRail::Mode::ConstantCurrent));
    m_mode->addItem(QStringLiteral("Voltage source + motional EMF"), static_cast<int>(LaplaceRail::Mode::VoltageDriven));
    m_mode->addItem(QStringLiteral("Generator / magnetic braking"), static_cast<int>(LaplaceRail::Mode::GeneratorBraking));
    form->addRow(QStringLiteral("Regime"), m_mode);
    m_b=spin(inputBox,0.50,0,1000,6,QStringLiteral(" T")); form->addRow(QStringLiteral("Magnetic field B"),m_b);
    m_length=spin(inputBox,0.10,0.000001,1000,6,QStringLiteral(" m")); form->addRow(QStringLiteral("Rail separation / rod length L"),m_length);
    m_angle=spin(inputBox,90.0,-360,360,3,QStringLiteral(" deg")); form->addRow(QStringLiteral("Effective coupling angle theta"),m_angle);
    m_mass=spin(inputBox,0.050,0.000001,1e6,6,QStringLiteral(" kg")); form->addRow(QStringLiteral("Rod mass m"),m_mass);
    m_current=spin(inputBox,2.0,-1e6,1e6,6,QStringLiteral(" A")); form->addRow(QStringLiteral("Imposed current I"),m_current);
    m_voltage=spin(inputBox,6.0,-1e6,1e6,6,QStringLiteral(" V")); form->addRow(QStringLiteral("Source voltage U"),m_voltage);
    m_resistance=spin(inputBox,2.0,0,1e9,6,QStringLiteral(" ohm")); form->addRow(QStringLiteral("Total circuit resistance R"),m_resistance);
    m_v0=spin(inputBox,0.0,-1e6,1e6,6,QStringLiteral(" m/s")); form->addRow(QStringLiteral("Initial velocity v0"),m_v0);
    m_duration=spin(inputBox,1.0,0.000001,1e6,6,QStringLiteral(" s")); form->addRow(QStringLiteral("Displayed duration"),m_duration);
    auto *actions=new QWidget(inputBox);auto *al=new QHBoxLayout(actions);al->setContentsMargins(0,0,0,0);auto *calc=new QPushButton(QStringLiteral("Calculate / update"),actions);auto *copy=new QPushButton(QStringLiteral("Copy result"),actions);al->addWidget(calc);al->addWidget(copy);al->addStretch(1);form->addRow(actions);
    root->addWidget(inputBox);

    m_formulaLabel=new QLabel(body);m_formulaLabel->setWordWrap(true);root->addWidget(m_formulaLabel);

    auto *visual = new QGroupBox(QStringLiteral("Interactive representation"), body); auto *vl=new QVBoxLayout(visual);
    m_sketch=new LaplaceRailSketchWidget(visual);vl->addWidget(m_sketch);
    auto *timeRow=new QWidget(visual);auto *tl=new QHBoxLayout(timeRow);tl->setContentsMargins(0,0,0,0);m_timeSlider=new QSlider(Qt::Horizontal,timeRow);m_timeSlider->setRange(0,1000);m_timeLabel=new QLabel(QStringLiteral("t = 0 s"),timeRow);tl->addWidget(new QLabel(QStringLiteral("Time"),timeRow));tl->addWidget(m_timeSlider,1);tl->addWidget(m_timeLabel);vl->addWidget(timeRow);
    m_graphQuantity=new QComboBox(visual);m_graphQuantity->addItems({QStringLiteral("Position x(t)"),QStringLiteral("Velocity v(t)"),QStringLiteral("Current I(t)"),QStringLiteral("Force F(t)"),QStringLiteral("Motional EMF e(t)"),QStringLiteral("Acceleration a(t)")});m_graphQuantity->setCurrentIndex(1);vl->addWidget(m_graphQuantity);
    m_graph=new LaplaceRailGraphWidget(visual);vl->addWidget(m_graph,1);
    m_stateLabel=new QLabel(visual);m_stateLabel->setWordWrap(true);vl->addWidget(m_stateLabel);root->addWidget(visual,1);

    m_summary=new QPlainTextEdit(body);m_summary->setReadOnly(true);m_summary->setLineWrapMode(QPlainTextEdit::NoWrap);m_summary->setMinimumHeight(150);root->addWidget(m_summary);

    auto *practice=new QGroupBox(QStringLiteral("Practice"),body);auto *pf=new QFormLayout(practice);
    m_exerciseTarget=new QComboBox(practice);m_exerciseTarget->addItems({QStringLiteral("Laplace force F"),QStringLiteral("Acceleration a"),QStringLiteral("Motional EMF e"),QStringLiteral("Constant-current velocity after t")});pf->addRow(QStringLiteral("Unknown"),m_exerciseTarget);
    m_exercisePrompt=new QLabel(practice);m_exercisePrompt->setWordWrap(true);pf->addRow(QStringLiteral("Exercise"),m_exercisePrompt);
    m_exerciseAnswer=spin(practice,0.0,-1e12,1e12,9);pf->addRow(QStringLiteral("Your answer (SI)"),m_exerciseAnswer);
    auto *pr=new QWidget(practice);auto *prl=new QHBoxLayout(pr);prl->setContentsMargins(0,0,0,0);auto *newEx=new QPushButton(QStringLiteral("New exercise"),pr);auto *check=new QPushButton(QStringLiteral("Check"),pr);m_exerciseStatus=new QLabel(pr);prl->addWidget(newEx);prl->addWidget(check);prl->addWidget(m_exerciseStatus,1);pf->addRow(pr);root->addWidget(practice);

    auto connectCalc=[&](QObject *obj,const char *signal){Q_UNUSED(obj);Q_UNUSED(signal);}; Q_UNUSED(connectCalc);
    connect(m_mode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateModeUi();recalculate();});
    for(QDoubleSpinBox *s:{m_b,m_length,m_angle,m_mass,m_current,m_voltage,m_resistance,m_v0,m_duration}) connect(s,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double){recalculate();});
    connect(calc,&QPushButton::clicked,this,[this]{recalculate();});connect(copy,&QPushButton::clicked,this,[this]{copySummary();});
    connect(m_graphQuantity,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int q){m_graph->setQuantity(q);});
    connect(m_timeSlider,&QSlider::valueChanged,this,[this](int v){if(m_result.valid&&!m_result.states.empty())selectTime(m_result.states.back().timeS*v/1000.0);});
    m_graph->setHoverHandler([this](double t){if(m_result.valid){const double tmax=m_result.states.empty()?0.0:m_result.states.back().timeS;const int slider=(tmax>0)?static_cast<int>(std::round(1000.0*t/tmax)):0;m_timeSlider->blockSignals(true);m_timeSlider->setValue(std::clamp(slider,0,1000));m_timeSlider->blockSignals(false);selectTime(t);}});
    connect(newEx,&QPushButton::clicked,this,[this]{createExercise();});connect(check,&QPushButton::clicked,this,[this]{checkExercise();});connect(m_exerciseTarget,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{createExercise();});
    updateModeUi();recalculate();createExercise();
}

LaplaceRail::Inputs LaplaceRailWidget::inputsFromUi() const
{
    LaplaceRail::Inputs in;
    in.mode=static_cast<LaplaceRail::Mode>(m_mode->currentData().toInt());in.magneticFieldT=m_b->value();in.railSeparationM=m_length->value();in.fieldAngleDeg=m_angle->value();in.massKg=m_mass->value();in.currentA=m_current->value();in.sourceVoltageV=m_voltage->value();in.totalResistanceOhm=m_resistance->value();in.initialVelocityMps=m_v0->value();in.durationS=m_duration->value();in.samples=401;return in;
}

void LaplaceRailWidget::updateModeUi()
{
    const auto mode=static_cast<LaplaceRail::Mode>(m_mode->currentData().toInt());
    m_current->setEnabled(mode==LaplaceRail::Mode::ConstantCurrent);m_voltage->setEnabled(mode==LaplaceRail::Mode::VoltageDriven);m_resistance->setEnabled(mode!=LaplaceRail::Mode::ConstantCurrent || m_resistance->value()>0.0);
    if(mode==LaplaceRail::Mode::ConstantCurrent)m_formulaLabel->setText(QStringLiteral("Constant current: F = B I L sin(theta), a = F/m, v = v0 + a t, x = x0 + v0 t + 1/2 a t². Motional EMF is still e = B L v sin(theta)."));
    else if(mode==LaplaceRail::Mode::VoltageDriven)m_formulaLabel->setText(QStringLiteral("Voltage-driven rail (no circuit inductance): e = B L v sin(theta), I = (U - e)/R, F = B I L sin(theta). The back-EMF reduces current as speed rises."));
    else m_formulaLabel->setText(QStringLiteral("Generator/braking: e = B L v sin(theta), I = -e/R and F opposes motion (Lenz law). Velocity decays exponentially when friction and inductance are neglected."));
}

void LaplaceRailWidget::recalculate()
{
    const auto in=inputsFromUi();m_result=LaplaceRail::solve(in);
    if(!m_result.valid){m_summary->setPlainText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(m_result.error)));m_graph->setResult({});return;}
    m_graph->setResult(m_result);m_graph->setQuantity(m_graphQuantity->currentIndex());
    QString text;
    text += QStringLiteral("Mode: %1\n").arg(QString::fromLatin1(LaplaceRail::modeName(in.mode)));
    text += QStringLiteral("k = B L sin(theta) = %1 N/A = %1 V/(m/s)\n").arg(number(m_result.couplingNPerA,10));
    if(m_result.timeConstantS>0)text += QStringLiteral("Electromechanical time constant tau = m R / k² = %1 s\n").arg(number(m_result.timeConstantS,10));
    if(in.mode==LaplaceRail::Mode::VoltageDriven && std::isfinite(m_result.terminalVelocityMps))text += QStringLiteral("Ideal terminal velocity U/k = %1 m/s\n").arg(number(m_result.terminalVelocityMps,10));
    if(!m_result.states.empty()){const auto&s0=m_result.states.front();const auto&sf=m_result.states.back();text+=QStringLiteral("t=0: F=%1 N | a=%2 m/s² | v=%3 m/s | I=%4 A | e=%5 V\n").arg(number(s0.forceN),number(s0.accelerationMps2),number(s0.velocityMps),number(s0.currentA),number(s0.motionalEmfV));text+=QStringLiteral("t=%1 s: x=%2 m | v=%3 m/s | I=%4 A | F=%5 N | e=%6 V\n").arg(number(sf.timeS),number(sf.positionM),number(sf.velocityMps),number(sf.currentA),number(sf.forceN),number(sf.motionalEmfV));text+=QStringLiteral("Power at selected endpoint: Pmech=%1 W | PJoule=%2 W | Psource=%3 W\n").arg(number(sf.mechanicalPowerW),number(sf.joulePowerW),number(sf.sourcePowerW));}
    text += QStringLiteral("\nSign convention: positive I with positive k gives positive rail force. Reverse B, current, or angle sign to reverse the force.\nAssumptions: uniform B, straight moving rod, no friction, no rail/contact resistance beyond R, and no circuit inductance in voltage/generator modes.");
    m_summary->setPlainText(text);selectTime(m_result.states.empty()?0.0:m_result.states.back().timeS*m_timeSlider->value()/1000.0);
}

void LaplaceRailWidget::selectTime(double timeS)
{
    if(!m_result.valid||m_result.states.empty())return;const double tmax=m_result.states.back().timeS;const double t=std::clamp(timeS,0.0,tmax);auto it=std::lower_bound(m_result.states.begin(),m_result.states.end(),t,[](const LaplaceRail::State&s,double x){return s.timeS<x;});const auto&s=(it==m_result.states.end()?m_result.states.back():*it);m_timeLabel->setText(QStringLiteral("t = %1 s").arg(number(s.timeS,6)));updateStateReadout(s);m_graph->setSelectedTime(s.timeS);m_sketch->setData(inputsFromUi(),s,tmax);
}

void LaplaceRailWidget::updateStateReadout(const LaplaceRail::State&s)
{m_stateLabel->setText(QStringLiteral("x=%1 m   v=%2 m/s   I=%3 A   e=%4 V   F=%5 N   a=%6 m/s²   Pmech=%7 W").arg(number(s.positionM,7),number(s.velocityMps,7),number(s.currentA,7),number(s.motionalEmfV,7),number(s.forceN,7),number(s.accelerationMps2,7),number(s.mechanicalPowerW,7)));}

void LaplaceRailWidget::copySummary(){QApplication::clipboard()->setText(m_summary->toPlainText());}

void LaplaceRailWidget::createExercise()
{
    auto *rng=QRandomGenerator::global();const double B=0.1*(2+rng->bounded(9));const double L=0.05*(1+rng->bounded(8));const double I=0.5*(1+rng->bounded(10));const double m=0.02*(1+rng->bounded(10));const double v=0.5*(1+rng->bounded(8));const double t=0.2*(1+rng->bounded(10));const double k=B*L;
    const int target=m_exerciseTarget->currentIndex();QString prompt;
    if(target==0){m_expectedExerciseAnswer=k*I;m_expectedExerciseUnit=QStringLiteral("N");prompt=QStringLiteral("B=%1 T, L=%2 m, I=%3 A, theta=90°. Calculate the Laplace force F.").arg(number(B,3),number(L,3),number(I,3));}
    else if(target==1){m_expectedExerciseAnswer=k*I/m;m_expectedExerciseUnit=QStringLiteral("m/s²");prompt=QStringLiteral("B=%1 T, L=%2 m, I=%3 A, m=%4 kg, theta=90°. Calculate acceleration a.").arg(number(B,3),number(L,3),number(I,3),number(m,3));}
    else if(target==2){m_expectedExerciseAnswer=k*v;m_expectedExerciseUnit=QStringLiteral("V");prompt=QStringLiteral("A rod of length L=%1 m moves at v=%2 m/s in B=%3 T at 90°. Calculate motional EMF e.").arg(number(L,3),number(v,3),number(B,3));}
    else{const double a=k*I/m;m_expectedExerciseAnswer=a*t;m_expectedExerciseUnit=QStringLiteral("m/s");prompt=QStringLiteral("From rest: B=%1 T, L=%2 m, I=%3 A, m=%4 kg, t=%5 s. Neglect friction. Calculate v(t).").arg(number(B,3),number(L,3),number(I,3),number(m,3),number(t,3));}
    m_exercisePrompt->setText(prompt);m_exerciseAnswer->setValue(0.0);m_exerciseStatus->setText(QStringLiteral("Enter an SI value, then Check."));
}

void LaplaceRailWidget::checkExercise()
{
    const double guess=m_exerciseAnswer->value();const double scale=std::max(1e-12,std::abs(m_expectedExerciseAnswer));const double rel=std::abs(guess-m_expectedExerciseAnswer)/scale;if(rel<=0.02)m_exerciseStatus->setText(QStringLiteral("Correct (within 2%) — %1 %2").arg(number(m_expectedExerciseAnswer,8),m_expectedExerciseUnit));else m_exerciseStatus->setText(QStringLiteral("Not yet. Expected %1 %2 (relative error %3%).").arg(number(m_expectedExerciseAnswer,8),m_expectedExerciseUnit,number(100.0*rel,5)));}
