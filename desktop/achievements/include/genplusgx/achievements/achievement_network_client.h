#pragma once

#include "genplusgx/achievements/achievement_bridge.h"

#include <QObject>

#include <cstddef>
#include <memory>
#include <string>

class QNetworkAccessManager;

namespace genplusgx::achievements {

struct NetworkMetrics final {
  std::size_t activeRequests{0U};
  std::uint64_t completedRequests{0U};
  std::uint64_t failedRequests{0U};
  std::string lastError;
};

class NetworkClient final : public QObject {
  Q_OBJECT

public:
  explicit NetworkClient(
    std::shared_ptr<ServerBridge> bridge,
    std::string userAgent,
    QObject* parent = nullptr);
  ~NetworkClient() override;

  void pump();
  [[nodiscard]] NetworkMetrics metrics() const;
  [[nodiscard]] static bool providerUrlAllowed(const std::string& url);

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::achievements
