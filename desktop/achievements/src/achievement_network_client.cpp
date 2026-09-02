#include "genplusgx/achievements/achievement_network_client.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace genplusgx::achievements {

class NetworkClient::Private final {
public:
  struct ActiveRequest final {
    std::uint64_t id{0U};
    QByteArray body;
    bool exceededLimit{false};
    bool timedOut{false};
    bool tlsFailure{false};
  };

  Private(NetworkClient& ownerValue,
    std::shared_ptr<ServerBridge> bridgeValue,
    std::string userAgentValue)
      : owner(ownerValue),
        bridge(std::move(bridgeValue)),
        userAgent(std::move(userAgentValue)),
        manager(&ownerValue)
  {
  }

  void start(ServerRequest request)
  {
    if (!NetworkClient::providerUrlAllowed(request.url)) {
      ++metrics.failedRequests;
      metrics.lastError = "RetroAchievements rejected a non-provider HTTPS URL.";
      static_cast<void>(bridge->submitResponse({
        .id = request.id,
        .httpStatusCode = -1,
        .body = {},
      }));
      return;
    }

    QNetworkRequest networkRequest{QUrl{QString::fromStdString(request.url)}};
    networkRequest.setAttribute(
      QNetworkRequest::RedirectPolicyAttribute,
      QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader,
      QString::fromStdString(userAgent));
    if (!request.contentType.empty()) {
      networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
        QString::fromStdString(request.contentType));
    }
    QNetworkReply* reply = request.postData.empty()
      ? manager.get(networkRequest)
      : manager.post(networkRequest, QByteArray::fromStdString(request.postData));
    reply->setReadBufferSize(
      static_cast<qint64>(maximumServerBodyBytes + 1U));
    active.emplace(reply, ActiveRequest{
      .id = request.id,
      .body = {},
      .exceededLimit = false,
      .timedOut = false,
      .tlsFailure = false,
    });

    QObject::connect(reply, &QIODevice::readyRead, &owner,
      [this, reply] { consume(reply); });
    QObject::connect(reply, &QNetworkReply::sslErrors, &owner,
      [this, reply](const QList<QSslError>&) {
        if (const auto found = active.find(reply); found != active.end()) {
          found->second.tlsFailure = true;
          reply->abort();
        }
      });
    QObject::connect(reply, &QNetworkReply::finished, &owner,
      [this, reply] { finish(reply); });
    QTimer::singleShot(15'000, reply, [this, reply] {
      if (const auto found = active.find(reply); found != active.end() &&
          reply->isRunning()) {
        found->second.timedOut = true;
        reply->abort();
      }
    });
  }

  void consume(QNetworkReply* reply)
  {
    const auto found = active.find(reply);
    if (found == active.end()) {
      return;
    }
    auto& state = found->second;
    const auto chunk = reply->readAll();
    if (state.body.size() + chunk.size() >
        static_cast<qsizetype>(maximumServerBodyBytes)) {
      state.exceededLimit = true;
      reply->abort();
      return;
    }
    state.body.append(chunk);
  }

  void finish(QNetworkReply* reply)
  {
    const auto found = active.find(reply);
    if (found == active.end()) {
      reply->deleteLater();
      return;
    }
    consume(reply);
    auto state = std::move(found->second);
    active.erase(found);

    const auto statusValue = reply->attribute(
      QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusValue.isValid() ? statusValue.toInt() : -1;
    const bool redirected = status >= 300 && status < 400;
    const bool transportFailed = state.exceededLimit || state.timedOut ||
      state.tlsFailure || redirected ||
      (reply->error() != QNetworkReply::NoError && status < 100);

    ServerResponse response{
      .id = state.id,
      .httpStatusCode = transportFailed ? -1 : status,
      .body = {},
    };
    if (!transportFailed) {
      response.body.assign(state.body.constData(),
        static_cast<std::size_t>(state.body.size()));
      ++metrics.completedRequests;
    } else {
      ++metrics.failedRequests;
      if (state.exceededLimit) {
        metrics.lastError = "RetroAchievements returned a response above the 4 MiB limit.";
      } else if (state.timedOut) {
        metrics.lastError = "RetroAchievements did not respond within 15 seconds.";
      } else if (state.tlsFailure) {
        metrics.lastError = "RetroAchievements TLS certificate validation failed.";
      } else if (redirected) {
        metrics.lastError = "RetroAchievements returned an unexpected redirect.";
      } else {
        metrics.lastError = "The RetroAchievements network request failed.";
      }
    }
    static_cast<void>(bridge->submitResponse(std::move(response)));
    reply->deleteLater();
  }

  NetworkClient& owner;
  std::shared_ptr<ServerBridge> bridge;
  std::string userAgent;
  QNetworkAccessManager manager;
  std::unordered_map<QNetworkReply*, ActiveRequest> active;
  NetworkMetrics metrics;
};

NetworkClient::NetworkClient(std::shared_ptr<ServerBridge> bridge,
  std::string userAgent,
  QObject* parent)
    : QObject(parent),
      private_(std::make_unique<Private>(
        *this, std::move(bridge), std::move(userAgent)))
{
}

NetworkClient::~NetworkClient()
{
  std::vector<QNetworkReply*> replies;
  replies.reserve(private_->active.size());
  for (const auto& [reply, state] : private_->active) {
    static_cast<void>(state);
    replies.push_back(reply);
  }
  for (auto* reply : replies) {
    reply->disconnect(this);
    reply->abort();
  }
}

void NetworkClient::pump()
{
  if (!private_->bridge) {
    return;
  }
  constexpr std::size_t maximumConcurrentRequests = 4U;
  while (private_->active.size() < maximumConcurrentRequests) {
    auto request = private_->bridge->takeRequest();
    if (!request) {
      break;
    }
    private_->start(std::move(*request));
  }
  private_->metrics.activeRequests = private_->active.size();
}

NetworkMetrics NetworkClient::metrics() const
{
  auto metrics = private_->metrics;
  metrics.activeRequests = private_->active.size();
  return metrics;
}

bool NetworkClient::providerUrlAllowed(const std::string& url)
{
  const QUrl parsed{QString::fromStdString(url), QUrl::StrictMode};
  if (!parsed.isValid() || parsed.scheme().compare(
        QStringLiteral("https"), Qt::CaseInsensitive) != 0 ||
      !parsed.userName().isEmpty() || !parsed.password().isEmpty() ||
      !parsed.fragment().isEmpty() ||
      (parsed.port(-1) != -1 && parsed.port(-1) != 443)) {
    return false;
  }
  const auto host = parsed.host().toLower();
  return host == QStringLiteral("retroachievements.org") ||
    host.endsWith(QStringLiteral(".retroachievements.org"));
}

} // namespace genplusgx::achievements
