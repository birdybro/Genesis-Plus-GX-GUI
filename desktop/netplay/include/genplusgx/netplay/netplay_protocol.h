#pragma once

#include "genplusgx/netplay/netplay_types.h"

#include <QByteArray>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace genplusgx::netplay {

inline constexpr qsizetype maximumWirePacketBytes = 4'096;
inline constexpr qsizetype maximumBufferedWireBytes = 32'768;
inline constexpr qsizetype maximumQueuedWireBytes = 65'536;

enum class WirePacketType : std::uint8_t {
  hostChallenge = 1U,
  guestProof = 2U,
  hostAccept = 3U,
  rejection = 4U,
  input = 16U,
};

struct GuestProofPacket final {
  std::array<std::uint8_t, nonceBytes> nonce{};
  NetplaySessionDescriptor descriptor;
  std::uint32_t inputDelayFrames{0U};
  std::uint32_t rollbackFrames{0U};
};

struct ProtocolDecodeResult final {
  bool success{false};
  std::string message;
};

class PacketFramer final {
public:
  [[nodiscard]] ProtocolDecodeResult append(const QByteArray& bytes);
  [[nodiscard]] std::optional<QByteArray> takePacket();
  void clear() noexcept;
  [[nodiscard]] qsizetype bufferedBytes() const noexcept { return buffer_.size(); }

  [[nodiscard]] static QByteArray frame(const QByteArray& payload);

private:
  QByteArray buffer_;
  bool invalid_{false};
};

[[nodiscard]] QByteArray encodeHostChallenge(
  const std::array<std::uint8_t, nonceBytes>& nonce);
[[nodiscard]] ProtocolDecodeResult decodeHostChallenge(
  const QByteArray& packet,
  std::array<std::uint8_t, nonceBytes>& nonce);

[[nodiscard]] QByteArray encodeGuestProof(
  const GuestProofPacket& packet,
  const std::array<std::uint8_t, nonceBytes>& hostNonce,
  const QByteArray& sessionCode);
[[nodiscard]] ProtocolDecodeResult decodeGuestProof(
  const QByteArray& packet,
  const std::array<std::uint8_t, nonceBytes>& hostNonce,
  const QByteArray& sessionCode,
  GuestProofPacket& output);

[[nodiscard]] QByteArray deriveSessionKey(
  const QByteArray& sessionCode,
  const std::array<std::uint8_t, nonceBytes>& hostNonce,
  const std::array<std::uint8_t, nonceBytes>& guestNonce,
  const NetplaySessionDescriptor& descriptor);

[[nodiscard]] QByteArray encodeHostAccept(
  const QByteArray& sessionKey,
  const NetplayConfiguration& configuration);
[[nodiscard]] ProtocolDecodeResult decodeHostAccept(
  const QByteArray& packet,
  const QByteArray& sessionKey,
  NetplayConfiguration& configuration);

[[nodiscard]] QByteArray encodeRejection(const std::string& message);
[[nodiscard]] ProtocolDecodeResult decodeRejection(
  const QByteArray& packet,
  std::string& message);

[[nodiscard]] QByteArray encodeAuthenticatedInput(
  const QByteArray& sessionKey,
  std::uint64_t sequence,
  const NetplayInputFrame& frame);
[[nodiscard]] ProtocolDecodeResult decodeAuthenticatedInput(
  const QByteArray& packet,
  const QByteArray& sessionKey,
  std::uint64_t expectedSequence,
  NetplayInputFrame& frame);

[[nodiscard]] WirePacketType packetType(const QByteArray& packet) noexcept;
[[nodiscard]] bool constantTimeEqual(
  const QByteArray& left,
  const QByteArray& right) noexcept;

} // namespace genplusgx::netplay
