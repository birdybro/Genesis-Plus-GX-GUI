#pragma once

#include "genplusgx/library/online_metadata_settings.h"
#include "genplusgx/persistence.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace genplusgx::ui {

class OnlineMetadataDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<PersistenceStatus(
    const library::OnlineMetadataSettings&)>;

  explicit OnlineMetadataDialog(QWidget* parent = nullptr);

  void setSettings(library::OnlineMetadataSettings settings);
  void setSettingsSink(SettingsSink sink);

private:
  void loadSettings();
  void updateEnabledState();
  [[nodiscard]] library::OnlineMetadataSettings editedSettings() const;
  bool applySettings();

  library::OnlineMetadataSettings settings_;
  SettingsSink settingsSink_;
  QCheckBox* enabled_{nullptr};
  QCheckBox* automatic_{nullptr};
  QCheckBox* artwork_{nullptr};
  QComboBox* provider_{nullptr};
  QLineEdit* endpoint_{nullptr};
  QLineEdit* language_{nullptr};
  QLineEdit* region_{nullptr};
  QSpinBox* cacheSize_{nullptr};
  QLabel* disclosure_{nullptr};
  QLabel* errorLabel_{nullptr};
  QDialogButtonBox* buttons_{nullptr};
  QPushButton* restoreDefaults_{nullptr};
};

} // namespace genplusgx::ui
