#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTranslator>
#include <QXmlStreamReader>

#include <iostream>
#include <map>
#include <set>
#include <string_view>

namespace {

struct Catalog final {
  QString language;
  QString sourceLanguage;
  std::map<std::pair<QString, QString>, QString> messages;
  QString error;
};

Catalog readCatalog(const QString& path, bool requireTranslations)
{
  QFile file{path};
  if (!file.open(QIODevice::ReadOnly)) {
    return {
      .language = {},
      .sourceLanguage = {},
      .messages = {},
      .error = QStringLiteral("Could not open %1").arg(path),
    };
  }
  QXmlStreamReader xml{&file};
  Catalog catalog;
  QString context;
  QString source;
  QString translation;
  bool unfinished = false;
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) {
      continue;
    }
    if (xml.name() == QStringLiteral("TS")) {
      catalog.language = xml.attributes().value(
        QStringLiteral("language")).toString();
      catalog.sourceLanguage = xml.attributes().value(
        QStringLiteral("sourcelanguage")).toString();
    } else if (xml.name() == QStringLiteral("name")) {
      context = xml.readElementText();
    } else if (xml.name() == QStringLiteral("source")) {
      source = xml.readElementText(QXmlStreamReader::IncludeChildElements);
    } else if (xml.name() == QStringLiteral("translation")) {
      unfinished = xml.attributes().value(QStringLiteral("type")) ==
        QStringLiteral("unfinished");
      translation = xml.readElementText(
        QXmlStreamReader::IncludeChildElements);
      if (!source.isEmpty()) {
        if (requireTranslations && (unfinished || translation.isEmpty())) {
          catalog.error = QStringLiteral(
            "Missing finished translation in %1: %2").arg(context, source);
          return catalog;
        }
        catalog.messages.insert_or_assign(
          {context, source}, translation);
      }
      source.clear();
      translation.clear();
      unfinished = false;
    }
  }
  if (xml.hasError()) {
    catalog.error = xml.errorString();
  }
  return catalog;
}

std::multiset<QString> placeholders(const QString& value)
{
  static const QRegularExpression expression{
    QStringLiteral(R"re(%(?:L?\d+|n|%))re")};
  std::multiset<QString> result;
  auto match = expression.globalMatch(value);
  while (match.hasNext()) {
    result.insert(match.next().captured());
  }
  return result;
}

bool permittedIdentity(const QString& value)
{
  static const QRegularExpression technical{
    QStringLiteral(
      R"re(^(?:[0-9XY]|(?:(?:Ctrl|Alt|Shift|Meta)\+)*(?:F(?:[1-9]|1[0-2])|[A-Z0-9]|Backspace|Escape|Insert|Space|Tab))$)re")};
  return technical.match(value).hasMatch();
}

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
  if (argc != 5) {
    std::cerr << "Expected source root, TS catalog, lupdate, and QM catalog\n";
    return 2;
  }
  const QString sourceRoot = QString::fromLocal8Bit(argv[1]);
  const QString committedPath = QString::fromLocal8Bit(argv[2]);
  const QString lupdate = QString::fromLocal8Bit(argv[3]);
  const QString qmPath = QString::fromLocal8Bit(argv[4]);

  QTemporaryDir directory;
  if (!directory.isValid()) {
    std::cerr << "Could not create a temporary catalog directory\n";
    return 3;
  }
  const auto extractedPath = QDir{directory.path()}.filePath(
    QStringLiteral("extracted.ts"));
  QProcess extraction;
  extraction.setWorkingDirectory(sourceRoot);
  extraction.start(lupdate,
    {QStringLiteral("desktop/app"), QStringLiteral("desktop/localization"),
      QStringLiteral("desktop/ui"), QStringLiteral("desktop/video"),
      QStringLiteral("-no-obsolete"),
      QStringLiteral("-locations"), QStringLiteral("relative"),
      QStringLiteral("-ts"), extractedPath});
  const bool finished = extraction.waitForFinished(30'000);
  const auto extractionOutput = QString::fromLocal8Bit(
    extraction.readAllStandardOutput() + extraction.readAllStandardError());

  bool passed = true;
  passed &= check(finished && extraction.exitStatus() == QProcess::NormalExit &&
      extraction.exitCode() == 0,
    "lupdate completes for every desktop source");
  passed &= check(!extractionOutput.contains(QStringLiteral("lacks Q_OBJECT")),
    "every tr()-owning widget has a stable runtime translation context");
  if (!passed) {
    std::cerr << extractionOutput.toStdString();
    return 4;
  }

  const auto extracted = readCatalog(extractedPath, false);
  const auto committed = readCatalog(committedPath, true);
  passed &= check(extracted.error.isEmpty() && committed.error.isEmpty(),
    "both generated and committed TS catalogs parse cleanly");
  passed &= check(committed.language == QStringLiteral("en_XA") &&
      committed.sourceLanguage == QStringLiteral("en"),
    "the committed catalog declares its source and pseudo locales");
  passed &= check(committed.messages.size() >= 950U,
    "the catalog covers the complete desktop UI rather than a token sample");

  std::set<std::pair<QString, QString>> extractedKeys;
  std::set<std::pair<QString, QString>> committedKeys;
  for (const auto& [key, value] : extracted.messages) {
    static_cast<void>(value);
    extractedKeys.insert(key);
  }
  for (const auto& [key, translation] : committed.messages) {
    committedKeys.insert(key);
    passed &= check(placeholders(key.second) == placeholders(translation),
      "a pseudo translation preserves every positional placeholder");
    if (key.second == translation && !permittedIdentity(key.second)) {
      std::cerr << "Unexpected identical translation in "
                << key.first.toStdString() << ": "
                << key.second.toStdString() << '\n';
      passed = false;
    }
  }
  passed &= check(extractedKeys == committedKeys,
    "the committed pseudo catalog exactly matches current translatable sources");

  QTranslator binaryCatalog;
  passed &= check(binaryCatalog.load(qmPath) && !binaryCatalog.isEmpty(),
    "lrelease produced a readable, non-empty binary catalog");
  return passed ? 0 : 1;
}
