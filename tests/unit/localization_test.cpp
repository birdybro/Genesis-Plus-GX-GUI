#include "genplusgx/localization/localization.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  if (argc != 2) {
    std::cerr << "Expected the built pseudo-localization catalog path\n";
    return 2;
  }

  bool passed = true;
  using namespace genplusgx::localization;
  passed &= check(isSupportedLanguagePreference(systemLanguage) &&
      isSupportedLanguagePreference(englishLanguage) &&
      isSupportedLanguagePreference(pseudoLanguage) &&
      !isSupportedLanguagePreference("../../untrusted"),
    "language preference validation is closed over the shipped choices");

  const auto choices = languageChoices();
  passed &= check(choices.size() == 3U &&
      choices[0].preference == systemLanguage &&
      choices[1].preference == englishLanguage &&
      choices[2].preference == pseudoLanguage && choices[2].testingOnly,
    "language choices expose system, English, and the test-only pseudo locale");

  QTemporaryDir directory;
  passed &= check(directory.isValid(),
    "a temporary translation directory is available");
  const auto catalog = QDir{directory.path()}.filePath(
    QStringLiteral("genplusgx_en_XA.qm"));
  passed &= check(QFile::copy(QString::fromLocal8Bit(argv[1]), catalog),
    "the built translation catalog can be staged");

  TranslationManager manager{application, {directory.path()}};
  auto status = manager.apply(englishLanguage);
  passed &= check(status && manager.requestedLanguage() == englishLanguage &&
      manager.effectiveLanguage() == englishLanguage &&
      !manager.usedEnglishFallback() && manager.loadedCatalogPath().isEmpty(),
    "explicit English requires no catalog or fallback");

  const auto hostLocale = QLocale::system().name();
  status = manager.apply(systemLanguage, {QStringLiteral("fr-FR")});
  passed &= check(status && manager.requestedLanguage() == systemLanguage &&
      manager.effectiveLanguage() == englishLanguage &&
      manager.usedEnglishFallback() && QLocale{}.name() == hostLocale,
    "an unavailable system locale falls back to source English without "
    "discarding host formatting");

  status = manager.apply(systemLanguage, {QStringLiteral("en-GB")});
  passed &= check(status && manager.effectiveLanguage() == englishLanguage &&
      !manager.usedEnglishFallback() && QLocale{}.name() == hostLocale,
    "an English system locale uses source text without discarding host formatting");

  status = manager.apply(systemLanguage, {QStringLiteral("en-XA")});
  passed &= check(status && manager.effectiveLanguage() == pseudoLanguage &&
      !manager.usedEnglishFallback(),
    "a supported system pseudo-locale activates its packaged catalog");

  status = manager.apply(pseudoLanguage);
  const auto translatedFile = QCoreApplication::translate(
    "genplusgx::ui::MainWindow", "&File");
  const auto translatedVolume = QCoreApplication::translate(
    "genplusgx::ui::MainWindow", "Volume: %1%2");
  passed &= check(status && manager.requestedLanguage() == pseudoLanguage &&
      manager.effectiveLanguage() == pseudoLanguage &&
      !manager.usedEnglishFallback() &&
      manager.loadedCatalogPath() == QFileInfo{catalog}.absoluteFilePath() &&
      translatedFile.startsWith(QStringLiteral("⟦")) &&
      translatedFile != QStringLiteral("&File") &&
      translatedVolume.contains(QStringLiteral("%1")) &&
      translatedVolume.contains(QStringLiteral("%2")),
    "the pseudo catalog loads and preserves runtime placeholders");

  status = manager.apply("invalid-locale");
  passed &= check(!status &&
      status.error == TranslationError::invalidPreference &&
      manager.requestedLanguage() == "invalid-locale" &&
      manager.effectiveLanguage() == englishLanguage &&
      manager.usedEnglishFallback() &&
      QCoreApplication::translate(
        "genplusgx::ui::MainWindow", "&File") == QStringLiteral("&File"),
    "an invalid explicit locale fails closed to untranslated English");

  TranslationManager missing{application,
    {QDir{directory.path()}.filePath(QStringLiteral("missing"))}};
  status = missing.apply(pseudoLanguage);
  passed &= check(!status &&
      status.error == TranslationError::catalogUnavailable &&
      missing.effectiveLanguage() == englishLanguage &&
      missing.usedEnglishFallback(),
    "an absent explicit catalog produces a visible status and safe fallback");

  status = missing.apply(systemLanguage, {QStringLiteral("en-XA")});
  passed &= check(!status && missing.requestedLanguage() == systemLanguage &&
      missing.effectiveLanguage() == englishLanguage &&
      missing.usedEnglishFallback() && QLocale{}.name() == hostLocale,
    "an absent system catalog falls back without discarding host formatting");

  const auto corruptDirectory = QDir{directory.path()}.filePath(
    QStringLiteral("corrupt"));
  passed &= check(QDir{}.mkpath(corruptDirectory),
    "a corrupt-catalog test directory can be staged");
  QFile corruptCatalog{QDir{corruptDirectory}.filePath(
    QStringLiteral("genplusgx_en_XA.qm"))};
  passed &= check(corruptCatalog.open(QIODevice::WriteOnly) &&
      corruptCatalog.write("not a Qt catalog") > 0 && corruptCatalog.flush(),
    "a corrupt catalog fixture can be staged");
  corruptCatalog.close();
  TranslationManager corrupt{application, {corruptDirectory}};
  status = corrupt.apply(pseudoLanguage);
  passed &= check(!status &&
      status.error == TranslationError::catalogUnavailable &&
      corrupt.effectiveLanguage() == englishLanguage &&
      corrupt.usedEnglishFallback(),
    "a corrupt catalog fails closed to untranslated English");

  const auto defaults = TranslationManager::defaultCatalogDirectories();
  passed &= check(defaults.size() >= 2 &&
      std::ranges::all_of(defaults, [](const auto& path) {
        return !path.isEmpty() && QDir::isAbsolutePath(path);
      }),
    "default catalog lookup is absolute and package-relative");
  return passed ? 0 : 1;
}
