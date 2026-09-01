#pragma once

#include "genplusgx/persistence.h"
#include "genplusgx/rewind_configuration.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QSpinBox;

namespace genplusgx::ui {

class RewindSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink =
    std::function<PersistenceStatus(const RewindConfiguration&)>;

  explicit RewindSettingsDialog(
    RewindConfiguration current,
    QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] RewindConfiguration settings() const;
  void setSettings(const RewindConfiguration& settings);

private:
  [[nodiscard]] bool apply();
  void restoreDefaults();
  void updateSummary();

  QCheckBox* enabled_{nullptr};
  QSpinBox* interval_{nullptr};
  QSpinBox* memory_{nullptr};
  QLabel* summary_{nullptr};
  QLabel* error_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
