#pragma once

#include "genplusgx/video/frame_exchange.h"

#include <QWidget>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class QLabel;
class QPaintEvent;

namespace genplusgx::video {

class DisplayWidget final : public QWidget {
public:
  explicit DisplayWidget(QWidget* parent = nullptr);

  void setFrameExchange(std::shared_ptr<VideoFrameExchange> exchange);
  [[nodiscard]] std::shared_ptr<VideoFrameExchange> frameExchange() const;
  [[nodiscard]] bool presentLatestFrame();
  void clearFrame();

  [[nodiscard]] bool hasFrame() const noexcept;
  [[nodiscard]] const CoreVideoFrameInfo& currentFrameInfo() const noexcept;
  [[nodiscard]] std::uint64_t currentGeneration() const noexcept;
  [[nodiscard]] std::span<const std::uint16_t> currentPixels() const noexcept;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  std::shared_ptr<VideoFrameExchange> exchange_;
  std::vector<std::uint16_t> pixels_;
  CoreVideoFrameInfo frame_;
  std::uint64_t generation_{0};
  QLabel* emptyLabel_{nullptr};
  bool hasFrame_{false};
};

} // namespace genplusgx::video
