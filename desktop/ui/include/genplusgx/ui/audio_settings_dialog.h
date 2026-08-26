#pragma once

#include "genplusgx/settings/audio_settings.h"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace genplusgx::ui {

class AudioSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<void(const settings::AudioSettings&)>;

  explicit AudioSettingsDialog(
    settings::AudioSettings settings,
    std::vector<std::string> availableDevices = {},
    QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  [[nodiscard]] settings::AudioSettings settings() const;
  void setSettings(const settings::AudioSettings& settings);
  void setAvailableDevices(std::vector<std::string> devices);

private:
  void apply();
  void restoreDefaults();
  void updateFilterControls();

  QComboBox* device_{nullptr};
  QSpinBox* latency_{nullptr};
  QSpinBox* volume_{nullptr};
  QCheckBox* muted_{nullptr};
  QComboBox* output_{nullptr};
  QSpinBox* psg_{nullptr};
  QSpinBox* fm_{nullptr};
  QSpinBox* cdda_{nullptr};
  QSpinBox* pcm_{nullptr};
  QComboBox* filter_{nullptr};
  QSpinBox* lowPass_{nullptr};
  QSpinBox* eqLow_{nullptr};
  QSpinBox* eqMid_{nullptr};
  QSpinBox* eqHigh_{nullptr};
  QComboBox* ym2612_{nullptr};
  QComboBox* ym2413Mode_{nullptr};
  QComboBox* ym2413Core_{nullptr};
  QCheckBox* highQualityFm_{nullptr};
  QCheckBox* highQualityPsg_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
