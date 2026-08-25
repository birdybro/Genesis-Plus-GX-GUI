#pragma once

#include "genplusgx/settings/appearance_settings.h"

#include <QDialog>

#include <functional>

class QComboBox;
class QLabel;

namespace genplusgx::ui {

class AppearanceSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink =
    std::function<PersistenceStatus(const settings::AppearanceSettings&)>;

  explicit AppearanceSettingsDialog(
    settings::AppearanceSettings current, QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] settings::AppearanceSettings settings() const;
  void setSettings(const settings::AppearanceSettings& settings);

private:
  [[nodiscard]] bool apply();
  void restoreDefaults();

  QComboBox* theme_{nullptr};
  QLabel* validation_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
