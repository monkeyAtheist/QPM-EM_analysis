#include "ui_style_manager.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

#include <algorithm>

namespace UiStyle
{
namespace
{
QPalette g_systemPalette;
QFont g_systemFont;
QString g_systemStyleKey;
bool g_captured = false;

QString canonicalStyleKey(const QString &objectName)
{
    for (const QString &key : QStyleFactory::keys())
        if (key.compare(objectName, Qt::CaseInsensitive) == 0)
            return key;
    return {};
}

QColor accentColor(Accent accent, bool dark)
{
    switch (accent)
    {
        case Accent::Teal:   return dark ? QColor(62, 210, 184) : QColor(0, 132, 118);
        case Accent::Amber:  return dark ? QColor(255, 190, 70) : QColor(194, 118, 0);
        case Accent::Purple: return dark ? QColor(190, 135, 255) : QColor(120, 70, 190);
        case Accent::Blue:
        default:             return dark ? QColor(92, 178, 255) : QColor(0, 112, 210);
    }
}

QPalette darkPalette(Accent accent)
{
    QPalette p;
    const QColor window(34, 35, 38);
    const QColor base(28, 29, 32);
    const QColor alternate(41, 43, 47);
    const QColor button(46, 48, 52);
    const QColor text(238, 241, 244);
    const QColor disabled(135, 139, 145);
    const QColor highlight = accentColor(accent, true);

    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, QColor(52, 54, 58));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, QColor(10, 15, 20));
    p.setColor(QPalette::Link, highlight);
    p.setColor(QPalette::LinkVisited, highlight.lighter(120));
    p.setColor(QPalette::PlaceholderText, QColor(155, 160, 166));
    p.setColor(QPalette::Mid, QColor(82, 85, 91));
    p.setColor(QPalette::Dark, QColor(18, 19, 21));
    p.setColor(QPalette::Light, QColor(76, 79, 84));
    p.setColor(QPalette::Midlight, QColor(60, 63, 68));

    for (auto group : {QPalette::Disabled, QPalette::Inactive})
    {
        p.setColor(group, QPalette::WindowText, disabled);
        p.setColor(group, QPalette::Text, disabled);
        p.setColor(group, QPalette::ButtonText, disabled);
        p.setColor(group, QPalette::PlaceholderText, QColor(112, 116, 122));
    }
    return p;
}

QPalette lightPalette(Accent accent)
{
    QPalette p;
    const QColor window(244, 246, 249);
    const QColor base(255, 255, 255);
    const QColor alternate(236, 240, 245);
    const QColor button(244, 246, 249);
    const QColor text(30, 34, 39);
    const QColor disabled(135, 140, 147);
    const QColor highlight = accentColor(accent, false);

    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 230));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, highlight);
    p.setColor(QPalette::LinkVisited, highlight.darker(120));
    p.setColor(QPalette::PlaceholderText, QColor(115, 120, 126));
    p.setColor(QPalette::Mid, QColor(178, 183, 190));
    p.setColor(QPalette::Dark, QColor(145, 150, 158));
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(220, 224, 230));

    for (auto group : {QPalette::Disabled, QPalette::Inactive})
    {
        p.setColor(group, QPalette::WindowText, disabled);
        p.setColor(group, QPalette::Text, disabled);
        p.setColor(group, QPalette::ButtonText, disabled);
        p.setColor(group, QPalette::PlaceholderText, QColor(165, 169, 175));
    }
    return p;
}

QString applicationStyleSheet(const QPalette &p, const Settings &settings)
{
    const bool dark = p.color(QPalette::Window).lightness() < 128;
    const QColor accent = accentColor(settings.accent, dark);
    const QColor base = p.color(QPalette::Base);
    const QColor alternate = p.color(QPalette::AlternateBase);
    const QColor mid = p.color(QPalette::Mid);
    const QColor text = p.color(QPalette::Text);
    const int pad = settings.density == Density::Compact ? 3 : settings.density == Density::Spacious ? 7 : 5;
    // Keep controls tall enough when the user increases the global UI font. A fixed
    // 22/26/30 px minimum can clip QSpinBox/QDoubleSpinBox text at +3…+6 pt.
    const int baseControlHeight = settings.density == Density::Compact ? 22 : settings.density == Density::Spacious ? 30 : 26;
    const int fontDrivenHeight = QFontMetrics(QApplication::font()).height() + (settings.density == Density::Compact ? 6 : settings.density == Density::Spacious ? 12 : 9);
    const int controlHeight = std::max(baseControlHeight + std::max(0, settings.fontPointDelta) * 2, fontDrivenHeight);

    return QStringLiteral(R"QSS(
QWidget { selection-background-color: %1; selection-color: palette(highlighted-text); }
QMenuBar { spacing: 4px; background-color: %7; color: %8; }
QMenuBar::item { padding: %2px 8px; background: transparent; color: %8; }
QMenuBar::item:selected, QMenuBar::item:pressed { background-color: %5; color: %8; border-radius: 3px; }
QMenu { padding: 4px; background-color: %7; color: %8; border: 1px solid %6; }
QMenu::item { padding: %2px 26px %2px 22px; border-radius: 3px; background: transparent; color: %8; }
QMenu::item:selected, QMenu::item:pressed { background-color: %5; color: %8; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background-color: %6; margin: 4px 8px; }
QGroupBox { margin-top: 9px; padding-top: 9px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; }
QPushButton, QToolButton { min-height: %4px; padding-left: 8px; padding-right: 8px; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: %4px; padding-left: 5px; padding-right: 5px; }
QComboBox { padding-right: 26px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 22px; border-left: 1px solid %6; }
QAbstractItemView { selection-background-color: %1; selection-color: palette(highlighted-text); }
QTableView, QTableWidget, QTreeView, QListView { background-color: %7; color: %8; alternate-background-color: %5; gridline-color: %6; }
QHeaderView { background-color: %5; color: %8; }
QHeaderView::section, QTableView QHeaderView::section, QTableWidget QHeaderView::section { background: %5; color: %8; padding: %2px 6px; border: 0; border-right: 1px solid %6; border-bottom: 1px solid %6; }
QTableCornerButton::section, QTableView QTableCornerButton::section, QTableWidget QTableCornerButton::section { background: %5; border: 0; border-right: 1px solid %6; border-bottom: 1px solid %6; }
QTabBar::tab { padding: %2px 10px; }
QSplitter::handle { background-color: %6; }
QSplitter::handle:hover { background-color: %1; }
QStatusBar { border-top: 1px solid %6; }
QToolTip { border: 1px solid %6; padding: 4px; }
)QSS")
        .arg(accent.name(), QString::number(pad), alternate.name(), QString::number(controlHeight), alternate.name(), mid.name(), base.name(), text.name());
}
}

void captureSystemDefaults(const QApplication &app)
{
    if (g_captured) return;
    g_systemPalette = app.palette();
    g_systemFont = app.font();
    g_systemStyleKey = canonicalStyleKey(app.style() ? app.style()->objectName() : QString());
    g_captured = true;
}

Settings loadSettings()
{
    QSettings s;
    Settings out;
    out.theme = static_cast<ThemeMode>(std::clamp(s.value(QStringLiteral("appearance/theme"), 0).toInt(), 0, 2));
    out.accent = static_cast<Accent>(std::clamp(s.value(QStringLiteral("appearance/accent"), 0).toInt(), 0, 3));
    out.density = static_cast<Density>(std::clamp(s.value(QStringLiteral("appearance/density"), 1).toInt(), 0, 2));
    out.fontPointDelta = std::clamp(s.value(QStringLiteral("appearance/fontPointDelta"), 0).toInt(), -3, 6);
    return out;
}

void saveSettings(const Settings &settings)
{
    QSettings s;
    s.setValue(QStringLiteral("appearance/theme"), static_cast<int>(settings.theme));
    s.setValue(QStringLiteral("appearance/accent"), static_cast<int>(settings.accent));
    s.setValue(QStringLiteral("appearance/density"), static_cast<int>(settings.density));
    s.setValue(QStringLiteral("appearance/fontPointDelta"), settings.fontPointDelta);
}

void apply(QApplication &app, const Settings &settings)
{
    captureSystemDefaults(app);

    // Native Windows styles can keep native white menu/header primitives even when a
    // dark application palette is installed. Fusion is palette-driven and therefore
    // gives deterministic Light/Dark rendering. System mode restores the captured style.
    if (settings.theme == ThemeMode::System)
    {
        if (!g_systemStyleKey.isEmpty() && (!app.style() || app.style()->objectName().compare(g_systemStyleKey, Qt::CaseInsensitive) != 0))
            if (QStyle *style = QStyleFactory::create(g_systemStyleKey))
                app.setStyle(style);
    }
    else if (!app.style() || app.style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0)
    {
        if (QStyle *style = QStyleFactory::create(QStringLiteral("Fusion")))
            app.setStyle(style);
    }

    QPalette palette;
    if (settings.theme == ThemeMode::Dark) palette = darkPalette(settings.accent);
    else if (settings.theme == ThemeMode::Light) palette = lightPalette(settings.accent);
    else
    {
        palette = g_systemPalette;
        // Even in System mode, use the configured accent for selection/link colors.
        const bool dark = palette.color(QPalette::Window).lightness() < 128;
        const QColor accent = accentColor(settings.accent, dark);
        palette.setColor(QPalette::Highlight, accent);
        palette.setColor(QPalette::Link, accent);
    }
    app.setPalette(palette);

    QFont font = g_systemFont;
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(std::max(6.0, font.pointSizeF() + settings.fontPointDelta));
    app.setFont(font);
    app.setStyleSheet(applicationStyleSheet(palette, settings));
}

void applySaved(QApplication &app)
{
    apply(app, loadSettings());
}

void resetToDefaults(QApplication &app)
{
    QSettings s;
    s.remove(QStringLiteral("appearance"));
    apply(app, Settings{});
}

QString themeName(ThemeMode mode)
{
    switch (mode) { case ThemeMode::Light: return QStringLiteral("Light"); case ThemeMode::Dark: return QStringLiteral("Dark"); default: return QStringLiteral("System"); }
}
QString accentName(Accent accent)
{
    switch (accent) { case Accent::Teal: return QStringLiteral("Teal"); case Accent::Amber: return QStringLiteral("Amber"); case Accent::Purple: return QStringLiteral("Purple"); default: return QStringLiteral("Blue"); }
}
QString densityName(Density density)
{
    switch (density) { case Density::Compact: return QStringLiteral("Compact"); case Density::Spacious: return QStringLiteral("Spacious"); default: return QStringLiteral("Comfortable"); }
}
} // namespace UiStyle
