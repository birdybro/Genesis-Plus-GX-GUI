#include "genplusgx/netplay/netplay_protocol.h"

#include <QCoreApplication>

#include <array>
#include <iostream>
#include <random>
#include <string>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  using namespace genplusgx::netplay;
  std::array<std::uint8_t, nonceBytes> hostNonce{};
  std::array<std::uint8_t, nonceBytes> guestNonce{};
  for (std::size_t index = 0U; index < nonceBytes; ++index) {
    hostNonce[index] = static_cast<std::uint8_t>(index);
    guestNonce[index] = static_cast<std::uint8_t>(index + 32U);
  }
  const NetplaySessionDescriptor descriptor{
    .gameSha256 = std::string(64U, '1'),
    .settingsSha256 = std::string(64U, '2'),
    .coreVersion = "abcdef123456",
  };
  const QByteArray code{"correct horse battery staple"};

  const auto challenge = encodeHostChallenge(hostNonce);
  std::array<std::uint8_t, nonceBytes> decodedNonce{};
  if (!check(decodeHostChallenge(challenge, decodedNonce).success &&
        decodedNonce == hostNonce, "Host challenge did not round-trip")) {
    return 1;
  }
  const GuestProofPacket proof{
    .nonce = guestNonce,
    .descriptor = descriptor,
    .inputDelayFrames = 2U,
    .rollbackFrames = 8U,
  };
  const auto encodedProof = encodeGuestProof(proof, hostNonce, code);
  GuestProofPacket decodedProof;
  if (!check(decodeGuestProof(
        encodedProof, hostNonce, code, decodedProof).success &&
        decodedProof.descriptor == descriptor,
      "Guest proof did not authenticate and round-trip") ||
      !check(!decodeGuestProof(encodedProof, hostNonce,
        QByteArray{"wrong-code"}, decodedProof).success,
      "A guest proof authenticated with the wrong code")) {
    return 1;
  }
  auto tamperedProof = encodedProof;
  tamperedProof[10] = static_cast<char>(tamperedProof[10] ^ 0x01);
  if (!check(!decodeGuestProof(
        tamperedProof, hostNonce, code, decodedProof).success,
      "A tampered guest proof authenticated")) {
    return 1;
  }

  const auto key = deriveSessionKey(code, hostNonce, guestNonce, descriptor);
  const NetplayConfiguration hostConfiguration{
    .role = NetplayRole::host,
    .localPlayer = 0U,
    .remotePlayer = 1U,
    .inputDelayFrames = 2U,
    .rollbackFrames = 8U,
  };
  NetplayConfiguration accepted;
  if (!check(decodeHostAccept(
        encodeHostAccept(key, hostConfiguration), key, accepted).success &&
        accepted.role == NetplayRole::guest && accepted.localPlayer == 1U,
      "Host acceptance did not authenticate or assign the guest")) {
    return 1;
  }

  const NetplayInputFrame input{
    .frameNumber = 42U,
    .state = {.connected = true, .buttons = 0x123U,
      .analogX = -1234, .analogY = 2345},
  };
  const auto encodedInput = encodeAuthenticatedInput(key, 1U, input);
  NetplayInputFrame decodedInput;
  if (!check(decodeAuthenticatedInput(
        encodedInput, key, 1U, decodedInput).success && decodedInput == input,
      "Authenticated input did not round-trip") ||
      !check(!decodeAuthenticatedInput(
        encodedInput, key, 2U, decodedInput).success,
      "A replayed input sequence was accepted")) {
    return 1;
  }
  auto tamperedInput = encodedInput;
  tamperedInput[5] = static_cast<char>(tamperedInput[5] ^ 0x40);
  if (!check(!decodeAuthenticatedInput(
        tamperedInput, key, 1U, decodedInput).success,
      "A tampered input packet authenticated")) {
    return 1;
  }

  PacketFramer framer;
  const auto wire1 = PacketFramer::frame(challenge);
  const auto wire2 = PacketFramer::frame(encodedInput);
  if (!check(framer.append(wire1.first(3)).success && !framer.takePacket(),
        "A fragmented prefix produced a packet") ||
      !check(framer.append(wire1.sliced(3) + wire2).success,
        "Framer rejected valid fragmented packets") ||
      !check(framer.takePacket() == challenge &&
        framer.takePacket() == encodedInput && !framer.takePacket(),
        "Framer did not preserve packet boundaries")) {
    return 1;
  }
  QByteArray oversized(4, '\0');
  oversized[0] = 0x7f;
  if (!check(!framer.append(oversized).success,
      "An oversized packet prefix was accepted")) {
    return 1;
  }
  framer.clear();
  if (!check(!framer.append(wire1 + oversized).success,
      "An oversized packet following a complete packet was accepted")) {
    return 1;
  }
  const QByteArray oversizedPayload(maximumWirePacketBytes + 1, '\0');
  if (!check(!decodeGuestProof(
        oversizedPayload, hostNonce, code, decodedProof).success,
      "An oversized guest proof reached the authenticated decoder")) {
    return 1;
  }
  std::string ignoredRejection;
  if (!check(!decodeRejection(oversizedPayload, ignoredRejection).success,
      "An oversized rejection reached the decoder")) {
    return 1;
  }

  std::mt19937 generator{0xC0FFEEU};
  for (int iteration = 0; iteration < 1'024; ++iteration) {
    const auto size = static_cast<qsizetype>(generator() % 256U);
    QByteArray fuzz(size, Qt::Uninitialized);
    for (qsizetype index = 0; index < size; ++index) {
      fuzz[index] = static_cast<char>(generator() & 0xffU);
    }
    NetplayInputFrame ignored;
    static_cast<void>(decodeAuthenticatedInput(fuzz, key, 1U, ignored));
    GuestProofPacket ignoredProof;
    static_cast<void>(decodeGuestProof(
      fuzz, hostNonce, code, ignoredProof));
  }
  return 0;
}
