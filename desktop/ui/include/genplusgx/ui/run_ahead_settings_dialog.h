#pragma once

#include "genplusgx/persistence.h"
#include "genplusgx/run_ahead_configuration.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QSpinBox;

namespace genplusgx::ui {

class RunAheadSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<PersistenceStatus(
    const RunAheadConfiguration&)>;

  explicit RunAheadSettingsDialog(
    RunAheadConfiguration current,
    QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] RunAheadConfiguration settings() const;
  void setSettings(const RunAheadConfiguration& settings);

private:
  [[nodiscard]] bool apply();
  void restoreDefaults();
  void updateSummary();

  QCheckBox* enabled_{nullptr};
  QSpinBox* frames_{nullptr};
  QLabel* summary_{nullptr};
  QLabel* error_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
