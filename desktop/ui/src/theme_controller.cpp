#include "genplusgx/ui/theme_controller.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QStyle>
#include <QStyleFactory>

namespace genplusgx::ui {
namespace {

void setCommonPalette(QPalette& palette,
  const QColor& window,
  const QColor& windowText,
  const QColor& base,
  const QColor& alternateBase,
  const QColor& button,
  const QColor& highlight,
  const QColor& highlightedText)
{
  palette.setColor(QPalette::Window, window);
  palette.setColor(QPalette::WindowText, windowText);
  palette.setColor(QPalette::Base, base);
  palette.setColor(QPalette::AlternateBase, alternateBase);
  palette.setColor(QPalette::ToolTipBase, base);
  palette.setColor(QPalette::ToolTipText, windowText);
  palette.setColor(QPalette::Text, windowText);
  palette.setColor(QPalette::Button, button);
  palette.setColor(QPalette::ButtonText, windowText);
  palette.setColor(QPalette::BrightText, QColor{255, 64, 64});
  palette.setColor(QPalette::Link, highlight);
  palette.setColor(QPalette::Highlight, highlight);
  palette.setColor(QPalette::HighlightedText, highlightedText);
  palette.setColor(QPalette::PlaceholderText,
    QColor{windowText.red(), windowText.green(), windowText.blue(), 160});

  const QColor disabledText = windowText.darker(160);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
  palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
  palette.setColor(QPalette::Disabled, QPalette::Highlight, alternateBase);
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
}

} // namespace

void configureHighDpiPolicy()
{
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

ThemeController::ThemeController(QApplication& application)
    : application_(application), systemPalette_(application.palette()),
      systemStyleName_(
        application.style() != nullptr ? application.style()->name() : QString{}),
      settings_(settings::defaultAppearanceSettings())
{
}

bool ThemeController::apply(const settings::AppearanceSettings& value)
{
  if (!settings::validateAppearanceSettings(value)) {
    return false;
  }

  application_.setStyleSheet({});
  if (value.theme == settings::ThemeMode::system) {
    if (!systemStyleName_.isEmpty()) {
      if (auto* style = QStyleFactory::create(systemStyleName_)) {
        application_.setStyle(style);
      }
    }
    application_.setPalette(systemPalette_);
  } else {
    if (auto* style = QStyleFactory::create(QStringLiteral("Fusion"))) {
      application_.setStyle(style);
    }
    application_.setPalette(
      value.theme == settings::ThemeMode::dark ? darkPalette() : lightPalette());
  }
  settings_ = value;
  return true;
}

const settings::AppearanceSettings& ThemeController::settings() const noexcept
{
  return settings_;
}

QPalette ThemeController::lightPalette()
{
  QPalette palette;
  setCommonPalette(palette,
    QColor{245, 246, 248},
    QColor{24, 26, 29},
    QColor{255, 255, 255},
    QColor{235, 238, 242},
    QColor{240, 242, 245},
    QColor{25, 91, 171},
    QColor{255, 255, 255});
  return palette;
}

QPalette ThemeController::darkPalette()
{
  QPalette palette;
  setCommonPalette(palette,
    QColor{35, 37, 41},
    QColor{238, 240, 243},
    QColor{24, 26, 29},
    QColor{45, 48, 53},
    QColor{48, 51, 56},
    QColor{67, 139, 230},
    QColor{8, 12, 18});
  return palette;
}

} // namespace genplusgx::ui
