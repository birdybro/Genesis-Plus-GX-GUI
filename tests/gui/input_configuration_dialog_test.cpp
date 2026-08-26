#include "genplusgx/input/input_profile.h"
#include "genplusgx/ui/binding_capture_button.h"
#include "genplusgx/ui/input_configuration_dialog.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QKeyCombination>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

class InputConfigurationDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void capturesBindingsAndReportsConflicts();
  void profilesDefaultsAndApplySemantics();
  void assignmentsAreValidated();
  void mainWindowActionsOpenStablePages();
};

void InputConfigurationDialogTest::capturesBindingsAndReportsConflicts()
{
  genplusgx::ui::InputConfigurationDialog dialog{
    genplusgx::input::defaultInputConfiguration()};
  dialog.show();
  QApplication::processEvents();

  auto* keyboardA = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("keyboardBindingAButton"));
  auto* keyboardB = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("keyboardBindingBButton"));
  auto* controllerA = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("controllerBindingAButton"));
  auto* controllerC = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("controllerBindingCButton"));
  auto* pauseHotkey = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("hotkeyPauseButton"));
  auto* softResetHotkey = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("hotkeySoftResetButton"));
  auto* fastForwardHold = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("hotkeyFastForwardHoldButton"));
  auto* fastForwardToggle = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("hotkeyFastForwardToggleButton"));
  auto* conflict = dialog.findChild<QLabel*>(QStringLiteral("inputConflictLabel"));
  QVERIFY(keyboardA != nullptr);
  QVERIFY(keyboardB != nullptr);
  QVERIFY(controllerA != nullptr);
  QVERIFY(controllerC != nullptr);
  QVERIFY(pauseHotkey != nullptr);
  QVERIFY(softResetHotkey != nullptr);
  QVERIFY(fastForwardHold != nullptr);
  QVERIFY(fastForwardToggle != nullptr);
  const auto tab =
    QKeyCombination{Qt::NoModifier, Qt::Key_Tab}.toCombined();
  const auto quoteLeft =
    QKeyCombination{Qt::NoModifier, Qt::Key_QuoteLeft}.toCombined();
  QCOMPARE(fastForwardHold->bindingCode(), tab);
  QCOMPARE(fastForwardToggle->bindingCode(), quoteLeft);
  QVERIFY(conflict != nullptr);

  QTest::mouseClick(keyboardA, Qt::LeftButton);
  QVERIFY(keyboardA->isCapturing());
  QVERIFY(keyboardA->text().contains(QStringLiteral("Press")));
  QTest::keyClick(keyboardA, Qt::Key_Q);
  QCOMPARE(keyboardA->bindingCode(), Qt::Key_Q);
  QVERIFY(!keyboardA->isCapturing());
  QVERIFY(!conflict->isVisible());

  const auto originalB = keyboardB->bindingCode();
  QTest::mouseClick(keyboardB, Qt::LeftButton);
  QTest::keyClick(keyboardB, Qt::Key_Q);
  QCOMPARE(keyboardB->bindingCode(), originalB);
  QVERIFY(conflict->isVisible());
  QVERIFY(conflict->text().contains(QStringLiteral("already assigned")));

  QTest::mouseClick(keyboardB, Qt::LeftButton);
  QTest::keyClick(keyboardB, Qt::Key_M);
  QCOMPARE(keyboardB->bindingCode(), originalB);
  QVERIFY(conflict->text().contains(QStringLiteral("hotkey")));

  const auto originalPause = pauseHotkey->bindingCode();
  QTest::mouseClick(pauseHotkey, Qt::LeftButton);
  QTest::keyClick(pauseHotkey, Qt::Key_Q);
  QCOMPARE(pauseHotkey->bindingCode(), originalPause);
  QVERIFY(conflict->text().contains(QStringLiteral("gameplay")));

  const auto controlP = QKeyCombination{
    Qt::ControlModifier, Qt::Key_P}.toCombined();
  QTest::mouseClick(pauseHotkey, Qt::LeftButton);
  QTest::keyClick(pauseHotkey, Qt::Key_P, Qt::ControlModifier);
  QCOMPARE(pauseHotkey->bindingCode(), controlP);
  QVERIFY(!conflict->isVisible());

  const auto originalSoftReset = softResetHotkey->bindingCode();
  QTest::mouseClick(softResetHotkey, Qt::LeftButton);
  QTest::keyClick(softResetHotkey, Qt::Key_P, Qt::ControlModifier);
  QCOMPARE(softResetHotkey->bindingCode(), originalSoftReset);
  QVERIFY(conflict->text().contains(QStringLiteral("already assigned")));

  QTest::mouseClick(keyboardB, Qt::LeftButton);
  QTest::keyClick(keyboardB, Qt::Key_P);
  QCOMPARE(keyboardB->bindingCode(), Qt::Key_P);
  QVERIFY(!conflict->isVisible());

  QTest::mouseClick(controllerA, Qt::LeftButton);
  QVERIFY(dialog.captureControllerButton(SDL_GAMEPAD_BUTTON_MISC1));
  QCOMPARE(controllerA->bindingCode(), static_cast<int>(SDL_GAMEPAD_BUTTON_MISC1));
  QVERIFY(!conflict->isVisible());

  const auto originalC = controllerC->bindingCode();
  QTest::mouseClick(controllerC, Qt::LeftButton);
  QVERIFY(dialog.captureControllerButton(SDL_GAMEPAD_BUTTON_SOUTH));
  QCOMPARE(controllerC->bindingCode(), originalC);
  QVERIFY(conflict->text().contains(QStringLiteral("already assigned")));

  QTest::mouseClick(controllerC, Qt::LeftButton);
  QVERIFY(controllerC->isCapturing());
  QTest::keyClick(controllerC, Qt::Key_Escape);
  // Escape is the keyboard-accessible cancellation path for either capture kind.
  QVERIFY(!controllerC->isCapturing());
}

void InputConfigurationDialogTest::profilesDefaultsAndApplySemantics()
{
  genplusgx::ui::InputConfigurationDialog dialog{
    genplusgx::input::defaultInputConfiguration()};
  int applyCount = 0;
  genplusgx::input::InputConfiguration applied;
  dialog.setConfigurationSink(
    [&applyCount, &applied](const auto& configuration) {
      ++applyCount;
      applied = configuration;
      return true;
    });
  dialog.show();

  auto* profileCombo = dialog.findChild<QComboBox*>(QStringLiteral("inputProfileCombo"));
  auto* add = dialog.findChild<QPushButton*>(QStringLiteral("addInputProfileButton"));
  auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("deleteInputProfileButton"));
  auto* deadzone = dialog.findChild<QSpinBox*>(QStringLiteral("controllerDeadzoneSpinBox"));
  auto* device = dialog.findChild<QComboBox*>(QStringLiteral("logicalDevicePlayer1Combo"));
  auto* axis = dialog.findChild<QComboBox*>(
    QStringLiteral("controllerAxisLeftXNegativeCombo"));
  auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("inputApplyButton"));
  auto* restore = dialog.findChild<QPushButton*>(QStringLiteral("inputRestoreDefaultsButton"));
  QVERIFY(profileCombo != nullptr);
  QVERIFY(add != nullptr);
  QVERIFY(remove != nullptr);
  QVERIFY(deadzone != nullptr);
  QVERIFY(device != nullptr);
  QVERIFY(axis != nullptr);
  QVERIFY(apply != nullptr);
  QVERIFY(restore != nullptr);

  QCOMPARE(profileCombo->count(), 1);
  QVERIFY(!remove->isEnabled());
  QTest::mouseClick(add, Qt::LeftButton);
  QCOMPARE(profileCombo->count(), 2);
  QCOMPARE(profileCombo->currentText(), QStringLiteral("Profile 2"));
  QVERIFY(remove->isEnabled());

  deadzone->setValue(14'000);
  device->setCurrentIndex(device->findData(
    static_cast<int>(genplusgx::input::LogicalDeviceType::segaMouse)));
  axis->setCurrentIndex(axis->findData(static_cast<int>(genplusgx::InputButton::a)));
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(applyCount, 1);
  QCOMPARE(applied.activeProfile, std::string{"Profile 2"});
  QCOMPARE(applied.active()->deadzone, 14'000);
  QCOMPARE(applied.active()->devices[0],
    genplusgx::input::LogicalDeviceType::segaMouse);
  QCOMPARE(applied.active()->controllerAxisBindings.front().input,
    genplusgx::InputButton::a);

  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(deadzone->value(), 8'000);
  QCOMPARE(device->currentData().toInt(),
    static_cast<int>(genplusgx::input::LogicalDeviceType::pad6Button));
  dialog.openTab(genplusgx::ui::InputConfigurationTab::hotkeys);
  auto* pauseHotkey = dialog.findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("hotkeyPauseButton"));
  QVERIFY(pauseHotkey != nullptr);
  QTest::mouseClick(pauseHotkey, Qt::LeftButton);
  QTest::keyClick(pauseHotkey, Qt::Key_P, Qt::ControlModifier);
  const auto controlP = QKeyCombination{
    Qt::ControlModifier, Qt::Key_P}.toCombined();
  QCOMPARE(pauseHotkey->bindingCode(), controlP);
  QTest::mouseClick(restore, Qt::LeftButton);
  const auto space = QKeyCombination{
    Qt::NoModifier, Qt::Key_Space}.toCombined();
  QCOMPARE(pauseHotkey->bindingCode(), space);
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(profileCombo->count(), 1);
  QCOMPARE(profileCombo->currentText(), QStringLiteral("Default"));

  dialog.setConfigurationSink([](const auto&) { return false; });
  QVERIFY(!dialog.applyChanges());
  QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("inputConflictLabel"))->text()
    .contains(QStringLiteral("could not be applied")));
}

void InputConfigurationDialogTest::assignmentsAreValidated()
{
  std::vector controllers{
    genplusgx::input::ControllerInfo{11U, "First virtual pad", 0U},
    genplusgx::input::ControllerInfo{22U, "Second virtual pad", 1U},
  };
  genplusgx::ui::InputConfigurationDialog dialog{
    genplusgx::input::defaultInputConfiguration(), controllers};
  std::vector<std::pair<std::uint32_t, std::size_t>> assignments;
  dialog.setAssignmentSink(
    [&assignments](std::uint32_t id, std::size_t player) {
      assignments.emplace_back(id, player);
      return true;
    });
  dialog.show();

  auto* first = dialog.findChild<QComboBox*>(QStringLiteral("controllerAssignment0Combo"));
  auto* second = dialog.findChild<QComboBox*>(QStringLiteral("controllerAssignment1Combo"));
  auto* conflict = dialog.findChild<QLabel*>(QStringLiteral("inputConflictLabel"));
  QVERIFY(first != nullptr);
  QVERIFY(second != nullptr);
  QCOMPARE(first->currentIndex(), 0);
  QCOMPARE(second->currentIndex(), 1);

  second->setCurrentIndex(0);
  QVERIFY(!dialog.applyChanges());
  QVERIFY(assignments.empty());
  QVERIFY(conflict->text().contains(QStringLiteral("unique")));

  first->setCurrentIndex(2);
  second->setCurrentIndex(3);
  QVERIFY(dialog.applyChanges());
  QCOMPARE(assignments.size(), 2U);
  QCOMPARE(assignments[0], std::make_pair(11U, std::size_t{2U}));
  QCOMPARE(assignments[1], std::make_pair(22U, std::size_t{3U}));

  dialog.setAssignmentSink([](std::uint32_t, std::size_t) { return false; });
  QVERIFY(!dialog.applyChanges());
  QVERIFY(conflict->text().contains(QStringLiteral("could not be applied")));
}

void InputConfigurationDialogTest::mainWindowActionsOpenStablePages()
{
  genplusgx::ui::MainWindow window;
  auto custom = genplusgx::input::defaultInputConfiguration();
  const auto pauseBinding = std::find_if(custom.hotkeys.begin(), custom.hotkeys.end(),
    [](const genplusgx::input::HotkeyBinding& binding) {
      return binding.action == genplusgx::input::EmulatorHotkeyAction::pause;
    });
  QVERIFY(pauseBinding != custom.hotkeys.end());
  pauseBinding->keyCombination = QKeyCombination{
    Qt::ControlModifier, Qt::Key_P}.toCombined();
  window.setInputConfiguration(custom);
  const QKeySequence controlP{
    QKeyCombination{Qt::ControlModifier, Qt::Key_P}};
  QCOMPARE(window.findChild<QAction*>(QStringLiteral("pauseAction"))->shortcut(),
    controlP);
  window.show();
  window.findChild<QAction*>(QStringLiteral("controllerConfigurationAction"))->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::InputConfigurationDialog*>(
    QStringLiteral("inputConfigurationDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  QCOMPARE(dialog->findChild<QTabWidget*>(QStringLiteral("inputConfigurationTabs"))
      ->currentIndex(), 0);

  window.findChild<QAction*>(QStringLiteral("playerAssignmentsAction"))->trigger();
  QApplication::processEvents();
  QCOMPARE(dialog->findChild<QTabWidget*>(QStringLiteral("inputConfigurationTabs"))
      ->currentIndex(), 1);
  QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("noControllersLabel")) != nullptr);
  window.setConnectedControllers({
    genplusgx::input::ControllerInfo{77U, "Hot-plug test pad", 0U}});
  QApplication::processEvents();
  QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("noControllersLabel")) == nullptr);
  QVERIFY(dialog->findChild<QComboBox*>(
    QStringLiteral("controllerAssignment0Combo")) != nullptr);
  dialog->openTab(genplusgx::ui::InputConfigurationTab::hotkeys);
  QCOMPARE(dialog->findChild<QTabWidget*>(QStringLiteral("inputConfigurationTabs"))
      ->currentIndex(), 3);
  QVERIFY(dialog->findChild<genplusgx::ui::BindingCaptureButton*>(
    QStringLiteral("hotkeySoftResetButton")) != nullptr);
  dialog->close();
}

QTEST_MAIN(InputConfigurationDialogTest)

#include "input_configuration_dialog_test.moc"
