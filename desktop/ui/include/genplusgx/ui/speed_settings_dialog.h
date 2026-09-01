#pragma once

#include "genplusgx/persistence.h"
#include "genplusgx/timing/speed_configuration.h"

#include <QDialog>

#include <functional>

class QLabel;
class QSpinBox;

namespace genplusgx::ui {

class SpeedSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<PersistenceStatus(
    const EmulationSpeedConfiguration&)>;

  explicit SpeedSettingsDialog(
    EmulationSpeedConfiguration current,
    QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] EmulationSpeedConfiguration settings() const;
  void setSettings(const EmulationSpeedConfiguration& settings);

private:
  [[nodiscard]] bool apply();
  void restoreDefaults();
  void updateSummary();

  QSpinBox* normal_{nullptr};
  QSpinBox* slow_{nullptr};
  QSpinBox* fast_{nullptr};
  QLabel* summary_{nullptr};
  QLabel* error_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
