#pragma once

#include "genplusgx/netplay/netplay_protocol.h"

#include <QHostAddress>
#include <QObject>
#include <QTimer>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

class QTcpServer;
class QTcpSocket;

namespace genplusgx::netplay {

struct NetplaySessionStatus final {
  NetplaySessionError error{NetplaySessionError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == NetplaySessionError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct NetplaySessionMetrics final {
  NetplaySessionState state{NetplaySessionState::disconnected};
  std::uint64_t sentPackets{0U};
  std::uint64_t receivedPackets{0U};
  std::uint64_t sentBytes{0U};
  std::uint64_t receivedBytes{0U};
  std::uint64_t authenticationFailures{0U};
  std::uint64_t protocolFailures{0U};
};

class NetplaySession final : public QObject {
  Q_OBJECT

public:
  explicit NetplaySession(QObject* parent = nullptr);
  ~NetplaySession() override;

  NetplaySession(const NetplaySession&) = delete;
  NetplaySession& operator=(const NetplaySession&) = delete;

  [[nodiscard]] NetplaySessionStatus host(
    NetplaySessionDescriptor descriptor,
    std::string sessionCode,
    std::uint16_t port = defaultPort,
    std::uint32_t inputDelayFrames = 2U,
    std::uint32_t rollbackFrames = 8U,
    QHostAddress address = QHostAddress::AnyIPv4);
  [[nodiscard]] NetplaySessionStatus join(
    const QString& hostName,
    NetplaySessionDescriptor descriptor,
    std::string sessionCode,
    std::uint16_t port = defaultPort,
    std::uint32_t inputDelayFrames = 2U,
    std::uint32_t rollbackFrames = 8U);
  void disconnectFromPeer(const std::string& reason = {});
  [[nodiscard]] NetplaySessionStatus sendInput(NetplayInputFrame frame);

  [[nodiscard]] NetplaySessionState state() const noexcept { return state_; }
  [[nodiscard]] std::uint16_t listeningPort() const noexcept;
  [[nodiscard]] NetplaySessionMetrics metrics() const noexcept;
  [[nodiscard]] const NetplayConfiguration& configuration() const noexcept
  {
    return configuration_;
  }

signals:
  void stateChanged(genplusgx::netplay::NetplaySessionState state);
  void peerConnected(genplusgx::netplay::NetplayConfiguration configuration);
  void peerDisconnected(QString reason);
  void inputReceived(genplusgx::netplay::NetplayInputFrame frame);
  void sessionError(
    genplusgx::netplay::NetplaySessionError error,
    QString detail);

private:
  void setState(NetplaySessionState state);
  void acceptConnection();
  void socketConnected();
  void socketReadyRead();
  void socketDisconnected();
  void socketError();
  void handshakeTimedOut();
  void processPacket(const QByteArray& packet);
  void processHostPacket(const QByteArray& packet);
  void processGuestPacket(const QByteArray& packet);
  void processConnectedPacket(const QByteArray& packet);
  void fail(NetplaySessionError error, std::string detail, bool notifyPeer);
  [[nodiscard]] bool sendPacket(const QByteArray& payload);
  void attachSocket(QTcpSocket* socket);
  void clearSecrets() noexcept;
  void resetTransport() noexcept;
  [[nodiscard]] std::array<std::uint8_t, nonceBytes> randomNonce() const;

  std::unique_ptr<QTcpServer> server_;
  QTcpSocket* socket_{nullptr};
  PacketFramer framer_;
  QTimer handshakeTimer_;
  NetplaySessionState state_{NetplaySessionState::disconnected};
  NetplaySessionDescriptor descriptor_;
  NetplayConfiguration configuration_;
  QByteArray sessionCode_;
  QByteArray sessionKey_;
  std::array<std::uint8_t, nonceBytes> hostNonce_{};
  std::array<std::uint8_t, nonceBytes> guestNonce_{};
  std::uint64_t sendSequence_{0U};
  std::uint64_t receiveSequence_{0U};
  std::uint64_t sentPackets_{0U};
  std::uint64_t receivedPackets_{0U};
  std::uint64_t sentBytes_{0U};
  std::uint64_t receivedBytes_{0U};
  std::uint64_t authenticationFailures_{0U};
  std::uint64_t protocolFailures_{0U};
  bool intentionalDisconnect_{false};
  bool failureInProgress_{false};
};

} // namespace genplusgx::netplay

Q_DECLARE_METATYPE(genplusgx::netplay::NetplayInputFrame)
Q_DECLARE_METATYPE(genplusgx::netplay::NetplayConfiguration)
Q_DECLARE_METATYPE(genplusgx::netplay::NetplaySessionState)
Q_DECLARE_METATYPE(genplusgx::netplay::NetplaySessionError)
