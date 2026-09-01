#include "genplusgx/capture/recording_service.h"

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

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

std::uint32_t littleEndian32(const QByteArray& bytes, qsizetype offset)
{
  return static_cast<std::uint32_t>(
    static_cast<std::uint8_t>(bytes[offset]) |
    (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 8U) |
    (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 16U) |
    (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 3])) << 24U));
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication application{argc, argv};
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory was unavailable")) {
    return 1;
  }
  const auto output = temporaryPath(temporary) / "recordings";
  genplusgx::capture::RecordingService service{2U, 8U};
  if (!check(service.begin({}).error ==
          genplusgx::capture::RecordingError::invalidRequest &&
        service.start() &&
        service.start().error ==
          genplusgx::capture::RecordingError::alreadyRunning,
        "Recording lifecycle validation failed")) {
    return 2;
  }
  const auto serviceStarted = service.waitForEvent(std::chrono::seconds{2});
  if (!check(serviceStarted && serviceStarted->type ==
          genplusgx::capture::RecordingEventType::serviceStarted,
        "Recording service did not announce startup")) {
    return 3;
  }
  auto invalidRequest = genplusgx::capture::RecordingRequest{};
  invalidRequest.operationId = 1U;
  invalidRequest.baseDirectory = "relative";
  if (!check(service.begin(invalidRequest).error ==
          genplusgx::capture::RecordingError::invalidRequest,
        "A relative recording path was accepted")) {
    return 4;
  }
  invalidRequest.baseDirectory = output;
  invalidRequest.nominalFramesPerSecond =
    std::numeric_limits<double>::quiet_NaN();
  if (!check(service.begin(invalidRequest).error ==
          genplusgx::capture::RecordingError::invalidRequest,
        "A non-finite recording frame rate was accepted")) {
    return 4;
  }
  const auto blockedDirectory = temporaryPath(temporary) / "ordinary-file";
  QFile blockedFile{pathToQString(blockedDirectory)};
  if (!check(blockedFile.open(QIODevice::WriteOnly) &&
        blockedFile.write("not a directory") > 0, "Blocker file setup failed")) {
    return 4;
  }
  blockedFile.close();
  invalidRequest.nominalFramesPerSecond = 60.0;
  invalidRequest.operationId = 9U;
  invalidRequest.baseDirectory = blockedDirectory;
  if (!check(service.begin(invalidRequest),
        "Asynchronous invalid-directory request was not queued")) {
    return 4;
  }
  const auto directoryFailure = service.waitForEvent(std::chrono::seconds{2});
  if (!check(directoryFailure && directoryFailure->type ==
          genplusgx::capture::RecordingEventType::recordingFailed &&
        directoryFailure->status.error ==
          genplusgx::capture::RecordingError::directoryCreationFailed,
        "An invalid recording directory did not fail asynchronously")) {
    return 4;
  }
  const auto timestamp = std::chrono::system_clock::time_point{
    std::chrono::milliseconds{1'700'000'000'123LL}};
  if (!check(service.begin({
        .operationId = 2U,
        .baseDirectory = output,
        .gameTitle = "Sonic: The / Hedgehog?",
        .gameId = std::string(64U, 'a'),
        .audioSampleRate = 48'000U,
        .nominalFramesPerSecond = 59.9227,
        .timestamp = timestamp,
      }), "A valid recording request was rejected")) {
    return 5;
  }
  const auto started = service.waitForEvent(std::chrono::seconds{3});
  if (!check(started && started->type ==
          genplusgx::capture::RecordingEventType::recordingStarted &&
        started->operationId == 2U && service.active(),
        "Recording did not become active")) {
    return 6;
  }

  const genplusgx::CoreVideoFrameInfo firstFrame{
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = 2U,
    .height = 2U,
    .frameNumber = 41U,
  };
  constexpr std::array<std::uint16_t, 4> firstPixels{
    0xf800U, 0x07e0U, 0x001fU, 0xffffU};
  constexpr std::array<genplusgx::StereoAudioFrame, 2> firstAudio{{
    {.left = -32'768, .right = 32'767},
    {.left = 1'234, .right = -4'321},
  }};
  const genplusgx::CoreAudioBatchInfo firstAudioInfo{
    .sampleRate = 48'000U,
    .channels = 2U,
    .frameCount = firstAudio.size(),
    .emulatedFrameNumber = 41U,
  };
  if (!check(service.submitFrame(
        firstFrame, firstPixels, firstAudioInfo, firstAudio),
        "The first native A/V frame was rejected")) {
    return 7;
  }
  const genplusgx::CoreVideoFrameInfo secondFrame{
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = 3U,
    .height = 1U,
    .frameNumber = 42U,
    .viewportChanged = true,
    .interlaced = true,
    .oddField = true,
  };
  constexpr std::array<std::uint16_t, 3> secondPixels{
    0x0000U, 0x8410U, 0xffffU};
  constexpr std::array<genplusgx::StereoAudioFrame, 2> secondAudio{{
    {.left = 10, .right = 20},
    {.left = 30, .right = 40},
  }};
  auto secondAudioInfo = firstAudioInfo;
  secondAudioInfo.emulatedFrameNumber = 42U;
  if (!check(service.submitFrame(
        secondFrame, secondPixels, secondAudioInfo, secondAudio),
        "A dynamic-geometry recording frame was rejected")) {
    return 8;
  }
  auto invalidFrame = secondFrame;
  invalidFrame.width = 0U;
  auto wrongRate = secondAudioInfo;
  wrongRate.sampleRate = 44'100U;
  if (!check(!service.submitFrame(
        invalidFrame, secondPixels, secondAudioInfo, secondAudio),
        "An invalid recording frame was accepted") ||
      !check(!service.submitFrame(
        secondFrame, secondPixels, wrongRate, secondAudio),
        "Audio with a session-mismatched sample rate was accepted") ||
      !check(service.end(3U), "The active recording could not be stopped")) {
    return 9;
  }
  const auto finished = service.waitForEvent(std::chrono::seconds{10});
  if (!check(finished && finished->type ==
          genplusgx::capture::RecordingEventType::recordingFinished &&
        finished->operationId == 3U && finished->status &&
        finished->metrics.writtenFrames == 2U &&
        finished->metrics.writtenAudioFrames == 4U &&
        finished->metrics.droppedFrames == 2U && !service.active(),
        "Recording completion metrics were incorrect")) {
    return 10;
  }
  const auto session = finished->path;
  if (!check(session.extension() == ".gpgx-recording" &&
        std::filesystem::is_directory(session) &&
        std::filesystem::is_regular_file(session / "manifest.json") &&
        std::filesystem::is_regular_file(session / "frames.jsonl") &&
        std::filesystem::is_regular_file(session / "audio.wav"),
        "Completed recording structure was not committed")) {
    return 11;
  }
  const auto firstPng = session / "frames" / "frame-000000000.png";
  const auto secondPng = session / "frames" / "frame-000000001.png";
  const QImage firstImage{pathToQString(firstPng)};
  const QImage secondImage{pathToQString(secondPng)};
  if (!check(!firstImage.isNull() && firstImage.size() == QSize{2, 2} &&
        firstImage.pixelColor(0, 0) == QColor{255, 0, 0} &&
        firstImage.pixelColor(1, 1) == QColor{255, 255, 255} &&
        !secondImage.isNull() && secondImage.size() == QSize{3, 1},
        "Native RGB565 frames did not survive lossless PNG capture")) {
    return 12;
  }
  QFile manifestFile{pathToQString(session / "manifest.json")};
  QFile frameLogFile{pathToQString(session / "frames.jsonl")};
  QFile audioFile{pathToQString(session / "audio.wav")};
  if (!check(manifestFile.open(QIODevice::ReadOnly) &&
        frameLogFile.open(QIODevice::ReadOnly) &&
        audioFile.open(QIODevice::ReadOnly),
        "Recording metadata streams could not be read")) {
    return 13;
  }
  const auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
  const auto frameLines = frameLogFile.readAll().split('\n');
  const auto wav = audioFile.readAll();
  if (!check(manifest.value(QStringLiteral("schemaVersion")).toInt() == 1 &&
        manifest.value(QStringLiteral("complete")).toBool() &&
        manifest.value(QStringLiteral("capturedFrames")).toInteger() == 2 &&
        manifest.value(QStringLiteral("droppedFrames")).toInteger() == 2 &&
        manifest.value(QStringLiteral("gameId")).toString().size() == 64 &&
        frameLines.size() == 3 &&
        QJsonDocument::fromJson(frameLines.front()).object()
          .value(QStringLiteral("emulatedFrame")).toInteger() == 41 &&
        wav.size() == 60 && wav.first(4) == "RIFF" &&
        wav.sliced(8, 8) == "WAVEfmt " && wav.sliced(36, 4) == "data" &&
        littleEndian32(wav, 24) == 48'000U &&
        littleEndian32(wav, 40) == 16U,
        "Recording manifest, index, or WAV header was incorrect")) {
    return 14;
  }
  if (!check(service.end(4U).error ==
          genplusgx::capture::RecordingError::busy &&
        service.stop(), "Idle-stop validation or service shutdown failed")) {
    return 15;
  }
  const auto stopped = service.pollEvent();
  if (!check(stopped && stopped->type ==
          genplusgx::capture::RecordingEventType::serviceStopped &&
        service.start() && service.stop(),
        "Recording service did not report stop or restart cleanly")) {
    return 16;
  }

  genplusgx::capture::RecordingService bounded{1U, 8U};
  if (!check(bounded.start() &&
        bounded.waitForEvent(std::chrono::seconds{2}).has_value() &&
        bounded.begin({
          .operationId = 20U,
          .baseDirectory = output,
          .gameTitle = "Sonic: The / Hedgehog?",
          .gameId = std::string(64U, 'b'),
          .audioSampleRate = 48'000U,
          .nominalFramesPerSecond = 60.0,
          .timestamp = timestamp,
        }), "Bounded recording setup failed")) {
    return 17;
  }
  const auto boundedStarted = bounded.waitForEvent(std::chrono::seconds{3});
  if (!check(boundedStarted && boundedStarted->type ==
          genplusgx::capture::RecordingEventType::recordingStarted,
        "Bounded recording did not start")) {
    return 18;
  }
  genplusgx::CoreVideoFrameInfo largeFrame{
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = 720U,
    .height = 576U,
    .frameNumber = 100U,
  };
  std::vector<std::uint16_t> largePixels(largeFrame.pixelCount());
  for (std::size_t index = 0U; index < largePixels.size(); ++index) {
    largePixels[index] = static_cast<std::uint16_t>(
      (index * 2'653U) & 0xffffU);
  }
  constexpr std::size_t attempts = 32U;
  std::size_t accepted = 0U;
  for (std::size_t attempt = 0U; attempt < attempts; ++attempt) {
    largeFrame.frameNumber = 100U + static_cast<std::uint64_t>(attempt);
    accepted += bounded.submitFrame(
      largeFrame, largePixels, genplusgx::CoreAudioBatchInfo{}, {}) ? 1U : 0U;
  }
  if (!check(accepted > 0U && accepted < attempts && bounded.end(21U),
        "The full recording queue did not reject excess frames")) {
    return 19;
  }
  const auto boundedFinished = bounded.waitForEvent(std::chrono::seconds{10});
  if (!check(boundedFinished && boundedFinished->type ==
          genplusgx::capture::RecordingEventType::recordingFinished &&
        boundedFinished->metrics.queueCapacity == 1U &&
        boundedFinished->metrics.peakQueuedFrames <= 1U &&
        boundedFinished->metrics.acceptedFrames == accepted &&
        boundedFinished->metrics.writtenFrames == accepted &&
        boundedFinished->metrics.droppedFrames == attempts - accepted &&
        boundedFinished->path != session &&
        boundedFinished->path.stem().string().ends_with("-1") &&
        bounded.stop(),
        "Recording queue metrics or collision naming were incorrect")) {
    return 20;
  }

  genplusgx::capture::RecordingService shutdownCapture{2U, 8U};
  if (!check(shutdownCapture.start() &&
        shutdownCapture.waitForEvent(std::chrono::seconds{2}).has_value() &&
        shutdownCapture.begin({
          .operationId = 30U,
          .baseDirectory = output,
          .gameTitle = "Shutdown capture",
          .gameId = std::string(64U, 'd'),
          .audioSampleRate = 48'000U,
          .nominalFramesPerSecond = 60.0,
          .timestamp = timestamp,
        }), "Shutdown recording setup failed")) {
    return 21;
  }
  const auto shutdownStarted =
    shutdownCapture.waitForEvent(std::chrono::seconds{3});
  if (!check(shutdownStarted && shutdownStarted->type ==
          genplusgx::capture::RecordingEventType::recordingStarted &&
        shutdownCapture.submitFrame(
          firstFrame, firstPixels, firstAudioInfo, firstAudio) &&
        shutdownCapture.stop(),
        "Active recording did not drain during service shutdown")) {
    return 22;
  }
  std::optional<genplusgx::capture::RecordingEvent> shutdownFinished;
  bool shutdownStopped = false;
  while (auto event = shutdownCapture.pollEvent()) {
    if (event->type ==
        genplusgx::capture::RecordingEventType::recordingFinished) {
      shutdownFinished = std::move(event);
    } else if (event->type ==
               genplusgx::capture::RecordingEventType::serviceStopped) {
      shutdownStopped = true;
    }
  }
  if (!check(shutdownFinished && shutdownStopped &&
        shutdownFinished->metrics.writtenFrames == 1U &&
        shutdownFinished->metrics.droppedFrames == 0U &&
        std::filesystem::is_regular_file(
          shutdownFinished->path / "manifest.json"),
        "Shutdown did not publish a complete drained recording")) {
    return 23;
  }

  constexpr std::size_t startStopAttempts = 24U;
  for (std::size_t attempt = 0U; attempt < startStopAttempts; ++attempt) {
    if (!check(shutdownCapture.start() &&
          shutdownCapture.waitForEvent(std::chrono::seconds{2}).has_value(),
          "Recording service could not restart for start/stop stress")) {
      return 24;
    }
    auto raceTimestamp = timestamp + std::chrono::milliseconds{
      static_cast<std::int64_t>(attempt + 1U)};
    if (!check(shutdownCapture.begin({
          .operationId = 40U + static_cast<std::uint64_t>(attempt),
          .baseDirectory = output,
          .gameTitle = "Start stop race",
          .gameId = std::string(64U, 'e'),
          .audioSampleRate = 48'000U,
          .nominalFramesPerSecond = 60.0,
          .timestamp = raceTimestamp,
        }) && shutdownCapture.stop(),
        "A recording start/stop race failed clean shutdown")) {
      return 25;
    }
  }
  std::error_code scanError;
  for (std::filesystem::recursive_directory_iterator iterator{output, scanError}, end;
       !scanError && iterator != end; iterator.increment(scanError)) {
    if (iterator->path().extension() == ".partial") {
      std::cerr << "Start/stop race left partial output: "
                << iterator->path() << '\n';
      return 26;
    }
  }
  if (!check(!scanError, "Recording stress output could not be inspected")) {
    return 27;
  }
  return 0;
}
