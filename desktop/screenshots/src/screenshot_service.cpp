#include "genplusgx/screenshots/screenshot_service.h"

#include "genplusgx/bounded_queue.h"
#include "genplusgx/persistence.h"
#include "genplusgx/video/frame_exchange.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QTemporaryFile>

#include <condition_variable>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

namespace genplusgx::screenshots {
namespace {

constexpr std::size_t maximumFilenameAttempts = 10'000U;

ScreenshotStatus failure(ScreenshotError error, std::string message)
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

QImage rgb565Image(
  const CoreVideoFrameInfo& frame, std::span<const std::uint16_t> pixels)
{
  QImage image{static_cast<int>(frame.width),
    static_cast<int>(frame.height),
    QImage::Format_RGB32};
  if (image.isNull()) {
    return {};
  }
  for (std::uint32_t y = 0U; y < frame.height; ++y) {
    auto* destination = reinterpret_cast<QRgb*>(image.scanLine(static_cast<int>(y)));
    const auto rowOffset = static_cast<std::size_t>(y) * frame.width;
    for (std::uint32_t x = 0U; x < frame.width; ++x) {
      const auto pixel = pixels[rowOffset + x];
      const auto red5 = static_cast<unsigned>((pixel >> 11U) & 0x1fU);
      const auto green6 = static_cast<unsigned>((pixel >> 5U) & 0x3fU);
      const auto blue5 = static_cast<unsigned>(pixel & 0x1fU);
      const auto red8 = static_cast<int>((red5 << 3U) | (red5 >> 2U));
      const auto green8 = static_cast<int>((green6 << 2U) | (green6 >> 4U));
      const auto blue8 = static_cast<int>((blue5 << 3U) | (blue5 >> 2U));
      destination[x] = qRgb(red8, green8, blue8);
    }
  }
  return image;
}

struct ScreenshotCommand final {
  std::uint64_t operationId{0};
  std::filesystem::path directory;
  std::string gameTitle;
  CoreVideoFrameInfo frame;
  std::vector<std::uint16_t> pixels;
};

} // namespace

ScreenshotResult writeNativeScreenshot(const std::filesystem::path& directory,
  std::string_view gameTitle,
  const CoreVideoFrameInfo& frame,
  std::span<const std::uint16_t> rgb565Pixels,
  std::chrono::system_clock::time_point timestamp)
{
  if (frame.format != CorePixelFormat::rgb565 || frame.width == 0U ||
      frame.height == 0U ||
      frame.pixelCount() > VideoFrameExchange::maximumSurfacePixels ||
      rgb565Pixels.size() < frame.pixelCount() ||
      frame.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      frame.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return {
      .status = failure(ScreenshotError::invalidFrame,
        "The native video frame is incomplete or has "
        "unsupported dimensions."),
      .path = {},
    };
  }
  if (directory.empty() || !directory.is_absolute() ||
      directory.native().size() > 4'096U) {
    return {
      .status = failure(ScreenshotError::invalidDirectory,
        "The screenshot directory must be a bounded absolute path."),
      .path = {},
    };
  }
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error || !std::filesystem::is_directory(directory, error) || error) {
    return {
      .status = failure(ScreenshotError::directoryCreationFailed,
        "The screenshot directory could not be created or opened."),
      .path = {},
    };
  }

  auto image = rgb565Image(frame, rgb565Pixels.first(frame.pixelCount()));
  if (image.isNull()) {
    return {
      .status = failure(ScreenshotError::encodeFailed,
        "The native video frame could not be converted to an image."),
      .path = {},
    };
  }
  const auto slug = sanitizeFilename(gameTitle.empty() ? "screenshot" : gameTitle, 80U);
  const auto timestampMilliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch())
      .count();
  const auto timestampText = QDateTime::fromMSecsSinceEpoch(timestampMilliseconds)
                               .toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
  const auto baseName = QStringLiteral("%1-%2-f%3")
                          .arg(QString::fromStdString(slug), timestampText)
                          .arg(static_cast<qulonglong>(frame.frameNumber));

  QTemporaryFile temporary{QDir{pathToQString(directory)}.filePath(
    QStringLiteral(".genplusgx-screenshot-XXXXXX.tmp"))};
  temporary.setAutoRemove(true);
  if (!temporary.open() || !image.save(&temporary, "PNG") || !temporary.flush()) {
    return {
      .status = failure(ScreenshotError::encodeFailed,
        "The screenshot PNG could not be encoded in the "
        "destination directory."),
      .path = {},
    };
  }
  temporary.close();

  for (std::size_t attempt = 0U; attempt < maximumFilenameAttempts; ++attempt) {
    const auto suffix = attempt == 0U ? QString{} : QStringLiteral("-%1").arg(attempt);
    const auto destination =
      directory / pathFromQString(baseName + suffix + QStringLiteral(".png"));
    if (QFileInfo::exists(pathToQString(destination))) {
      continue;
    }
    if (temporary.rename(pathToQString(destination))) {
      temporary.setAutoRemove(false);
      return {.status = {}, .path = destination};
    }
    if (QFileInfo::exists(pathToQString(destination))) {
      continue;
    }
    return {
      .status = failure(ScreenshotError::fileWriteFailed,
        "The screenshot could not be committed atomically."),
      .path = {},
    };
  }
  return {
    .status = failure(ScreenshotError::filenameExhausted,
      "No collision-free screenshot filename was available."),
    .path = {},
  };
}

class ScreenshotService::Private final {
public:
  Private(std::size_t commandCapacity, std::size_t eventCapacity)
      : commands_(commandCapacity), events_(eventCapacity)
  {
  }

  ScreenshotStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || accepting_) {
      return failure(
        ScreenshotError::alreadyRunning, "The screenshot service is already running.");
    }
    commands_.clear();
    events_.clear();
    stopRequested_ = false;
    accepting_ = true;
    shutdownStatus_ = {};
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      accepting_ = false;
      return failure(ScreenshotError::threadFailure,
        "The screenshot worker could not start: " + std::string{error.what()});
    }
    return {};
  }

  ScreenshotStatus request(std::uint64_t operationId,
    std::filesystem::path directory,
    std::string gameTitle,
    CoreVideoFrameInfo frame,
    std::vector<std::uint16_t> pixels)
  {
    if (operationId == 0U || directory.empty() || gameTitle.size() > 1'024U ||
        frame.pixelCount() == 0U || pixels.size() != frame.pixelCount()) {
      return failure(ScreenshotError::invalidRequest,
        "The screenshot request is incomplete or exceeds a fixed limit.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_) {
      return failure(ScreenshotError::notRunning,
        "The screenshot service is not accepting requests.");
    }
    if (!commands_.tryPush({.operationId = operationId,
          .directory = std::move(directory),
          .gameTitle = std::move(gameTitle),
          .frame = frame,
          .pixels = std::move(pixels)})) {
      return failure(
        ScreenshotError::queueFull, "The bounded screenshot queue is full.");
    }
    wake_.notify_one();
    return {};
  }

  std::optional<ScreenshotEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<ScreenshotEvent> waitForEvent(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  ScreenshotStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        accepting_ = false;
        return shutdownStatus_;
      }
      accepting_ = false;
      stopRequested_ = true;
      commands_.clear();
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
      .type = ScreenshotEventType::serviceStarted,
      .operationId = 0U,
      .status = {},
      .path = {},
    });
    while (true) {
      std::optional<ScreenshotCommand> command;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] { return stopRequested_ || !commands_.empty(); });
        if (stopRequested_) {
          break;
        }
        command = commands_.pop();
      }
      if (!command) {
        continue;
      }
      auto result = writeNativeScreenshot(
        command->directory, command->gameTitle, command->frame, command->pixels);
      publish({
        .type = result.status ? ScreenshotEventType::screenshotSaved
                              : ScreenshotEventType::screenshotFailed,
        .operationId = command->operationId,
        .status = std::move(result.status),
        .path = std::move(result.path),
      });
    }
    finish({});
  }

  void publish(ScreenshotEvent event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  void finish(ScreenshotStatus status)
  {
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      shutdownStatus_ = status;
    }
    publish({
      .type = ScreenshotEventType::serviceStopped,
      .operationId = 0U,
      .status = std::move(status),
      .path = {},
    });
  }

  BoundedQueue<ScreenshotCommand> commands_;
  BoundedQueue<ScreenshotEvent> events_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  ScreenshotStatus shutdownStatus_;
  bool accepting_{false};
  bool stopRequested_{false};
};

ScreenshotService::ScreenshotService(
  std::size_t commandCapacity, std::size_t eventCapacity)
    : private_(std::make_unique<Private>(commandCapacity, eventCapacity))
{
}

ScreenshotService::~ScreenshotService() { static_cast<void>(private_->stop()); }

ScreenshotStatus ScreenshotService::start() { return private_->start(); }

ScreenshotStatus ScreenshotService::request(std::uint64_t operationId,
  std::filesystem::path directory,
  std::string gameTitle,
  CoreVideoFrameInfo frame,
  std::vector<std::uint16_t> pixels)
{
  return private_->request(
    operationId, std::move(directory), std::move(gameTitle), frame, std::move(pixels));
}

std::optional<ScreenshotEvent> ScreenshotService::pollEvent()
{
  return private_->pollEvent();
}

std::optional<ScreenshotEvent> ScreenshotService::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

ScreenshotStatus ScreenshotService::stop() { return private_->stop(); }

} // namespace genplusgx::screenshots
