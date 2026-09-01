#include "genplusgx/capture/recording_service.h"

#include "genplusgx/bounded_queue.h"
#include "genplusgx/persistence.h"
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace genplusgx::capture {
namespace {

constexpr std::size_t wavHeaderBytes = 44U;
constexpr std::uint32_t stereoChannels = 2U;
constexpr std::uint32_t pcmBitsPerSample = 16U;
constexpr std::size_t maximumFilenameAttempts = 1'000U;
constexpr std::uint64_t manifestReserveBytes = 64U * 1024U;

RecordingStatus failure(RecordingError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

QString pathToQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

std::filesystem::path pathFromQString(const QString& path)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().constData()};
#endif
}

void putLittleEndian16(QByteArray& bytes, std::size_t offset, std::uint16_t value)
{
  bytes[static_cast<qsizetype>(offset)] = static_cast<char>(value & 0xffU);
  bytes[static_cast<qsizetype>(offset + 1U)] =
    static_cast<char>((value >> 8U) & 0xffU);
}

void putLittleEndian32(QByteArray& bytes, std::size_t offset, std::uint32_t value)
{
  for (std::size_t byte = 0U; byte < 4U; ++byte) {
    bytes[static_cast<qsizetype>(offset + byte)] =
      static_cast<char>((value >> (byte * 8U)) & 0xffU);
  }
}

QByteArray wavHeader(std::uint32_t sampleRate, std::uint64_t audioFrames)
{
  const auto maximumDataBytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 36U;
  const auto requestedDataBytes = audioFrames * stereoChannels *
    (pcmBitsPerSample / 8U);
  const auto dataBytes = static_cast<std::uint32_t>(
    std::min(requestedDataBytes, maximumDataBytes));
  QByteArray header(static_cast<qsizetype>(wavHeaderBytes), '\0');
  std::copy_n("RIFF", 4, header.begin());
  putLittleEndian32(header, 4U, 36U + dataBytes);
  std::copy_n("WAVEfmt ", 8, header.begin() + 8);
  putLittleEndian32(header, 16U, 16U);
  putLittleEndian16(header, 20U, 1U);
  putLittleEndian16(header, 22U, static_cast<std::uint16_t>(stereoChannels));
  putLittleEndian32(header, 24U, sampleRate);
  putLittleEndian32(header, 28U,
    sampleRate * stereoChannels * (pcmBitsPerSample / 8U));
  putLittleEndian16(header, 32U,
    static_cast<std::uint16_t>(stereoChannels * (pcmBitsPerSample / 8U)));
  putLittleEndian16(header, 34U, static_cast<std::uint16_t>(pcmBitsPerSample));
  std::copy_n("data", 4, header.begin() + 36);
  putLittleEndian32(header, 40U, dataBytes);
  return header;
}

QImage rgb565Image(
  const CoreVideoFrameInfo& frame, std::span<const std::uint16_t> pixels)
{
  QImage image{static_cast<int>(frame.width), static_cast<int>(frame.height),
    QImage::Format_RGB32};
  if (image.isNull()) {
    return {};
  }
  for (std::uint32_t y = 0U; y < frame.height; ++y) {
    auto* destination = reinterpret_cast<QRgb*>(
      image.scanLine(static_cast<int>(y)));
    const auto row = static_cast<std::size_t>(y) * frame.width;
    for (std::uint32_t x = 0U; x < frame.width; ++x) {
      const auto pixel = pixels[row + x];
      const auto red5 = static_cast<unsigned>((pixel >> 11U) & 0x1fU);
      const auto green6 = static_cast<unsigned>((pixel >> 5U) & 0x3fU);
      const auto blue5 = static_cast<unsigned>(pixel & 0x1fU);
      destination[x] = qRgb(
        static_cast<int>((red5 << 3U) | (red5 >> 2U)),
        static_cast<int>((green6 << 2U) | (green6 >> 4U)),
        static_cast<int>((blue5 << 3U) | (blue5 >> 2U)));
    }
  }
  return image;
}

enum class ServiceState : std::uint8_t {
  stopped,
  idle,
  starting,
  recording,
  stopping,
};

struct FrameSlot final {
  FrameSlot()
    : pixels(maximumCoreSurfacePixels, 0U),
      audio(RecordingService::maximumAudioFramesPerBatch)
  {
  }

  CoreVideoFrameInfo video;
  std::vector<std::uint16_t> pixels;
  std::vector<StereoAudioFrame> audio;
  std::size_t pixelCount{0U};
  std::size_t audioFrameCount{0U};
  std::uint64_t captureIndex{0U};
};

struct Session final {
  RecordingRequest request;
  std::filesystem::path partialPath;
  std::filesystem::path finalPath;
  std::unique_ptr<QFile> audioFile;
  std::unique_ptr<QFile> frameLog;
  RecordingStatus terminalStatus;
  std::string stopReason{"user"};
  std::uint64_t writtenFrames{0U};
  std::uint64_t writtenAudioFrames{0U};
  std::uint64_t outputBytes{wavHeaderBytes};
};

struct SessionOpenResult final {
  RecordingStatus status;
  Session session;
};

SessionOpenResult openSession(const RecordingRequest& request)
{
  std::error_code error;
  std::filesystem::create_directories(request.baseDirectory, error);
  if (error || !std::filesystem::is_directory(request.baseDirectory, error) ||
      error) {
    return {
      .status = failure(RecordingError::directoryCreationFailed,
        "The recording directory could not be created or opened."),
      .session = {},
    };
  }
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
    request.timestamp.time_since_epoch()).count();
  const auto stamp = QDateTime::fromMSecsSinceEpoch(milliseconds)
    .toUTC().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
  const auto slug = QString::fromStdString(sanitizeFilename(
    request.gameTitle.empty() ? "recording" : request.gameTitle, 80U));

  Session session;
  session.request = request;
  for (std::size_t attempt = 0U; attempt < maximumFilenameAttempts; ++attempt) {
    const auto suffix = attempt == 0U
      ? QString{} : QStringLiteral("-%1").arg(attempt);
    const auto baseName = slug + QStringLiteral("-") + stamp + suffix;
    session.finalPath = request.baseDirectory /
      pathFromQString(baseName + QStringLiteral(".gpgx-recording"));
    session.partialPath = request.baseDirectory /
      pathFromQString(baseName + QStringLiteral(".gpgx-recording.partial"));
    const bool finalExists = std::filesystem::exists(session.finalPath, error);
    if (error) {
      return {
        .status = failure(RecordingError::directoryCreationFailed,
          "The recording destination could not be inspected safely."),
        .session = std::move(session),
      };
    }
    if (!finalExists) {
      const bool created =
        std::filesystem::create_directory(session.partialPath, error);
      if (error) {
        return {
          .status = failure(RecordingError::directoryCreationFailed,
            "A unique recording directory could not be created."),
          .session = std::move(session),
        };
      }
      if (created) {
        break;
      }
    }
    error.clear();
    session.finalPath.clear();
    session.partialPath.clear();
  }
  if (session.partialPath.empty()) {
    return {
      .status = failure(RecordingError::fileCommitFailed,
        "No collision-free recording directory was available."),
      .session = {},
    };
  }
  if (!std::filesystem::create_directory(session.partialPath / "frames", error) ||
      error) {
    std::filesystem::remove_all(session.partialPath, error);
    return {
      .status = failure(RecordingError::directoryCreationFailed,
        "The partial recording directory could not be created."),
      .session = std::move(session),
    };
  }
  session.audioFile = std::make_unique<QFile>(
    pathToQString(session.partialPath / "audio.wav"));
  session.frameLog = std::make_unique<QFile>(
    pathToQString(session.partialPath / "frames.jsonl"));
  if (!session.audioFile->open(QIODevice::ReadWrite | QIODevice::Truncate) ||
      session.audioFile->write(QByteArray(static_cast<qsizetype>(wavHeaderBytes), '\0')) !=
        static_cast<qint64>(wavHeaderBytes) ||
      !session.frameLog->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    session.audioFile->close();
    session.frameLog->close();
    std::filesystem::remove_all(session.partialPath, error);
    return {
      .status = failure(RecordingError::fileOpenFailed,
        "The recording audio or frame index could not be opened."),
      .session = std::move(session),
    };
  }
  return {.status = {}, .session = std::move(session)};
}

RecordingStatus appendAudio(Session& session, const FrameSlot& slot)
{
  if (slot.audioFrameCount == 0U) {
    return {};
  }
  const auto byteCount = slot.audioFrameCount * sizeof(StereoAudioFrame);
  QByteArray encoded(static_cast<qsizetype>(byteCount), Qt::Uninitialized);
  auto* output = reinterpret_cast<std::uint8_t*>(encoded.data());
  for (std::size_t index = 0U; index < slot.audioFrameCount; ++index) {
    const auto left = static_cast<std::uint16_t>(slot.audio[index].left);
    const auto right = static_cast<std::uint16_t>(slot.audio[index].right);
    output[index * 4U] = static_cast<std::uint8_t>(left & 0xffU);
    output[index * 4U + 1U] = static_cast<std::uint8_t>(left >> 8U);
    output[index * 4U + 2U] = static_cast<std::uint8_t>(right & 0xffU);
    output[index * 4U + 3U] = static_cast<std::uint8_t>(right >> 8U);
  }
  if (session.outputBytes + static_cast<std::uint64_t>(byteCount) >
      RecordingService::maximumSessionBytes) {
    return failure(RecordingError::outputLimitReached,
      "The recording reached its fixed 8 GiB output limit.");
  }
  if (session.audioFile->write(encoded) != encoded.size()) {
    return failure(RecordingError::fileWriteFailed,
      "The recording audio stream could not be written.");
  }
  session.writtenAudioFrames += slot.audioFrameCount;
  session.outputBytes += byteCount;
  return {};
}

RecordingStatus writeFrame(Session& session, const FrameSlot& slot)
{
  const auto image = rgb565Image(
    slot.video, std::span<const std::uint16_t>{slot.pixels}.first(slot.pixelCount));
  if (image.isNull()) {
    return failure(RecordingError::encodeFailed,
      "The recording frame could not be converted to an image.");
  }
  QByteArray png;
  QBuffer buffer{&png};
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") ||
      png.size() <= 0 ||
      static_cast<std::size_t>(png.size()) >
        RecordingService::maximumFramePngBytes) {
    return failure(RecordingError::encodeFailed,
      "The recording frame PNG could not be encoded within its fixed limit.");
  }
  const auto filename = QStringLiteral("frame-%1.png")
    .arg(static_cast<qulonglong>(slot.captureIndex), 9, 10, QLatin1Char{'0'});
  const QJsonObject record{
    {QStringLiteral("captureIndex"),
      static_cast<qint64>(slot.captureIndex)},
    {QStringLiteral("emulatedFrame"),
      static_cast<qint64>(slot.video.frameNumber)},
    {QStringLiteral("file"), filename},
    {QStringLiteral("width"), static_cast<int>(slot.video.width)},
    {QStringLiteral("height"), static_cast<int>(slot.video.height)},
    {QStringLiteral("interlaced"), slot.video.interlaced},
    {QStringLiteral("oddField"), slot.video.oddField},
    {QStringLiteral("audioFrames"),
      static_cast<qint64>(slot.audioFrameCount)},
  };
  auto line = QJsonDocument{record}.toJson(QJsonDocument::Compact);
  line.append('\n');
  const auto additionalBytes = static_cast<std::uint64_t>(png.size()) +
    static_cast<std::uint64_t>(line.size()) +
    static_cast<std::uint64_t>(slot.audioFrameCount * sizeof(StereoAudioFrame));
  const auto dataLimit = RecordingService::maximumSessionBytes -
    manifestReserveBytes;
  if (additionalBytes > dataLimit ||
      session.outputBytes > dataLimit - additionalBytes) {
    return failure(RecordingError::outputLimitReached,
      "The recording reached its fixed 8 GiB output limit.");
  }
  QSaveFile output{pathToQString(
    session.partialPath / "frames" / pathFromQString(filename))};
  if (!output.open(QIODevice::WriteOnly) || output.write(png) != png.size() ||
      !output.commit()) {
    return failure(RecordingError::fileWriteFailed,
      "The recording frame PNG could not be committed atomically.");
  }
  if (session.frameLog->write(line) != line.size()) {
    return failure(RecordingError::fileWriteFailed,
      "The recording frame index could not be written.");
  }
  const auto audioStatus = appendAudio(session, slot);
  if (!audioStatus) {
    return audioStatus;
  }
  ++session.writtenFrames;
  session.outputBytes += static_cast<std::uint64_t>(png.size() + line.size());
  return {};
}

RecordingStatus writeManifest(
  Session& session, const RecordingMetrics& metrics, bool complete)
{
  const auto startedMilliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      session.request.timestamp.time_since_epoch()).count();
  const QJsonObject manifest{
    {QStringLiteral("schemaVersion"), 1},
    {QStringLiteral("format"), QStringLiteral("png-wav-v1")},
    {QStringLiteral("complete"), complete},
    {QStringLiteral("gameTitle"),
      QString::fromUtf8(session.request.gameTitle)},
    {QStringLiteral("gameId"), QString::fromUtf8(session.request.gameId)},
    {QStringLiteral("startedUtc"),
      QDateTime::fromMSecsSinceEpoch(startedMilliseconds)
        .toUTC().toString(Qt::ISODateWithMs)},
    {QStringLiteral("nominalFramesPerSecond"),
      session.request.nominalFramesPerSecond},
    {QStringLiteral("audioSampleRate"),
      static_cast<int>(session.request.audioSampleRate)},
    {QStringLiteral("capturedFrames"),
      static_cast<qint64>(session.writtenFrames)},
    {QStringLiteral("droppedFrames"),
      static_cast<qint64>(metrics.droppedFrames)},
    {QStringLiteral("audioFrames"),
      static_cast<qint64>(session.writtenAudioFrames)},
    {QStringLiteral("outputBytes"),
      static_cast<qint64>(session.outputBytes)},
    {QStringLiteral("stopReason"), QString::fromStdString(session.stopReason)},
  };
  QSaveFile file{pathToQString(session.partialPath / "manifest.json")};
  const auto encoded = QJsonDocument{manifest}.toJson(QJsonDocument::Indented);
  if (static_cast<std::uint64_t>(encoded.size()) > manifestReserveBytes ||
      session.outputBytes > RecordingService::maximumSessionBytes -
        static_cast<std::uint64_t>(encoded.size())) {
    return failure(RecordingError::outputLimitReached,
      "The recording manifest would exceed the fixed 8 GiB output limit.");
  }
  if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() ||
      !file.commit()) {
    return failure(RecordingError::fileCommitFailed,
      "The recording manifest could not be committed atomically.");
  }
  session.outputBytes += static_cast<std::uint64_t>(encoded.size());
  return {};
}

RecordingStatus finalizeSession(
  Session& session, const RecordingMetrics& metrics)
{
  const auto header = wavHeader(
    session.request.audioSampleRate, session.writtenAudioFrames);
  if (!session.audioFile->seek(0) ||
      session.audioFile->write(header) != header.size() ||
      !session.audioFile->flush() || !session.frameLog->flush()) {
    if (session.terminalStatus) {
      session.terminalStatus = failure(RecordingError::fileWriteFailed,
        "The recording streams could not be finalized.");
    }
  }
  session.audioFile->close();
  session.frameLog->close();
  const bool completeBeforeManifest = session.terminalStatus.ok();
  const auto manifestStatus = writeManifest(
    session, metrics, completeBeforeManifest);
  if (!manifestStatus && session.terminalStatus) {
    session.terminalStatus = manifestStatus;
  }
  if (!session.terminalStatus) {
    return session.terminalStatus;
  }
  std::error_code error;
  std::filesystem::rename(session.partialPath, session.finalPath, error);
  if (error) {
    return failure(RecordingError::fileCommitFailed,
      "The completed recording directory could not be committed atomically.");
  }
  return {};
}

} // namespace

class RecordingService::Private final {
public:
  Private(std::size_t frameCapacity, std::size_t eventCapacity)
    : readySlots_(frameCapacity), events_(eventCapacity)
  {
    if (frameCapacity == 0U || frameCapacity > 64U) {
      throw std::invalid_argument{
        "A recording service requires between 1 and 64 frame slots."};
    }
    slots_.reserve(frameCapacity);
    freeSlots_.reserve(frameCapacity);
    for (std::size_t index = 0U; index < frameCapacity; ++index) {
      slots_.emplace_back();
      freeSlots_.push_back(index);
    }
    metrics_.queueCapacity = frameCapacity;
  }

  RecordingStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || state_ != ServiceState::stopped) {
      return failure(RecordingError::alreadyRunning,
        "The recording service is already running.");
    }
    resetSlotsLocked();
    events_.clear();
    shutdownStatus_ = {};
    serviceStop_ = false;
    accepting_ = true;
    state_ = ServiceState::idle;
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      accepting_ = false;
      state_ = ServiceState::stopped;
      return failure(RecordingError::threadFailure,
        "The recording worker could not start: " + std::string{error.what()});
    }
    return {};
  }

  RecordingStatus begin(RecordingRequest request)
  {
    if (request.operationId == 0U || request.baseDirectory.empty() ||
        !request.baseDirectory.is_absolute() ||
        request.baseDirectory.native().size() > 4'096U ||
        request.gameTitle.size() > 1'024U || request.gameId.size() > 128U ||
        request.audioSampleRate < 8'000U || request.audioSampleRate > 48'000U ||
        !std::isfinite(request.nominalFramesPerSecond) ||
        request.nominalFramesPerSecond < 40.0 ||
        request.nominalFramesPerSecond > 70.0 ||
        request.timestamp.time_since_epoch().count() < 0) {
      return failure(RecordingError::invalidRequest,
        "The recording request is incomplete or exceeds a fixed limit.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || state_ == ServiceState::stopped) {
      return failure(RecordingError::notRunning,
        "The recording service is not accepting requests.");
    }
    if (state_ != ServiceState::idle || beginRequest_) {
      return failure(RecordingError::busy,
        "A recording is already active or changing state.");
    }
    metrics_ = {.queueCapacity = slots_.size()};
    currentSampleRate_ = request.audioSampleRate;
    finishOperationId_ = request.operationId;
    beginRequest_ = std::move(request);
    state_ = ServiceState::starting;
    wake_.notify_one();
    return {};
  }

  RecordingStatus end(std::uint64_t operationId)
  {
    if (operationId == 0U) {
      return failure(RecordingError::invalidRequest,
        "A recording operation ID must be nonzero.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || state_ == ServiceState::stopped) {
      return failure(RecordingError::notRunning,
        "The recording service is not running.");
    }
    if (state_ != ServiceState::recording) {
      return failure(RecordingError::busy,
        "No active recording can be stopped right now.");
    }
    captureActive_.store(false, std::memory_order_release);
    state_ = ServiceState::stopping;
    finishRequested_ = true;
    finishOperationId_ = operationId;
    finishReason_ = "user";
    wake_.notify_one();
    return {};
  }

  bool active() const noexcept
  {
    return captureActive_.load(std::memory_order_acquire);
  }

  bool submitFrame(const CoreVideoFrameInfo& video,
    std::span<const std::uint16_t> pixels,
    const CoreAudioBatchInfo& audio,
    std::span<const StereoAudioFrame> audioFrames) noexcept
  {
    if (!active()) {
      return false;
    }
    const auto pixelCount = video.pixelCount();
    const bool videoValid = video.format == CorePixelFormat::rgb565 &&
      video.width > 0U && video.height > 0U &&
      pixelCount <= maximumCoreSurfacePixels &&
      pixels.size() >= pixelCount;
    const bool audioValid =
      (audioFrames.empty() && audio.frameCount == 0U) ||
      (!audioFrames.empty() && audio.channels == stereoChannels &&
        audio.frameCount == audioFrames.size() &&
        audio.frameCount <= maximumAudioFramesPerBatch);
    if (!videoValid || !audioValid) {
      std::scoped_lock lock{mutex_};
      ++metrics_.droppedFrames;
      return false;
    }

    std::scoped_lock lock{mutex_};
    if (!captureActive_.load(std::memory_order_relaxed) ||
        state_ != ServiceState::recording) {
      return false;
    }
    if ((!audioFrames.empty() && audio.sampleRate != currentSampleRate_) ||
        freeSlots_.empty() || readyCount_ == readySlots_.size()) {
      ++metrics_.droppedFrames;
      return false;
    }
    if (metrics_.acceptedFrames >= maximumSessionFrames) {
      captureActive_.store(false, std::memory_order_release);
      state_ = ServiceState::stopping;
      finishRequested_ = true;
      finishReason_ = "frame-limit";
      ++metrics_.droppedFrames;
      wake_.notify_one();
      return false;
    }
    const auto slotIndex = freeSlots_.back();
    freeSlots_.pop_back();
    auto& slot = slots_[slotIndex];
    slot.video = video;
    slot.pixelCount = pixelCount;
    slot.audioFrameCount = audioFrames.size();
    slot.captureIndex = metrics_.acceptedFrames;
    std::ranges::copy_n(pixels.begin(),
      static_cast<std::ptrdiff_t>(pixelCount), slot.pixels.begin());
    std::ranges::copy(audioFrames, slot.audio.begin());
    readySlots_[(readyHead_ + readyCount_) % readySlots_.size()] = slotIndex;
    ++readyCount_;
    ++metrics_.acceptedFrames;
    metrics_.queuedFrames = readyCount_;
    metrics_.peakQueuedFrames = std::max(
      metrics_.peakQueuedFrames, metrics_.queuedFrames);
    wake_.notify_one();
    return true;
  }

  std::optional<RecordingEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<RecordingEvent> waitForEvent(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  RecordingMetrics metrics() const
  {
    std::scoped_lock lock{mutex_};
    auto result = metrics_;
    result.active = captureActive_.load(std::memory_order_relaxed);
    result.queuedFrames = readyCount_;
    return result;
  }

  RecordingStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        accepting_ = false;
        state_ = ServiceState::stopped;
        return shutdownStatus_;
      }
      accepting_ = false;
      captureActive_.store(false, std::memory_order_release);
      serviceStop_ = true;
      beginRequest_.reset();
      if (state_ == ServiceState::recording) {
        state_ = ServiceState::stopping;
        finishRequested_ = true;
        finishReason_ = "application-shutdown";
      }
      wake_.notify_all();
      eventReady_.notify_all();
    }
    thread_.join();
    std::scoped_lock lock{mutex_};
    return shutdownStatus_;
  }

private:
  void threadMain()
  {
    publish({
      .type = RecordingEventType::serviceStarted,
      .operationId = 0U,
      .status = {},
      .path = {},
      .metrics = metrics(),
    });
    while (true) {
      std::optional<RecordingRequest> begin;
      std::optional<std::size_t> frameSlot;
      bool finish = false;
      bool stop = false;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] {
          return serviceStop_ || beginRequest_.has_value() ||
            readyCount_ > 0U || finishRequested_;
        });
        if (beginRequest_) {
          begin = std::move(beginRequest_);
          beginRequest_.reset();
        } else if (readyCount_ > 0U) {
          frameSlot = readySlots_[readyHead_];
          readyHead_ = (readyHead_ + 1U) % readySlots_.size();
          --readyCount_;
          metrics_.queuedFrames = readyCount_;
        } else if (finishRequested_) {
          finish = true;
          finishRequested_ = false;
        } else if (serviceStop_) {
          stop = true;
        }
      }

      if (begin) {
        auto opened = openSession(*begin);
        if (!opened.status) {
          {
            std::scoped_lock lock{mutex_};
            state_ = ServiceState::idle;
          }
          publish({
            .type = RecordingEventType::recordingFailed,
            .operationId = begin->operationId,
            .status = std::move(opened.status),
            .path = std::move(opened.session.partialPath),
            .metrics = metrics(),
          });
        } else {
          session_ = std::move(opened.session);
          bool stoppingForShutdown = false;
          {
            std::scoped_lock lock{mutex_};
            if (serviceStop_) {
              state_ = ServiceState::stopping;
              finishRequested_ = true;
              finishReason_ = "application-shutdown";
              stoppingForShutdown = true;
            } else {
              state_ = ServiceState::recording;
              captureActive_.store(true, std::memory_order_release);
            }
          }
          if (stoppingForShutdown) {
            wake_.notify_one();
            continue;
          }
          publish({
            .type = RecordingEventType::recordingStarted,
            .operationId = begin->operationId,
            .status = {},
            .path = session_->finalPath,
            .metrics = metrics(),
          });
        }
        continue;
      }

      if (frameSlot) {
        auto status = session_
          ? writeFrame(*session_, slots_[*frameSlot])
          : failure(RecordingError::fileWriteFailed,
              "A queued frame had no active recording session.");
        {
          std::scoped_lock lock{mutex_};
          freeSlots_.push_back(*frameSlot);
          if (status) {
            ++metrics_.writtenFrames;
            metrics_.writtenAudioFrames = session_->writtenAudioFrames;
            metrics_.outputBytes = session_->outputBytes;
          } else {
            captureActive_.store(false, std::memory_order_release);
            state_ = ServiceState::stopping;
            if (session_ && session_->terminalStatus) {
              session_->terminalStatus = status;
            }
            finishReason_ = status.error == RecordingError::outputLimitReached
              ? "output-limit" : "write-error";
            while (readyCount_ > 0U) {
              freeSlots_.push_back(readySlots_[readyHead_]);
              readyHead_ = (readyHead_ + 1U) % readySlots_.size();
              --readyCount_;
              ++metrics_.droppedFrames;
            }
            metrics_.queuedFrames = 0U;
            finishRequested_ = true;
          }
        }
        wake_.notify_one();
        continue;
      }

      if (finish) {
        if (session_) {
          session_->stopReason = finishReason_;
          auto finalMetrics = metrics();
          auto status = finalizeSession(*session_, finalMetrics);
          const auto successPath = session_->finalPath;
          const auto failurePath = session_->partialPath;
          const bool completed = status.ok();
          {
            std::scoped_lock lock{mutex_};
            metrics_.writtenAudioFrames = session_->writtenAudioFrames;
            metrics_.outputBytes = session_->outputBytes;
            if (!status && serviceStop_) {
              shutdownStatus_ = status;
            }
            state_ = ServiceState::idle;
            resetSlotsLocked();
          }
          publish({
            .type = completed ? RecordingEventType::recordingFinished
                              : RecordingEventType::recordingFailed,
            .operationId = finishOperationId_,
            .status = std::move(status),
            .path = completed ? successPath : failurePath,
            .metrics = metrics(),
          });
          session_.reset();
        } else {
          std::scoped_lock lock{mutex_};
          state_ = ServiceState::idle;
        }
        {
          std::scoped_lock lock{mutex_};
          stop = serviceStop_;
        }
      }

      if (stop) {
        break;
      }
    }
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      captureActive_.store(false, std::memory_order_release);
      state_ = ServiceState::stopped;
    }
    publish({
      .type = RecordingEventType::serviceStopped,
      .operationId = 0U,
      .status = shutdownStatus_,
      .path = {},
      .metrics = metrics(),
    });
  }

  void resetSlotsLocked()
  {
    readyHead_ = 0U;
    readyCount_ = 0U;
    freeSlots_.clear();
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
      freeSlots_.push_back(index);
    }
    metrics_.queuedFrames = 0U;
  }

  void publish(RecordingEvent event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::vector<FrameSlot> slots_;
  std::vector<std::size_t> freeSlots_;
  std::vector<std::size_t> readySlots_;
  std::size_t readyHead_{0U};
  std::size_t readyCount_{0U};
  BoundedQueue<RecordingEvent> events_;
  std::optional<RecordingRequest> beginRequest_;
  std::optional<Session> session_;
  std::thread thread_;
  RecordingMetrics metrics_;
  RecordingStatus shutdownStatus_;
  ServiceState state_{ServiceState::stopped};
  std::atomic_bool captureActive_{false};
  std::uint32_t currentSampleRate_{48'000U};
  std::uint64_t finishOperationId_{0U};
  std::string finishReason_{"user"};
  bool accepting_{false};
  bool finishRequested_{false};
  bool serviceStop_{false};
};

RecordingService::RecordingService(
  std::size_t frameCapacity, std::size_t eventCapacity)
  : private_(std::make_unique<Private>(frameCapacity, eventCapacity))
{
}

RecordingService::~RecordingService()
{
  static_cast<void>(private_->stop());
}

RecordingStatus RecordingService::start() { return private_->start(); }

RecordingStatus RecordingService::begin(RecordingRequest request)
{
  return private_->begin(std::move(request));
}

RecordingStatus RecordingService::end(std::uint64_t operationId)
{
  return private_->end(operationId);
}

std::optional<RecordingEvent> RecordingService::pollEvent()
{
  return private_->pollEvent();
}

std::optional<RecordingEvent> RecordingService::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

RecordingStatus RecordingService::stop() { return private_->stop(); }

RecordingMetrics RecordingService::metrics() const { return private_->metrics(); }

bool RecordingService::active() const noexcept { return private_->active(); }

bool RecordingService::submitFrame(const CoreVideoFrameInfo& video,
  std::span<const std::uint16_t> rgb565Pixels,
  const CoreAudioBatchInfo& audio,
  std::span<const StereoAudioFrame> audioFrames) noexcept
{
  return private_->submitFrame(video, rgb565Pixels, audio, audioFrames);
}

} // namespace genplusgx::capture
