#pragma once

#include "genplusgx/settings/video_settings.h"

#include <QDialog>

#include <functional>
#include <filesystem>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QPushButton;

namespace genplusgx::ui {

class VideoSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<bool(const settings::VideoSettings&)>;
  using PresetChooser = std::function<std::optional<std::filesystem::path>(
    const std::filesystem::path&)>;

  explicit VideoSettingsDialog(
    settings::VideoSettings settings,
    QWidget* parent = nullptr);

  void setSettingsSink(SettingsSink sink);
  void setPresetChooser(PresetChooser chooser);
  [[nodiscard]] settings::VideoSettings settings() const;
  void setSettings(const settings::VideoSettings& settings);

private:
  [[nodiscard]] bool apply();
  void restoreDefaults();
  void choosePreset();
  void updateShaderControls();
  void loadShaderParameters();
  void clearShaderParameters();

  QComboBox* aspect_{nullptr};
  QComboBox* scaling_{nullptr};
  QComboBox* presentationFilter_{nullptr};
  QComboBox* presentationSync_{nullptr};
  QComboBox* presentationBuffering_{nullptr};
  QComboBox* overscan_{nullptr};
  QComboBox* ntscFilter_{nullptr};
  QComboBox* interlacedRender_{nullptr};
  QCheckBox* gameGearExtended_{nullptr};
  QComboBox* shaderMode_{nullptr};
  QLabel* shaderPath_{nullptr};
  QPushButton* chooseShaderPreset_{nullptr};
  QLabel* shaderValidation_{nullptr};
  QGroupBox* shaderParametersGroup_{nullptr};
  QFormLayout* shaderParametersForm_{nullptr};
  std::vector<video::ShaderParameter> shaderParameterMetadata_;
  std::vector<QDoubleSpinBox*> shaderParameterEditors_;
  video::ShaderConfiguration shaderConfiguration_;
  SettingsSink settingsSink_;
  PresetChooser presetChooser_;
};

} // namespace genplusgx::ui
