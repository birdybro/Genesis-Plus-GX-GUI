#include "genplusgx/capture/recording_service.h"
#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::filesystem::path temporaryPath(const QTemporaryDir& directory)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{directory.path().toStdWString()};
#else
  return std::filesystem::path{directory.path().toUtf8().constData()};
#endif
}

QString pathToQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

std::optional<genplusgx::EmulationEvent> waitForWorkerOperation(
  genplusgx::EmulationWorker& worker, std::uint64_t operationId)
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

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker, genplusgx::EmulationCommand command)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  const auto event = waitForWorkerOperation(worker, operationId);
  return event && event->succeeded();
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary recording root was unavailable")) {
    return 1;
  }
  const auto output = temporaryPath(temporary) / "recordings";
  auto recorder = std::make_shared<genplusgx::capture::RecordingService>();
  if (!check(recorder->start(), "Recording service start failed")) {
    return 2;
  }
  const auto recorderStarted = recorder->waitForEvent(2s);
  if (!check(recorderStarted && recorderStarted->type ==
          genplusgx::capture::RecordingEventType::serviceStarted,
        "Recording service start event was missing")) {
    return 3;
  }
  genplusgx::EmulationWorker worker{64U, 64U, 48'000, {}, {}, {}, recorder};
  if (!check(worker.start(), "Emulation worker start failed") ||
      !check(worker.waitForEvent(2s).has_value(),
        "Emulation worker start event was missing") ||
      !check(submitAndSucceed(
        worker, genplusgx::EmulationCommand::load(1U, game.path())),
        "Synthetic game load failed")) {
    return 4;
  }
  if (!check(recorder->begin({
        .operationId = 10U,
        .baseDirectory = output,
        .gameTitle = "Synthetic core recording",
        .gameId = std::string(64U, 'c'),
        .audioSampleRate = 48'000U,
        .nominalFramesPerSecond = 59.9227,
        .timestamp = std::chrono::system_clock::time_point{
          std::chrono::milliseconds{1'700'000'100'000LL}},
      }), "Core recording request failed")) {
    return 5;
  }
  const auto recordingStarted = recorder->waitForEvent(3s);
  if (!check(recordingStarted && recordingStarted->type ==
          genplusgx::capture::RecordingEventType::recordingStarted,
        "Core recording did not become active")) {
    return 6;
  }
  for (std::uint64_t operation = 2U; operation < 6U; ++operation) {
    if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, operation)),
          "A recorded frame advance failed")) {
      return 7;
    }
  }
  if (!check(recorder->end(11U), "Core recording stop failed")) {
    return 8;
  }
  const auto finished = recorder->waitForEvent(10s);
  if (!check(finished && finished->type ==
          genplusgx::capture::RecordingEventType::recordingFinished &&
        finished->metrics.acceptedFrames == 4U &&
        finished->metrics.writtenFrames == 4U &&
        finished->metrics.droppedFrames == 0U &&
        finished->metrics.writtenAudioFrames > 0U,
        "Core recording did not preserve all native A/V frames")) {
    return 9;
  }
  const auto session = finished->path;
  QFile manifestFile{pathToQString(session / "manifest.json")};
  QFile frameLogFile{pathToQString(session / "frames.jsonl")};
  QFile audioFile{pathToQString(session / "audio.wav")};
  const QImage firstFrame{pathToQString(
    session / "frames" / "frame-000000000.png")};
  if (!check(manifestFile.open(QIODevice::ReadOnly) &&
        frameLogFile.open(QIODevice::ReadOnly) &&
        audioFile.open(QIODevice::ReadOnly) && !firstFrame.isNull(),
        "Core recording output could not be opened")) {
    return 10;
  }
  const auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
  const auto frameLines = frameLogFile.readAll().split('\n');
  if (!check(manifest.value(QStringLiteral("complete")).toBool() &&
        manifest.value(QStringLiteral("capturedFrames")).toInteger() == 4 &&
        manifest.value(QStringLiteral("audioFrames")).toInteger() > 0 &&
        frameLines.size() == 5 && firstFrame.width() > 0 &&
        firstFrame.height() > 0 && audioFile.size() > 44,
        "Core recording manifest, frames, or audio were incomplete")) {
    return 11;
  }
  if (!check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::unloadGame, 20U)),
        "Recorded game unload failed") ||
      !check(worker.stop(), "Recorded emulation worker stop failed") ||
      !check(recorder->stop(), "Recording service stop failed")) {
    return 12;
  }
  return 0;
}
