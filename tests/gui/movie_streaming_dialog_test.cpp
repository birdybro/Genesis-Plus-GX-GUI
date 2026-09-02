#include "genplusgx/ui/input_movie_editor_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/streaming_dialog.h"
#include "synthetic_rom.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTest>
#include <QTemporaryDir>
#include <QTextEdit>

#include <memory>
#include <optional>

namespace {

genplusgx::movies::InputMovie movieFixture()
{
  genplusgx::movies::InputMovie movie;
  movie.descriptor = {
    .gameSha256 = std::string(64U, 'a'),
    .settingsSha256 = std::string(64U, 'b'),
    .coreVersion = "synthetic-test-core",
  };
  movie.metadata.author = "Original author";
  movie.initialState = {1U, 2U, 3U, 4U};
  movie.frames.resize(4U);
  movie.frames[0].players[0].connected = true;
  movie.frames[0].players[0].buttons =
    genplusgx::buttonMask(genplusgx::InputButton::right);
  return movie;
}

class MovieDialogService final : public genplusgx::ui::DialogService {
public:
  std::optional<std::filesystem::path> movieSave;
  QString lastError;

  std::optional<std::filesystem::path> chooseGame(
    QWidget*, const std::filesystem::path&) override
  {
    return std::nullopt;
  }

  std::optional<std::filesystem::path> chooseMovieSave(
    QWidget*, const std::filesystem::path&) override
  {
    return movieSave;
  }

  void showError(QWidget*, const QString&, const QString& message) override
  {
    lastError = message;
  }
};

class MovieStreamingDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void tasEditorMutatesTimelineAndAccountsRerecords();
  void streamingDialogExposesBoundedLoopbackControls();
  void mainWindowExposesMovieAndStreamingActions();
  void mainWindowGatesMovieTransitionsAndGameChanges();
};

void MovieStreamingDialogTest::tasEditorMutatesTimelineAndAccountsRerecords()
{
  genplusgx::ui::InputMovieEditorDialog dialog{movieFixture()};
  auto* timeline = dialog.findChild<QTableView*>(
    QStringLiteral("movieTimeline"));
  auto* frame = dialog.findChild<QSpinBox*>(
    QStringLiteral("movieFrameNumber"));
  auto* player = dialog.findChild<QComboBox*>(QStringLiteral("moviePlayer"));
  auto* connected = dialog.findChild<QCheckBox*>(
    QStringLiteral("moviePlayerConnected"));
  auto* buttonA = dialog.findChild<QCheckBox*>(QStringLiteral("movieButtonA"));
  auto* apply = dialog.findChild<QPushButton*>(
    QStringLiteral("movieApplyFrame"));
  auto* duplicate = dialog.findChild<QPushButton*>(
    QStringLiteral("movieDuplicateFrame"));
  auto* remove = dialog.findChild<QPushButton*>(
    QStringLiteral("movieDeleteFrame"));
  auto* truncate = dialog.findChild<QPushButton*>(
    QStringLiteral("movieTruncateAfter"));
  QVERIFY(timeline != nullptr);
  QVERIFY(frame != nullptr);
  QVERIFY(player != nullptr);
  QVERIFY(connected != nullptr);
  QVERIFY(buttonA != nullptr);
  QVERIFY(apply != nullptr);
  QVERIFY(duplicate != nullptr);
  QVERIFY(remove != nullptr);
  QVERIFY(truncate != nullptr);
  QCOMPARE(timeline->model()->rowCount(), 4);
  QCOMPARE(player->count(), 8);
  QVERIFY(!timeline->accessibleName().isEmpty());
  QVERIFY(!frame->accessibleName().isEmpty());
  QVERIFY(!player->accessibleName().isEmpty());

  frame->setValue(1);
  connected->setChecked(true);
  buttonA->setChecked(true);
  QTest::mouseClick(apply, Qt::LeftButton);
  QVERIFY(genplusgx::hasButton(
    dialog.movie().frames[1].players[0].buttons,
    genplusgx::InputButton::a));
  QCOMPARE(dialog.movie().metadata.rerecordCount, 1U);

  QTest::mouseClick(duplicate, Qt::LeftButton);
  QCOMPARE(dialog.movie().frames.size(), 5U);
  QCOMPARE(dialog.movie().metadata.rerecordCount, 2U);
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(dialog.movie().frames.size(), 4U);
  QCOMPARE(dialog.movie().metadata.rerecordCount, 3U);
  frame->setValue(1);
  QTest::mouseClick(truncate, Qt::LeftButton);
  QCOMPARE(dialog.movie().frames.size(), 2U);
  QCOMPARE(dialog.movie().metadata.rerecordCount, 4U);
  QVERIFY(dialog.movie().valid());
  auto* author = dialog.findChild<QLineEdit*>(QStringLiteral("movieAuthor"));
  auto* notes = dialog.findChild<QTextEdit*>(QStringLiteral("movieNotes"));
  auto* dialogButtons = dialog.findChild<QDialogButtonBox*>(
    QStringLiteral("movieEditorButtons"));
  QVERIFY(author != nullptr);
  QVERIFY(notes != nullptr);
  QVERIFY(dialogButtons != nullptr);
  author->setText(QString(128, QChar{u'é'}));
  QString longNotes;
  longNotes.reserve(4'000);
  for (int index = 0; index < 2'000; ++index) {
    longNotes += QString::fromUtf8("😀");
  }
  notes->setPlainText(longNotes);
  QTest::mouseClick(dialogButtons->button(QDialogButtonBox::Save), Qt::LeftButton);
  QVERIFY(dialog.movie().metadata.author.size() <= 128U);
  QVERIFY(dialog.movie().metadata.notes.size() <= 4'096U);
  QVERIFY(dialog.movie().valid());
}

void MovieStreamingDialogTest::streamingDialogExposesBoundedLoopbackControls()
{
  genplusgx::ui::StreamingDialog dialog;
  bool requestedStart = false;
  bool requestedStop = false;
  genplusgx::capture::StreamingConfiguration requested;
  dialog.setRequestSink(
    [&](bool start, genplusgx::capture::StreamingConfiguration configuration) {
      requestedStart = requestedStart || start;
      requestedStop = requestedStop || !start;
      requested = configuration;
      return genplusgx::capture::StreamingStatus{};
    });
  auto* port = dialog.findChild<QSpinBox*>(QStringLiteral("streamingPort"));
  auto* clients = dialog.findChild<QSpinBox*>(
    QStringLiteral("streamingMaximumClients"));
  auto* toggle = dialog.findChild<QPushButton*>(
    QStringLiteral("streamingToggle"));
  auto* state = dialog.findChild<QLabel*>(QStringLiteral("streamingState"));
  auto* metrics = dialog.findChild<QLabel*>(QStringLiteral("streamingMetrics"));
  QVERIFY(port != nullptr);
  QVERIFY(clients != nullptr);
  QVERIFY(toggle != nullptr);
  QVERIFY(state != nullptr);
  QVERIFY(metrics != nullptr);
  QVERIFY(!port->accessibleName().isEmpty());
  QVERIFY(!clients->accessibleName().isEmpty());
  QVERIFY(!toggle->accessibleDescription().isEmpty());

  port->setValue(45'678);
  clients->setValue(4);
  QTest::mouseClick(toggle, Qt::LeftButton);
  QVERIFY(requestedStart);
  QCOMPARE(requested.port, 45'678U);
  QCOMPARE(requested.maximumClients, 4U);

  dialog.setMetrics({
    .active = true,
    .port = 45'678U,
    .connectedClients = 2U,
    .queueDepth = 1U,
    .queueCapacity = 4U,
    .broadcastFrames = 120U,
    .droppedFrames = 2U,
    .bytesSent = 65'536U,
  });
  QVERIFY(!port->isEnabled());
  QVERIFY(!clients->isEnabled());
  QVERIFY(state->text().contains(QStringLiteral("127.0.0.1:45678")));
  QVERIFY(metrics->text().contains(QStringLiteral("Queue: 1/4")));
  QTest::mouseClick(toggle, Qt::LeftButton);
  QVERIFY(requestedStop);
}

void MovieStreamingDialogTest::mainWindowExposesMovieAndStreamingActions()
{
  genplusgx::ui::MainWindow window;
  const auto* record = window.findChild<QAction*>(
    QStringLiteral("movieRecordAction"));
  const auto* playback = window.findChild<QAction*>(
    QStringLiteral("moviePlaybackAction"));
  const auto* editor = window.findChild<QAction*>(
    QStringLiteral("movieEditorAction"));
  const auto* streaming = window.findChild<QAction*>(
    QStringLiteral("streamingAction"));
  QVERIFY(record != nullptr);
  QVERIFY(playback != nullptr);
  QVERIFY(editor != nullptr);
  QVERIFY(streaming != nullptr);
  QVERIFY(!record->isEnabled());
  QVERIFY(!playback->isEnabled());
  QVERIFY(!streaming->isEnabled());

  window.setStreamingSink(
    [](bool, genplusgx::capture::StreamingConfiguration) {
      return genplusgx::capture::StreamingStatus{};
    });
  QVERIFY(streaming->isEnabled());
  window.showStreaming();
  QVERIFY(window.findChild<genplusgx::ui::StreamingDialog*>(
    QStringLiteral("streamingDialog")) != nullptr);
}

void MovieStreamingDialogTest::mainWindowGatesMovieTransitionsAndGameChanges()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto root = genplusgx::ui::pathFromQString(temporary.path());
  const genplusgx::ApplicationPaths paths{root / "application-data"};
  QVERIFY(paths.initialize());
  auto dialogs = std::make_shared<MovieDialogService>();
  dialogs->movieSave = root / "capture.gpgx-movie";
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  window.setApplicationPaths(paths);
  const genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  const genplusgx::test::TemporaryFixture replacement{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  window.setGameLoaded(game.path());
  std::vector<genplusgx::ui::MovieUiOperation> operations;
  window.setMovieSink([&](genplusgx::ui::MovieUiRequest request) {
    operations.push_back(request.operation);
    return genplusgx::PersistenceStatus{};
  });
  auto* record = window.findChild<QAction*>(
    QStringLiteral("movieRecordAction"));
  auto* open = window.findChild<QAction*>(QStringLiteral("openGameAction"));
  auto* close = window.findChild<QAction*>(QStringLiteral("closeGameAction"));
  QVERIFY(record != nullptr);
  QVERIFY(open != nullptr);
  QVERIFY(close != nullptr);
  QVERIFY(record->isEnabled());

  record->trigger();
  QCOMPARE(operations.size(), std::size_t{1});
  QCOMPARE(operations.front(),
    genplusgx::ui::MovieUiOperation::startRecording);
  QVERIFY(!record->isEnabled());
  QVERIFY(!open->isEnabled());
  QVERIFY(!close->isEnabled());
  QVERIFY(!window.requestGameLoad(replacement.path()));
  QVERIFY(dialogs->lastError.contains(
    QStringLiteral("input movie operation")));
  dialogs->lastError.clear();
  window.setMovieState(genplusgx::ui::MovieUiState::recording,
    *dialogs->movieSave, 3U, 3U);
  QVERIFY(record->isEnabled());
  QVERIFY(record->text().contains(QStringLiteral("Stop")));
  QVERIFY(!open->isEnabled());
  QVERIFY(!close->isEnabled());
  QVERIFY(!window.requestGameLoad(replacement.path()));
  QVERIFY(dialogs->lastError.contains(
    QStringLiteral("Stop the active input movie")));

  record->trigger();
  QCOMPARE(operations.back(), genplusgx::ui::MovieUiOperation::stopRecording);
  QVERIFY(!record->isEnabled());
  window.showMovieError("Synthetic finalization failure");
  QVERIFY(record->isEnabled());
  QVERIFY(!open->isEnabled());
  window.setMovieState(genplusgx::ui::MovieUiState::idle,
    *dialogs->movieSave, 3U, 3U);
  QVERIFY(open->isEnabled());
  QVERIFY(close->isEnabled());

  auto* playback = window.findChild<QAction*>(
    QStringLiteral("moviePlaybackAction"));
  auto* netplay = window.findChild<QAction*>(QStringLiteral("netplayAction"));
  QVERIFY(playback != nullptr);
  QVERIFY(netplay != nullptr);
  window.setNetplaySessionState(
    genplusgx::netplay::NetplaySessionState::connected);
  QVERIFY(!record->isEnabled());
  QVERIFY(!playback->isEnabled());
  window.setNetplaySessionState(
    genplusgx::netplay::NetplaySessionState::disconnected);
  QVERIFY(record->isEnabled());
  QVERIFY(playback->isEnabled());

  genplusgx::achievements::Snapshot achievements;
  achievements.enabled = true;
  achievements.authenticated = true;
  achievements.gameLoaded = true;
  achievements.hardcore = true;
  window.setAchievementSnapshot(achievements);
  QVERIFY(!record->isEnabled());
  QVERIFY(!playback->isEnabled());
  achievements.hardcore = false;
  achievements.gameLoaded = false;
  window.setAchievementSnapshot(achievements);
  QVERIFY(record->isEnabled());
  QVERIFY(playback->isEnabled());
}

} // namespace

QTEST_MAIN(MovieStreamingDialogTest)
#include "movie_streaming_dialog_test.moc"
