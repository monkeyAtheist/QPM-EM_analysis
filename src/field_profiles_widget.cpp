#include "widgets/field_profiles_widget.h"

#include "electrostatic_model.h"
#include "magnetostatic_model.h"
#include "widgets/electrostatic_canvas.h"
#include "widgets/field_profile_plot.h"
#include "widgets/magnetostatic_canvas.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QDoubleSpinBox *coordinateSpin(QWidget *parent, double value)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(-10000.0, 10000.0);
    spin->setDecimals(6);
    spin->setSingleStep(0.1);
    spin->setValue(value);
    return spin;
}

QString csvNumber(double value)
{
    if (!std::isfinite(value))
        return QString();
    return QString::number(value, 'g', 15);
}

QString formatEngineering(double value, const QString &unit)
{
    if (!std::isfinite(value))
        return QStringLiteral("n/a");
    if (std::abs(value) < 1.0e-30)
        return QStringLiteral("0 %1").arg(unit);

    struct Prefix { double scale; const char *symbol; };
    static const Prefix prefixes[] = {
        {1.0e12, "T"}, {1.0e9, "G"}, {1.0e6, "M"}, {1.0e3, "k"}, {1.0, ""},
        {1.0e-3, "m"}, {1.0e-6, "u"}, {1.0e-9, "n"}, {1.0e-12, "p"}
    };
    const double magnitude = std::abs(value);
    for (const auto &prefix : prefixes)
    {
        if (magnitude >= prefix.scale * 0.999 || prefix.scale == 1.0e-12)
            return QStringLiteral("%1 %2%3")
                .arg(value / prefix.scale, 0, 'g', 6)
                .arg(QString::fromLatin1(prefix.symbol), unit);
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'g', 6).arg(unit);
}

QTableWidgetItem *readonlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

void setSpinSilently(QDoubleSpinBox *spin, double value)
{
    if (!spin) return;
    const QSignalBlocker blocker(spin);
    spin->setValue(value);
}

class ElectrostaticProfilePage final : public QWidget
{
public:
    ElectrostaticProfilePage(ElectrostaticModel *model, ElectrostaticCanvas *canvas, QWidget *parent = nullptr)
        : QWidget(parent), m_model(model), m_canvas(canvas)
    {
        buildUi();
        if (m_model)
            connect(m_model, &ElectrostaticModel::changed, this, [this] { scheduleProfileUpdate(); });
        if (m_canvas)
        {
            connect(m_canvas, &ElectrostaticCanvas::probeSegmentEdited, this,
                    [this](double ax, double ay, double az, double bx, double by, double bz) {
                        setSpinSilently(m_ax, ax); setSpinSilently(m_ay, ay); setSpinSilently(m_az, az);
                        setSpinSilently(m_bx, bx); setSpinSilently(m_by, by); setSpinSilently(m_bz, bz);
                        scheduleProfileUpdate();
                    });
            connect(m_canvas, &ElectrostaticCanvas::probeSegmentEditFinished, this, [this] { updateProfile(); });
        }
        updateSegmentOverlay();
        updateProfile();
    }

private:
    void buildUi()
    {
        auto *root = new QVBoxLayout(this);
        auto *top = new QHBoxLayout();

        auto *segmentGroup = new QGroupBox(QStringLiteral("3D probe segment A → B"), this);
        auto *segment = new QFormLayout(segmentGroup);
        auto makeRow = [&](const QString &prefix, QDoubleSpinBox *&x, QDoubleSpinBox *&y, QDoubleSpinBox *&z, double defaultX) {
            auto *row = new QWidget(segmentGroup);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            x = coordinateSpin(row, defaultX);
            y = coordinateSpin(row, 0.0);
            z = coordinateSpin(row, 0.0);
            layout->addWidget(new QLabel(QStringLiteral("x"), row)); layout->addWidget(x);
            layout->addWidget(new QLabel(QStringLiteral("y"), row)); layout->addWidget(y);
            layout->addWidget(new QLabel(QStringLiteral("z"), row)); layout->addWidget(z);
            segment->addRow(prefix, row);
        };
        makeRow(QStringLiteral("A"), m_ax, m_ay, m_az, -1.5);
        makeRow(QStringLiteral("B"), m_bx, m_by, m_bz, 1.5);

        auto *buttons = new QWidget(segmentGroup);
        auto *buttonLayout = new QHBoxLayout(buttons);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        auto *aFromM = new QPushButton(QStringLiteral("A = M"), buttons);
        auto *bFromM = new QPushButton(QStringLiteral("B = M"), buttons);
        auto *swap = new QPushButton(QStringLiteral("Swap A/B"), buttons);
        buttonLayout->addWidget(aFromM); buttonLayout->addWidget(bFromM); buttonLayout->addWidget(swap);
        segment->addRow(buttons);
        m_showSegment = new QCheckBox(QStringLiteral("Show and drag A/B on electrostatic canvas"), segmentGroup);
        m_showSegment->setChecked(true);
        segment->addRow(m_showSegment);
        top->addWidget(segmentGroup, 2);

        auto *analysisGroup = new QGroupBox(QStringLiteral("Profile"), this);
        auto *analysis = new QFormLayout(analysisGroup);
        m_quantity = new QComboBox(analysisGroup);
        m_quantity->addItems({QStringLiteral("|E|"), QStringLiteral("Ex"), QStringLiteral("Ey"), QStringLiteral("Ez"), QStringLiteral("Potential V")});
        m_samples = new QSpinBox(analysisGroup);
        m_samples->setRange(16, 1000);
        m_samples->setValue(180);
        analysis->addRow(QStringLiteral("Quantity"), m_quantity);
        analysis->addRow(QStringLiteral("Samples"), m_samples);
        auto *refresh = new QPushButton(QStringLiteral("Recalculate"), analysisGroup);
        auto *freeze = new QPushButton(QStringLiteral("Freeze current curve"), analysisGroup);
        auto *clearFrozen = new QPushButton(QStringLiteral("Clear frozen curves"), analysisGroup);
        auto *exportCsv = new QPushButton(QStringLiteral("Export CSV..."), analysisGroup);
        analysis->addRow(refresh);
        analysis->addRow(freeze);
        analysis->addRow(clearFrozen);
        analysis->addRow(exportCsv);
        m_status = new QLabel(analysisGroup);
        m_status->setWordWrap(true);
        analysis->addRow(m_status);
        top->addWidget(analysisGroup, 1);
        root->addLayout(top);

        m_plot = new FieldProfilePlot(this);
        root->addWidget(m_plot, 1);

        m_recalcTimer = new QTimer(this);
        m_recalcTimer->setSingleShot(true);
        m_recalcTimer->setInterval(80);
        connect(m_recalcTimer, &QTimer::timeout, this, [this] { updateProfile(); });

        const auto endpointChanged = [this] { updateSegmentOverlay(); scheduleProfileUpdate(); };
        for (auto *spin : {m_ax, m_ay, m_az, m_bx, m_by, m_bz})
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, endpointChanged);
        connect(m_quantity, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateProfile(); });
        connect(m_samples, qOverload<int>(&QSpinBox::valueChanged), this, [this] { scheduleProfileUpdate(); });
        connect(m_showSegment, &QCheckBox::toggled, this, [this] { updateSegmentOverlay(); });
        connect(refresh, &QPushButton::clicked, this, [this] { updateProfile(); });
        connect(freeze, &QPushButton::clicked, this, [this] {
            if (m_lastDistance.isEmpty() || m_lastValues.isEmpty()) return;
            FieldProfileSeries snapshot;
            snapshot.distanceMeters = m_lastDistance;
            snapshot.values = m_lastValues;
            snapshot.name = QStringLiteral("%1 [snapshot %2]").arg(m_quantity->currentText()).arg(++m_snapshotCounter);
            snapshot.unit = (m_quantity->currentIndex() == 4) ? QStringLiteral("V") : QStringLiteral("V/m");
            snapshot.dashed = true;
            m_frozenSeries.push_back(snapshot);
            updateProfile();
        });
        connect(clearFrozen, &QPushButton::clicked, this, [this] { m_frozenSeries.clear(); updateProfile(); });
        connect(aFromM, &QPushButton::clicked, this, [this] {
            if (!m_canvas) return;
            const auto p = m_canvas->measurementPoint();
            m_ax->setValue(p.x); m_ay->setValue(p.y); m_az->setValue(p.z);
        });
        connect(bFromM, &QPushButton::clicked, this, [this] {
            if (!m_canvas) return;
            const auto p = m_canvas->measurementPoint();
            m_bx->setValue(p.x); m_by->setValue(p.y); m_bz->setValue(p.z);
        });
        connect(swap, &QPushButton::clicked, this, [this] {
            const double ax = m_ax->value(), ay = m_ay->value(), az = m_az->value();
            m_ax->setValue(m_bx->value()); m_ay->setValue(m_by->value()); m_az->setValue(m_bz->value());
            m_bx->setValue(ax); m_by->setValue(ay); m_bz->setValue(az);
        });
        connect(exportCsv, &QPushButton::clicked, this, [this] { exportProfile(); });
    }

    ElectrostaticVec3 pointA() const { return {m_ax->value(), m_ay->value(), m_az->value()}; }
    ElectrostaticVec3 pointB() const { return {m_bx->value(), m_by->value(), m_bz->value()}; }

    void scheduleProfileUpdate()
    {
        if (m_recalcTimer)
            m_recalcTimer->start();
    }

    void updateSegmentOverlay()
    {
        if (m_canvas)
            m_canvas->setProbeSegment(pointA(), pointB(), m_showSegment && m_showSegment->isChecked());
    }

    void updateProfile()
    {
        if (!m_model || !m_plot) return;
        if (m_recalcTimer) m_recalcTimer->stop();
        const auto a = pointA();
        const auto b = pointB();
        const auto delta = b - a;
        const double length = delta.norm();
        const int n = std::max(2, m_samples->value());
        QVector<double> distance; distance.reserve(n);
        QVector<double> values; values.reserve(n);
        int invalid = 0;
        for (int i = 0; i < n; ++i)
        {
            const double t = double(i) / double(n - 1);
            const auto p = a + delta * t;
            const auto f = m_model->fieldAt(p);
            double value = std::numeric_limits<double>::quiet_NaN();
            if (!f.singular)
            {
                switch (m_quantity->currentIndex())
                {
                case 0: value = f.electricField.norm(); break;
                case 1: value = f.electricField.x; break;
                case 2: value = f.electricField.y; break;
                case 3: value = f.electricField.z; break;
                case 4: if (f.potentialDefined) value = f.potential; break;
                }
            }
            if (!std::isfinite(value)) ++invalid;
            distance.push_back(t * length);
            values.push_back(value);
        }
        const bool potential = m_quantity->currentIndex() == 4;
        const QString unit = potential ? QStringLiteral("V") : QStringLiteral("V/m");
        FieldProfileSeries live;
        live.distanceMeters = distance;
        live.values = values;
        live.name = m_quantity->currentText();
        live.unit = unit;
        QVector<FieldProfileSeries> series = m_frozenSeries;
        series.push_back(live);
        m_plot->setSeries(series, QStringLiteral("Electrostatic A→B profiles"));
        m_lastDistance = distance; m_lastValues = values;
        m_status->setText(QStringLiteral("Length: %1 m | %2 samples | %3 frozen curve(s)%4 | Drag A/B directly on the canvas")
                              .arg(length, 0, 'g', 6).arg(n).arg(m_frozenSeries.size())
                              .arg(invalid ? QStringLiteral(" | %1 undefined/singular").arg(invalid) : QString()));
    }

    void exportProfile()
    {
        if (m_lastDistance.isEmpty()) return;
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export electrostatic profile"), QStringLiteral("electrostatic_profile.csv"), QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        { QMessageBox::warning(this, QStringLiteral("Export"), file.errorString()); return; }
        QTextStream out(&file);
        out << "distance_m," << m_quantity->currentText() << "\n";
        for (int i = 0; i < m_lastDistance.size() && i < m_lastValues.size(); ++i)
            out << csvNumber(m_lastDistance[i]) << ',' << csvNumber(m_lastValues[i]) << '\n';
    }

    ElectrostaticModel *m_model = nullptr;
    ElectrostaticCanvas *m_canvas = nullptr;
    QDoubleSpinBox *m_ax = nullptr, *m_ay = nullptr, *m_az = nullptr, *m_bx = nullptr, *m_by = nullptr, *m_bz = nullptr;
    QComboBox *m_quantity = nullptr;
    QSpinBox *m_samples = nullptr;
    QCheckBox *m_showSegment = nullptr;
    QLabel *m_status = nullptr;
    FieldProfilePlot *m_plot = nullptr;
    QTimer *m_recalcTimer = nullptr;
    QVector<double> m_lastDistance, m_lastValues;
    QVector<FieldProfileSeries> m_frozenSeries;
    int m_snapshotCounter = 0;
};

class MagnetostaticProfilePage final : public QWidget
{
public:
    MagnetostaticProfilePage(MagnetostaticModel *model, MagnetostaticCanvas *canvas, QWidget *parent = nullptr)
        : QWidget(parent), m_model(model), m_canvas(canvas)
    {
        buildUi();
        if (m_model)
            connect(m_model, &MagnetostaticModel::changed, this, [this] { scheduleProfileUpdate(); });
        if (m_canvas)
        {
            connect(m_canvas, &MagnetostaticCanvas::probeSegmentEdited, this,
                    [this](double ax, double ay, double az, double bx, double by, double bz) {
                        setSpinSilently(m_ax, ax); setSpinSilently(m_ay, ay); setSpinSilently(m_az, az);
                        setSpinSilently(m_bx, bx); setSpinSilently(m_by, by); setSpinSilently(m_bz, bz);
                        scheduleProfileUpdate();
                    });
            connect(m_canvas, &MagnetostaticCanvas::probeSegmentEditFinished, this, [this] { updateProfile(); });
        }
        updateSegmentOverlay();
        updateProfile();
    }

private:
    void buildUi()
    {
        auto *root = new QVBoxLayout(this);
        auto *top = new QHBoxLayout();
        auto *segmentGroup = new QGroupBox(QStringLiteral("3D probe segment A → B"), this);
        auto *segment = new QFormLayout(segmentGroup);
        auto makeRow = [&](const QString &prefix, QDoubleSpinBox *&x, QDoubleSpinBox *&y, QDoubleSpinBox *&z, double dx) {
            auto *row = new QWidget(segmentGroup);
            auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0);
            x = coordinateSpin(row, dx); y = coordinateSpin(row, 0.0); z = coordinateSpin(row, 0.0);
            layout->addWidget(new QLabel(QStringLiteral("x"), row)); layout->addWidget(x);
            layout->addWidget(new QLabel(QStringLiteral("y"), row)); layout->addWidget(y);
            layout->addWidget(new QLabel(QStringLiteral("z"), row)); layout->addWidget(z);
            segment->addRow(prefix, row);
        };
        makeRow(QStringLiteral("A"), m_ax, m_ay, m_az, -1.5);
        makeRow(QStringLiteral("B"), m_bx, m_by, m_bz, 1.5);
        auto *buttons = new QWidget(segmentGroup);
        auto *bl = new QHBoxLayout(buttons); bl->setContentsMargins(0, 0, 0, 0);
        auto *aFromM = new QPushButton(QStringLiteral("A = M"), buttons);
        auto *bFromM = new QPushButton(QStringLiteral("B = M"), buttons);
        auto *swap = new QPushButton(QStringLiteral("Swap A/B"), buttons);
        bl->addWidget(aFromM); bl->addWidget(bFromM); bl->addWidget(swap); segment->addRow(buttons);
        m_showSegment = new QCheckBox(QStringLiteral("Show and drag A/B on magnetostatic canvas"), segmentGroup);
        m_showSegment->setChecked(true); segment->addRow(m_showSegment); top->addWidget(segmentGroup, 2);

        auto *analysisGroup = new QGroupBox(QStringLiteral("Profile"), this);
        auto *analysis = new QFormLayout(analysisGroup);
        m_quantity = new QComboBox(analysisGroup);
        m_quantity->addItems({QStringLiteral("|B|"), QStringLiteral("Bx"), QStringLiteral("By"), QStringLiteral("Bz"), QStringLiteral("|H|")});
        m_samples = new QSpinBox(analysisGroup); m_samples->setRange(16, 800); m_samples->setValue(140);
        analysis->addRow(QStringLiteral("Quantity"), m_quantity); analysis->addRow(QStringLiteral("Samples"), m_samples);
        auto *refresh = new QPushButton(QStringLiteral("Recalculate"), analysisGroup);
        auto *freeze = new QPushButton(QStringLiteral("Freeze current curve"), analysisGroup);
        auto *clearFrozen = new QPushButton(QStringLiteral("Clear frozen curves"), analysisGroup);
        auto *exportCsv = new QPushButton(QStringLiteral("Export CSV..."), analysisGroup);
        analysis->addRow(refresh); analysis->addRow(freeze); analysis->addRow(clearFrozen); analysis->addRow(exportCsv);
        m_status = new QLabel(analysisGroup); m_status->setWordWrap(true); analysis->addRow(m_status);
        top->addWidget(analysisGroup, 1); root->addLayout(top);
        m_plot = new FieldProfilePlot(this); root->addWidget(m_plot, 1);

        m_recalcTimer = new QTimer(this);
        m_recalcTimer->setSingleShot(true);
        m_recalcTimer->setInterval(100);
        connect(m_recalcTimer, &QTimer::timeout, this, [this] { updateProfile(); });

        const auto endpointChanged = [this] { updateSegmentOverlay(); scheduleProfileUpdate(); };
        for (auto *spin : {m_ax, m_ay, m_az, m_bx, m_by, m_bz})
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, endpointChanged);
        connect(m_quantity, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateProfile(); });
        connect(m_samples, qOverload<int>(&QSpinBox::valueChanged), this, [this] { scheduleProfileUpdate(); });
        connect(m_showSegment, &QCheckBox::toggled, this, [this] { updateSegmentOverlay(); });
        connect(refresh, &QPushButton::clicked, this, [this] { updateProfile(); });
        connect(freeze, &QPushButton::clicked, this, [this] {
            if (m_lastDistance.isEmpty() || m_lastValues.isEmpty()) return;
            FieldProfileSeries snapshot;
            snapshot.distanceMeters = m_lastDistance;
            snapshot.values = m_lastValues;
            snapshot.name = QStringLiteral("%1 [snapshot %2]").arg(m_quantity->currentText()).arg(++m_snapshotCounter);
            snapshot.unit = (m_quantity->currentIndex() == 4) ? QStringLiteral("A/m") : QStringLiteral("T");
            snapshot.dashed = true;
            m_frozenSeries.push_back(snapshot);
            updateProfile();
        });
        connect(clearFrozen, &QPushButton::clicked, this, [this] { m_frozenSeries.clear(); updateProfile(); });
        connect(aFromM, &QPushButton::clicked, this, [this] {
            if (!m_canvas) return;
            const auto p = m_canvas->measurementPoint(); m_ax->setValue(p.x); m_ay->setValue(p.y); m_az->setValue(p.z);
        });
        connect(bFromM, &QPushButton::clicked, this, [this] {
            if (!m_canvas) return;
            const auto p = m_canvas->measurementPoint(); m_bx->setValue(p.x); m_by->setValue(p.y); m_bz->setValue(p.z);
        });
        connect(swap, &QPushButton::clicked, this, [this] {
            const double ax = m_ax->value(), ay = m_ay->value(), az = m_az->value();
            m_ax->setValue(m_bx->value()); m_ay->setValue(m_by->value()); m_az->setValue(m_bz->value());
            m_bx->setValue(ax); m_by->setValue(ay); m_bz->setValue(az);
        });
        connect(exportCsv, &QPushButton::clicked, this, [this] { exportProfile(); });
    }

    MagnetostaticVec3 pointA() const { return {m_ax->value(), m_ay->value(), m_az->value()}; }
    MagnetostaticVec3 pointB() const { return {m_bx->value(), m_by->value(), m_bz->value()}; }
    void scheduleProfileUpdate() { if (m_recalcTimer) m_recalcTimer->start(); }
    void updateSegmentOverlay() { if (m_canvas) m_canvas->setProbeSegment(pointA(), pointB(), m_showSegment && m_showSegment->isChecked()); }

    void updateProfile()
    {
        if (!m_model || !m_plot) return;
        if (m_recalcTimer) m_recalcTimer->stop();
        const auto a = pointA(), b = pointB(), delta = b - a;
        const double length = delta.norm();
        const int n = std::max(2, m_samples->value());
        QVector<double> distance, values; distance.reserve(n); values.reserve(n);
        int invalid = 0;
        for (int i = 0; i < n; ++i)
        {
            const double t = double(i) / double(n - 1);
            const auto p = a + delta * t;
            const auto f = m_model->fieldAt(p);
            double value = std::numeric_limits<double>::quiet_NaN();
            if (!f.singular)
            {
                switch (m_quantity->currentIndex())
                {
                case 0: value = f.magneticFluxDensity.norm(); break;
                case 1: value = f.magneticFluxDensity.x; break;
                case 2: value = f.magneticFluxDensity.y; break;
                case 3: value = f.magneticFluxDensity.z; break;
                case 4: value = f.magneticField.norm(); break;
                }
            }
            if (!std::isfinite(value)) ++invalid;
            distance.push_back(t * length); values.push_back(value);
        }
        const bool h = m_quantity->currentIndex() == 4;
        const QString unit = h ? QStringLiteral("A/m") : QStringLiteral("T");
        FieldProfileSeries live;
        live.distanceMeters = distance;
        live.values = values;
        live.name = m_quantity->currentText();
        live.unit = unit;
        QVector<FieldProfileSeries> series = m_frozenSeries;
        series.push_back(live);
        m_plot->setSeries(series, QStringLiteral("Magnetostatic A→B profiles"));
        m_lastDistance = distance; m_lastValues = values;
        m_status->setText(QStringLiteral("Length: %1 m | %2 samples | %3 frozen curve(s)%4 | Drag A/B directly on the canvas")
                              .arg(length, 0, 'g', 6).arg(n).arg(m_frozenSeries.size())
                              .arg(invalid ? QStringLiteral(" | %1 undefined/singular").arg(invalid) : QString()));
    }

    void exportProfile()
    {
        if (m_lastDistance.isEmpty()) return;
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export magnetostatic profile"), QStringLiteral("magnetostatic_profile.csv"), QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        { QMessageBox::warning(this, QStringLiteral("Export"), file.errorString()); return; }
        QTextStream out(&file);
        out << "distance_m," << m_quantity->currentText() << "\n";
        for (int i = 0; i < m_lastDistance.size() && i < m_lastValues.size(); ++i)
            out << csvNumber(m_lastDistance[i]) << ',' << csvNumber(m_lastValues[i]) << '\n';
    }

    MagnetostaticModel *m_model = nullptr;
    MagnetostaticCanvas *m_canvas = nullptr;
    QDoubleSpinBox *m_ax = nullptr, *m_ay = nullptr, *m_az = nullptr, *m_bx = nullptr, *m_by = nullptr, *m_bz = nullptr;
    QComboBox *m_quantity = nullptr;
    QSpinBox *m_samples = nullptr;
    QCheckBox *m_showSegment = nullptr;
    QLabel *m_status = nullptr;
    FieldProfilePlot *m_plot = nullptr;
    QTimer *m_recalcTimer = nullptr;
    QVector<double> m_lastDistance, m_lastValues;
    QVector<FieldProfileSeries> m_frozenSeries;
    int m_snapshotCounter = 0;
};

class PointProbesPage final : public QWidget
{
public:
    PointProbesPage(ElectrostaticModel *electrostaticModel,
                    ElectrostaticCanvas *electrostaticCanvas,
                    MagnetostaticModel *magnetostaticModel,
                    MagnetostaticCanvas *magnetostaticCanvas,
                    QWidget *parent = nullptr)
        : QWidget(parent),
          m_electrostaticModel(electrostaticModel),
          m_electrostaticCanvas(electrostaticCanvas),
          m_magnetostaticModel(magnetostaticModel),
          m_magnetostaticCanvas(magnetostaticCanvas)
    {
        buildUi();

        m_probes = {
            {QStringLiteral("P1"), {-1.0, 0.0, 0.0}},
            {QStringLiteral("P2"), { 0.0, 0.0, 0.0}},
            {QStringLiteral("P3"), { 1.0, 0.0, 0.0}}
        };
        m_selectedProbe = 0;

        if (m_electrostaticModel)
            connect(m_electrostaticModel, &ElectrostaticModel::changed, this, [this] { scheduleRefresh(); });
        if (m_magnetostaticModel)
            connect(m_magnetostaticModel, &MagnetostaticModel::changed, this, [this] { scheduleRefresh(); });

        if (m_electrostaticCanvas)
        {
            connect(m_electrostaticCanvas, &ElectrostaticCanvas::pointProbeSelected, this, [this](int index) { selectProbe(index); });
            connect(m_electrostaticCanvas, &ElectrostaticCanvas::pointProbePositionEdited, this,
                    [this](int index, double x, double y, double z) { moveProbeFromCanvas(index, x, y, z); });
            connect(m_electrostaticCanvas, &ElectrostaticCanvas::pointProbeEditFinished, this, [this](int) { refreshTable(); });
        }
        if (m_magnetostaticCanvas)
        {
            connect(m_magnetostaticCanvas, &MagnetostaticCanvas::pointProbeSelected, this, [this](int index) { selectProbe(index); });
            connect(m_magnetostaticCanvas, &MagnetostaticCanvas::pointProbePositionEdited, this,
                    [this](int index, double x, double y, double z) { moveProbeFromCanvas(index, x, y, z); });
            connect(m_magnetostaticCanvas, &MagnetostaticCanvas::pointProbeEditFinished, this, [this](int) { refreshTable(); });
        }

        syncCanvases();
        refreshTable();
    }

private:
    struct Probe
    {
        QString name;
        ElectrostaticVec3 position;
    };

    enum Column
    {
        Name = 0, X, Y, Z,
        EMag, Ex, Ey, Ez, Potential,
        BMag, Bx, By, Bz, HMag,
        Status,
        ColumnCount
    };

    void buildUi()
    {
        auto *root = new QVBoxLayout(this);
        auto *description = new QLabel(
            QStringLiteral("Point probes are shared by both field models. Select or drag P1, P2… directly on either canvas; only the two coordinates of the active XY/XZ/YZ plane change, while the hidden coordinate is preserved."), this);
        description->setWordWrap(true);
        root->addWidget(description);

        auto *toolbar = new QHBoxLayout();
        auto *add = new QPushButton(QStringLiteral("Add probe"), this);
        auto *duplicate = new QPushButton(QStringLiteral("Duplicate"), this);
        auto *remove = new QPushButton(QStringLiteral("Remove"), this);
        auto *clear = new QPushButton(QStringLiteral("Clear"), this);
        auto *fromElectroM = new QPushButton(QStringLiteral("Selected = electrostatic M"), this);
        auto *fromMagM = new QPushButton(QStringLiteral("Selected = magnetostatic M"), this);
        auto *exportCsv = new QPushButton(QStringLiteral("Export CSV..."), this);
        m_showOnCanvases = new QCheckBox(QStringLiteral("Show / drag probes on canvases"), this);
        m_showOnCanvases->setChecked(true);
        toolbar->addWidget(add);
        toolbar->addWidget(duplicate);
        toolbar->addWidget(remove);
        toolbar->addWidget(clear);
        toolbar->addSpacing(12);
        toolbar->addWidget(fromElectroM);
        toolbar->addWidget(fromMagM);
        toolbar->addStretch(1);
        toolbar->addWidget(m_showOnCanvases);
        toolbar->addWidget(exportCsv);
        root->addLayout(toolbar);

        m_table = new QTableWidget(this);
        m_table->setColumnCount(ColumnCount);
        m_table->setHorizontalHeaderLabels({
            QStringLiteral("Probe"), QStringLiteral("X [m]"), QStringLiteral("Y [m]"), QStringLiteral("Z [m]"),
            QStringLiteral("|E|"), QStringLiteral("Ex"), QStringLiteral("Ey"), QStringLiteral("Ez"), QStringLiteral("V"),
            QStringLiteral("|B|"), QStringLiteral("Bx"), QStringLiteral("By"), QStringLiteral("Bz"), QStringLiteral("|H|"),
            QStringLiteral("Status")
        });
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setAlternatingRowColors(true);
        m_table->verticalHeader()->setVisible(false);
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        root->addWidget(m_table, 1);

        m_summary = new QLabel(this);
        m_summary->setWordWrap(true);
        root->addWidget(m_summary);

        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(60);
        connect(m_refreshTimer, &QTimer::timeout, this, [this] { refreshTable(); });

        connect(add, &QPushButton::clicked, this, [this] { addProbe(); });
        connect(duplicate, &QPushButton::clicked, this, [this] { duplicateProbe(); });
        connect(remove, &QPushButton::clicked, this, [this] { removeProbe(); });
        connect(clear, &QPushButton::clicked, this, [this] {
            m_probes.clear(); m_selectedProbe = -1; syncCanvases(); refreshTable();
        });
        connect(fromElectroM, &QPushButton::clicked, this, [this] {
            if (!m_electrostaticCanvas) return;
            ensureSelectedProbe();
            m_probes[m_selectedProbe].position = m_electrostaticCanvas->measurementPoint();
            syncCanvases(); refreshTable(); selectProbe(m_selectedProbe);
        });
        connect(fromMagM, &QPushButton::clicked, this, [this] {
            if (!m_magnetostaticCanvas) return;
            ensureSelectedProbe();
            const auto p = m_magnetostaticCanvas->measurementPoint();
            m_probes[m_selectedProbe].position = {p.x, p.y, p.z};
            syncCanvases(); refreshTable(); selectProbe(m_selectedProbe);
        });
        connect(exportCsv, &QPushButton::clicked, this, [this] { exportProbes(); });
        connect(m_showOnCanvases, &QCheckBox::toggled, this, [this](bool enabled) {
            if (m_electrostaticCanvas) m_electrostaticCanvas->setPointProbesVisible(enabled);
            if (m_magnetostaticCanvas) m_magnetostaticCanvas->setPointProbesVisible(enabled);
        });
        connect(m_table, &QTableWidget::currentCellChanged, this,
                [this](int currentRow, int, int, int) {
                    if (m_refreshing) return;
                    if (currentRow >= 0 && currentRow < m_probes.size())
                    {
                        m_selectedProbe = currentRow;
                        syncCanvases();
                    }
                });
        connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) { handleItemChanged(item); });
    }

    void scheduleRefresh()
    {
        if (m_refreshTimer) m_refreshTimer->start();
    }

    void ensureSelectedProbe()
    {
        if (m_selectedProbe >= 0 && m_selectedProbe < m_probes.size()) return;
        addProbe();
    }

    void addProbe()
    {
        ElectrostaticVec3 p{};
        if (m_electrostaticCanvas) p = m_electrostaticCanvas->measurementPoint();
        Probe probe;
        probe.name = QStringLiteral("P%1").arg(m_probes.size() + 1);
        probe.position = p;
        m_probes.push_back(probe);
        m_selectedProbe = m_probes.size() - 1;
        syncCanvases(); refreshTable(); selectProbe(m_selectedProbe);
    }

    void duplicateProbe()
    {
        ensureSelectedProbe();
        Probe copy = m_probes[m_selectedProbe];
        copy.name = QStringLiteral("P%1").arg(m_probes.size() + 1);
        copy.position.x += 0.15;
        m_probes.push_back(copy);
        m_selectedProbe = m_probes.size() - 1;
        syncCanvases(); refreshTable(); selectProbe(m_selectedProbe);
    }

    void removeProbe()
    {
        if (m_selectedProbe < 0 || m_selectedProbe >= m_probes.size()) return;
        m_probes.removeAt(m_selectedProbe);
        if (m_probes.isEmpty()) m_selectedProbe = -1;
        else m_selectedProbe = std::clamp(m_selectedProbe, 0, int(m_probes.size()) - 1);
        syncCanvases(); refreshTable(); selectProbe(m_selectedProbe);
    }

    void selectProbe(int index)
    {
        if (index < 0 || index >= m_probes.size()) return;
        m_selectedProbe = index;
        syncCanvases();
        if (m_table && m_table->currentRow() != index)
        {
            const QSignalBlocker blocker(m_table);
            m_table->setCurrentCell(index, 0);
            m_table->selectRow(index);
        }
    }

    void moveProbeFromCanvas(int index, double x, double y, double z)
    {
        if (index < 0 || index >= m_probes.size()) return;
        m_selectedProbe = index;
        m_probes[index].position = {x, y, z};
        syncCanvases();
        scheduleRefresh();
    }

    void syncCanvases()
    {
        QVector<ElectrostaticVec3> electroPositions;
        QVector<MagnetostaticVec3> magneticPositions;
        QStringList names;
        electroPositions.reserve(m_probes.size());
        magneticPositions.reserve(m_probes.size());
        for (const auto &probe : m_probes)
        {
            electroPositions.push_back(probe.position);
            magneticPositions.push_back({probe.position.x, probe.position.y, probe.position.z});
            names.push_back(probe.name);
        }
        if (m_electrostaticCanvas)
            m_electrostaticCanvas->setPointProbes(electroPositions, names, m_selectedProbe);
        if (m_magnetostaticCanvas)
            m_magnetostaticCanvas->setPointProbes(magneticPositions, names, m_selectedProbe);
    }

    QString statusFor(const ElectrostaticFieldResult &e, const MagnetostaticFieldResult &b) const
    {
        QStringList notes;
        if (e.singular) notes << QStringLiteral("E singular");
        if (!e.potentialDefined) notes << QStringLiteral("V reference n/a");
        if (b.singular) notes << QStringLiteral("B singular");
        return notes.isEmpty() ? QStringLiteral("OK") : notes.join(QStringLiteral("; "));
    }

    void refreshTable()
    {
        if (!m_table) return;
        if (m_refreshTimer) m_refreshTimer->stop();
        m_refreshing = true;
        const QSignalBlocker blocker(m_table);
        m_table->setRowCount(m_probes.size());

        int singularCount = 0;
        for (int row = 0; row < m_probes.size(); ++row)
        {
            const auto &probe = m_probes[row];
            const ElectrostaticFieldResult e = m_electrostaticModel
                ? m_electrostaticModel->fieldAt(probe.position)
                : ElectrostaticFieldResult{};
            const MagnetostaticVec3 mp{probe.position.x, probe.position.y, probe.position.z};
            const MagnetostaticFieldResult b = m_magnetostaticModel
                ? m_magnetostaticModel->fieldAt(mp)
                : MagnetostaticFieldResult{};

            auto *nameItem = new QTableWidgetItem(probe.name);
            m_table->setItem(row, Name, nameItem);
            m_table->setItem(row, X, new QTableWidgetItem(QString::number(probe.position.x, 'g', 9)));
            m_table->setItem(row, Y, new QTableWidgetItem(QString::number(probe.position.y, 'g', 9)));
            m_table->setItem(row, Z, new QTableWidgetItem(QString::number(probe.position.z, 'g', 9)));

            m_table->setItem(row, EMag, readonlyItem(e.singular ? QStringLiteral("n/a") : formatEngineering(e.electricField.norm(), QStringLiteral("V/m"))));
            m_table->setItem(row, Ex, readonlyItem(e.singular ? QStringLiteral("n/a") : formatEngineering(e.electricField.x, QStringLiteral("V/m"))));
            m_table->setItem(row, Ey, readonlyItem(e.singular ? QStringLiteral("n/a") : formatEngineering(e.electricField.y, QStringLiteral("V/m"))));
            m_table->setItem(row, Ez, readonlyItem(e.singular ? QStringLiteral("n/a") : formatEngineering(e.electricField.z, QStringLiteral("V/m"))));
            m_table->setItem(row, Potential, readonlyItem((!e.singular && e.potentialDefined) ? formatEngineering(e.potential, QStringLiteral("V")) : QStringLiteral("n/a")));

            m_table->setItem(row, BMag, readonlyItem(b.singular ? QStringLiteral("n/a") : formatEngineering(b.magneticFluxDensity.norm(), QStringLiteral("T"))));
            m_table->setItem(row, Bx, readonlyItem(b.singular ? QStringLiteral("n/a") : formatEngineering(b.magneticFluxDensity.x, QStringLiteral("T"))));
            m_table->setItem(row, By, readonlyItem(b.singular ? QStringLiteral("n/a") : formatEngineering(b.magneticFluxDensity.y, QStringLiteral("T"))));
            m_table->setItem(row, Bz, readonlyItem(b.singular ? QStringLiteral("n/a") : formatEngineering(b.magneticFluxDensity.z, QStringLiteral("T"))));
            m_table->setItem(row, HMag, readonlyItem(b.singular ? QStringLiteral("n/a") : formatEngineering(b.magneticField.norm(), QStringLiteral("A/m"))));
            const QString status = statusFor(e, b);
            if (status != QStringLiteral("OK")) ++singularCount;
            m_table->setItem(row, Status, readonlyItem(status));
        }

        if (m_selectedProbe >= 0 && m_selectedProbe < m_probes.size())
        {
            m_table->setCurrentCell(m_selectedProbe, 0);
            m_table->selectRow(m_selectedProbe);
        }
        m_refreshing = false;
        m_summary->setText(QStringLiteral("%1 probe(s) | selected: %2%3")
                               .arg(m_probes.size())
                               .arg(m_selectedProbe >= 0 && m_selectedProbe < m_probes.size() ? m_probes[m_selectedProbe].name : QStringLiteral("none"))
                               .arg(singularCount ? QStringLiteral(" | %1 probe(s) have an undefined/singular quantity").arg(singularCount) : QString()));
    }

    void handleItemChanged(QTableWidgetItem *item)
    {
        if (m_refreshing || !item) return;
        const int row = item->row();
        if (row < 0 || row >= m_probes.size()) return;
        if (item->column() == Name)
        {
            const QString name = item->text().trimmed();
            m_probes[row].name = name.isEmpty() ? QStringLiteral("P%1").arg(row + 1) : name;
        }
        else if (item->column() >= X && item->column() <= Z)
        {
            bool ok = false;
            const double value = item->text().toDouble(&ok);
            if (!ok || !std::isfinite(value))
            {
                refreshTable();
                return;
            }
            if (item->column() == X) m_probes[row].position.x = value;
            if (item->column() == Y) m_probes[row].position.y = value;
            if (item->column() == Z) m_probes[row].position.z = value;
        }
        else
        {
            return;
        }
        m_selectedProbe = row;
        syncCanvases();
        scheduleRefresh();
    }

    void exportProbes()
    {
        if (m_probes.isEmpty()) return;
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export point probes"), QStringLiteral("field_probes.csv"), QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        { QMessageBox::warning(this, QStringLiteral("Export"), file.errorString()); return; }
        QTextStream out(&file);
        out << "name,x_m,y_m,z_m,E_V_per_m,Ex_V_per_m,Ey_V_per_m,Ez_V_per_m,V_V,B_T,Bx_T,By_T,Bz_T,H_A_per_m,status\n";
        for (const auto &probe : m_probes)
        {
            const auto e = m_electrostaticModel ? m_electrostaticModel->fieldAt(probe.position) : ElectrostaticFieldResult{};
            const MagnetostaticVec3 mp{probe.position.x, probe.position.y, probe.position.z};
            const auto b = m_magnetostaticModel ? m_magnetostaticModel->fieldAt(mp) : MagnetostaticFieldResult{};
            out << probe.name << ',' << csvNumber(probe.position.x) << ',' << csvNumber(probe.position.y) << ',' << csvNumber(probe.position.z) << ',';
            out << (e.singular ? QString() : csvNumber(e.electricField.norm())) << ','
                << (e.singular ? QString() : csvNumber(e.electricField.x)) << ','
                << (e.singular ? QString() : csvNumber(e.electricField.y)) << ','
                << (e.singular ? QString() : csvNumber(e.electricField.z)) << ','
                << ((!e.singular && e.potentialDefined) ? csvNumber(e.potential) : QString()) << ',';
            out << (b.singular ? QString() : csvNumber(b.magneticFluxDensity.norm())) << ','
                << (b.singular ? QString() : csvNumber(b.magneticFluxDensity.x)) << ','
                << (b.singular ? QString() : csvNumber(b.magneticFluxDensity.y)) << ','
                << (b.singular ? QString() : csvNumber(b.magneticFluxDensity.z)) << ','
                << (b.singular ? QString() : csvNumber(b.magneticField.norm())) << ',';
            QString status = statusFor(e, b);
            status.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            out << QStringLiteral("\"") << status << QStringLiteral("\"\n");
        }
    }

    ElectrostaticModel *m_electrostaticModel = nullptr;
    ElectrostaticCanvas *m_electrostaticCanvas = nullptr;
    MagnetostaticModel *m_magnetostaticModel = nullptr;
    MagnetostaticCanvas *m_magnetostaticCanvas = nullptr;
    QVector<Probe> m_probes;
    int m_selectedProbe = -1;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_showOnCanvases = nullptr;
    QLabel *m_summary = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_refreshing = false;

};

class SingleDomainPointProbesPage final : public QWidget
{
public:
    enum class Domain { Electrostatic, Magnetostatic };

    SingleDomainPointProbesPage(Domain domain,
                                ElectrostaticModel *electrostaticModel,
                                ElectrostaticCanvas *electrostaticCanvas,
                                MagnetostaticModel *magnetostaticModel,
                                MagnetostaticCanvas *magnetostaticCanvas,
                                QWidget *parent = nullptr)
        : QWidget(parent),
          m_domain(domain),
          m_electrostaticModel(electrostaticModel),
          m_electrostaticCanvas(electrostaticCanvas),
          m_magnetostaticModel(magnetostaticModel),
          m_magnetostaticCanvas(magnetostaticCanvas)
    {
        buildUi();
        m_probes = {
            {QStringLiteral("P1"), -1.0, 0.0, 0.0},
            {QStringLiteral("P2"),  0.0, 0.0, 0.0},
            {QStringLiteral("P3"),  1.0, 0.0, 0.0}
        };
        m_selectedProbe = 0;

        if (m_domain == Domain::Electrostatic && m_electrostaticModel)
            connect(m_electrostaticModel, &ElectrostaticModel::changed, this, [this] { scheduleRefresh(); });
        if (m_domain == Domain::Magnetostatic && m_magnetostaticModel)
            connect(m_magnetostaticModel, &MagnetostaticModel::changed, this, [this] { scheduleRefresh(); });

        if (m_domain == Domain::Electrostatic && m_electrostaticCanvas)
        {
            connect(m_electrostaticCanvas, &ElectrostaticCanvas::pointProbeSelected,
                    this, [this](int index) { selectProbe(index); });
            connect(m_electrostaticCanvas, &ElectrostaticCanvas::pointProbePositionEdited,
                    this, [this](int index, double x, double y, double z) { moveProbeFromCanvas(index, x, y, z); });
            connect(m_electrostaticCanvas, &ElectrostaticCanvas::pointProbeEditFinished,
                    this, [this](int) { refreshTable(); });
        }
        if (m_domain == Domain::Magnetostatic && m_magnetostaticCanvas)
        {
            connect(m_magnetostaticCanvas, &MagnetostaticCanvas::pointProbeSelected,
                    this, [this](int index) { selectProbe(index); });
            connect(m_magnetostaticCanvas, &MagnetostaticCanvas::pointProbePositionEdited,
                    this, [this](int index, double x, double y, double z) { moveProbeFromCanvas(index, x, y, z); });
            connect(m_magnetostaticCanvas, &MagnetostaticCanvas::pointProbeEditFinished,
                    this, [this](int) { refreshTable(); });
        }

        syncCanvas();
        refreshTable();
    }

private:
    struct Probe
    {
        QString name;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    enum Column
    {
        Name = 0, X, Y, Z,
        Magnitude, ComponentX, ComponentY, ComponentZ, ScalarExtra,
        Status, ColumnCount
    };

    void buildUi()
    {
        auto *root = new QVBoxLayout(this);
        const bool electro = m_domain == Domain::Electrostatic;
        auto *description = new QLabel(
            electro
                ? QStringLiteral("Electrostatic point probes. Select or drag P1, P2… directly on the electrostatic canvas; the hidden coordinate of the active XY/XZ/YZ plane is preserved.")
                : QStringLiteral("Magnetostatic point probes. Select or drag P1, P2… directly on the magnetostatic canvas; the hidden coordinate of the active XY/XZ/YZ plane is preserved."),
            this);
        description->setWordWrap(true);
        root->addWidget(description);

        auto *toolbar = new QHBoxLayout();
        auto *add = new QPushButton(QStringLiteral("Add probe"), this);
        auto *duplicate = new QPushButton(QStringLiteral("Duplicate"), this);
        auto *remove = new QPushButton(QStringLiteral("Remove"), this);
        auto *clear = new QPushButton(QStringLiteral("Clear"), this);
        auto *fromM = new QPushButton(QStringLiteral("Selected = M"), this);
        auto *exportCsv = new QPushButton(QStringLiteral("Export CSV..."), this);
        m_showOnCanvas = new QCheckBox(QStringLiteral("Show / drag probes on field view"), this);
        m_showOnCanvas->setChecked(true);
        toolbar->addWidget(add);
        toolbar->addWidget(duplicate);
        toolbar->addWidget(remove);
        toolbar->addWidget(clear);
        toolbar->addSpacing(12);
        toolbar->addWidget(fromM);
        toolbar->addStretch(1);
        toolbar->addWidget(m_showOnCanvas);
        toolbar->addWidget(exportCsv);
        root->addLayout(toolbar);

        m_table = new QTableWidget(this);
        m_table->setColumnCount(ColumnCount);
        m_table->setHorizontalHeaderLabels(electro
            ? QStringList{QStringLiteral("Probe"), QStringLiteral("X [m]"), QStringLiteral("Y [m]"), QStringLiteral("Z [m]"),
                          QStringLiteral("|E|"), QStringLiteral("Ex"), QStringLiteral("Ey"), QStringLiteral("Ez"),
                          QStringLiteral("V"), QStringLiteral("Status")}
            : QStringList{QStringLiteral("Probe"), QStringLiteral("X [m]"), QStringLiteral("Y [m]"), QStringLiteral("Z [m]"),
                          QStringLiteral("|B|"), QStringLiteral("Bx"), QStringLiteral("By"), QStringLiteral("Bz"),
                          QStringLiteral("|H|"), QStringLiteral("Status")});
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setAlternatingRowColors(true);
        m_table->verticalHeader()->setVisible(false);
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        root->addWidget(m_table, 1);

        m_summary = new QLabel(this);
        m_summary->setWordWrap(true);
        root->addWidget(m_summary);

        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(60);
        connect(m_refreshTimer, &QTimer::timeout, this, [this] { refreshTable(); });

        connect(add, &QPushButton::clicked, this, [this] { addProbe(); });
        connect(duplicate, &QPushButton::clicked, this, [this] { duplicateProbe(); });
        connect(remove, &QPushButton::clicked, this, [this] { removeProbe(); });
        connect(clear, &QPushButton::clicked, this, [this] {
            m_probes.clear(); m_selectedProbe = -1; syncCanvas(); refreshTable();
        });
        connect(fromM, &QPushButton::clicked, this, [this] {
            ensureSelectedProbe();
            if (m_selectedProbe < 0) return;
            if (m_domain == Domain::Electrostatic && m_electrostaticCanvas)
            {
                const auto p = m_electrostaticCanvas->measurementPoint();
                m_probes[m_selectedProbe].x = p.x; m_probes[m_selectedProbe].y = p.y; m_probes[m_selectedProbe].z = p.z;
            }
            else if (m_domain == Domain::Magnetostatic && m_magnetostaticCanvas)
            {
                const auto p = m_magnetostaticCanvas->measurementPoint();
                m_probes[m_selectedProbe].x = p.x; m_probes[m_selectedProbe].y = p.y; m_probes[m_selectedProbe].z = p.z;
            }
            syncCanvas(); refreshTable(); selectProbe(m_selectedProbe);
        });
        connect(exportCsv, &QPushButton::clicked, this, [this] { exportProbes(); });
        connect(m_showOnCanvas, &QCheckBox::toggled, this, [this](bool enabled) {
            if (m_domain == Domain::Electrostatic && m_electrostaticCanvas)
                m_electrostaticCanvas->setPointProbesVisible(enabled);
            if (m_domain == Domain::Magnetostatic && m_magnetostaticCanvas)
                m_magnetostaticCanvas->setPointProbesVisible(enabled);
        });
        connect(m_table, &QTableWidget::currentCellChanged, this,
                [this](int currentRow, int, int, int) {
                    if (m_refreshing) return;
                    if (currentRow >= 0 && currentRow < m_probes.size())
                    {
                        m_selectedProbe = currentRow;
                        syncCanvas();
                    }
                });
        connect(m_table, &QTableWidget::itemChanged, this,
                [this](QTableWidgetItem *item) { handleItemChanged(item); });
    }

    void scheduleRefresh() { if (m_refreshTimer) m_refreshTimer->start(); }

    void ensureSelectedProbe()
    {
        if (m_selectedProbe >= 0 && m_selectedProbe < m_probes.size()) return;
        addProbe();
    }

    void addProbe()
    {
        Probe probe;
        probe.name = QStringLiteral("P%1").arg(m_probes.size() + 1);
        if (m_domain == Domain::Electrostatic && m_electrostaticCanvas)
        {
            const auto p = m_electrostaticCanvas->measurementPoint();
            probe.x = p.x; probe.y = p.y; probe.z = p.z;
        }
        else if (m_domain == Domain::Magnetostatic && m_magnetostaticCanvas)
        {
            const auto p = m_magnetostaticCanvas->measurementPoint();
            probe.x = p.x; probe.y = p.y; probe.z = p.z;
        }
        m_probes.push_back(probe);
        m_selectedProbe = m_probes.size() - 1;
        syncCanvas(); refreshTable(); selectProbe(m_selectedProbe);
    }

    void duplicateProbe()
    {
        ensureSelectedProbe();
        if (m_selectedProbe < 0) return;
        Probe copy = m_probes[m_selectedProbe];
        copy.name = QStringLiteral("P%1").arg(m_probes.size() + 1);
        copy.x += 0.15;
        m_probes.push_back(copy);
        m_selectedProbe = m_probes.size() - 1;
        syncCanvas(); refreshTable(); selectProbe(m_selectedProbe);
    }

    void removeProbe()
    {
        if (m_selectedProbe < 0 || m_selectedProbe >= m_probes.size()) return;
        m_probes.removeAt(m_selectedProbe);
        if (m_probes.isEmpty()) m_selectedProbe = -1;
        else m_selectedProbe = std::clamp(m_selectedProbe, 0, int(m_probes.size()) - 1);
        syncCanvas(); refreshTable();
        if (m_selectedProbe >= 0) selectProbe(m_selectedProbe);
    }

    void selectProbe(int index)
    {
        if (index < 0 || index >= m_probes.size()) return;
        m_selectedProbe = index;
        syncCanvas();
        if (m_table && m_table->currentRow() != index)
        {
            const QSignalBlocker blocker(m_table);
            m_table->setCurrentCell(index, 0);
            m_table->selectRow(index);
        }
    }

    void moveProbeFromCanvas(int index, double x, double y, double z)
    {
        if (index < 0 || index >= m_probes.size()) return;
        m_selectedProbe = index;
        m_probes[index].x = x; m_probes[index].y = y; m_probes[index].z = z;
        syncCanvas();
        scheduleRefresh();
    }

    void syncCanvas()
    {
        QStringList names;
        names.reserve(m_probes.size());
        if (m_domain == Domain::Electrostatic && m_electrostaticCanvas)
        {
            QVector<ElectrostaticVec3> positions;
            positions.reserve(m_probes.size());
            for (const auto &probe : m_probes)
            {
                positions.push_back({probe.x, probe.y, probe.z});
                names.push_back(probe.name);
            }
            m_electrostaticCanvas->setPointProbes(positions, names, m_selectedProbe);
        }
        else if (m_domain == Domain::Magnetostatic && m_magnetostaticCanvas)
        {
            QVector<MagnetostaticVec3> positions;
            positions.reserve(m_probes.size());
            for (const auto &probe : m_probes)
            {
                positions.push_back({probe.x, probe.y, probe.z});
                names.push_back(probe.name);
            }
            m_magnetostaticCanvas->setPointProbes(positions, names, m_selectedProbe);
        }
    }

    void refreshTable()
    {
        if (!m_table) return;
        if (m_refreshTimer) m_refreshTimer->stop();
        m_refreshing = true;
        const QSignalBlocker blocker(m_table);
        m_table->setRowCount(m_probes.size());
        int issueCount = 0;

        for (int row = 0; row < m_probes.size(); ++row)
        {
            const auto &probe = m_probes[row];
            m_table->setItem(row, Name, new QTableWidgetItem(probe.name));
            m_table->setItem(row, X, new QTableWidgetItem(QString::number(probe.x, 'g', 9)));
            m_table->setItem(row, Y, new QTableWidgetItem(QString::number(probe.y, 'g', 9)));
            m_table->setItem(row, Z, new QTableWidgetItem(QString::number(probe.z, 'g', 9)));

            QString status = QStringLiteral("OK");
            if (m_domain == Domain::Electrostatic)
            {
                const auto f = m_electrostaticModel
                    ? m_electrostaticModel->fieldAt({probe.x, probe.y, probe.z})
                    : ElectrostaticFieldResult{};
                m_table->setItem(row, Magnitude, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.electricField.norm(), QStringLiteral("V/m"))));
                m_table->setItem(row, ComponentX, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.electricField.x, QStringLiteral("V/m"))));
                m_table->setItem(row, ComponentY, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.electricField.y, QStringLiteral("V/m"))));
                m_table->setItem(row, ComponentZ, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.electricField.z, QStringLiteral("V/m"))));
                m_table->setItem(row, ScalarExtra, readonlyItem((!f.singular && f.potentialDefined) ? formatEngineering(f.potential, QStringLiteral("V")) : QStringLiteral("n/a")));
                QStringList notes;
                if (f.singular) notes << QStringLiteral("E singular");
                if (!f.potentialDefined) notes << QStringLiteral("V reference n/a");
                if (!notes.isEmpty()) status = notes.join(QStringLiteral("; "));
            }
            else
            {
                const auto f = m_magnetostaticModel
                    ? m_magnetostaticModel->fieldAt({probe.x, probe.y, probe.z})
                    : MagnetostaticFieldResult{};
                m_table->setItem(row, Magnitude, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.magneticFluxDensity.norm(), QStringLiteral("T"))));
                m_table->setItem(row, ComponentX, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.magneticFluxDensity.x, QStringLiteral("T"))));
                m_table->setItem(row, ComponentY, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.magneticFluxDensity.y, QStringLiteral("T"))));
                m_table->setItem(row, ComponentZ, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.magneticFluxDensity.z, QStringLiteral("T"))));
                m_table->setItem(row, ScalarExtra, readonlyItem(f.singular ? QStringLiteral("n/a") : formatEngineering(f.magneticField.norm(), QStringLiteral("A/m"))));
                if (f.singular) status = QStringLiteral("B singular");
            }
            if (status != QStringLiteral("OK")) ++issueCount;
            m_table->setItem(row, Status, readonlyItem(status));
        }

        if (m_selectedProbe >= 0 && m_selectedProbe < m_probes.size())
        {
            m_table->setCurrentCell(m_selectedProbe, 0);
            m_table->selectRow(m_selectedProbe);
        }
        m_refreshing = false;
        m_summary->setText(QStringLiteral("%1 probe(s) | selected: %2%3")
                               .arg(m_probes.size())
                               .arg(m_selectedProbe >= 0 && m_selectedProbe < m_probes.size() ? m_probes[m_selectedProbe].name : QStringLiteral("none"))
                               .arg(issueCount ? QStringLiteral(" | %1 probe(s) have an undefined/singular quantity").arg(issueCount) : QString()));
    }

    void handleItemChanged(QTableWidgetItem *item)
    {
        if (m_refreshing || !item) return;
        const int row = item->row();
        if (row < 0 || row >= m_probes.size()) return;
        if (item->column() == Name)
        {
            const QString name = item->text().trimmed();
            m_probes[row].name = name.isEmpty() ? QStringLiteral("P%1").arg(row + 1) : name;
        }
        else if (item->column() >= X && item->column() <= Z)
        {
            bool ok = false;
            const double value = item->text().toDouble(&ok);
            if (!ok || !std::isfinite(value)) { refreshTable(); return; }
            if (item->column() == X) m_probes[row].x = value;
            if (item->column() == Y) m_probes[row].y = value;
            if (item->column() == Z) m_probes[row].z = value;
        }
        else return;
        m_selectedProbe = row;
        syncCanvas(); scheduleRefresh();
    }

    void exportProbes()
    {
        if (m_probes.isEmpty()) return;
        const bool electro = m_domain == Domain::Electrostatic;
        const QString defaultName = electro ? QStringLiteral("electrostatic_probes.csv") : QStringLiteral("magnetostatic_probes.csv");
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export point probes"), defaultName, QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        { QMessageBox::warning(this, QStringLiteral("Export"), file.errorString()); return; }
        QTextStream out(&file);
        if (electro)
            out << "name,x_m,y_m,z_m,E_V_per_m,Ex_V_per_m,Ey_V_per_m,Ez_V_per_m,V_V,status\n";
        else
            out << "name,x_m,y_m,z_m,B_T,Bx_T,By_T,Bz_T,H_A_per_m,status\n";

        for (const auto &probe : m_probes)
        {
            out << probe.name << ',' << csvNumber(probe.x) << ',' << csvNumber(probe.y) << ',' << csvNumber(probe.z) << ',';
            QString status = QStringLiteral("OK");
            if (electro)
            {
                const auto f = m_electrostaticModel ? m_electrostaticModel->fieldAt({probe.x, probe.y, probe.z}) : ElectrostaticFieldResult{};
                out << (f.singular ? QString() : csvNumber(f.electricField.norm())) << ','
                    << (f.singular ? QString() : csvNumber(f.electricField.x)) << ','
                    << (f.singular ? QString() : csvNumber(f.electricField.y)) << ','
                    << (f.singular ? QString() : csvNumber(f.electricField.z)) << ','
                    << ((!f.singular && f.potentialDefined) ? csvNumber(f.potential) : QString()) << ',';
                if (f.singular) status = QStringLiteral("E singular");
                else if (!f.potentialDefined) status = QStringLiteral("V reference n/a");
            }
            else
            {
                const auto f = m_magnetostaticModel ? m_magnetostaticModel->fieldAt({probe.x, probe.y, probe.z}) : MagnetostaticFieldResult{};
                out << (f.singular ? QString() : csvNumber(f.magneticFluxDensity.norm())) << ','
                    << (f.singular ? QString() : csvNumber(f.magneticFluxDensity.x)) << ','
                    << (f.singular ? QString() : csvNumber(f.magneticFluxDensity.y)) << ','
                    << (f.singular ? QString() : csvNumber(f.magneticFluxDensity.z)) << ','
                    << (f.singular ? QString() : csvNumber(f.magneticField.norm())) << ',';
                if (f.singular) status = QStringLiteral("B singular");
            }
            status.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            out << QStringLiteral("\"") << status << QStringLiteral("\"\n");
        }
    }

    Domain m_domain;
    ElectrostaticModel *m_electrostaticModel = nullptr;
    ElectrostaticCanvas *m_electrostaticCanvas = nullptr;
    MagnetostaticModel *m_magnetostaticModel = nullptr;
    MagnetostaticCanvas *m_magnetostaticCanvas = nullptr;
    QVector<Probe> m_probes;
    int m_selectedProbe = -1;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_showOnCanvas = nullptr;
    QLabel *m_summary = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_refreshing = false;
};
}

FieldProfilesWidget::FieldProfilesWidget(ElectrostaticModel *electrostaticModel,
                                         ElectrostaticCanvas *electrostaticCanvas,
                                         MagnetostaticModel *magnetostaticModel,
                                         MagnetostaticCanvas *magnetostaticCanvas,
                                         QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(new ElectrostaticProfilePage(electrostaticModel, electrostaticCanvas, tabs), QStringLiteral("Electrostatic profile"));
    tabs->addTab(new MagnetostaticProfilePage(magnetostaticModel, magnetostaticCanvas, tabs), QStringLiteral("Magnetostatic profile"));
    tabs->addTab(new PointProbesPage(electrostaticModel, electrostaticCanvas, magnetostaticModel, magnetostaticCanvas, tabs), QStringLiteral("Point probes"));
    layout->addWidget(tabs, 1);
}

QWidget *createElectrostaticProfilePage(ElectrostaticModel *model,
                                        ElectrostaticCanvas *canvas,
                                        QWidget *parent)
{
    return new ElectrostaticProfilePage(model, canvas, parent);
}

QWidget *createElectrostaticPointProbesPage(ElectrostaticModel *model,
                                            ElectrostaticCanvas *canvas,
                                            QWidget *parent)
{
    return new SingleDomainPointProbesPage(SingleDomainPointProbesPage::Domain::Electrostatic,
                                           model, canvas, nullptr, nullptr, parent);
}

QWidget *createMagnetostaticProfilePage(MagnetostaticModel *model,
                                        MagnetostaticCanvas *canvas,
                                        QWidget *parent)
{
    return new MagnetostaticProfilePage(model, canvas, parent);
}

QWidget *createMagnetostaticPointProbesPage(MagnetostaticModel *model,
                                            MagnetostaticCanvas *canvas,
                                            QWidget *parent)
{
    return new SingleDomainPointProbesPage(SingleDomainPointProbesPage::Domain::Magnetostatic,
                                           nullptr, nullptr, model, canvas, parent);
}
