#include "genplusgx/capture/streaming_service.h"

#include "genplusgx/bounded_queue.h"

#include <QByteArray>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace genplusgx::capture {
namespace {

constexpr std::string_view streamGreeting = "GPGX-AV/1\n";
constexpr std::array<char, 4U> frameMagic{'G', 'X', 'F', '1'};

StreamingStatus failure(StreamingError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

struct FrameSlot final {
  FrameSlot()
    : pixels(maximumCoreSurfacePixels),
      audio(StreamingService::maximumAudioFramesPerBatch)
  {
  }

  CoreVideoFrameInfo video;
  CoreAudioBatchInfo audioInfo;
  std::vector<std::uint16_t> pixels;
  std::vector<StereoAudioFrame> audio;
  std::size_t pixelCount{0U};
  std::size_t audioCount{0U};
};

void appendU16(QByteArray& output, std::uint16_t value)
{
  output.append(static_cast<char>(value & 0xffU));
  output.append(static_cast<char>(value >> 8U));
}

void appendU32(QByteArray& output, std::uint32_t value)
{
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    output.append(static_cast<char>(value >> shift));
  }
}

void appendU64(QByteArray& output, std::uint64_t value)
{
  for (unsigned shift = 0U; shift < 64U; shift += 8U) {
    output.append(static_cast<char>(value >> shift));
  }
}

void encodeFrame(const FrameSlot& slot, QByteArray& output)
{
  const auto pixelBytes = slot.pixelCount * sizeof(std::uint16_t);
  const auto audioBytes = slot.audioCount * sizeof(StereoAudioFrame);
  constexpr std::size_t headerBytes = 40U;
  output.clear();
  output.reserve(static_cast<qsizetype>(headerBytes + pixelBytes + audioBytes));
  output.append(frameMagic.data(), static_cast<qsizetype>(frameMagic.size()));
  appendU32(output, static_cast<std::uint32_t>(
    headerBytes - 8U + pixelBytes + audioBytes));
  appendU64(output, slot.video.frameNumber);
  appendU32(output, slot.video.width);
  appendU32(output, slot.video.height);
  appendU32(output,
    (slot.video.interlaced ? 1U : 0U) | (slot.video.oddField ? 2U : 0U));
  appendU32(output, slot.audioInfo.sampleRate);
  appendU32(output, static_cast<std::uint32_t>(slot.audioCount));
  appendU32(output, static_cast<std::uint32_t>(slot.pixelCount));
  for (std::size_t index = 0U; index < slot.pixelCount; ++index) {
    appendU16(output, slot.pixels[index]);
  }
  for (std::size_t index = 0U; index < slot.audioCount; ++index) {
    appendU16(output, static_cast<std::uint16_t>(slot.audio[index].left));
    appendU16(output, static_cast<std::uint16_t>(slot.audio[index].right));
  }
}

} // namespace

class StreamingService::Private final {
public:
  Private(std::size_t frameCapacity, std::size_t eventCapacity)
    : ready_(frameCapacity), events_(eventCapacity)
  {
    if (frameCapacity == 0U || frameCapacity > 32U || eventCapacity == 0U) {
      throw std::invalid_argument{
        "A streaming service requires bounded, nonzero queues."};
    }
    slots_.reserve(frameCapacity);
    free_.reserve(frameCapacity);
    for (std::size_t index = 0U; index < frameCapacity; ++index) {
      slots_.emplace_back();
      free_.push_back(index);
    }
    metrics_.queueCapacity = frameCapacity;
  }

  StreamingStatus start(StreamingConfiguration configuration)
  {
    if (!configuration.valid()) {
      return failure(StreamingError::invalidConfiguration,
        "The local stream port or client limit is invalid.");
    }
    std::scoped_lock lock{mutex_};
    if (thread_.joinable()) {
      return failure(StreamingError::alreadyRunning,
        "The local A/V stream is already running.");
    }
    configuration_ = configuration;
    events_.clear();
    ready_.clear();
    free_.clear();
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
      free_.push_back(index);
    }
    metrics_ = {.queueCapacity = slots_.size()};
    stopRequested_.store(false, std::memory_order_release);
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      return failure(StreamingError::threadFailure,
        "The local stream thread could not start: " + std::string{error.what()});
    }
    return {};
  }

  StreamingStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        return {};
      }
      stopRequested_.store(true, std::memory_order_release);
      wake_.notify_all();
    }
    thread_.join();
    return shutdownStatus_;
  }

  bool active() const noexcept
  {
    return active_.load(std::memory_order_acquire);
  }

  bool submitFrame(
    const CoreVideoFrameInfo& video,
    std::span<const std::uint16_t> pixels,
    const CoreAudioBatchInfo& audioInfo,
    std::span<const StereoAudioFrame> audio) noexcept
  {
    if (!active() || video.format != CorePixelFormat::rgb565 ||
        video.width == 0U || video.height == 0U ||
        video.pixelCount() > maximumCoreSurfacePixels ||
        pixels.size() < video.pixelCount() ||
        audioInfo.frameCount != audio.size() ||
        audio.size() > StreamingService::maximumAudioFramesPerBatch) {
      return false;
    }
    std::scoped_lock lock{mutex_};
    if (!active_.load(std::memory_order_relaxed) || free_.empty()) {
      ++metrics_.droppedFrames;
      return false;
    }
    const auto index = free_.back();
    free_.pop_back();
    auto& slot = slots_[index];
    slot.video = video;
    slot.audioInfo = audioInfo;
    slot.pixelCount = video.pixelCount();
    slot.audioCount = audio.size();
    std::ranges::copy(pixels.first(slot.pixelCount), slot.pixels.begin());
    std::ranges::copy(audio, slot.audio.begin());
    if (!ready_.tryPush(index)) {
      free_.push_back(index);
      ++metrics_.droppedFrames;
      return false;
    }
    ++metrics_.acceptedFrames;
    metrics_.queueDepth = ready_.size();
    metrics_.peakQueueDepth = std::max(
      metrics_.peakQueueDepth, metrics_.queueDepth);
    wake_.notify_one();
    return true;
  }

  std::optional<StreamingEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<StreamingEvent> waitForEvent(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  StreamingMetrics metrics() const
  {
    std::scoped_lock lock{mutex_};
    return metrics_;
  }

private:
  void publish(StreamingEventType type, StreamingStatus status = {})
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(
      events_.dropOldestAndPush({type, std::move(status), metrics_}));
    eventReady_.notify_all();
  }

  std::optional<std::size_t> takeFrame()
  {
    std::scoped_lock lock{mutex_};
    auto index = ready_.pop();
    metrics_.queueDepth = ready_.size();
    return index;
  }

  void releaseFrame(std::size_t index)
  {
    std::scoped_lock lock{mutex_};
    free_.push_back(index);
  }

  void updateClientCount(std::size_t count)
  {
    std::scoped_lock lock{mutex_};
    metrics_.connectedClients = count;
  }

  void threadMain()
  {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, configuration_.port)) {
      const auto detail = server.errorString().toStdString();
      {
        std::scoped_lock lock{mutex_};
        shutdownStatus_ = failure(StreamingError::listenFailed,
          "The loopback A/V stream could not listen: " + detail);
      }
      publish(StreamingEventType::failed, shutdownStatus_);
      return;
    }
    {
      std::scoped_lock lock{mutex_};
      metrics_.active = true;
      metrics_.port = server.serverPort();
      shutdownStatus_ = {};
    }
    active_.store(true, std::memory_order_release);
    publish(StreamingEventType::started);

    std::vector<std::unique_ptr<QTcpSocket>> clients;
    QByteArray packet;
    while (!stopRequested_.load(std::memory_order_acquire)) {
      static_cast<void>(server.waitForNewConnection(5));
      while (server.hasPendingConnections()) {
        std::unique_ptr<QTcpSocket> socket{server.nextPendingConnection()};
        if (!socket) {
          break;
        }
        if (clients.size() >= configuration_.maximumClients) {
          socket->disconnectFromHost();
          continue;
        }
        socket->write(streamGreeting.data(),
          static_cast<qint64>(streamGreeting.size()));
        socket->flush();
        clients.push_back(std::move(socket));
        updateClientCount(clients.size());
        publish(StreamingEventType::clientConnected);
      }

      const auto oldCount = clients.size();
      std::erase_if(clients, [](const auto& socket) {
        return socket->state() == QAbstractSocket::UnconnectedState;
      });
      if (clients.size() != oldCount) {
        updateClientCount(clients.size());
        publish(StreamingEventType::clientDisconnected);
      }

      while (const auto index = takeFrame()) {
        encodeFrame(slots_[*index], packet);
        bool broadcast = false;
        for (auto& socket : clients) {
          if (socket->bytesToWrite() >
              static_cast<qint64>(StreamingService::maximumClientBacklogBytes)) {
            socket->abort();
            std::scoped_lock lock{mutex_};
            ++metrics_.disconnectedSlowClients;
            continue;
          }
          const auto written = socket->write(packet);
          if (written == packet.size()) {
            socket->flush();
            broadcast = true;
            std::scoped_lock lock{mutex_};
            metrics_.bytesSent += static_cast<std::uint64_t>(written);
          }
        }
        if (broadcast) {
          std::scoped_lock lock{mutex_};
          ++metrics_.broadcastFrames;
        }
        releaseFrame(*index);
      }

      std::unique_lock lock{mutex_};
      wake_.wait_for(lock, std::chrono::milliseconds{5}, [this] {
        return stopRequested_.load(std::memory_order_acquire) || !ready_.empty();
      });
    }

    active_.store(false, std::memory_order_release);
    for (auto& socket : clients) {
      socket->disconnectFromHost();
      static_cast<void>(socket->waitForDisconnected(50));
    }
    server.close();
    {
      std::scoped_lock lock{mutex_};
      metrics_.active = false;
      metrics_.connectedClients = 0U;
      while (auto index = ready_.pop()) {
        free_.push_back(*index);
        ++metrics_.droppedFrames;
      }
      metrics_.queueDepth = 0U;
    }
    publish(StreamingEventType::stopped, shutdownStatus_);
  }

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  BoundedQueue<std::size_t> ready_;
  BoundedQueue<StreamingEvent> events_;
  std::vector<FrameSlot> slots_;
  std::vector<std::size_t> free_;
  std::thread thread_;
  StreamingConfiguration configuration_;
  StreamingMetrics metrics_;
  StreamingStatus shutdownStatus_;
  std::atomic_bool active_{false};
  std::atomic_bool stopRequested_{false};
};

StreamingService::StreamingService(
  std::size_t frameCapacity, std::size_t eventCapacity)
  : private_(std::make_unique<Private>(frameCapacity, eventCapacity))
{
}

StreamingService::~StreamingService()
{
  static_cast<void>(stop());
}

StreamingStatus StreamingService::start(StreamingConfiguration configuration)
{
  return private_->start(configuration);
}

StreamingStatus StreamingService::stop()
{
  return private_->stop();
}

std::optional<StreamingEvent> StreamingService::pollEvent()
{
  return private_->pollEvent();
}

std::optional<StreamingEvent> StreamingService::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

StreamingMetrics StreamingService::metrics() const
{
  return private_->metrics();
}

bool StreamingService::active() const noexcept
{
  return private_->active();
}

bool StreamingService::submitFrame(
  const CoreVideoFrameInfo& video,
  std::span<const std::uint16_t> rgb565Pixels,
  const CoreAudioBatchInfo& audio,
  std::span<const StereoAudioFrame> audioFrames) noexcept
{
  return private_->submitFrame(video, rgb565Pixels, audio, audioFrames);
}

} // namespace genplusgx::capture
