#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/audio_settings_dialog.h"
#include "genplusgx/ui/per_game_settings_dialog.h"
#include "genplusgx/ui/video_settings_dialog.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest/QTest>

#include <vector>

class PerGameSettingsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void sparseOverridesCanBeEditedAndCleared();
  void persistenceFailureDoesNotCommit();
  void mainWindowGatesThePerGameSession();
};

void PerGameSettingsDialogTest::sparseOverridesCanBeEditedAndCleared()
{
  using namespace genplusgx;
  settings::GlobalGameSettings global;
  global.inputProfile = "Default";
  settings::PerGameSettings overrides;
  auto video = global.video;
  video.aspect = video::AspectMode::fourThree;
  overrides.video = video;
  overrides.inputProfile = "Arcade";

  ui::PerGameSettingsDialog dialog{
    overrides, global, {"Default", "Arcade"}, {"Test Output"}};
  std::vector<settings::PerGameSettings> applied;
  dialog.setConfigurationSink([&applied](const auto& configuration) {
    applied.push_back(configuration);
    return PersistenceStatus{};
  });
  dialog.show();
  QApplication::processEvents();

  auto* videoCheck =
    dialog.findChild<QCheckBox*>(QStringLiteral("overrideVideoCheckBox"));
  auto* audioCheck =
    dialog.findChild<QCheckBox*>(QStringLiteral("overrideAudioCheckBox"));
  auto* systemCheck =
    dialog.findChild<QCheckBox*>(QStringLiteral("overrideSystemCheckBox"));
  auto* biosCheck =
    dialog.findChild<QCheckBox*>(QStringLiteral("overrideBiosCheckBox"));
  auto* inputCheck =
    dialog.findChild<QCheckBox*>(QStringLiteral("overrideInputProfileCheckBox"));
  auto* input =
    dialog.findChild<QComboBox*>(QStringLiteral("perGameInputProfileCombo"));
  auto* editVideo =
    dialog.findChild<QPushButton*>(QStringLiteral("editPerGameVideoButton"));
  auto* editAudio =
    dialog.findChild<QPushButton*>(QStringLiteral("editPerGameAudioButton"));
  auto* editSystem =
    dialog.findChild<QPushButton*>(QStringLiteral("editPerGameSystemButton"));
  auto* editBios =
    dialog.findChild<QPushButton*>(QStringLiteral("editPerGameBiosButton"));
  auto* apply =
    dialog.findChild<QPushButton*>(QStringLiteral("applyPerGameSettingsButton"));
  auto* useGlobal =
    dialog.findChild<QPushButton*>(QStringLiteral("useGlobalSettingsButton"));
  QVERIFY(videoCheck != nullptr && audioCheck != nullptr && systemCheck != nullptr);
  QVERIFY(biosCheck != nullptr && inputCheck != nullptr && input != nullptr);
  QVERIFY(editVideo != nullptr && editAudio != nullptr && editSystem != nullptr);
  QVERIFY(editBios != nullptr && apply != nullptr);
  QVERIFY(useGlobal != nullptr);
  QVERIFY(videoCheck->isChecked());
  QVERIFY(!audioCheck->isChecked());
  QVERIFY(inputCheck->isChecked());
  QVERIFY(editVideo->isEnabled());
  QVERIFY(
    !editAudio->isEnabled() && !editSystem->isEnabled() && !editBios->isEnabled());
  QCOMPARE(input->currentText(), QStringLiteral("Arcade"));

  audioCheck->setChecked(true);
  QVERIFY(editAudio->isEnabled());
  QTest::mouseClick(editAudio, Qt::LeftButton);
  QApplication::processEvents();
  auto* audioDialog =
    dialog.findChild<ui::AudioSettingsDialog*>(QStringLiteral("audioSettingsDialog"));
  QVERIFY(audioDialog != nullptr && audioDialog->isVisible());
  QVERIFY(!audioDialog->findChild<QComboBox*>(QStringLiteral("audioOutputDeviceCombo"))
      ->isEnabled());
  QVERIFY(!audioDialog->findChild<QSpinBox*>(QStringLiteral("audioLatencySpinBox"))
      ->isEnabled());
  audioDialog->close();
  audioCheck->setChecked(false);

  systemCheck->setChecked(true);
  biosCheck->setChecked(true);
  QVERIFY(editSystem->isEnabled() && editBios->isEnabled());
  systemCheck->setChecked(false);
  biosCheck->setChecked(false);

  QTest::mouseClick(editVideo, Qt::LeftButton);
  QApplication::processEvents();
  auto* videoDialog =
    dialog.findChild<ui::VideoSettingsDialog*>(QStringLiteral("videoSettingsDialog"));
  QVERIFY(videoDialog != nullptr && videoDialog->isVisible());
  auto* aspect = videoDialog->findChild<QComboBox*>(QStringLiteral("videoAspectCombo"));
  aspect->setCurrentIndex(
    aspect->findData(static_cast<int>(video::AspectMode::stretch)));
  QTest::mouseClick(
    videoDialog->findChild<QPushButton*>(QStringLiteral("applyVideoSettingsButton")),
    Qt::LeftButton);
  videoDialog->close();

  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(applied.size(), 1U);
  QVERIFY(applied.back().video.has_value());
  QCOMPARE(applied.back().video->aspect, video::AspectMode::stretch);
  QCOMPARE(*applied.back().inputProfile, std::string{"Arcade"});
  QVERIFY(!applied.back().audio.has_value());

  QTest::mouseClick(useGlobal, Qt::LeftButton);
  QVERIFY(!videoCheck->isChecked());
  QVERIFY(!inputCheck->isChecked());
  QVERIFY(!editVideo->isEnabled());
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(applied.size(), 2U);
  QVERIFY(applied.back().empty());
}

void PerGameSettingsDialogTest::persistenceFailureDoesNotCommit()
{
  using namespace genplusgx;
  settings::GlobalGameSettings global;
  global.inputProfile = "Default";
  ui::PerGameSettingsDialog dialog{{}, global, {"Default"}, {}};
  dialog.setConfigurationSink([](const auto&) {
    return PersistenceStatus{
      .error = PersistenceError::fileWriteFailed,
      .message = "Synthetic settings write failure",
    };
  });
  dialog.show();
  auto* videoCheck =
    dialog.findChild<QCheckBox*>(QStringLiteral("overrideVideoCheckBox"));
  videoCheck->setChecked(true);
  QTest::mouseClick(
    dialog.findChild<QPushButton*>(QStringLiteral("applyPerGameSettingsButton")),
    Qt::LeftButton);
  const auto* validation =
    dialog.findChild<QLabel*>(QStringLiteral("perGameSettingsValidationLabel"));
  QVERIFY(validation->isVisible());
  QCOMPARE(validation->text(), QStringLiteral("Synthetic settings write failure"));
}

void PerGameSettingsDialogTest::mainWindowGatesThePerGameSession()
{
  using namespace genplusgx;
  ui::MainWindow window;
  window.show();
  auto* action = window.findChild<QAction*>(QStringLiteral("perGameSettingsAction"));
  QVERIFY(action != nullptr);
  QVERIFY(!action->isEnabled());

  window.setGameLoaded("synthetic.bin");
  QVERIFY(!action->isEnabled());
  settings::GlobalGameSettings global;
  global.inputProfile = "Default";
  std::vector<settings::PerGameSettings> applied;
  window.setPerGameSettingsSink([&applied](const auto& configuration) {
    applied.push_back(configuration);
    return PersistenceStatus{};
  });
  window.setPerGameSettingsSession({}, global);
  QVERIFY(action->isEnabled());
  action->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<ui::PerGameSettingsDialog*>(
    QStringLiteral("perGameSettingsDialog"));
  QVERIFY(dialog != nullptr && dialog->isVisible());
  dialog->findChild<QCheckBox*>(QStringLiteral("overrideVideoCheckBox"))
    ->setChecked(true);
  QTest::mouseClick(
    dialog->findChild<QPushButton*>(QStringLiteral("applyPerGameSettingsButton")),
    Qt::LeftButton);
  QCOMPARE(applied.size(), 1U);
  QVERIFY(applied.back().video.has_value());

  window.setGameLoading("replacement.bin");
  QApplication::processEvents();
  QVERIFY(!action->isEnabled());
  QVERIFY(window.findChild<ui::PerGameSettingsDialog*>(
            QStringLiteral("perGameSettingsDialog")) == nullptr);
  window.setNoGameLoaded();
  QVERIFY(!action->isEnabled());
}

QTEST_MAIN(PerGameSettingsDialogTest)

#include "per_game_settings_dialog_test.moc"
