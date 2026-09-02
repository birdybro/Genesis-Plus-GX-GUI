#include "genplusgx/updates/update_client.h"

#include "genplusgx/updates/update_manifest.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <system_error>
#include <utility>

namespace genplusgx::updates {
namespace {

constexpr int transferTimeoutMilliseconds = 30'000;
constexpr int downloadDeadlineMilliseconds = 30 * 60 * 1'000;
constexpr int maximumRedirects = 5;

Status failure(Error error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

HttpResult httpFailure(Status status, int statusCode = 0)
{
  return {.status = std::move(status), .statusCode = statusCode, .data = {}};
}

DownloadResult downloadFailure(Status status, const Asset& asset)
{
  return {.status = std::move(status), .path = {}, .asset = asset};
}

CheckResult checkFailure(Status status)
{
  return {.status = std::move(status), .manifest = {}, .asset = std::nullopt,
    .updateAvailable = false};
}

bool cancelled(const Cancellation& cancellation)
{
  return cancellation && cancellation();
}

QNetworkRequest requestFor(const QUrl& url,
  const std::vector<std::string>& trustedCaDer)
{
  QNetworkRequest request{url};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
    QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(transferTimeoutMilliseconds);
  request.setRawHeader("User-Agent", "Genesis-Plus-GX-GUI-Signed-Updater/1");
  request.setRawHeader("Accept", "application/octet-stream, application/json");
  if (!trustedCaDer.empty()) {
    auto configuration = QSslConfiguration::defaultConfiguration();
    auto certificates = configuration.caCertificates();
    for (const auto& bytes : trustedCaDer) {
      certificates.append(QSslCertificate{
        QByteArray::fromStdString(bytes), QSsl::Der});
    }
    configuration.setCaCertificates(certificates);
    request.setSslConfiguration(configuration);
  }
  return request;
}

std::optional<QUrl> redirectTarget(QNetworkReply& reply)
{
  const auto target = reply.attribute(QNetworkRequest::RedirectionTargetAttribute);
  if (!target.isValid()) {
    return std::nullopt;
  }
  return reply.url().resolved(target.toUrl());
}

bool successStatus(int status)
{
  return status >= 200 && status < 300;
}

bool safeAssetIdentity(const Asset& asset)
{
  const bool safeName = !asset.fileName.empty() && asset.fileName.size() <= 255U &&
    asset.fileName != "." && asset.fileName != ".." &&
    asset.fileName.find('/') == std::string::npos &&
    asset.fileName.find('\\') == std::string::npos &&
    asset.fileName.find('\0') == std::string::npos;
  const bool validDigest = asset.sha256.size() == 64U &&
    std::ranges::all_of(asset.sha256, [](unsigned char value) {
      return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    });
  return safeName && validDigest;
}

QString pathToQString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

} // namespace

class QtHttpTransport::Private final {
public:
  Private(Cancellation cancellation, std::vector<std::string> ca)
    : cancellation_(std::move(cancellation)), additionalTrustedCaDer_(std::move(ca))
  {
  }

  HttpResult get(const std::string& initialUrl, std::size_t maximumBytes,
    const Trust& trust)
  {
    if (maximumBytes == 0U || !validateTrustedUrl(initialUrl, trust)) {
      return httpFailure(failure(Error::invalidRequest,
        "The update request URL or response limit is invalid."));
    }
    QNetworkAccessManager manager;
    QUrl url{QString::fromStdString(initialUrl), QUrl::StrictMode};
    for (int redirect = 0; redirect <= maximumRedirects; ++redirect) {
      if (cancelled(cancellation_)) {
        return httpFailure(failure(Error::cancelled,
          "The update request was cancelled."));
      }
      auto* reply = manager.get(requestFor(url, additionalTrustedCaDer_));
      QEventLoop loop;
      QTimer cancellationTimer;
      QTimer deadlineTimer;
      QByteArray bytes;
      bool oversized = false;
      bool deadlineExpired = false;
      const auto consume = [&] {
        const auto chunk = reply->readAll();
        if (chunk.isEmpty() || oversized) {
          return;
        }
        if (bytes.size() + chunk.size() > static_cast<qsizetype>(maximumBytes)) {
          oversized = true;
          bytes.clear();
          reply->abort();
          return;
        }
        bytes.append(chunk);
      };
      QObject::connect(reply, &QNetworkReply::readyRead, reply, consume);
      cancellationTimer.setInterval(20);
      QObject::connect(&cancellationTimer, &QTimer::timeout, reply,
        [this, reply] {
          if (cancelled(cancellation_)) {
            reply->abort();
          }
        });
      QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
      deadlineTimer.setSingleShot(true);
      QObject::connect(&deadlineTimer, &QTimer::timeout, reply,
        [&deadlineExpired, reply] {
          deadlineExpired = true;
          reply->abort();
        });
      cancellationTimer.start();
      deadlineTimer.start(transferTimeoutMilliseconds);
      loop.exec();
      deadlineTimer.stop();
      cancellationTimer.stop();
      consume();
      const auto statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const auto next = redirectTarget(*reply);
      if (next) {
        reply->deleteLater();
        if (redirect == maximumRedirects ||
            !validateTrustedUrl(next->toString(QUrl::FullyEncoded).toStdString(),
              trust, true)) {
          return httpFailure(failure(Error::redirectRejected,
            "The update server returned an untrusted or excessive redirect."),
            statusCode);
        }
        url = *next;
        continue;
      }
      if (cancelled(cancellation_)) {
        reply->deleteLater();
        return httpFailure(failure(Error::cancelled,
          "The update request was cancelled."), statusCode);
      }
      if (deadlineExpired) {
        reply->deleteLater();
        return httpFailure(failure(Error::network,
          "The update server request timed out."), statusCode);
      }
      if (oversized) {
        reply->deleteLater();
        return httpFailure(failure(Error::responseTooLarge,
          "The update server response exceeds its size limit."), statusCode);
      }
      if (reply->error() != QNetworkReply::NoError || !successStatus(statusCode)) {
        const auto detail = reply->errorString().toStdString();
        reply->deleteLater();
        return httpFailure(failure(Error::network,
          "The update server request failed: " + detail), statusCode);
      }
      const auto declared = reply->header(QNetworkRequest::ContentLengthHeader);
      if (declared.isValid() && declared.toLongLong() >
          static_cast<qint64>(maximumBytes)) {
        reply->deleteLater();
        return httpFailure(failure(Error::responseTooLarge,
          "The update server response exceeds its size limit."), statusCode);
      }
      reply->deleteLater();
      if (bytes.size() <= 0 || bytes.size() > static_cast<qsizetype>(maximumBytes)) {
        return httpFailure(failure(Error::responseTooLarge,
          "The update server response is empty or exceeds its size limit."),
          statusCode);
      }
      return {.status = {}, .statusCode = statusCode,
        .data = std::vector<std::uint8_t>(bytes.begin(), bytes.end())};
    }
    return httpFailure(failure(Error::redirectRejected,
      "The update server returned too many redirects."));
  }

  DownloadResult download(const Asset& asset,
    const std::filesystem::path& destinationDirectory, const Trust& trust)
  {
    if (asset.size == 0U || asset.size > maximumPackageBytes ||
        destinationDirectory.empty() || !safeAssetIdentity(asset) ||
        !validateTrustedUrl(asset.url, trust)) {
      return downloadFailure(failure(Error::invalidRequest,
        "The update download request is invalid."), asset);
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(destinationDirectory, filesystemError);
    if (filesystemError || !std::filesystem::is_directory(
          destinationDirectory, filesystemError)) {
      return downloadFailure(failure(Error::io,
        "The update download directory could not be created."), asset);
    }
    const auto path = destinationDirectory / asset.fileName;
    QSaveFile output{pathToQString(path)};
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
      return downloadFailure(failure(Error::io,
        "The update package could not be opened for an atomic write."), asset);
    }

    QNetworkAccessManager manager;
    QUrl url{QString::fromStdString(asset.url), QUrl::StrictMode};
    for (int redirect = 0; redirect <= maximumRedirects; ++redirect) {
      if (cancelled(cancellation_)) {
        output.cancelWriting();
        return downloadFailure(failure(Error::cancelled,
          "The update download was cancelled."), asset);
      }
      auto* reply = manager.get(requestFor(url, additionalTrustedCaDer_));
      QEventLoop loop;
      QTimer cancellationTimer;
      QTimer deadlineTimer;
      QCryptographicHash hash{QCryptographicHash::Sha256};
      std::uint64_t received = 0U;
      bool writeFailed = false;
      bool oversized = false;
      bool deadlineExpired = false;
      const auto consume = [&] {
        const auto bytes = reply->readAll();
        if (bytes.isEmpty() || writeFailed || oversized) {
          return;
        }
        received += static_cast<std::uint64_t>(bytes.size());
        if (received > asset.size || received > maximumPackageBytes) {
          oversized = true;
          reply->abort();
          return;
        }
        hash.addData(bytes);
        writeFailed = output.write(bytes) != bytes.size();
        if (writeFailed) {
          reply->abort();
        }
      };
      QObject::connect(reply, &QNetworkReply::readyRead, reply, consume);
      QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
      cancellationTimer.setInterval(20);
      QObject::connect(&cancellationTimer, &QTimer::timeout, reply,
        [this, reply] {
          if (cancelled(cancellation_)) {
            reply->abort();
          }
        });
      deadlineTimer.setSingleShot(true);
      QObject::connect(&deadlineTimer, &QTimer::timeout, reply,
        [&deadlineExpired, reply] {
          deadlineExpired = true;
          reply->abort();
        });
      cancellationTimer.start();
      deadlineTimer.start(downloadDeadlineMilliseconds);
      loop.exec();
      deadlineTimer.stop();
      cancellationTimer.stop();
      consume();
      const auto statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const auto next = redirectTarget(*reply);
      if (next) {
        reply->deleteLater();
        if (received != 0U || redirect == maximumRedirects ||
            !validateTrustedUrl(next->toString(QUrl::FullyEncoded).toStdString(),
              trust, true)) {
          output.cancelWriting();
          return downloadFailure(failure(Error::redirectRejected,
            "The update package server returned an untrusted redirect."), asset);
        }
        url = *next;
        continue;
      }
      const auto replyError = reply->error();
      const auto detail = reply->errorString().toStdString();
      reply->deleteLater();
      if (cancelled(cancellation_)) {
        output.cancelWriting();
        return downloadFailure(failure(Error::cancelled,
          "The update download was cancelled."), asset);
      }
      if (deadlineExpired) {
        output.cancelWriting();
        return downloadFailure(failure(Error::network,
          "The update package download timed out."), asset);
      }
      if (writeFailed) {
        output.cancelWriting();
        return downloadFailure(failure(Error::io,
          "The update package could not be written."), asset);
      }
      if (oversized || received != asset.size) {
        output.cancelWriting();
        return downloadFailure(failure(Error::responseTooLarge,
          "The update package size does not match the signed manifest."), asset);
      }
      if (replyError != QNetworkReply::NoError || !successStatus(statusCode)) {
        output.cancelWriting();
        return downloadFailure(failure(Error::network,
          "The update package download failed: " + detail), asset);
      }
      if (hash.result().toHex().toStdString() != asset.sha256) {
        output.cancelWriting();
        return downloadFailure(failure(Error::hashMismatch,
          "The downloaded package does not match the signed SHA-256 digest."), asset);
      }
      if (!output.commit()) {
        return downloadFailure(failure(Error::io,
          "The verified update package could not be committed atomically."), asset);
      }
      return {.status = {}, .path = path, .asset = asset};
    }
    output.cancelWriting();
    return downloadFailure(failure(Error::redirectRejected,
      "The update package server returned too many redirects."), asset);
  }

private:
  Cancellation cancellation_;
  std::vector<std::string> additionalTrustedCaDer_;
};

QtHttpTransport::QtHttpTransport(Cancellation cancellation,
  std::vector<std::string> additionalTrustedCaDer)
  : private_(std::make_unique<Private>(std::move(cancellation),
      std::move(additionalTrustedCaDer)))
{
}

QtHttpTransport::~QtHttpTransport() = default;

HttpResult QtHttpTransport::get(const std::string& url,
  std::size_t maximumBytes, const Trust& trust)
{
  return private_->get(url, maximumBytes, trust);
}

DownloadResult QtHttpTransport::download(const Asset& asset,
  const std::filesystem::path& destinationDirectory, const Trust& trust)
{
  return private_->download(asset, destinationDirectory, trust);
}

CheckResult checkForUpdate(const Settings& settings,
  std::string_view currentVersion, const Trust& trust, HttpTransport& transport)
{
  if (!validateSettings(settings)) {
    return checkFailure(failure(Error::invalidSettings,
      "The update settings are invalid."));
  }
  const auto current = parseSemanticVersion(currentVersion);
  if (!current) {
    return checkFailure(failure(Error::invalidRequest,
      "The running application version is invalid."));
  }
  const auto manifestResponse = transport.get(
    trust.manifestUrl, maximumManifestBytes, trust);
  if (!manifestResponse.status) {
    return checkFailure(manifestResponse.status);
  }
  const auto signatureResponse = transport.get(trust.signatureUrl, 96U, trust);
  if (!signatureResponse.status) {
    return checkFailure(signatureResponse.status);
  }
  auto verified = verifyAndParseManifest(manifestResponse.data,
    signatureResponse.data, trust);
  if (!verified.status) {
    return checkFailure(verified.status);
  }
  if (!settings.highestSeenVersion.empty()) {
    const auto highest = parseSemanticVersion(settings.highestSeenVersion);
    if (!highest || verified.manifest.version < *highest) {
      return checkFailure(failure(Error::rollbackDetected,
        "The signed release is older than a previously verified release."));
    }
  }
  const bool available = verified.manifest.version > *current;
  auto asset = selectAsset(verified.manifest, currentPlatform(),
    currentArchitecture());
  if (available && !asset) {
    return {.status = failure(Error::unsupportedPlatform,
      "This signed release does not contain a package for this platform."),
      .manifest = std::move(verified.manifest), .asset = std::nullopt,
      .updateAvailable = false};
  }
  return {.status = {}, .manifest = std::move(verified.manifest),
    .asset = std::move(asset), .updateAvailable = available};
}

} // namespace genplusgx::updates
