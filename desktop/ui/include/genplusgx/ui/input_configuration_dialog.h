#pragma once

#include "genplusgx/input/input_profile.h"

#include <QDialog>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;

namespace genplusgx::ui {

class BindingCaptureButton;

enum class InputConfigurationTab {
  bindings,
  assignments,
  advanced,
  hotkeys,
};

class InputConfigurationDialog final : public QDialog {
  Q_OBJECT

public:
  using ConfigurationSink = std::function<bool(const input::InputConfiguration&)>;
  using AssignmentSink = std::function<bool(std::uint32_t, std::size_t)>;

  explicit InputConfigurationDialog(
    input::InputConfiguration configuration,
    std::vector<input::ControllerInfo> controllers = {},
    QWidget* parent = nullptr);

  void setConfigurationSink(ConfigurationSink sink);
  void setAssignmentSink(AssignmentSink sink);
  void setControllers(std::vector<input::ControllerInfo> controllers);
  void openTab(InputConfigurationTab tab);

  [[nodiscard]] input::InputConfiguration configuration() const;
  [[nodiscard]] bool captureControllerButton(SDL_GamepadButton button);
  [[nodiscard]] bool applyChanges();

private:
  void buildProfileRow(QVBoxLayout& layout);
  QWidget* buildBindingsPage();
  QWidget* buildAssignmentsPage();
  QWidget* buildAdvancedPage();
  QWidget* buildHotkeysPage();
  void refreshProfileList();
  void refreshEditor();
  void refreshBindings();
  void refreshHotkeys();
  void refreshAssignments();
  void addProfile();
  void deleteProfile();
  void restoreCurrentProfile();
  void selectProfile(int index);
  void keyboardCaptured(InputButton input, int key);
  void controllerCaptured(InputButton input, SDL_GamepadButton button);
  void hotkeyCaptured(input::EmulatorHotkeyAction action, int keyCombination);
  void cancelOtherCaptures(BindingCaptureButton* active);
  void showConflict(const QString& message);
  void clearConflict();
  [[nodiscard]] input::InputProfile* activeProfile();
  [[nodiscard]] const input::InputProfile* activeProfile() const;

  input::InputConfiguration configuration_;
  std::vector<input::ControllerInfo> controllers_;
  ConfigurationSink configurationSink_;
  AssignmentSink assignmentSink_;
  QComboBox* profileCombo_{nullptr};
  QPushButton* deleteProfileButton_{nullptr};
  QTabWidget* tabs_{nullptr};
  QLabel* conflictLabel_{nullptr};
  QSpinBox* deadzoneSpin_{nullptr};
  QDialogButtonBox* buttons_{nullptr};
  QWidget* assignmentsPage_{nullptr};
  QFormLayout* assignmentsLayout_{nullptr};
  std::vector<BindingCaptureButton*> keyboardButtons_;
  std::vector<BindingCaptureButton*> controllerButtons_;
  std::vector<BindingCaptureButton*> hotkeyButtons_;
  std::vector<QComboBox*> assignmentCombos_;
  std::vector<QComboBox*> deviceCombos_;
  std::vector<QComboBox*> axisMappingCombos_;
  std::vector<int> reservedHotkeys_;
};

} // namespace genplusgx::ui
