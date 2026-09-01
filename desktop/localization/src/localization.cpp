#include "genplusgx/localization/localization.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QTranslator>

#include <algorithm>
#include <utility>

namespace genplusgx::localization {
namespace {

std::string toUtf8(const QString& value)
{
  const auto encoded = value.toUtf8();
  return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

QString normalizedLocale(QString value)
{
  value.replace(u'-', u'_');
  const QLocale locale{value};
  if (locale.language() == QLocale::C) {
    return {};
  }
  return locale.name();
}

bool selectsEnglish(const QString& locale)
{
  return locale == QStringLiteral("en") ||
         locale.startsWith(QStringLiteral("en_"));
}

} // namespace

bool isSupportedLanguagePreference(std::string_view preference) noexcept
{
  return preference == systemLanguage || preference == englishLanguage ||
         preference == pseudoLanguage;
}

std::vector<LanguageChoice> languageChoices()
{
  return {
    {std::string{systemLanguage},
      QCoreApplication::translate("Localization", "System language"), false},
    {std::string{englishLanguage},
      QCoreApplication::translate("Localization", "English"), false},
    {std::string{pseudoLanguage},
      QCoreApplication::translate(
        "Localization", "Pseudo-localization (layout testing)"),
      true},
  };
}

QString languagePreferenceDisplayName(std::string_view preference)
{
  const auto choices = languageChoices();
  const auto found = std::ranges::find_if(
    choices, [preference](const auto& choice) {
      return choice.preference == preference;
    });
  return found == choices.end()
    ? QCoreApplication::translate("Localization", "Unknown")
    : found->displayName;
}

TranslationManager::TranslationManager(
  QCoreApplication& application, QStringList catalogDirectories)
    : application_(application),
      catalogDirectories_(catalogDirectories.empty()
          ? defaultCatalogDirectories()
          : std::move(catalogDirectories)),
      translator_(std::make_unique<QTranslator>())
{
}

TranslationManager::~TranslationManager()
{
  static_cast<void>(application_.removeTranslator(translator_.get()));
}

TranslationStatus TranslationManager::apply(
  std::string_view preference, QStringList systemUiLanguages)
{
  static_cast<void>(application_.removeTranslator(translator_.get()));
  translator_ = std::make_unique<QTranslator>();
  loadedCatalogPath_.clear();
  usedEnglishFallback_ = false;
  requestedLanguage_ = std::string{preference};

  if (!isSupportedLanguagePreference(preference)) {
    activateEnglish(true);
    return {
      .error = TranslationError::invalidPreference,
      .message = "The requested interface language is not supported.",
    };
  }
  if (preference == englishLanguage) {
    activateEnglish(false);
    return {};
  }
  if (preference == pseudoLanguage) {
    return activateCatalog(QString::fromLatin1(
      pseudoLanguage.data(), static_cast<qsizetype>(pseudoLanguage.size())));
  }

  if (systemUiLanguages.empty()) {
    systemUiLanguages = QLocale::system().uiLanguages();
  }
  for (const auto& candidate : systemUiLanguages) {
    auto canonicalCandidate = candidate;
    canonicalCandidate.replace(u'-', u'_');
    if (canonicalCandidate.compare(
          QStringLiteral("en_XA"), Qt::CaseInsensitive) == 0) {
      return activateCatalog(QStringLiteral("en_XA"), true);
    }
    const auto normalized = normalizedLocale(candidate);
    if (normalized.isEmpty()) {
      continue;
    }
    if (selectsEnglish(normalized)) {
      activateEnglish(false, true);
      return {};
    }
  }

  activateEnglish(true, true);
  return {};
}

const std::string& TranslationManager::requestedLanguage() const noexcept
{
  return requestedLanguage_;
}

const std::string& TranslationManager::effectiveLanguage() const noexcept
{
  return effectiveLanguage_;
}

bool TranslationManager::usedEnglishFallback() const noexcept
{
  return usedEnglishFallback_;
}

QString TranslationManager::loadedCatalogPath() const
{
  return loadedCatalogPath_;
}

QStringList TranslationManager::defaultCatalogDirectories()
{
  const QDir executableDirectory{QCoreApplication::applicationDirPath()};
  QStringList directories{
    executableDirectory.filePath(QStringLiteral("translations")),
    executableDirectory.filePath(
      QStringLiteral("../share/Genesis-Plus-GX-GUI/translations")),
    executableDirectory.filePath(QStringLiteral("../Resources/translations")),
  };
  for (auto& directory : directories) {
    directory = QDir::cleanPath(directory);
  }
  directories.removeDuplicates();
  return directories;
}

TranslationStatus TranslationManager::activateCatalog(
  const QString& localeName, bool preserveSystemLocaleOnFailure)
{
  const auto fileName =
    QStringLiteral("genplusgx_%1.qm").arg(localeName);
  for (const auto& directory : catalogDirectories_) {
    const auto path = QDir{directory}.filePath(fileName);
    if (!QFileInfo{path}.isFile()) {
      continue;
    }
    if (!translator_->load(path)) {
      activateEnglish(true, preserveSystemLocaleOnFailure);
      return {
        .error = TranslationError::catalogUnavailable,
        .message = "The interface translation catalog is invalid.",
      };
    }
    if (!application_.installTranslator(translator_.get())) {
      activateEnglish(true, preserveSystemLocaleOnFailure);
      return {
        .error = TranslationError::installationFailed,
        .message = "The interface translation catalog could not be installed.",
      };
    }
    effectiveLanguage_ = toUtf8(localeName);
    usedEnglishFallback_ = false;
    loadedCatalogPath_ = QFileInfo{path}.absoluteFilePath();
    QLocale::setDefault(QLocale{localeName});
    return {};
  }

  activateEnglish(true, preserveSystemLocaleOnFailure);
  return {
    .error = TranslationError::catalogUnavailable,
    .message = "The requested interface translation catalog is unavailable.",
  };
}

void TranslationManager::activateEnglish(
  bool fallback, bool preserveSystemLocale)
{
  effectiveLanguage_ = std::string{englishLanguage};
  usedEnglishFallback_ = fallback;
  loadedCatalogPath_.clear();
  QLocale::setDefault(preserveSystemLocale
      ? QLocale::system()
      : QLocale{QLocale::English, QLocale::UnitedStates});
}

} // namespace genplusgx::localization
