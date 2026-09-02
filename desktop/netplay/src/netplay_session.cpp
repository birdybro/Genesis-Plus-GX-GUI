#include "genplusgx/netplay/netplay_session.h"

#include <QAbstractSocket>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <limits>
#include <utility>

namespace genplusgx::netplay {
namespace {

constexpr int handshakeTimeoutMilliseconds = 10'000;
constexpr std::size_t minimumSessionCodeBytes = 6U;
constexpr std::size_t maximumSessionCodeBytes = 128U;
constexpr qsizetype maximumHostNameCharacters = 255;

NetplaySessionStatus failure(NetplaySessionError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

bool validSessionCode(const std::string& value) noexcept
{
  return value.size() >= minimumSessionCodeBytes &&
    value.size() <= maximumSessionCodeBytes;
}

std::string socketMessage(const QAbstractSocket& socket)
{
  const auto message = socket.errorString().toStdString();
  return message.empty() ? "The peer connection failed." : message;
}

} // namespace

NetplaySession::NetplaySession(QObject* parent)
  : QObject(parent), server_(std::make_unique<QTcpServer>())
{
  handshakeTimer_.setSingleShot(true);
  connect(&handshakeTimer_, &QTimer::timeout,
    this, &NetplaySession::handshakeTimedOut);
  connect(server_.get(), &QTcpServer::newConnection,
    this, &NetplaySession::acceptConnection);
}

NetplaySession::~NetplaySession()
{
  intentionalDisconnect_ = true;
  resetTransport();
  clearSecrets();
}

NetplaySessionStatus NetplaySession::host(
  NetplaySessionDescriptor descriptor,
  std::string sessionCode,
  std::uint16_t port,
  std::uint32_t inputDelayFrames,
  std::uint32_t rollbackFrames,
  QHostAddress address)
{
  if (state_ != NetplaySessionState::disconnected) {
    return failure(NetplaySessionError::invalidConfiguration,
      "Disconnect the current netplay session before hosting another one.");
  }
  NetplayConfiguration configuration{
    .role = NetplayRole::host,
    .localPlayer = 0U,
    .remotePlayer = 1U,
    .inputDelayFrames = inputDelayFrames,
    .rollbackFrames = rollbackFrames,
  };
  if (!descriptor.valid() || !configuration.valid() ||
      !validSessionCode(sessionCode)) {
    return failure(NetplaySessionError::invalidConfiguration,
      "The game identity, session code, delay, or rollback window is invalid.");
  }
  descriptor_ = std::move(descriptor);
  configuration_ = configuration;
  sessionCode_ = QByteArray::fromStdString(sessionCode);
  std::ranges::fill(sessionCode, '\0');
  intentionalDisconnect_ = false;
  if (!server_->listen(address, port)) {
    const auto detail = server_->errorString().toStdString();
    clearSecrets();
    return failure(NetplaySessionError::listenFailed,
      detail.empty() ? "The netplay listen socket could not be opened." : detail);
  }
  setState(NetplaySessionState::listening);
  return {};
}

NetplaySessionStatus NetplaySession::join(
  const QString& hostName,
  NetplaySessionDescriptor descriptor,
  std::string sessionCode,
  std::uint16_t port,
  std::uint32_t inputDelayFrames,
  std::uint32_t rollbackFrames)
{
  if (state_ != NetplaySessionState::disconnected) {
    return failure(NetplaySessionError::invalidConfiguration,
      "Disconnect the current netplay session before joining another one.");
  }
  NetplayConfiguration configuration{
    .role = NetplayRole::guest,
    .localPlayer = 1U,
    .remotePlayer = 0U,
    .inputDelayFrames = inputDelayFrames,
    .rollbackFrames = rollbackFrames,
  };
  const auto normalizedHost = hostName.trimmed();
  if (normalizedHost.isEmpty() ||
      normalizedHost.size() > maximumHostNameCharacters || !descriptor.valid() ||
      !configuration.valid() || !validSessionCode(sessionCode)) {
    return failure(NetplaySessionError::invalidConfiguration,
      "The host, game identity, session code, delay, or rollback window is invalid.");
  }
  descriptor_ = std::move(descriptor);
  configuration_ = configuration;
  sessionCode_ = QByteArray::fromStdString(sessionCode);
  std::ranges::fill(sessionCode, '\0');
  intentionalDisconnect_ = false;
  auto* socket = new QTcpSocket(this);
  attachSocket(socket);
  setState(NetplaySessionState::connecting);
  handshakeTimer_.start(handshakeTimeoutMilliseconds);
  socket->connectToHost(normalizedHost, port);
  return {};
}

void NetplaySession::disconnectFromPeer(const std::string& reason)
{
  const bool wasActive = state_ != NetplaySessionState::disconnected;
  intentionalDisconnect_ = true;
  resetTransport();
  clearSecrets();
  setState(NetplaySessionState::disconnected);
  if (wasActive) {
    emit peerDisconnected(QString::fromStdString(
      reason.empty() ? "Netplay disconnected." : reason));
  }
}

NetplaySessionStatus NetplaySession::sendInput(NetplayInputFrame frame)
{
  if (state_ != NetplaySessionState::connected || socket_ == nullptr ||
      sessionKey_.size() != static_cast<qsizetype>(authenticationTagBytes)) {
    return failure(NetplaySessionError::transportFailure,
      "No authenticated netplay peer is connected.");
  }
  const auto packet = encodeAuthenticatedInput(
    sessionKey_, ++sendSequence_, frame);
  if (packet.isEmpty()) {
    return failure(NetplaySessionError::protocolViolation,
      "The local input frame could not be encoded.");
  }
  if (!sendPacket(packet)) {
    return failure(NetplaySessionError::transportFailure,
      "The netplay transport could not queue the input packet.");
  }
  return {};
}

std::uint16_t NetplaySession::listeningPort() const noexcept
{
  return server_->isListening()
    ? static_cast<std::uint16_t>(server_->serverPort()) : 0U;
}

NetplaySessionMetrics NetplaySession::metrics() const noexcept
{
  return {
    .state = state_,
    .sentPackets = sentPackets_,
    .receivedPackets = receivedPackets_,
    .sentBytes = sentBytes_,
    .receivedBytes = receivedBytes_,
    .authenticationFailures = authenticationFailures_,
    .protocolFailures = protocolFailures_,
  };
}

void NetplaySession::setState(NetplaySessionState state)
{
  if (state_ == state) {
    return;
  }
  state_ = state;
  emit stateChanged(state_);
}

void NetplaySession::acceptConnection()
{
  if (state_ != NetplaySessionState::listening || socket_ != nullptr) {
    while (auto* rejected = server_->nextPendingConnection()) {
      const auto packet = PacketFramer::frame(
        encodeRejection("The host already has a peer."));
      rejected->write(packet);
      rejected->disconnectFromHost();
      rejected->deleteLater();
    }
    return;
  }
  auto* accepted = server_->nextPendingConnection();
  if (accepted == nullptr) {
    return;
  }
  server_->close();
  attachSocket(accepted);
  hostNonce_ = randomNonce();
  setState(NetplaySessionState::authenticating);
  handshakeTimer_.start(handshakeTimeoutMilliseconds);
  if (!sendPacket(encodeHostChallenge(hostNonce_))) {
    return;
  }
}

void NetplaySession::socketConnected()
{
  if (configuration_.role == NetplayRole::guest &&
      state_ == NetplaySessionState::connecting) {
    setState(NetplaySessionState::authenticating);
  }
}

void NetplaySession::socketReadyRead()
{
  if (socket_ == nullptr) {
    return;
  }
  const auto bytes = socket_->readAll();
  receivedBytes_ += static_cast<std::uint64_t>(bytes.size());
  if (const auto appended = framer_.append(bytes); !appended.success) {
    ++protocolFailures_;
    fail(NetplaySessionError::protocolViolation, appended.message, false);
    return;
  }
  while (auto packet = framer_.takePacket()) {
    ++receivedPackets_;
    processPacket(*packet);
    if (state_ == NetplaySessionState::disconnected) {
      return;
    }
  }
}

void NetplaySession::socketDisconnected()
{
  if (failureInProgress_ || intentionalDisconnect_ ||
      state_ == NetplaySessionState::disconnected) {
    return;
  }
  const auto detail = state_ == NetplaySessionState::connected
    ? "The netplay peer disconnected." : "The peer disconnected during authentication.";
  resetTransport();
  clearSecrets();
  setState(NetplaySessionState::disconnected);
  emit peerDisconnected(QString::fromLatin1(detail));
}

void NetplaySession::socketError()
{
  if (intentionalDisconnect_ || failureInProgress_ ||
      state_ == NetplaySessionState::disconnected) {
    return;
  }
  // Qt reports an orderly peer FIN through errorOccurred before it emits
  // disconnected.  Let socketDisconnected classify that as a peer departure;
  // treating it as a connection failure produces a duplicate, misleading
  // error whenever the other player leaves normally.
  if (socket_ != nullptr &&
      socket_->error() == QAbstractSocket::RemoteHostClosedError) {
    return;
  }
  fail(NetplaySessionError::connectionFailed,
    socket_ == nullptr ? "The peer connection failed." : socketMessage(*socket_),
    false);
}

void NetplaySession::handshakeTimedOut()
{
  if (state_ == NetplaySessionState::connecting ||
      state_ == NetplaySessionState::authenticating) {
    fail(NetplaySessionError::timeout,
      "The netplay authentication handshake timed out.", false);
  }
}

void NetplaySession::processPacket(const QByteArray& packet)
{
  if (state_ == NetplaySessionState::connected) {
    processConnectedPacket(packet);
  } else if (configuration_.role == NetplayRole::host) {
    processHostPacket(packet);
  } else {
    processGuestPacket(packet);
  }
}

void NetplaySession::processHostPacket(const QByteArray& packet)
{
  if (state_ != NetplaySessionState::authenticating ||
      packetType(packet) != WirePacketType::guestProof) {
    ++protocolFailures_;
    fail(NetplaySessionError::protocolViolation,
      "The guest sent an unexpected handshake packet.", true);
    return;
  }
  GuestProofPacket proof;
  const auto decoded = decodeGuestProof(
    packet, hostNonce_, sessionCode_, proof);
  if (!decoded.success) {
    ++authenticationFailures_;
    fail(NetplaySessionError::authenticationFailed, decoded.message, true);
    return;
  }
  if (proof.descriptor != descriptor_) {
    fail(NetplaySessionError::incompatiblePeer,
      "The peer loaded a different game, core build, or deterministic settings.",
      true);
    return;
  }
  if (proof.inputDelayFrames != configuration_.inputDelayFrames ||
      proof.rollbackFrames != configuration_.rollbackFrames) {
    fail(NetplaySessionError::incompatiblePeer,
      "The peer selected different input-delay or rollback settings.", true);
    return;
  }
  guestNonce_ = proof.nonce;
  sessionKey_ = deriveSessionKey(
    sessionCode_, hostNonce_, guestNonce_, descriptor_);
  clearSecrets();
  if (!sendPacket(encodeHostAccept(sessionKey_, configuration_))) {
    return;
  }
  handshakeTimer_.stop();
  setState(NetplaySessionState::connected);
  emit peerConnected(configuration_);
}

void NetplaySession::processGuestPacket(const QByteArray& packet)
{
  if (packetType(packet) == WirePacketType::rejection) {
    std::string detail;
    static_cast<void>(decodeRejection(packet, detail));
    fail(NetplaySessionError::authenticationFailed,
      detail.empty() ? "The host rejected this netplay session." : detail,
      false);
    return;
  }
  if (state_ != NetplaySessionState::authenticating) {
    ++protocolFailures_;
    fail(NetplaySessionError::protocolViolation,
      "The host sent a packet outside the authentication handshake.", false);
    return;
  }
  if (packetType(packet) == WirePacketType::hostChallenge) {
    const auto decoded = decodeHostChallenge(packet, hostNonce_);
    if (!decoded.success) {
      ++protocolFailures_;
      fail(NetplaySessionError::protocolViolation, decoded.message, false);
      return;
    }
    guestNonce_ = randomNonce();
    GuestProofPacket proof{
      .nonce = guestNonce_,
      .descriptor = descriptor_,
      .inputDelayFrames = configuration_.inputDelayFrames,
      .rollbackFrames = configuration_.rollbackFrames,
    };
    if (!sendPacket(encodeGuestProof(proof, hostNonce_, sessionCode_))) {
      return;
    }
    sessionKey_ = deriveSessionKey(
      sessionCode_, hostNonce_, guestNonce_, descriptor_);
    return;
  }
  if (packetType(packet) == WirePacketType::hostAccept && !sessionKey_.isEmpty()) {
    NetplayConfiguration accepted;
    const auto decoded = decodeHostAccept(packet, sessionKey_, accepted);
    if (!decoded.success) {
      ++authenticationFailures_;
      fail(NetplaySessionError::authenticationFailed, decoded.message, false);
      return;
    }
    if (accepted.inputDelayFrames != configuration_.inputDelayFrames ||
        accepted.rollbackFrames != configuration_.rollbackFrames) {
      fail(NetplaySessionError::incompatiblePeer,
        "The host accepted different netplay timing settings.", false);
      return;
    }
    configuration_ = accepted;
    clearSecrets();
    handshakeTimer_.stop();
    setState(NetplaySessionState::connected);
    emit peerConnected(configuration_);
    return;
  }
  ++protocolFailures_;
  fail(NetplaySessionError::protocolViolation,
    "The host sent an unexpected handshake packet.", false);
}

void NetplaySession::processConnectedPacket(const QByteArray& packet)
{
  if (packetType(packet) != WirePacketType::input) {
    ++protocolFailures_;
    fail(NetplaySessionError::protocolViolation,
      "The peer sent an unsupported authenticated packet.", false);
    return;
  }
  NetplayInputFrame frame;
  const auto decoded = decodeAuthenticatedInput(
    packet, sessionKey_, receiveSequence_ + 1U, frame);
  if (!decoded.success) {
    ++authenticationFailures_;
    fail(NetplaySessionError::authenticationFailed, decoded.message, false);
    return;
  }
  ++receiveSequence_;
  emit inputReceived(frame);
}

void NetplaySession::fail(
  NetplaySessionError error,
  std::string detail,
  bool notifyPeer)
{
  if (failureInProgress_) {
    return;
  }
  failureInProgress_ = true;
  if (notifyPeer && socket_ != nullptr &&
      socket_->state() == QAbstractSocket::ConnectedState) {
    const auto wire = PacketFramer::frame(encodeRejection(detail));
    static_cast<void>(socket_->write(wire));
    static_cast<void>(socket_->flush());
  }
  intentionalDisconnect_ = true;
  resetTransport();
  clearSecrets();
  setState(NetplaySessionState::disconnected);
  emit sessionError(error, QString::fromStdString(detail));
  emit peerDisconnected(QString::fromStdString(detail));
  failureInProgress_ = false;
}

bool NetplaySession::sendPacket(const QByteArray& payload)
{
  if (socket_ == nullptr) {
    return false;
  }
  const auto wire = PacketFramer::frame(payload);
  if (wire.isEmpty()) {
    fail(NetplaySessionError::protocolViolation,
      "A netplay packet exceeded its fixed wire limit.", false);
    return false;
  }
  if (socket_->bytesToWrite() > maximumQueuedWireBytes - wire.size()) {
    fail(NetplaySessionError::transportFailure,
      "The bounded netplay transport output queue is full.", false);
    return false;
  }
  const auto written = socket_->write(wire);
  if (written != wire.size()) {
    fail(NetplaySessionError::transportFailure,
      "The netplay transport could not queue a complete packet.", false);
    return false;
  }
  ++sentPackets_;
  sentBytes_ += static_cast<std::uint64_t>(written);
  return true;
}

void NetplaySession::attachSocket(QTcpSocket* socket)
{
  socket_ = socket;
  socket_->setReadBufferSize(maximumBufferedWireBytes);
  connect(socket_, &QTcpSocket::connected,
    this, &NetplaySession::socketConnected);
  connect(socket_, &QTcpSocket::readyRead,
    this, &NetplaySession::socketReadyRead);
  connect(socket_, &QTcpSocket::disconnected,
    this, &NetplaySession::socketDisconnected);
  connect(socket_, &QTcpSocket::errorOccurred,
    this, &NetplaySession::socketError);
}

void NetplaySession::clearSecrets() noexcept
{
  if (!sessionCode_.isEmpty()) {
    std::fill(sessionCode_.begin(), sessionCode_.end(), '\0');
    sessionCode_.clear();
  }
}

void NetplaySession::resetTransport() noexcept
{
  handshakeTimer_.stop();
  server_->close();
  framer_.clear();
  if (socket_ != nullptr) {
    socket_->disconnect(this);
    socket_->abort();
    socket_->deleteLater();
    socket_ = nullptr;
  }
  std::fill(sessionKey_.begin(), sessionKey_.end(), '\0');
  sessionKey_.clear();
  sendSequence_ = 0U;
  receiveSequence_ = 0U;
  hostNonce_.fill(0U);
  guestNonce_.fill(0U);
}

std::array<std::uint8_t, nonceBytes> NetplaySession::randomNonce() const
{
  std::array<std::uint8_t, nonceBytes> result{};
  auto* generator = QRandomGenerator::system();
  for (std::size_t index = 0U; index < result.size(); index += 8U) {
    const auto value = generator->generate64();
    const auto count = std::min<std::size_t>(8U, result.size() - index);
    for (std::size_t byte = 0U; byte < count; ++byte) {
      result[index + byte] = static_cast<std::uint8_t>(
        value >> static_cast<unsigned int>(byte * 8U));
    }
  }
  return result;
}

} // namespace genplusgx::netplay
