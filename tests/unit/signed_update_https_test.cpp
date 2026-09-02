#include "genplusgx/updates/update_client.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QHostAddress>
#include <QMap>
#include <QMetaObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <memory>
#include <string>
#include <vector>

namespace {

QByteArray readFixture(const QString& directory, const QString& name)
{
  QFile file{directory + '/' + name};
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

class TestHttpsServer final : public QObject {
  Q_OBJECT

public:
  ~TestHttpsServer() override
  {
    if (!thread_.isRunning()) {
      return;
    }
    auto* applicationThread = QCoreApplication::instance()->thread();
    QMetaObject::invokeMethod(this, [this, applicationThread] {
      server_->close();
      server_.reset();
      moveToThread(applicationThread);
    }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
  }

  bool start(const QString& fixtures)
  {
    const QSslCertificate certificate{
      readFixture(fixtures, QStringLiteral("cloud-test-server.der")), QSsl::Der};
    const QSslKey key{
      readFixture(fixtures, QStringLiteral("cloud-test-server-key.der")),
      QSsl::Rsa, QSsl::Der};
    if (certificate.isNull() || key.isNull()) {
      return false;
    }
    thread_.start();
    moveToThread(&thread_);
    bool started = false;
    QMetaObject::invokeMethod(this, [this, &started, certificate, key] {
      server_ = std::make_unique<QSslServer>();
      auto configuration = QSslConfiguration::defaultConfiguration();
      configuration.setLocalCertificate(certificate);
      configuration.setPrivateKey(key);
      configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
      configuration.setAllowedNextProtocols({
        QSslConfiguration::NextProtocolHttp1_1});
      server_->setSslConfiguration(configuration);
      connect(server_.get(), &QTcpServer::pendingConnectionAvailable,
        this, [this] {
        while (server_->hasPendingConnections()) {
          auto* socket = qobject_cast<QSslSocket*>(server_->nextPendingConnection());
          if (socket == nullptr) {
            continue;
          }
          buffers_.insert(socket, {});
          connect(socket, &QIODevice::readyRead, this, [this, socket] {
            buffers_[socket] += socket->readAll();
            process(socket);
          });
          connect(socket, &QObject::destroyed, this,
            [this, socket] { buffers_.remove(socket); });
        }
      });
      started = server_->listen(QHostAddress::LocalHost);
      if (started) {
        port_ = server_->serverPort();
      }
    }, Qt::BlockingQueuedConnection);
    return started;
  }

  [[nodiscard]] quint16 port() const noexcept { return port_; }

private:
  void process(QSslSocket* socket)
  {
    auto& buffer = buffers_[socket];
    const auto headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
      return;
    }
    const auto requestLine = buffer.left(headerEnd).split('\n').front()
      .trimmed().split(' ');
    if (requestLine.size() < 2) {
      respond(socket, 400, "Bad Request", {});
      return;
    }
    const auto path = requestLine[1];
    if (path == "/small") {
      respond(socket, 200, "OK", "signed-response");
    } else if (path == "/oversized") {
      respond(socket, 200, "OK", QByteArray{128, 'x'});
    } else if (path == "/redirect") {
      const auto location = QByteArray{"https://127.0.0.1:"} +
        QByteArray::number(port_) + "/small?token=fixture";
      respond(socket, 302, "Found", {}, location);
    } else if (path == "/redirect-loop") {
      const auto location = QByteArray{"https://127.0.0.1:"} +
        QByteArray::number(port_) + "/redirect-loop";
      respond(socket, 302, "Found", {}, location);
    } else if (path == "/redirect-untrusted") {
      respond(socket, 302, "Found", {}, "https://example.invalid/payload");
    } else if (path.startsWith("/small?")) {
      respond(socket, 200, "OK", "signed-response");
    } else if (path == "/package") {
      respond(socket, 200, "OK", "verified-package");
    } else if (path == "/package-redirect") {
      const auto location = QByteArray{"https://127.0.0.1:"} +
        QByteArray::number(port_) + "/package";
      respond(socket, 302, "Found", {}, location);
    } else {
      respond(socket, 404, "Not Found", {});
    }
  }

  static void respond(QSslSocket* socket, int status, const char* reason,
    const QByteArray& body, const QByteArray& location = {})
  {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason +
      "\r\nContent-Length: " + QByteArray::number(body.size()) +
      "\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n";
    if (!location.isEmpty()) {
      response += "Location: " + location + "\r\n";
    }
    response += "\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
  }

  QThread thread_;
  std::unique_ptr<QSslServer> server_;
  quint16 port_{0U};
  QMap<QSslSocket*, QByteArray> buffers_;
};

class SignedUpdateHttpsTest final : public QObject {
  Q_OBJECT

private slots:
  void tlsRedirectBoundsAndStreamingVerification();
};

void SignedUpdateHttpsTest::tlsRedirectBoundsAndStreamingVerification()
{
  using namespace genplusgx::updates;
  const auto fixtureDirectory = qEnvironmentVariable("GENPLUSGX_TEST_FIXTURES");
  QVERIFY(!fixtureDirectory.isEmpty());
  const auto ca = readFixture(fixtureDirectory, QStringLiteral("cloud-test-ca.der"));
  QVERIFY(!ca.isEmpty());
  TestHttpsServer server;
  QVERIFY2(server.start(fixtureDirectory), "The loopback TLS server did not start");
  const auto base = QStringLiteral("https://127.0.0.1:%1").arg(server.port());
  Trust trust{.publicKeyHex = std::string(64, '0'), .keyId = "fixture",
    .manifestUrl = (base + "/small").toStdString(),
    .signatureUrl = (base + "/small").toStdString(),
    .repositoryUrl = base.toStdString(), .allowedHosts = {"127.0.0.1"}};
  trust.allowNonDefaultHttpsPorts = true;

  QtHttpTransport transport{{}, {ca.toStdString()}};
  const auto fetched = transport.get((base + "/small").toStdString(), 64U, trust);
  QVERIFY2(fetched.status, fetched.status.message.c_str());
  QCOMPARE(fetched.data, std::vector<std::uint8_t>({'s','i','g','n','e','d','-',
    'r','e','s','p','o','n','s','e'}));
  const auto oversized = transport.get(
    (base + "/oversized").toStdString(), 16U, trust);
  QCOMPARE(oversized.status.error, Error::responseTooLarge);
  const auto redirected = transport.get(
    (base + "/redirect").toStdString(), 64U, trust);
  QVERIFY2(redirected.status, redirected.status.message.c_str());
  QCOMPARE(redirected.data, fetched.data);
  QCOMPARE(transport.get((base + "/redirect-loop").toStdString(), 64U, trust)
      .status.error, Error::redirectRejected);
  QCOMPARE(transport.get((base + "/redirect-untrusted").toStdString(), 64U,
      trust).status.error, Error::redirectRejected);
  QCOMPARE(transport.get("http://127.0.0.1/file", 64U, trust).status.error,
    Error::invalidRequest);

  const QByteArray package{"verified-package"};
  const auto digest = QCryptographicHash::hash(package, QCryptographicHash::Sha256)
    .toHex().toStdString();
  Asset asset{.platform = "linux", .architecture = "x86_64",
    .format = "tar.gz", .fileName = "fixture.tar.gz",
    .url = (base + "/package").toStdString(), .sha256 = digest,
    .size = static_cast<std::uint64_t>(package.size())};
  QTemporaryDir destination;
  QVERIFY(destination.isValid());
  const auto downloaded = transport.download(asset,
    destination.path().toStdString(), trust);
  QVERIFY2(downloaded.status, downloaded.status.message.c_str());
  QFile installed{QString::fromStdString(downloaded.path.string())};
  QVERIFY(installed.open(QIODevice::ReadOnly));
  QCOMPARE(installed.readAll(), package);

  asset.url = (base + "/package-redirect").toStdString();
  asset.fileName = "redirected.tar.gz";
  const auto redirectedDownload = transport.download(asset,
    destination.path().toStdString(), trust);
  QVERIFY2(redirectedDownload.status,
    redirectedDownload.status.message.c_str());
  QFile redirectedPackage{QString::fromStdString(
    redirectedDownload.path.string())};
  QVERIFY(redirectedPackage.open(QIODevice::ReadOnly));
  QCOMPARE(redirectedPackage.readAll(), package);

  asset.url = (base + "/package").toStdString();
  asset.sha256 = std::string(64, '0');
  asset.fileName = "rejected.tar.gz";
  QFile existing{destination.path() + "/rejected.tar.gz"};
  QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(existing.write("existing-package"), qint64{16});
  existing.close();
  const auto rejected = transport.download(asset,
    destination.path().toStdString(), trust);
  QCOMPARE(rejected.status.error, Error::hashMismatch);
  QVERIFY(existing.open(QIODevice::ReadOnly));
  QCOMPARE(existing.readAll(), QByteArray{"existing-package"});
  existing.close();

  asset.sha256 = digest;
  asset.fileName = "../escaped.tar.gz";
  const auto unsafe = transport.download(asset,
    destination.path().toStdString(), trust);
  QCOMPARE(unsafe.status.error, Error::invalidRequest);
  QVERIFY(!QFile::exists(destination.path() + "/../escaped.tar.gz"));

  asset.fileName = "invalid-digest.tar.gz";
  asset.sha256 = std::string(64, 'A');
  QCOMPARE(transport.download(asset, destination.path().toStdString(), trust)
      .status.error, Error::invalidRequest);

  QtHttpTransport cancelled{[] { return true; }, {ca.toStdString()}};
  QCOMPARE(cancelled.get((base + "/small").toStdString(), 64U, trust).status.error,
    Error::cancelled);
  QtHttpTransport untrusted;
  QCOMPARE(untrusted.get((base + "/small").toStdString(), 64U, trust).status.error,
    Error::network);
}

} // namespace

QTEST_MAIN(SignedUpdateHttpsTest)
#include "signed_update_https_test.moc"
