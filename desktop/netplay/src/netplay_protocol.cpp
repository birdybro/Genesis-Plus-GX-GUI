#include "genplusgx/netplay/netplay_protocol.h"

#include <QDataStream>
#include <QIODevice>
#include <QMessageAuthenticationCode>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <utility>

namespace genplusgx::netplay {
namespace {

constexpr auto streamVersion = QDataStream::Qt_6_8;
constexpr std::string_view guestProofDomain = "genplusgx-netplay-guest-v1";
constexpr std::string_view sessionKeyDomain = "genplusgx-netplay-key-v1";
constexpr std::string_view hostAcceptDomain = "genplusgx-netplay-accept-v1";

QByteArray bytes(std::span<const std::uint8_t> value)
{
  return QByteArray{
    reinterpret_cast<const char*>(value.data()),
    static_cast<qsizetype>(value.size())};
}

void configure(QDataStream& stream)
{
  stream.setVersion(streamVersion);
  stream.setByteOrder(QDataStream::BigEndian);
}

void appendString(QDataStream& stream, const std::string& value)
{
  const auto data = QByteArray::fromStdString(value);
  stream << static_cast<quint16>(data.size());
  if (!data.isEmpty()) {
    stream.writeRawData(data.constData(), static_cast<int>(data.size()));
  }
}

bool takeString(QDataStream& stream, std::string& value, qsizetype maximum)
{
  quint16 size = 0U;
  stream >> size;
  if (stream.status() != QDataStream::Ok || size > maximum) {
    return false;
  }
  QByteArray data(static_cast<qsizetype>(size), Qt::Uninitialized);
  if (size > 0U && stream.readRawData(data.data(), size) != size) {
    return false;
  }
  value = data.toStdString();
  return true;
}

QByteArray descriptorBytes(const NetplaySessionDescriptor& descriptor)
{
  QByteArray output;
  QDataStream stream{&output, QIODevice::WriteOnly};
  configure(stream);
  appendString(stream, descriptor.gameSha256);
  appendString(stream, descriptor.settingsSha256);
  appendString(stream, descriptor.coreVersion);
  return output;
}

bool takeDescriptor(QDataStream& stream, NetplaySessionDescriptor& descriptor)
{
  return takeString(stream, descriptor.gameSha256, 64U) &&
    takeString(stream, descriptor.settingsSha256, 64U) &&
    takeString(stream, descriptor.coreVersion, 64U) && descriptor.valid();
}

QByteArray hmac(const QByteArray& key, const QByteArray& message)
{
  return QMessageAuthenticationCode::hash(
    message, key, QCryptographicHash::Sha256);
}

QByteArray domainBytes(std::string_view domain)
{
  return QByteArray{domain.data(), static_cast<qsizetype>(domain.size())};
}

ProtocolDecodeResult failure(std::string message)
{
  return {.success = false, .message = std::move(message)};
}

ProtocolDecodeResult success()
{
  return {.success = true, .message = {}};
}

bool exactlyConsumed(QDataStream& stream)
{
  return stream.status() == QDataStream::Ok && stream.atEnd();
}

QByteArray authenticatedPayload(
  const QByteArray& unsignedPayload,
  const QByteArray& key)
{
  auto output = unsignedPayload;
  output.append(hmac(key, unsignedPayload));
  return output;
}

ProtocolDecodeResult verifyAuthenticated(
  const QByteArray& packet,
  const QByteArray& key,
  QByteArray& unsignedPayload)
{
  if (packet.size() <= static_cast<qsizetype>(authenticationTagBytes) ||
      packet.size() > maximumWirePacketBytes) {
    return failure("The authenticated netplay packet has an invalid size.");
  }
  const auto payloadBytes = packet.size() -
    static_cast<qsizetype>(authenticationTagBytes);
  unsignedPayload = packet.first(payloadBytes);
  const auto received = packet.sliced(payloadBytes);
  if (!constantTimeEqual(received, hmac(key, unsignedPayload))) {
    unsignedPayload.clear();
    return failure("Netplay packet authentication failed.");
  }
  return success();
}

} // namespace

ProtocolDecodeResult PacketFramer::append(const QByteArray& bytes)
{
  if (invalid_) {
    return failure("The netplay packet stream is already invalid.");
  }
  if (bytes.size() > maximumBufferedWireBytes - buffer_.size()) {
    invalid_ = true;
    buffer_.clear();
    return failure("The bounded netplay receive buffer overflowed.");
  }
  buffer_.append(bytes);
  qsizetype offset = 0;
  while (buffer_.size() - offset >= 4) {
    QDataStream stream{buffer_.sliced(offset, 4)};
    configure(stream);
    quint32 size = 0U;
    stream >> size;
    if (size == 0U || size > static_cast<quint32>(maximumWirePacketBytes)) {
      invalid_ = true;
      buffer_.clear();
      return failure("The netplay packet length is invalid.");
    }
    const auto framedSize = 4 + static_cast<qsizetype>(size);
    if (buffer_.size() - offset < framedSize) {
      break;
    }
    offset += framedSize;
  }
  return success();
}

std::optional<QByteArray> PacketFramer::takePacket()
{
  if (invalid_ || buffer_.size() < 4) {
    return std::nullopt;
  }
  QDataStream stream{buffer_};
  configure(stream);
  quint32 size = 0U;
  stream >> size;
  if (buffer_.size() < 4 + static_cast<qsizetype>(size)) {
    return std::nullopt;
  }
  auto packet = buffer_.sliced(4, static_cast<qsizetype>(size));
  buffer_.remove(0, 4 + static_cast<qsizetype>(size));
  return packet;
}

void PacketFramer::clear() noexcept
{
  buffer_.clear();
  invalid_ = false;
}

QByteArray PacketFramer::frame(const QByteArray& payload)
{
  if (payload.isEmpty() || payload.size() > maximumWirePacketBytes) {
    return {};
  }
  QByteArray output;
  QDataStream stream{&output, QIODevice::WriteOnly};
  configure(stream);
  stream << static_cast<quint32>(payload.size());
  output.append(payload);
  return output;
}

QByteArray encodeHostChallenge(
  const std::array<std::uint8_t, nonceBytes>& nonce)
{
  QByteArray output;
  QDataStream stream{&output, QIODevice::WriteOnly};
  configure(stream);
  stream << static_cast<quint8>(WirePacketType::hostChallenge)
         << static_cast<quint16>(protocolVersion);
  stream.writeRawData(bytes(nonce).constData(), static_cast<int>(nonce.size()));
  return output;
}

ProtocolDecodeResult decodeHostChallenge(
  const QByteArray& packet,
  std::array<std::uint8_t, nonceBytes>& nonce)
{
  if (packet.size() != 1 + 2 + static_cast<qsizetype>(nonceBytes)) {
    return failure("The netplay host challenge has an invalid size.");
  }
  QDataStream stream{packet};
  configure(stream);
  quint8 type = 0U;
  quint16 version = 0U;
  stream >> type >> version;
  if (type != static_cast<quint8>(WirePacketType::hostChallenge) ||
      version != protocolVersion ||
      stream.readRawData(reinterpret_cast<char*>(nonce.data()),
        static_cast<int>(nonce.size())) != static_cast<int>(nonce.size()) ||
      !exactlyConsumed(stream)) {
    return failure("The netplay host challenge is malformed or incompatible.");
  }
  return success();
}

QByteArray encodeGuestProof(
  const GuestProofPacket& packet,
  const std::array<std::uint8_t, nonceBytes>& hostNonce,
  const QByteArray& sessionCode)
{
  QByteArray output;
  QDataStream stream{&output, QIODevice::WriteOnly};
  configure(stream);
  stream << static_cast<quint8>(WirePacketType::guestProof)
         << static_cast<quint16>(protocolVersion);
  stream.writeRawData(bytes(packet.nonce).constData(),
    static_cast<int>(packet.nonce.size()));
  output.append(descriptorBytes(packet.descriptor));
  QDataStream tail{&output, QIODevice::Append};
  configure(tail);
  tail << static_cast<quint8>(packet.inputDelayFrames)
       << static_cast<quint8>(packet.rollbackFrames);
  auto proofInput = domainBytes(guestProofDomain);
  proofInput.append(bytes(hostNonce));
  proofInput.append(output);
  output.append(hmac(sessionCode, proofInput));
  return output;
}

ProtocolDecodeResult decodeGuestProof(
  const QByteArray& packet,
  const std::array<std::uint8_t, nonceBytes>& hostNonce,
  const QByteArray& sessionCode,
  GuestProofPacket& output)
{
  if (packet.size() <= static_cast<qsizetype>(authenticationTagBytes) ||
      packet.size() > maximumWirePacketBytes) {
    return failure("The netplay guest proof is truncated.");
  }
  const auto unsignedSize = packet.size() -
    static_cast<qsizetype>(authenticationTagBytes);
  const auto unsignedPacket = packet.first(unsignedSize);
  auto proofInput = domainBytes(guestProofDomain);
  proofInput.append(bytes(hostNonce));
  proofInput.append(unsignedPacket);
  if (!constantTimeEqual(packet.sliced(unsignedSize), hmac(sessionCode, proofInput))) {
    return failure("The session code was rejected by the host.");
  }
  QDataStream stream{unsignedPacket};
  configure(stream);
  quint8 type = 0U;
  quint16 version = 0U;
  stream >> type >> version;
  if (type != static_cast<quint8>(WirePacketType::guestProof) ||
      version != protocolVersion ||
      stream.readRawData(reinterpret_cast<char*>(output.nonce.data()),
        static_cast<int>(output.nonce.size())) !=
          static_cast<int>(output.nonce.size()) ||
      !takeDescriptor(stream, output.descriptor)) {
    return failure("The netplay guest proof is malformed or incompatible.");
  }
  quint8 delay = 0U;
  quint8 rollback = 0U;
  stream >> delay >> rollback;
  output.inputDelayFrames = delay;
  output.rollbackFrames = rollback;
  if (!exactlyConsumed(stream) || delay > maximumInputDelayFrames ||
      rollback == 0U || rollback > maximumRollbackFrames) {
    return failure("The netplay guest requested invalid timing limits.");
  }
  return success();
}

QByteArray deriveSessionKey(
  const QByteArray& sessionCode,
  const std::array<std::uint8_t, nonceBytes>& hostNonce,
  const std::array<std::uint8_t, nonceBytes>& guestNonce,
  const NetplaySessionDescriptor& descriptor)
{
  auto material = domainBytes(sessionKeyDomain);
  material.append(bytes(hostNonce));
  material.append(bytes(guestNonce));
  material.append(descriptorBytes(descriptor));
  return hmac(sessionCode, material);
}

QByteArray encodeHostAccept(
  const QByteArray& sessionKey,
  const NetplayConfiguration& configuration)
{
  QByteArray payload;
  QDataStream stream{&payload, QIODevice::WriteOnly};
  configure(stream);
  stream << static_cast<quint8>(WirePacketType::hostAccept)
         << static_cast<quint16>(protocolVersion)
         << static_cast<quint8>(configuration.inputDelayFrames)
         << static_cast<quint8>(configuration.rollbackFrames);
  auto signedPayload = domainBytes(hostAcceptDomain);
  signedPayload.append(payload);
  payload.append(hmac(sessionKey, signedPayload));
  return payload;
}

ProtocolDecodeResult decodeHostAccept(
  const QByteArray& packet,
  const QByteArray& sessionKey,
  NetplayConfiguration& configuration)
{
  constexpr qsizetype unsignedSize = 1 + 2 + 1 + 1;
  if (packet.size() != unsignedSize +
      static_cast<qsizetype>(authenticationTagBytes)) {
    return failure("The netplay host acceptance is malformed.");
  }
  const auto payload = packet.first(unsignedSize);
  auto signedPayload = domainBytes(hostAcceptDomain);
  signedPayload.append(payload);
  if (!constantTimeEqual(packet.sliced(unsignedSize), hmac(sessionKey, signedPayload))) {
    return failure("The netplay host could not be authenticated.");
  }
  QDataStream stream{payload};
  configure(stream);
  quint8 type = 0U;
  quint16 version = 0U;
  quint8 delay = 0U;
  quint8 rollback = 0U;
  stream >> type >> version >> delay >> rollback;
  if (type != static_cast<quint8>(WirePacketType::hostAccept) ||
      version != protocolVersion || !exactlyConsumed(stream)) {
    return failure("The netplay host acceptance is incompatible.");
  }
  configuration = {
    .role = NetplayRole::guest,
    .localPlayer = 1U,
    .remotePlayer = 0U,
    .inputDelayFrames = delay,
    .rollbackFrames = rollback,
  };
  if (!configuration.valid()) {
    return failure("The host selected invalid netplay timing limits.");
  }
  return success();
}

QByteArray encodeRejection(const std::string& message)
{
  QByteArray output;
  QDataStream stream{&output, QIODevice::WriteOnly};
  configure(stream);
  stream << static_cast<quint8>(WirePacketType::rejection);
  appendString(stream, message.substr(0U, 512U));
  return output;
}

ProtocolDecodeResult decodeRejection(
  const QByteArray& packet,
  std::string& message)
{
  if (packet.isEmpty() || packet.size() > maximumWirePacketBytes) {
    return failure("The netplay rejection packet has an invalid size.");
  }
  QDataStream stream{packet};
  configure(stream);
  quint8 type = 0U;
  stream >> type;
  if (type != static_cast<quint8>(WirePacketType::rejection) ||
      !takeString(stream, message, 512U) || !exactlyConsumed(stream)) {
    return failure("The netplay rejection packet is malformed.");
  }
  return success();
}

QByteArray encodeAuthenticatedInput(
  const QByteArray& sessionKey,
  std::uint64_t sequence,
  const NetplayInputFrame& frame)
{
  QByteArray payload;
  QDataStream stream{&payload, QIODevice::WriteOnly};
  configure(stream);
  stream << static_cast<quint8>(WirePacketType::input)
         << static_cast<quint64>(sequence)
         << static_cast<quint64>(frame.frameNumber)
         << static_cast<quint8>(frame.state.connected ? 1U : 0U)
         << static_cast<quint16>(frame.state.buttons)
         << static_cast<qint16>(frame.state.analogX)
         << static_cast<qint16>(frame.state.analogY);
  return authenticatedPayload(payload, sessionKey);
}

ProtocolDecodeResult decodeAuthenticatedInput(
  const QByteArray& packet,
  const QByteArray& sessionKey,
  std::uint64_t expectedSequence,
  NetplayInputFrame& frame)
{
  QByteArray payload;
  if (const auto verified = verifyAuthenticated(packet, sessionKey, payload);
      !verified.success) {
    return verified;
  }
  QDataStream stream{payload};
  configure(stream);
  quint8 type = 0U;
  quint64 sequence = 0U;
  quint64 frameNumber = 0U;
  quint8 connected = 0U;
  quint16 buttons = 0U;
  qint16 analogX = 0;
  qint16 analogY = 0;
  stream >> type >> sequence >> frameNumber >> connected >> buttons >>
    analogX >> analogY;
  constexpr InputButtonSet validButtons =
    (1U << 12U) - 1U;
  if (type != static_cast<quint8>(WirePacketType::input) ||
      sequence != expectedSequence || connected > 1U ||
      (buttons & ~validButtons) != 0U || !exactlyConsumed(stream)) {
    return failure("The authenticated netplay input sequence or payload is invalid.");
  }
  frame = {
    .frameNumber = frameNumber,
    .state = {
      .connected = connected != 0U,
      .buttons = buttons,
      .analogX = analogX,
      .analogY = analogY,
    },
  };
  return success();
}

WirePacketType packetType(const QByteArray& packet) noexcept
{
  return packet.isEmpty()
    ? static_cast<WirePacketType>(0U)
    : static_cast<WirePacketType>(static_cast<std::uint8_t>(packet.front()));
}

bool constantTimeEqual(
  const QByteArray& left,
  const QByteArray& right) noexcept
{
  if (left.size() != right.size()) {
    return false;
  }
  unsigned char difference = 0U;
  for (qsizetype index = 0; index < left.size(); ++index) {
    difference = static_cast<unsigned char>(difference |
      static_cast<unsigned char>(
        static_cast<unsigned char>(left[index]) ^
        static_cast<unsigned char>(right[index])));
  }
  return difference == 0U;
}

} // namespace genplusgx::netplay
