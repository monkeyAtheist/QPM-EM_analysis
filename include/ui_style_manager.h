#pragma once

#include <QPalette>
#include <QString>

class QApplication;

namespace UiStyle
{
enum class ThemeMode
{
    System = 0,
    Light,
    Dark
};

enum class Accent
{
    Blue = 0,
    Teal,
    Amber,
    Purple
};

enum class Density
{
    Compact = 0,
    Comfortable,
    Spacious
};

struct Settings
{
    ThemeMode theme = ThemeMode::System;
    Accent accent = Accent::Blue;
    Density density = Density::Comfortable;
    int fontPointDelta = 0;
};

void captureSystemDefaults(const QApplication &app);
Settings loadSettings();
void saveSettings(const Settings &settings);
void apply(QApplication &app, const Settings &settings);
void applySaved(QApplication &app);
void resetToDefaults(QApplication &app);

QString themeName(ThemeMode mode);
QString accentName(Accent accent);
QString densityName(Density density);
} // namespace UiStyle
