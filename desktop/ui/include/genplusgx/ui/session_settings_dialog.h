#pragma once

#include "genplusgx/settings/session_settings.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;

namespace genplusgx::ui {

class SessionSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<PersistenceStatus(bool)>;

  explicit SessionSettingsDialog(bool resumeOnLaunch, QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] bool resumeOnLaunch() const noexcept;
  void setResumeOnLaunch(bool enabled);

private:
  [[nodiscard]] bool apply();

  QCheckBox* resumeOnLaunch_{nullptr};
  QLabel* detail_{nullptr};
  QLabel* error_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
