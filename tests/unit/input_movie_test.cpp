#include "genplusgx/movies/input_movie.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <limits>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::filesystem::path nativePath(const QTemporaryDir& directory)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{directory.path().toStdWString()};
#else
  return std::filesystem::path{directory.path().toStdString()};
#endif
}

genplusgx::movies::InputMovie fixture()
{
  genplusgx::movies::InputMovie movie;
  movie.descriptor = {
    .gameSha256 = std::string(64U, 'a'),
    .settingsSha256 = std::string(64U, 'b'),
    .coreVersion = "0123456789ab",
  };
  movie.metadata = {
    .author = "Synthetic test author",
    .notes = "Original deterministic fixture; no commercial data.",
    .rerecordCount = 7U,
  };
  movie.startFrame = 42U;
  movie.initialState.resize(2'048U);
  for (std::size_t index = 0U; index < movie.initialState.size(); ++index) {
    movie.initialState[index] = static_cast<std::uint8_t>(index & 0xffU);
  }
  genplusgx::InputSnapshot first;
  first.players[0].connected = true;
  first.players[0].buttons = genplusgx::buttonMask(genplusgx::InputButton::right);
  genplusgx::InputSnapshot second = first;
  second.players[0].buttons = genplusgx::InputButton::a |
    genplusgx::InputButton::start;
  second.players[1].connected = true;
  second.players[1].analogX = -12'345;
  second.players[1].analogY = 23'456;
  movie.frames = {first, first, first, second, second, first};
  return movie;
}

QByteArray readFile(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  QFile file{QString::fromStdWString(path.wstring())};
#else
  QFile file{QString::fromStdString(path.string())};
#endif
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

bool writeFile(const std::filesystem::path& path, const QByteArray& bytes)
{
#if defined(Q_OS_WIN)
  QFile file{QString::fromStdWString(path.wstring())};
#else
  QFile file{QString::fromStdString(path.string())};
#endif
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
    file.write(bytes) == bytes.size();
}

QByteArray withFreshDigest(QByteArray content)
{
  content.append(QCryptographicHash::hash(content, QCryptographicHash::Sha256));
  return content;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory creation failed")) {
    return 1;
  }
  const auto root = nativePath(temporary);
  const auto firstPath = root / "fixture.gpgx-movie";
  const auto secondPath = root / "deterministic.gpgx-movie";
  auto movie = fixture();
  if (!check(movie.valid(), "Valid synthetic movie was rejected") ||
      !check(genplusgx::movies::saveMovie(firstPath, movie),
        "Input movie could not be saved") ||
      !check(genplusgx::movies::saveMovie(secondPath, movie),
        "Second input movie could not be saved")) {
    return 1;
  }
  const auto firstBytes = readFile(firstPath);
  const auto secondBytes = readFile(secondPath);
  const auto loaded = genplusgx::movies::loadMovie(firstPath);
  if (!check(!firstBytes.isEmpty() && firstBytes == secondBytes,
        "Movie encoding was not deterministic") ||
      !check(firstBytes.size() < 3'000,
        "Run-length movie encoding did not remain compact") ||
      !check(loaded.status && loaded.movie == movie,
        "Movie round-trip changed deterministic data")) {
    return 1;
  }

  auto wrong = movie.descriptor;
  wrong.gameSha256 = std::string(64U, 'c');
  if (!check(genplusgx::movies::compatibleMovie(movie, movie.descriptor),
        "Matching movie identity was rejected") ||
      !check(genplusgx::movies::compatibleMovie(movie, wrong).error ==
          genplusgx::movies::MovieError::incompatibleMovie,
        "Wrong-game movie was accepted")) {
    return 1;
  }
  wrong = movie.descriptor;
  wrong.settingsSha256 = std::string(64U, 'd');
  if (!check(!genplusgx::movies::compatibleMovie(movie, wrong),
        "Wrong-settings movie was accepted")) {
    return 1;
  }
  wrong = movie.descriptor;
  wrong.coreVersion = "different";
  if (!check(!genplusgx::movies::compatibleMovie(movie, wrong),
        "Wrong-core movie was accepted")) {
    return 1;
  }

  auto edited = movie;
  auto replacement = edited.frames[0];
  replacement.players[0].buttons = genplusgx::buttonMask(
    genplusgx::InputButton::b);
  if (!check(genplusgx::movies::setFrame(edited, 1U, replacement),
        "TAS frame edit failed") ||
      !check(genplusgx::movies::insertFrames(edited, 2U, 2U, replacement),
        "TAS frame insertion failed") ||
      !check(genplusgx::movies::eraseFrames(edited, 0U, 1U),
        "TAS frame deletion failed") ||
      !check(genplusgx::movies::branchFrom(edited, 3U),
        "TAS branch failed") ||
      !check(edited.frames.size() == 4U &&
          edited.metadata.rerecordCount == movie.metadata.rerecordCount + 4U,
        "TAS edit accounting was incorrect") ||
      !check(!genplusgx::movies::eraseFrames(
          edited, 0U, edited.frames.size()),
        "TAS editor allowed deleting the entire timeline")) {
    return 1;
  }

  auto invalid = movie;
  invalid.frames[0].players[0].buttons = 0x8000U;
  if (!check(!invalid.valid() &&
        genplusgx::movies::saveMovie(root / "invalid.gpgx-movie", invalid).error ==
          genplusgx::movies::MovieError::invalidMovie,
        "Unsupported input bits were accepted") ||
      !check(genplusgx::movies::saveMovie(
          "relative.gpgx-movie", movie).error ==
            genplusgx::movies::MovieError::invalidPath,
        "Relative movie destination was accepted")) {
    return 1;
  }
  invalid = movie;
  invalid.frames[0].sequence = 1U;
  if (!check(!invalid.valid(),
        "Non-canonical movie input sequence was accepted")) {
    return 1;
  }
  auto saturated = movie;
  saturated.metadata.rerecordCount =
    std::numeric_limits<std::uint64_t>::max();
  if (!check(genplusgx::movies::setFrame(saturated, 0U, replacement),
        "TAS edit at saturated rerecord count failed") ||
      !check(saturated.metadata.rerecordCount ==
          std::numeric_limits<std::uint64_t>::max(),
        "TAS rerecord counter overflowed")) {
    return 1;
  }

  for (qsizetype mutation = 0; mutation < 64; ++mutation) {
    auto corrupt = firstBytes;
    const auto offset = static_cast<qsizetype>(
      (static_cast<std::uint64_t>(mutation) *
       static_cast<std::uint64_t>(corrupt.size())) / 64U);
    corrupt[std::min(offset, corrupt.size() - 1)] ^= static_cast<char>(0x5a);
    const auto path = root / ("corrupt-" + std::to_string(mutation) +
      ".gpgx-movie");
    if (!check(writeFile(path, corrupt), "Corrupt fixture could not be written") ||
        !check(genplusgx::movies::loadMovie(path).status.error ==
            genplusgx::movies::MovieError::corruptFile,
          "Bounded corruption mutation was accepted")) {
      return 1;
    }
  }
  if (!check(writeFile(root / "truncated.gpgx-movie", firstBytes.first(12)),
        "Truncated fixture could not be written") ||
      !check(!genplusgx::movies::loadMovie(
          root / "truncated.gpgx-movie").status,
        "Truncated movie was accepted")) {
    return 1;
  }
  auto unsupportedContent = firstBytes.chopped(32);
  unsupportedContent[8] = 2;
  const auto unsupportedPath = root / "unsupported.gpgx-movie";
  auto trailingContent = firstBytes.chopped(32);
  trailingContent.append(static_cast<char>(0x7f));
  const auto trailingPath = root / "trailing.gpgx-movie";
  if (!check(writeFile(unsupportedPath,
          withFreshDigest(std::move(unsupportedContent))),
        "Unsupported-version movie could not be written") ||
      !check(genplusgx::movies::loadMovie(unsupportedPath).status.error ==
          genplusgx::movies::MovieError::unsupportedVersion,
        "An unknown movie schema was not distinguished") ||
      !check(writeFile(trailingPath,
          withFreshDigest(std::move(trailingContent))),
        "Trailing-data movie could not be written") ||
      !check(genplusgx::movies::loadMovie(trailingPath).status.error ==
          genplusgx::movies::MovieError::corruptFile,
        "Checksummed trailing movie data was accepted")) {
    return 1;
  }
  return 0;
}
