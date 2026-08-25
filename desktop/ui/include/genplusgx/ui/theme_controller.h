#pragma once

#include "genplusgx/settings/appearance_settings.h"

#include <QPalette>
#include <QString>

class QApplication;

namespace genplusgx::ui {

void configureHighDpiPolicy();

class ThemeController final {
public:
  explicit ThemeController(QApplication& application);

  [[nodiscard]] bool apply(const settings::AppearanceSettings& settings);
  [[nodiscard]] const settings::AppearanceSettings& settings() const noexcept;

  [[nodiscard]] static QPalette lightPalette();
  [[nodiscard]] static QPalette darkPalette();

private:
  QApplication& application_;
  QPalette systemPalette_;
  QString systemStyleName_;
  settings::AppearanceSettings settings_;
};

} // namespace genplusgx::ui
