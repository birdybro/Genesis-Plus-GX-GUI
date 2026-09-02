#include "genplusgx/cloud/cloud_remote.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace genplusgx::cloud {
namespace {

constexpr int transferTimeoutMilliseconds = 15'000;

Status failure(Error error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

bool validRelativeRemotePath(const std::string& path)
{
  if (path.empty() || path.size() > 512U || path.front() == '/' ||
      path.back() == '/' || path.find('\\') != std::string::npos) {
    return false;
  }
  std::size_t begin = 0U;
  while (begin < path.size()) {
    const auto end = path.find('/', begin);
    const auto length = (end == std::string::npos ? path.size() : end) - begin;
    if (length == 0U || length > 128U || path.substr(begin, length) == "." ||
        path.substr(begin, length) == ".." ||
        !std::all_of(path.begin() + static_cast<std::ptrdiff_t>(begin),
          path.begin() + static_cast<std::ptrdiff_t>(begin + length),
          [](unsigned char byte) {
            return std::isalnum(byte) != 0 || byte == '-' || byte == '_' ||
              byte == '.';
          })) {
      return false;
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }
  return true;
}

Error responseError(int status)
{
  return status == 401 || status == 403 ? Error::authenticationFailed
                                        : Error::transportFailed;
}

} // namespace

class WebDavRemoteStore::Private final {
public:
  Private(Settings settings, std::string password, Cancellation cancellation,
    const std::vector<std::string>& additionalTrustedCaDer)
      : settings_(std::move(settings)), password_(std::move(password)),
        cancellation_(std::move(cancellation))
  {
    endpoint_ = QUrl{QString::fromStdString(settings_.endpoint)};
    auto path = endpoint_.path();
    if (!path.endsWith('/')) {
      path += '/';
    }
    endpoint_.setPath(path);
    sslConfiguration_ = QSslConfiguration::defaultConfiguration();
    sslConfiguration_.setAllowedNextProtocols({
      QSslConfiguration::NextProtocolHttp1_1});
    auto authorities = sslConfiguration_.caCertificates();
    for (const auto& der : additionalTrustedCaDer) {
      const auto certificates = QSslCertificate::fromData(
        QByteArray::fromStdString(der), QSsl::Der);
      authorities.append(certificates);
    }
    sslConfiguration_.setCaCertificates(authorities);
  }

  ~Private()
  {
    std::fill(password_.begin(), password_.end(), '\0');
  }

  QUrl urlFor(const std::string& relativePath) const
  {
    auto url = endpoint_;
    const auto relative = QString::fromStdString(relativePath);
    auto path = url.path();
    const auto parts = relative.split('/');
    for (qsizetype index = 0; index < parts.size(); ++index) {
      path += QString::fromUtf8(QUrl::toPercentEncoding(parts[index]));
      if (index + 1 < parts.size()) {
        path += '/';
      }
    }
    url.setPath(path, QUrl::DecodedMode);
    return url;
  }

  QNetworkRequest requestFor(const std::string& relativePath) const
  {
    QNetworkRequest request{urlFor(relativePath)};
    request.setTransferTimeout(transferTimeoutMilliseconds);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
      QNetworkRequest::ManualRedirectPolicy);
    request.setSslConfiguration(sslConfiguration_);
    const auto credentials = QByteArray::fromStdString(
      settings_.username + ':' + password_).toBase64();
    request.setRawHeader("Authorization", "Basic " + credentials);
    request.setRawHeader("User-Agent", "Genesis-Plus-GX-GUI/CloudSync");
    return request;
  }

  struct ReplyResult final {
    Status status;
    int httpStatus{0};
    std::vector<std::uint8_t> data;
    std::string etag;
  };

  ReplyResult finish(QNetworkReply* reply, std::size_t maximumBytes)
  {
    QEventLoop loop;
    QTimer cancellationTimer;
    cancellationTimer.setInterval(25);
    QObject::connect(&cancellationTimer, &QTimer::timeout, reply, [this, reply] {
      if (cancellation_ && cancellation_()) {
        reply->abort();
      }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    bool overflow = false;
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply,
      [reply, maximumBytes, &overflow] {
      const auto contentLength = reply->header(
        QNetworkRequest::ContentLengthHeader);
      if (contentLength.isValid() &&
          contentLength.toULongLong() > maximumBytes) {
        overflow = true;
        reply->abort();
      }
    });
    QObject::connect(reply, &QIODevice::readyRead, reply,
      [reply, maximumBytes, &overflow] {
      const auto available = reply->bytesAvailable();
      if (available < 0 || static_cast<std::uint64_t>(available) > maximumBytes) {
        overflow = true;
        reply->abort();
      }
    });
    cancellationTimer.start();
    loop.exec();
    cancellationTimer.stop();
    const auto statusAttribute = reply->attribute(
      QNetworkRequest::HttpStatusCodeAttribute);
    const auto httpStatus = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const auto etag = reply->rawHeader("ETag").toStdString();
    const auto body = overflow ? QByteArray{} : reply->readAll();
    const auto networkError = reply->error();
    reply->deleteLater();
    if (overflow || body.size() > static_cast<qsizetype>(maximumBytes)) {
      return {.status = failure(Error::dataTooLarge,
        "The WebDAV response exceeded its fixed byte limit."),
        .httpStatus = httpStatus, .data = {}, .etag = {}};
    }
    if (cancellation_ && cancellation_()) {
      return {.status = failure(Error::cancelled,
        "Cloud synchronization was cancelled."), .httpStatus = httpStatus,
        .data = {}, .etag = {}};
    }
    if (networkError != QNetworkReply::NoError && httpStatus == 0) {
      return {.status = failure(Error::transportFailed,
        "The HTTPS WebDAV request failed: " +
          replyErrorString(networkError)), .httpStatus = 0, .data = {},
          .etag = {}};
    }
    return {
      .status = {},
      .httpStatus = httpStatus,
      .data = {reinterpret_cast<const std::uint8_t*>(body.constData()),
        reinterpret_cast<const std::uint8_t*>(body.constData() + body.size())},
      .etag = etag,
    };
  }

  static std::string replyErrorString(QNetworkReply::NetworkError error)
  {
    switch (error) {
      case QNetworkReply::TimeoutError:
        return "the request timed out";
      case QNetworkReply::SslHandshakeFailedError:
        return "TLS certificate validation failed";
      case QNetworkReply::HostNotFoundError:
        return "the server name could not be resolved";
      case QNetworkReply::ConnectionRefusedError:
        return "the server refused the connection";
      case QNetworkReply::OperationCanceledError:
        return "the request was cancelled";
      default:
        return "network error " + std::to_string(static_cast<int>(error));
    }
  }

  Settings settings_;
  std::string password_;
  Cancellation cancellation_;
  QUrl endpoint_;
  QSslConfiguration sslConfiguration_;
  QNetworkAccessManager manager_;
};

WebDavRemoteStore::WebDavRemoteStore(
  Settings settings, std::string password, Cancellation cancellation,
  std::vector<std::string> additionalTrustedCaDer)
    : private_(std::make_unique<Private>(
        std::move(settings), std::move(password), std::move(cancellation),
        additionalTrustedCaDer))
{
}

WebDavRemoteStore::~WebDavRemoteStore() = default;

Status WebDavRemoteStore::ensureCollection(const std::string& relativePath)
{
  if (!validRelativeRemotePath(relativePath)) {
    return failure(Error::invalidPath, "The remote collection path is invalid.");
  }
  auto request = private_->requestFor(relativePath);
  auto* reply = private_->manager_.sendCustomRequest(request, "MKCOL");
  const auto result = private_->finish(reply, 64U * 1024U);
  if (!result.status) {
    return result.status;
  }
  if (result.httpStatus == 201) {
    return {};
  }
  if (result.httpStatus == 405) {
    auto verification = private_->requestFor(relativePath);
    verification.setRawHeader("Depth", "0");
    auto* verificationReply = private_->manager_.sendCustomRequest(
      verification, "PROPFIND");
    const auto verified = private_->finish(verificationReply, 64U * 1024U);
    if (!verified.status) {
      return verified.status;
    }
    if (verified.httpStatus == 200 || verified.httpStatus == 207) {
      return {};
    }
    return failure(responseError(verified.httpStatus),
      "The existing WebDAV collection could not be verified (HTTP " +
        std::to_string(verified.httpStatus) + ").");
  }
  return failure(responseError(result.httpStatus),
    "The WebDAV server rejected collection creation (HTTP " +
      std::to_string(result.httpStatus) + ").");
}

RemoteReadResult WebDavRemoteStore::read(
  const std::string& relativePath, std::size_t maximumBytes)
{
  if (!validRelativeRemotePath(relativePath) || maximumBytes > maximumTransferBytes) {
    return {.status = failure(Error::invalidPath,
      "The remote read path or byte limit is invalid."), .exists = false,
      .data = {}, .etag = {}};
  }
  auto request = private_->requestFor(relativePath);
  auto* reply = private_->manager_.get(request);
  auto result = private_->finish(reply, maximumBytes);
  if (!result.status) {
    return {.status = std::move(result.status), .exists = false,
      .data = {}, .etag = {}};
  }
  if (result.httpStatus == 404) {
    return {.status = {}, .exists = false, .data = {}, .etag = {}};
  }
  if (result.httpStatus != 200) {
    return {.status = failure(responseError(result.httpStatus),
      "The WebDAV download failed (HTTP " +
        std::to_string(result.httpStatus) + ")."), .exists = false,
        .data = {}, .etag = {}};
  }
  return {.status = {}, .exists = true, .data = std::move(result.data),
    .etag = std::move(result.etag)};
}

RemoteWriteResult WebDavRemoteStore::write(const std::string& relativePath,
  std::span<const std::uint8_t> data, WriteCondition condition,
  const std::string& etag)
{
  if (!validRelativeRemotePath(relativePath) || data.size() > maximumTransferBytes ||
      (condition == WriteCondition::match && etag.empty())) {
    return {.status = failure(Error::invalidPath,
      "The remote write path, data, or precondition is invalid."),
      .preconditionFailed = false, .etag = {}};
  }
  auto request = private_->requestFor(relativePath);
  request.setHeader(QNetworkRequest::ContentTypeHeader,
    QStringLiteral("application/octet-stream"));
  if (condition == WriteCondition::createOnly) {
    request.setRawHeader("If-None-Match", "*");
  } else {
    request.setRawHeader("If-Match", QByteArray::fromStdString(etag));
  }
  const auto body = QByteArray::fromRawData(
    reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()));
  auto* reply = private_->manager_.put(request, body);
  auto result = private_->finish(reply, 64U * 1024U);
  if (!result.status) {
    return {.status = std::move(result.status), .preconditionFailed = false,
      .etag = {}};
  }
  if (result.httpStatus == 412) {
    return {.status = {}, .preconditionFailed = true, .etag = {}};
  }
  if (result.httpStatus != 200 && result.httpStatus != 201 &&
      result.httpStatus != 204) {
    return {.status = failure(responseError(result.httpStatus),
      "The WebDAV upload failed (HTTP " +
        std::to_string(result.httpStatus) + ")."),
      .preconditionFailed = false, .etag = {}};
  }
  return {.status = {}, .preconditionFailed = false,
    .etag = std::move(result.etag)};
}

} // namespace genplusgx::cloud
