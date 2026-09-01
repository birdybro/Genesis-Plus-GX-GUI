#include "genplusgx/emulation_worker.h"
#include "genplusgx/state_storage_service.h"
#include "genplusgx/ui/main_window.h"

#include "synthetic_rom.h"

#include <QAction>
#include <QByteArray>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <tuple>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::optional<genplusgx::EmulationEvent> waitForWorkerOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

std::optional<genplusgx::StateStorageEvent> waitForStorageOperation(
  genplusgx::StateStorageService& storage,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = storage.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

std::vector<std::uint8_t> fakePng()
{
  const auto bytes = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  return {
    reinterpret_cast<const std::uint8_t*>(bytes.constData()),
    reinterpret_cast<const std::uint8_t*>(bytes.constData()) +
      static_cast<std::size_t>(bytes.size())};
}

std::array<genplusgx::ui::StateSlotView, 10> viewsFor(
  const genplusgx::StateSlotSummaries& summaries)
{
  std::array<genplusgx::ui::StateSlotView, 10> views{};
  for (std::size_t index = 0U; index < summaries.size(); ++index) {
    views[index].slot = summaries[index].slot;
    views[index].schemaVersion = summaries[index].metadata.schemaVersion;
    views[index].timestamp = summaries[index].metadata.timestamp;
    views[index].emulatedFrameNumber =
      summaries[index].metadata.emulatedFrameNumber;
    views[index].payloadBytes = summaries[index].metadata.payloadBytes;
    views[index].name = summaries[index].metadata.name;
    views[index].thumbnailPng = summaries[index].metadata.thumbnailPng;
    views[index].detail = summaries[index].message;
    switch (summaries[index].availability) {
      case genplusgx::StateSlotAvailability::empty:
        views[index].state = genplusgx::ui::StateSlotViewState::empty;
        break;
      case genplusgx::StateSlotAvailability::available:
        views[index].state = genplusgx::ui::StateSlotViewState::available;
        break;
      case genplusgx::StateSlotAvailability::invalid:
        views[index].state = genplusgx::ui::StateSlotViewState::invalid;
        break;
    }
  }
  return views;
}

class SaveStateWorkflowTest final : public QObject {
  Q_OBJECT

private slots:
  void saveRestoreDeleteAndRejectWrongGame();
};

void SaveStateWorkflowTest::saveRestoreDeleteAndRejectWrongGame()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const genplusgx::ApplicationPaths paths{
    std::filesystem::path{temporary.path().toStdString()} / "app"};
  genplusgx::test::TemporaryFixture first{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  auto secondBytes = genplusgx::test::makeGenesisRamMarkerRom();
  secondBytes.back() ^= 0x01U;
  genplusgx::test::TemporaryFixture second{std::move(secondBytes), ".md"};

  genplusgx::EmulationWorker worker;
  genplusgx::StateStorageService storage{paths};
  QVERIFY(worker.start());
  QVERIFY(storage.start());
  QVERIFY(worker.waitForEvent(2s).has_value());
  QVERIFY(storage.waitForEvent(2s).has_value());

  constexpr std::uint64_t firstGeneration = 1U;
  QVERIFY(worker.submit(genplusgx::EmulationCommand::load(100U, first.path())));
  const auto loaded = waitForWorkerOperation(worker, 100U);
  QVERIFY(loaded && loaded->succeeded());
  QVERIFY(loaded->hardware != 0U);

  genplusgx::ui::MainWindow window;
  window.setGameLoaded(first.path());
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::activate(
    200U, firstGeneration, first.path(), loaded->hardware)));
  const auto activated = waitForStorageOperation(storage, 200U);
  QVERIFY(activated && activated->succeeded());
  window.setStateSlotViews(viewsFor(activated->slotSummaries));
  window.setStateSessionReady(true);

  std::vector<genplusgx::ui::StateUiRequest> requests;
  window.setStateOperationSink([&requests](auto request) {
    requests.push_back(std::move(request));
  });

  QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::frameAdvance, 101U)));
  const auto firstFrame = waitForWorkerOperation(worker, 101U);
  QVERIFY(firstFrame && firstFrame->succeeded());

  auto* saveAction = window.findChild<QAction*>(QStringLiteral("saveStateAction"));
  auto* loadAction = window.findChild<QAction*>(QStringLiteral("loadStateAction"));
  auto* deleteAction = window.findChild<QAction*>(QStringLiteral("deleteStateAction"));
  QVERIFY(saveAction->isEnabled());
  QVERIFY(!loadAction->isEnabled());
  saveAction->trigger();
  QCOMPARE(requests.size(), std::size_t{1});
  QVERIFY(requests.back().operation == genplusgx::ui::StateUiOperation::save);

  QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::captureState, 300U)));
  auto captured = waitForWorkerOperation(worker, 300U);
  QVERIFY(captured && captured->succeeded() && !captured->rawState.empty());
  const auto savedRawState = captured->rawState;
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::save(
    300U,
    firstGeneration,
    0U,
    captured->frameNumber,
    std::move(captured->rawState),
    "First marker",
    fakePng())));
  const auto stored = waitForStorageOperation(storage, 300U);
  QVERIFY(stored && stored->succeeded());
  QCOMPARE(stored->slotSummaries[0].metadata.emulatedFrameNumber, 1U);
  QCOMPARE(stored->slotSummaries[0].metadata.name, std::string{"First marker"});
  QCOMPARE(stored->slotSummaries[0].metadata.thumbnailPng, fakePng());
  window.setStateSlotViews(viewsFor(stored->slotSummaries));
  window.setStateOperationBusy(false);
  window.showStateOperationSuccess(genplusgx::ui::StateUiOperation::save, 0U);
  QVERIFY(loadAction->isEnabled());
  QVERIFY(deleteAction->isEnabled());

  const auto exportedPath = paths.statesDirectory() / "workflow-export.gpgxstate";
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::file(
    genplusgx::StateStorageCommandType::exportSlot,
    320U,
    firstGeneration,
    0U,
    exportedPath)));
  const auto exported = waitForStorageOperation(storage, 320U);
  QVERIFY(exported && exported->succeeded() &&
    exported->type == genplusgx::StateStorageEventType::slotExported);
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::rename(
    321U, firstGeneration, 0U, "Renamed marker")));
  const auto renamed = waitForStorageOperation(storage, 321U);
  QVERIFY(renamed && renamed->succeeded() &&
    renamed->slotSummaries[0].metadata.name == "Renamed marker");
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::file(
    genplusgx::StateStorageCommandType::importSlot,
    322U,
    firstGeneration,
    1U,
    exportedPath)));
  const auto imported = waitForStorageOperation(storage, 322U);
  QVERIFY(imported && imported->succeeded() &&
    imported->slotSummaries[1].metadata.name == "First marker" &&
    imported->slotSummaries[1].metadata.thumbnailPng == fakePng());
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::simple(
    genplusgx::StateStorageCommandType::loadSlot,
    323U,
    firstGeneration,
    1U)));
  const auto importedPayload = waitForStorageOperation(storage, 323U);
  QVERIFY(importedPayload && importedPayload->succeeded());
  QCOMPARE(importedPayload->rawPayload, savedRawState);

  for (std::uint64_t operation = 301U; operation <= 303U; ++operation) {
    QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
      genplusgx::EmulationCommandType::frameAdvance, operation)));
    const auto frame = waitForWorkerOperation(worker, operation);
    QVERIFY(frame && frame->succeeded());
  }
  QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::captureState, 304U)));
  const auto changed = waitForWorkerOperation(worker, 304U);
  QVERIFY(changed && changed->succeeded());
  QVERIFY(changed->rawState != savedRawState);

  loadAction->trigger();
  QCOMPARE(requests.size(), std::size_t{2});
  QVERIFY(requests.back().operation == genplusgx::ui::StateUiOperation::load);
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::simple(
    genplusgx::StateStorageCommandType::loadSlot,
    305U,
    firstGeneration,
    0U)));
  const auto stateToRestore = waitForStorageOperation(storage, 305U);
  QVERIFY(stateToRestore && stateToRestore->succeeded());
  QCOMPARE(stateToRestore->rawPayload, savedRawState);
  QVERIFY(worker.submit(genplusgx::EmulationCommand::restore(
    305U, stateToRestore->rawPayload)));
  const auto restored = waitForWorkerOperation(worker, 305U);
  QVERIFY(restored && restored->succeeded());
  window.setStateOperationBusy(false);
  window.showStateOperationSuccess(genplusgx::ui::StateUiOperation::load, 0U);

  QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::captureState, 306U)));
  const auto recaptured = waitForWorkerOperation(worker, 306U);
  QVERIFY(recaptured && recaptured->succeeded());
  QCOMPARE(recaptured->rawState, savedRawState);

  const auto firstIdentity = genplusgx::identifyGame(first.path());
  const auto secondIdentity = genplusgx::identifyGame(second.path());
  QVERIFY(firstIdentity.status && secondIdentity.status);
  genplusgx::SaveStateManager manager{paths};
  const auto encoded = genplusgx::readFileBounded(
    manager.statePath(firstIdentity.identity, 0U),
    genplusgx::SaveStateManager::maximumFileBytes);
  QVERIFY(encoded.status && encoded.exists);

  deleteAction->trigger();
  QCOMPARE(requests.size(), std::size_t{3});
  QVERIFY(requests.back().operation == genplusgx::ui::StateUiOperation::remove);
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::simple(
    genplusgx::StateStorageCommandType::deleteSlot,
    307U,
    firstGeneration,
    0U)));
  const auto deleted = waitForStorageOperation(storage, 307U);
  QVERIFY(deleted && deleted->succeeded());
  window.setStateSlotViews(viewsFor(deleted->slotSummaries));
  window.setStateOperationBusy(false);
  QVERIFY(!loadAction->isEnabled());
  QVERIFY(!deleteAction->isEnabled());

  QVERIFY(genplusgx::writeFileAtomically(
    manager.statePath(secondIdentity.identity, 0U),
    encoded.data,
    encoded.data.size()));
  constexpr std::uint64_t secondGeneration = 2U;
  window.setGameLoaded(second.path());
  QVERIFY(storage.submit(genplusgx::StateStorageCommand::activate(
    308U, secondGeneration, second.path(), loaded->hardware)));
  const auto secondActivated = waitForStorageOperation(storage, 308U);
  QVERIFY(secondActivated && secondActivated->succeeded());
  QCOMPARE(
    secondActivated->slotSummaries[0].availability,
    genplusgx::StateSlotAvailability::invalid);
  QVERIFY(secondActivated->slotSummaries[0].message.find("different game") !=
    std::string::npos);
  window.setStateSlotViews(viewsFor(secondActivated->slotSummaries));
  window.setStateSessionReady(true);
  QVERIFY(!loadAction->isEnabled());
  QVERIFY(deleteAction->isEnabled());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("stateSlotAction0"))->text()
    .contains(QStringLiteral("Invalid")));

  QVERIFY(worker.stop());
  QVERIFY(storage.stop());
}

} // namespace

QTEST_MAIN(SaveStateWorkflowTest)

#include "save_state_workflow_test.moc"
