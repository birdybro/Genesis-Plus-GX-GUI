#pragma once

#include "genplusgx/core_system_settings.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QComboBox;

namespace genplusgx::ui {

class SystemSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<void(const CoreSystemSettings&)>;

  explicit SystemSettingsDialog(
    CoreSystemSettings settings,
    QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] CoreSystemSettings settings() const;
  void setSettings(const CoreSystemSettings& settings);

private:
  void apply();
  void restoreDefaults();

  QComboBox* hardware_{nullptr};
  QComboBox* region_{nullptr};
  QComboBox* videoStandard_{nullptr};
  QComboBox* masterClock_{nullptr};
  QCheckBox* illegalAccessLockups_{nullptr};
  QCheckBox* addressErrors_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
