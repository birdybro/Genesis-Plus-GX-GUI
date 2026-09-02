#include "genplusgx/movies/input_movie.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <array>
#include <limits>
#include <system_error>
#include <utility>

namespace genplusgx::movies {
namespace {

constexpr std::array<char, 8U> magic{'G', 'P', 'G', 'X', 'M', 'O', 'V', '1'};
constexpr std::size_t digestBytes = 32U;
constexpr InputButtonSet validButtons = (1U << 12U) - 1U;
constexpr std::size_t maximumAuthorBytes = 128U;
constexpr std::size_t maximumNotesBytes = 4U * 1024U;
constexpr std::size_t maximumCoreVersionBytes = 64U;

MovieStatus failure(MovieError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

void accountRerecord(MovieMetadata& metadata) noexcept
{
  if (metadata.rerecordCount != std::numeric_limits<std::uint64_t>::max()) {
    ++metadata.rerecordCount;
  }
}

QString pathText(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

bool lowercaseHex(std::string_view value, std::size_t size) noexcept
{
  return value.size() == size && std::ranges::all_of(value, [](char character) {
    return (character >= '0' && character <= '9') ||
      (character >= 'a' && character <= 'f');
  });
}

bool validUtf8(std::string_view value, std::size_t maximum) noexcept
{
  if (value.size() > maximum) {
    return false;
  }
  const auto bytes = QByteArray{value.data(), static_cast<qsizetype>(value.size())};
  return QString::fromUtf8(bytes).toUtf8() == bytes;
}

class Writer final {
public:
  void bytes(std::span<const std::uint8_t> value)
  {
    data_.append(reinterpret_cast<const char*>(value.data()),
      static_cast<qsizetype>(value.size()));
  }

  void text(std::string_view value)
  {
    data_.append(value.data(), static_cast<qsizetype>(value.size()));
  }

  void u8(std::uint8_t value) { data_.append(static_cast<char>(value)); }

  void u16(std::uint16_t value)
  {
    u8(static_cast<std::uint8_t>(value & 0xffU));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }

  void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }

  void u32(std::uint32_t value)
  {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
      u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void u64(std::uint64_t value)
  {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
      u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  [[nodiscard]] QByteArray take() { return std::move(data_); }

private:
  QByteArray data_;
};

class Reader final {
public:
  explicit Reader(const QByteArray& value)
    : value_(reinterpret_cast<const std::uint8_t*>(value.constData()),
        static_cast<std::size_t>(value.size()))
  {
  }

  [[nodiscard]] bool bytes(std::size_t count, std::span<const std::uint8_t>& output)
  {
    if (count > remaining()) {
      return false;
    }
    output = value_.subspan(offset_, count);
    offset_ += count;
    return true;
  }

  [[nodiscard]] bool text(std::size_t count, std::string& output)
  {
    std::span<const std::uint8_t> source;
    if (!bytes(count, source)) {
      return false;
    }
    output.assign(reinterpret_cast<const char*>(source.data()), source.size());
    return true;
  }

  [[nodiscard]] bool u8(std::uint8_t& value)
  {
    if (remaining() < 1U) {
      return false;
    }
    value = value_[offset_++];
    return true;
  }

  [[nodiscard]] bool u16(std::uint16_t& value)
  {
    std::uint8_t low = 0U;
    std::uint8_t high = 0U;
    if (!u8(low) || !u8(high)) {
      return false;
    }
    value = static_cast<std::uint16_t>(low |
      (static_cast<std::uint16_t>(high) << 8U));
    return true;
  }

  [[nodiscard]] bool i16(std::int16_t& value)
  {
    std::uint16_t encoded = 0U;
    if (!u16(encoded)) {
      return false;
    }
    value = static_cast<std::int16_t>(encoded);
    return true;
  }

  [[nodiscard]] bool u32(std::uint32_t& value)
  {
    value = 0U;
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
      std::uint8_t byte = 0U;
      if (!u8(byte)) {
        return false;
      }
      value |= static_cast<std::uint32_t>(byte) << shift;
    }
    return true;
  }

  [[nodiscard]] bool u64(std::uint64_t& value)
  {
    value = 0U;
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
      std::uint8_t byte = 0U;
      if (!u8(byte)) {
        return false;
      }
      value |= static_cast<std::uint64_t>(byte) << shift;
    }
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept
  {
    return value_.size() - offset_;
  }

private:
  std::span<const std::uint8_t> value_;
  std::size_t offset_{0U};
};

InputSnapshot normalized(InputSnapshot input) noexcept
{
  input.sequence = 0U;
  return input;
}

std::uint32_t runCount(const std::vector<InputSnapshot>& frames)
{
  if (frames.empty()) {
    return 0U;
  }
  std::uint32_t count = 1U;
  auto previous = normalized(frames.front());
  for (std::size_t index = 1U; index < frames.size(); ++index) {
    const auto current = normalized(frames[index]);
    if (current != previous) {
      ++count;
      previous = current;
    }
  }
  return count;
}

QByteArray encode(const InputMovie& movie)
{
  Writer writer;
  writer.text(std::string_view{magic.data(), magic.size()});
  writer.u32(movieSchemaVersion);
  writer.u32(0U);
  writer.u64(movie.startFrame);
  writer.u64(movie.frames.size());
  writer.u64(movie.metadata.rerecordCount);
  writer.u32(static_cast<std::uint32_t>(movie.initialState.size()));
  writer.u16(static_cast<std::uint16_t>(movie.descriptor.gameSha256.size()));
  writer.u16(static_cast<std::uint16_t>(movie.descriptor.settingsSha256.size()));
  writer.u16(static_cast<std::uint16_t>(movie.descriptor.coreVersion.size()));
  writer.u16(static_cast<std::uint16_t>(movie.metadata.author.size()));
  writer.u32(static_cast<std::uint32_t>(movie.metadata.notes.size()));
  writer.u32(runCount(movie.frames));
  writer.text(movie.descriptor.gameSha256);
  writer.text(movie.descriptor.settingsSha256);
  writer.text(movie.descriptor.coreVersion);
  writer.text(movie.metadata.author);
  writer.text(movie.metadata.notes);
  writer.bytes(movie.initialState);

  for (std::size_t first = 0U; first < movie.frames.size();) {
    const auto state = normalized(movie.frames[first]);
    std::size_t last = first + 1U;
    while (last < movie.frames.size() &&
           normalized(movie.frames[last]) == state &&
           last - first < std::numeric_limits<std::uint32_t>::max()) {
      ++last;
    }
    writer.u32(static_cast<std::uint32_t>(last - first));
    for (const auto& player : state.players) {
      writer.u8(player.connected ? 1U : 0U);
      writer.u16(player.buttons);
      writer.i16(player.analogX);
      writer.i16(player.analogY);
    }
    first = last;
  }
  auto encoded = writer.take();
  encoded.append(QCryptographicHash::hash(encoded, QCryptographicHash::Sha256));
  return encoded;
}

MovieReadResult decode(const QByteArray& file)
{
  if (file.size() < static_cast<qsizetype>(magic.size() + digestBytes)) {
    return {failure(MovieError::corruptFile,
      "The input movie is truncated."), {}};
  }
  const auto contentSize = file.size() - static_cast<qsizetype>(digestBytes);
  const auto content = file.first(contentSize);
  const auto digest = file.sliced(contentSize);
  if (QCryptographicHash::hash(content, QCryptographicHash::Sha256) != digest) {
    return {failure(MovieError::corruptFile,
      "The input movie checksum does not match its contents."), {}};
  }

  Reader reader{content};
  std::span<const std::uint8_t> encodedMagic;
  std::uint32_t version = 0U;
  std::uint32_t flags = 0U;
  InputMovie movie;
  std::uint64_t frameCount = 0U;
  std::uint32_t stateBytes = 0U;
  std::uint16_t gameBytes = 0U;
  std::uint16_t settingsBytes = 0U;
  std::uint16_t coreBytes = 0U;
  std::uint16_t authorBytes = 0U;
  std::uint32_t notesBytes = 0U;
  std::uint32_t records = 0U;
  if (!reader.bytes(magic.size(), encodedMagic) ||
      !std::ranges::equal(encodedMagic,
        std::span<const char>{magic.data(), magic.size()},
        [](std::uint8_t left, char right) {
          return left == static_cast<std::uint8_t>(right);
        }) ||
      !reader.u32(version) || !reader.u32(flags)) {
    return {failure(MovieError::corruptFile,
      "The input movie header is invalid."), {}};
  }
  if (version != movieSchemaVersion) {
    return {failure(MovieError::unsupportedVersion,
      "The input movie uses an unsupported format version."), {}};
  }
  if (flags != 0U || !reader.u64(movie.startFrame) ||
      !reader.u64(frameCount) || !reader.u64(movie.metadata.rerecordCount) ||
      !reader.u32(stateBytes) || !reader.u16(gameBytes) ||
      !reader.u16(settingsBytes) || !reader.u16(coreBytes) ||
      !reader.u16(authorBytes) || !reader.u32(notesBytes) ||
      !reader.u32(records) || frameCount == 0U ||
      frameCount > maximumMovieFrames ||
      stateBytes == 0U || stateBytes > maximumInitialStateBytes ||
      gameBytes != 64U || settingsBytes != 64U ||
      coreBytes == 0U || coreBytes > maximumCoreVersionBytes ||
      authorBytes > maximumAuthorBytes || notesBytes > maximumNotesBytes ||
      records == 0U || records > frameCount) {
    return {failure(MovieError::corruptFile,
      "The input movie header exceeds a fixed limit or is inconsistent."), {}};
  }
  if (!reader.text(gameBytes, movie.descriptor.gameSha256) ||
      !reader.text(settingsBytes, movie.descriptor.settingsSha256) ||
      !reader.text(coreBytes, movie.descriptor.coreVersion) ||
      !reader.text(authorBytes, movie.metadata.author) ||
      !reader.text(notesBytes, movie.metadata.notes)) {
    return {failure(MovieError::corruptFile,
      "The input movie metadata is truncated."), {}};
  }
  std::span<const std::uint8_t> state;
  if (!reader.bytes(stateBytes, state)) {
    return {failure(MovieError::corruptFile,
      "The input movie initial state is truncated."), {}};
  }
  movie.initialState.assign(state.begin(), state.end());
  movie.frames.reserve(static_cast<std::size_t>(frameCount));
  for (std::uint32_t record = 0U; record < records; ++record) {
    std::uint32_t duration = 0U;
    InputSnapshot input;
    if (!reader.u32(duration) || duration == 0U ||
        duration > frameCount - movie.frames.size()) {
      return {failure(MovieError::corruptFile,
        "The input movie contains an invalid input run."), {}};
    }
    for (auto& player : input.players) {
      std::uint8_t connected = 0U;
      if (!reader.u8(connected) || connected > 1U ||
          !reader.u16(player.buttons) || !reader.i16(player.analogX) ||
          !reader.i16(player.analogY)) {
        return {failure(MovieError::corruptFile,
          "The input movie contains a truncated controller record."), {}};
      }
      player.connected = connected != 0U;
    }
    if (!validMovieInput(input)) {
      return {failure(MovieError::corruptFile,
        "The input movie contains an unsupported controller value."), {}};
    }
    movie.frames.insert(movie.frames.end(), duration, input);
  }
  if (movie.frames.size() != frameCount || reader.remaining() != 0U ||
      !movie.valid()) {
    return {failure(MovieError::corruptFile,
      "The input movie payload is inconsistent or has trailing data."), {}};
  }
  return {{}, std::move(movie)};
}

} // namespace

bool MovieDescriptor::valid() const noexcept
{
  return lowercaseHex(gameSha256, 64U) &&
    lowercaseHex(settingsSha256, 64U) &&
    validUtf8(coreVersion, maximumCoreVersionBytes) && !coreVersion.empty();
}

bool validMovieInput(const InputSnapshot& input) noexcept
{
  return std::ranges::all_of(input.players, [](const InputDeviceState& player) {
    return (player.buttons & ~validButtons) == 0U;
  });
}

bool InputMovie::valid() const noexcept
{
  return descriptor.valid() &&
    validUtf8(metadata.author, maximumAuthorBytes) &&
    validUtf8(metadata.notes, maximumNotesBytes) &&
    !initialState.empty() && initialState.size() <= maximumInitialStateBytes &&
    !frames.empty() && frames.size() <= maximumMovieFrames &&
    std::ranges::all_of(frames, [](const InputSnapshot& frame) {
      return frame.sequence == 0U && validMovieInput(frame);
    });
}

MovieStatus compatibleMovie(
  const InputMovie& movie, const MovieDescriptor& expected) noexcept
{
  if (!movie.valid() || !expected.valid()) {
    return failure(MovieError::invalidMovie,
      "The movie or active session identity is invalid.");
  }
  if (movie.descriptor.gameSha256 != expected.gameSha256) {
    return failure(MovieError::incompatibleMovie,
      "This input movie was recorded for a different game.");
  }
  if (movie.descriptor.settingsSha256 != expected.settingsSha256) {
    return failure(MovieError::incompatibleMovie,
      "This input movie requires different deterministic system, input, BIOS, or cheat settings.");
  }
  if (movie.descriptor.coreVersion != expected.coreVersion) {
    return failure(MovieError::incompatibleMovie,
      "This input movie was recorded with a different core build.");
  }
  return {};
}

MovieStatus saveMovie(const std::filesystem::path& path, const InputMovie& movie)
{
  if (path.empty() || !path.is_absolute() || path.native().size() > 4'096U) {
    return failure(MovieError::invalidPath,
      "The input movie destination must be an absolute, bounded path.");
  }
  if (!movie.valid()) {
    return failure(MovieError::invalidMovie,
      "The input movie is empty or contains invalid metadata or controller input.");
  }
  const auto encoded = encode(movie);
  if (static_cast<std::size_t>(encoded.size()) > maximumMovieFileBytes) {
    return failure(MovieError::fileTooLarge,
      "The encoded input movie exceeds its fixed 96 MiB limit.");
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return failure(MovieError::fileOpenFailed,
      "The input movie directory could not be created.");
  }
  QSaveFile output{pathText(path)};
  if (!output.open(QIODevice::WriteOnly)) {
    return failure(MovieError::fileOpenFailed,
      "The input movie destination could not be opened.");
  }
  if (output.write(encoded) != encoded.size() || !output.commit()) {
    return failure(MovieError::fileWriteFailed,
      "The input movie could not be committed atomically.");
  }
  return {};
}

MovieReadResult loadMovie(const std::filesystem::path& path)
{
  if (path.empty() || !path.is_absolute() || path.native().size() > 4'096U) {
    return {failure(MovieError::invalidPath,
      "The input movie source must be an absolute, bounded path."), {}};
  }
  QFile input{pathText(path)};
  if (!input.open(QIODevice::ReadOnly)) {
    return {failure(MovieError::fileOpenFailed,
      "The input movie could not be opened."), {}};
  }
  if (input.size() <= 0 ||
      input.size() > static_cast<qint64>(maximumMovieFileBytes)) {
    return {failure(MovieError::fileTooLarge,
      "The input movie is empty or exceeds its fixed 96 MiB limit."), {}};
  }
  const auto encoded = input.readAll();
  if (encoded.size() != input.size()) {
    return {failure(MovieError::fileReadFailed,
      "The input movie could not be read completely."), {}};
  }
  return decode(encoded);
}

MovieStatus setFrame(
  InputMovie& movie, std::size_t index, InputSnapshot input) noexcept
{
  if (index >= movie.frames.size() || !validMovieInput(input)) {
    return failure(MovieError::invalidMovie,
      "The TAS frame index or controller input is invalid.");
  }
  input.sequence = 0U;
  movie.frames[index] = input;
  accountRerecord(movie.metadata);
  return {};
}

MovieStatus insertFrames(
  InputMovie& movie, std::size_t index, std::size_t count,
  const InputSnapshot& input) noexcept
{
  if (index > movie.frames.size() || count == 0U ||
      count > maximumMovieFrames - movie.frames.size() ||
      !validMovieInput(input)) {
    return failure(MovieError::frameLimitReached,
      "The TAS insertion is invalid or exceeds one million frames.");
  }
  auto normalizedInput = input;
  normalizedInput.sequence = 0U;
  movie.frames.insert(movie.frames.begin() + static_cast<std::ptrdiff_t>(index),
    count, normalizedInput);
  accountRerecord(movie.metadata);
  return {};
}

MovieStatus eraseFrames(
  InputMovie& movie, std::size_t index, std::size_t count) noexcept
{
  if (count == 0U || index >= movie.frames.size() ||
      count > movie.frames.size() - index || count == movie.frames.size()) {
    return failure(MovieError::invalidMovie,
      "The TAS deletion range is invalid or would remove every frame.");
  }
  const auto first = movie.frames.begin() + static_cast<std::ptrdiff_t>(index);
  movie.frames.erase(first, first + static_cast<std::ptrdiff_t>(count));
  accountRerecord(movie.metadata);
  return {};
}

MovieStatus branchFrom(
  InputMovie& movie, std::size_t firstChangedFrame) noexcept
{
  if (firstChangedFrame >= movie.frames.size()) {
    return failure(MovieError::invalidMovie,
      "The TAS branch frame is outside the movie timeline.");
  }
  movie.frames.erase(movie.frames.begin() +
    static_cast<std::ptrdiff_t>(firstChangedFrame + 1U), movie.frames.end());
  accountRerecord(movie.metadata);
  return {};
}

} // namespace genplusgx::movies
