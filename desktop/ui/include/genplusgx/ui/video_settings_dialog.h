#pragma once

#include "genplusgx/settings/video_settings.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QComboBox;

namespace genplusgx::ui {

class VideoSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<bool(const settings::VideoSettings&)>;

  explicit VideoSettingsDialog(
    settings::VideoSettings settings,
    QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] settings::VideoSettings settings() const;
  void setSettings(const settings::VideoSettings& settings);

private:
  [[nodiscard]] bool apply();
  void restoreDefaults();

  QComboBox* aspect_{nullptr};
  QComboBox* scaling_{nullptr};
  QComboBox* presentationFilter_{nullptr};
  QComboBox* overscan_{nullptr};
  QComboBox* ntscFilter_{nullptr};
  QComboBox* interlacedRender_{nullptr};
  QCheckBox* gameGearExtended_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
