#include "genplusgx/cloud/cloud_remote.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QMap>
#include <QPointer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTest>
#include <QThread>

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

QByteArray readFixture(const QString& directory, const QString& name)
{
  QFile file{directory + '/' + name};
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

class TestWebDavServer final : public QObject {
  Q_OBJECT

public:
  ~TestWebDavServer() override
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

  [[nodiscard]] quint16 port() const { return port_; }
  [[nodiscard]] std::string lastAuthorization()
  {
    std::string result;
    QMetaObject::invokeMethod(this, [this, &result] {
      result = lastAuthorization_;
    }, Qt::BlockingQueuedConnection);
    return result;
  }

private:
  void process(QSslSocket* socket)
  {
    auto& buffer = buffers_[socket];
    const auto headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
      return;
    }
    const auto header = buffer.left(headerEnd);
    const auto lines = header.split('\n');
    if (lines.empty()) {
      respond(socket, 400, "Bad Request", {});
      return;
    }
    const auto requestLine = lines.front().trimmed().split(' ');
    if (requestLine.size() < 2) {
      respond(socket, 400, "Bad Request", {});
      return;
    }
    QMap<QByteArray, QByteArray> headers;
    for (qsizetype index = 1; index < lines.size(); ++index) {
      const auto line = lines[index].trimmed();
      const auto separator = line.indexOf(':');
      if (separator > 0) {
        headers.insert(line.left(separator).toLower(),
          line.mid(separator + 1).trimmed());
      }
    }
    bool lengthOk = false;
    const auto contentLength = headers.value("content-length", "0")
      .toLongLong(&lengthOk);
    if (!lengthOk || contentLength < 0 || contentLength > 1024 * 1024) {
      respond(socket, 400, "Bad Request", {});
      return;
    }
    const auto bodyStart = headerEnd + 4;
    if (buffer.size() - bodyStart < contentLength) {
      return;
    }
    const auto method = requestLine[0];
    const auto path = requestLine[1];
    const auto body = buffer.mid(bodyStart, contentLength);
    lastAuthorization_ = headers.value("authorization").toStdString();
    if (headers.value("authorization") != "Basic dGVzdGVyOnNlY3JldA==") {
      respond(socket, 401, "Unauthorized", {});
      return;
    }
    const auto key = path.toStdString();
    if (method == "MKCOL") {
      if (collections_.contains(key)) {
        respond(socket, 405, "Method Not Allowed", {});
      } else {
        collections_.insert(key);
        respond(socket, 201, "Created", {});
      }
      return;
    }
    if (method == "PROPFIND") {
      respond(socket, collections_.contains(key) ? 207 : 404,
        collections_.contains(key) ? "Multi-Status" : "Not Found",
        collections_.contains(key)
          ? QByteArray{"<?xml version=\"1.0\"?><multistatus xmlns=\"DAV:\"/>"}
          : QByteArray{});
      return;
    }
    if (method == "GET") {
      const auto found = files_.find(key);
      if (found == files_.end()) {
        respond(socket, 404, "Not Found", {});
      } else {
        const QByteArray responseBody{
          reinterpret_cast<const char*>(found->second.data()),
          static_cast<qsizetype>(found->second.size())};
        respond(socket, 200, "OK", responseBody,
          QByteArray::fromStdString(etags_[key]));
      }
      return;
    }
    if (method == "PUT") {
      const bool exists = files_.contains(key);
      const auto noneMatch = headers.value("if-none-match");
      const auto match = headers.value("if-match");
      if ((noneMatch == "*" && exists) ||
          (!match.isEmpty() && (!exists || match.toStdString() != etags_[key]))) {
        respond(socket, 412, "Precondition Failed", {});
        return;
      }
      files_[key] = {body.begin(), body.end()};
      etags_[key] = '"' + std::to_string(++revision_) + '"';
      respond(socket, exists ? 204 : 201, exists ? "No Content" : "Created", {},
        QByteArray::fromStdString(etags_[key]));
      return;
    }
    respond(socket, 405, "Method Not Allowed", {});
  }

  static void respond(QSslSocket* socket, int status, const char* reason,
    const QByteArray& body, const QByteArray& etag = {})
  {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason +
      "\r\nContent-Length: " + QByteArray::number(body.size()) +
      "\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n";
    if (!etag.isEmpty()) {
      response += "ETag: " + etag + "\r\n";
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
  std::set<std::string> collections_;
  std::map<std::string, std::vector<std::uint8_t>> files_;
  std::map<std::string, std::string> etags_;
  std::uint64_t revision_{0U};
  std::string lastAuthorization_;
};

class CloudWebDavTest final : public QObject {
  Q_OBJECT

private slots:
  void realTlsWebDavRequestsAreConditionalAndBounded();
};

void CloudWebDavTest::realTlsWebDavRequestsAreConditionalAndBounded()
{
  const auto fixtureDirectory = qEnvironmentVariable("GENPLUSGX_TEST_FIXTURES");
  QVERIFY(!fixtureDirectory.isEmpty());
  const auto ca = readFixture(fixtureDirectory, QStringLiteral("cloud-test-ca.der"));
  QVERIFY(!ca.isEmpty());
  TestWebDavServer server;
  QVERIFY2(server.start(fixtureDirectory), "The loopback TLS server did not start");

  genplusgx::cloud::Settings settings;
  settings.enabled = true;
  settings.endpoint = QStringLiteral("https://127.0.0.1:%1/webdav")
    .arg(server.port()).toStdString();
  settings.username = "tester";
  genplusgx::cloud::WebDavRemoteStore remote{
    settings, "secret", {}, {ca.toStdString()}};

  const auto firstCollection = remote.ensureCollection("Genesis-Plus-GX-GUI");
  QVERIFY2(firstCollection, firstCollection.message.c_str());
  QVERIFY(remote.ensureCollection("Genesis-Plus-GX-GUI"));
  QVERIFY(remote.ensureCollection("Genesis-Plus-GX-GUI/objects"));
  QCOMPARE(server.lastAuthorization(), std::string{"Basic dGVzdGVyOnNlY3JldA=="});

  const std::vector<std::uint8_t> object{'b', 'o', 'u', 'n', 'd', 'e', 'd'};
  auto written = remote.write("Genesis-Plus-GX-GUI/objects/object.bin", object,
    genplusgx::cloud::WriteCondition::createOnly);
  QVERIFY(written.status);
  QVERIFY(!written.preconditionFailed);
  written = remote.write("Genesis-Plus-GX-GUI/objects/object.bin", object,
    genplusgx::cloud::WriteCondition::createOnly);
  QVERIFY(written.status);
  QVERIFY(written.preconditionFailed);

  const auto loaded = remote.read(
    "Genesis-Plus-GX-GUI/objects/object.bin", object.size());
  QVERIFY(loaded.status);
  QVERIFY(loaded.exists);
  QCOMPARE(loaded.data, object);
  QVERIFY(!loaded.etag.empty());

  const std::vector<std::uint8_t> manifest{'{', '}'};
  written = remote.write("Genesis-Plus-GX-GUI/manifest.json", manifest,
    genplusgx::cloud::WriteCondition::createOnly);
  QVERIFY(written.status);
  QVERIFY(!written.preconditionFailed);
  const auto manifestEtag = written.etag;
  QVERIFY(!manifestEtag.empty());
  written = remote.write("Genesis-Plus-GX-GUI/manifest.json", manifest,
    genplusgx::cloud::WriteCondition::match, "\"wrong\"");
  QVERIFY(written.status);
  QVERIFY(written.preconditionFailed);
  written = remote.write("Genesis-Plus-GX-GUI/manifest.json", manifest,
    genplusgx::cloud::WriteCondition::match, manifestEtag);
  QVERIFY(written.status);
  QVERIFY(!written.preconditionFailed);

  const auto bounded = remote.read(
    "Genesis-Plus-GX-GUI/objects/object.bin", 2U);
  QVERIFY(!bounded.status);
  QCOMPARE(bounded.status.error, genplusgx::cloud::Error::dataTooLarge);
  QVERIFY(!remote.read("../escape", 16U).status);

  genplusgx::cloud::WebDavRemoteStore wrongPassword{
    settings, "wrong", {}, {ca.toStdString()}};
  const auto denied = wrongPassword.ensureCollection("Genesis-Plus-GX-GUI");
  QVERIFY(!denied);
  QCOMPARE(denied.error, genplusgx::cloud::Error::authenticationFailed);

  genplusgx::cloud::WebDavRemoteStore untrusted{settings, "secret"};
  const auto tlsRejected = untrusted.ensureCollection("Genesis-Plus-GX-GUI");
  QVERIFY(!tlsRejected);
  QCOMPARE(tlsRejected.error, genplusgx::cloud::Error::transportFailed);
}

} // namespace

QTEST_MAIN(CloudWebDavTest)
#include "cloud_webdav_test.moc"
