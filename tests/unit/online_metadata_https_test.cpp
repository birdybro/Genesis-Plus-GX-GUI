#include "genplusgx/library/online_metadata_client.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QMap>
#include <QMetaObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
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
          if (socket->bytesAvailable() > 0) {
            buffers_[socket] += socket->readAll();
            process(socket);
          }
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

  [[nodiscard]] std::vector<std::string> requestedPaths()
  {
    std::vector<std::string> paths;
    QMetaObject::invokeMethod(this, [this, &paths] { paths = requestedPaths_; },
      Qt::BlockingQueuedConnection);
    return paths;
  }

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
      respond(socket, 400, "Bad Request", "text/plain", {});
      return;
    }
    const auto path = requestLine[1];
    requestedPaths_.push_back(path.toStdString());
    if (path == "/ok") {
      respond(socket, 200, "OK", "application/json", QByteArray{"{\"ok\":true}"});
    } else if (path == "/oversized") {
      respond(socket, 200, "OK", "application/json", QByteArray{64, 'x'});
    } else if (path == "/redirect") {
      const auto location = QByteArray{"https://127.0.0.1:"} +
        QByteArray::number(port_) + "/ok";
      respond(socket, 302, "Found", "text/plain", {}, location);
    } else {
      respond(socket, 404, "Not Found", "text/plain", {});
    }
  }

  static void respond(QSslSocket* socket, int status, const char* reason,
    const char* contentType, const QByteArray& body,
    const QByteArray& location = {})
  {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason +
      "\r\nContent-Length: " + QByteArray::number(body.size()) +
      "\r\nContent-Type: " + contentType + "\r\nConnection: close\r\n";
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
  std::vector<std::string> requestedPaths_;
};

class OnlineMetadataHttpsTest final : public QObject {
  Q_OBJECT

private slots:
  void tlsRedirectAndTransferBoundsAreEnforced();
};

void OnlineMetadataHttpsTest::tlsRedirectAndTransferBoundsAreEnforced()
{
  using namespace genplusgx::library;
  const auto fixtureDirectory = qEnvironmentVariable("GENPLUSGX_TEST_FIXTURES");
  QVERIFY(!fixtureDirectory.isEmpty());
  const auto ca = readFixture(
    fixtureDirectory, QStringLiteral("cloud-test-ca.der"));
  QVERIFY(!ca.isEmpty());
  TestHttpsServer server;
  QVERIFY2(server.start(fixtureDirectory), "The loopback TLS server did not start");
  const auto base = QStringLiteral("https://127.0.0.1:%1").arg(server.port());

  QtOnlineHttpTransport transport{{}, {ca.toStdString()}};
  const auto ok = transport.get((base + QStringLiteral("/ok")).toStdString(), 64U);
  QVERIFY2(ok.status, ok.status.message.c_str());
  QCOMPARE(ok.statusCode, 200);
  QCOMPARE(ok.contentType, std::string{"application/json"});
  QCOMPARE(ok.data, (std::vector<std::uint8_t>{'{', '"', 'o', 'k', '"', ':',
    't', 'r', 'u', 'e', '}'}));

  const auto oversized = transport.get(
    (base + QStringLiteral("/oversized")).toStdString(), 8U);
  QVERIFY(!oversized.status);
  QCOMPARE(oversized.status.error, OnlineMetadataError::dataTooLarge);

  const auto beforeRedirect = server.requestedPaths().size();
  const auto redirected = transport.get(
    (base + QStringLiteral("/redirect")).toStdString(), 64U);
  QVERIFY(redirected.status);
  QCOMPARE(redirected.statusCode, 302);
  const auto paths = server.requestedPaths();
  QCOMPARE(paths.size(), beforeRedirect + 1U);
  QCOMPARE(paths.back(), std::string{"/redirect"});

  const auto insecure = transport.get("http://example.test/data", 64U);
  QVERIFY(!insecure.status);
  QCOMPARE(insecure.status.error, OnlineMetadataError::invalidRequest);
  const auto credentials = transport.get(
    "https://user:secret@example.test/data", 64U);
  QVERIFY(!credentials.status);
  QCOMPARE(credentials.status.error, OnlineMetadataError::invalidRequest);

  QtOnlineHttpTransport untrusted;
  const auto rejected = untrusted.get(
    (base + QStringLiteral("/ok")).toStdString(), 64U);
  QVERIFY(!rejected.status);
  QCOMPARE(rejected.status.error, OnlineMetadataError::transportFailed);
}

} // namespace

QTEST_MAIN(OnlineMetadataHttpsTest)
#include "online_metadata_https_test.moc"
