#include "genplusgx/ui/about_dialog.h"

#include "genplusgx/version.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace genplusgx::ui {

AboutDialog::AboutDialog(QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("aboutDialog"));
  setWindowTitle(tr("About Genesis Plus GX GUI"));
  setModal(true);
  setMinimumWidth(440);

  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  auto* title = new QLabel(tr("<h2>Genesis Plus GX GUI</h2>"), this);
  title->setObjectName(QStringLiteral("aboutTitleLabel"));
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  auto* version = new QLabel(
    tr("Version %1<br>Git commit %2")
      .arg(QString::fromLatin1(GENPLUSGX_VERSION),
        QString::fromLatin1(GENPLUSGX_GIT_COMMIT)),
    this);
  version->setObjectName(QStringLiteral("aboutVersionLabel"));
  version->setAlignment(Qt::AlignCenter);
  version->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  layout->addWidget(version);

  auto* description = new QLabel(
    tr("A native desktop frontend for the Genesis Plus GX emulator core. "
       "This project is an independent fork and is not officially endorsed by "
       "the upstream Genesis Plus GX project or Sega."),
    this);
  description->setObjectName(QStringLiteral("aboutDescriptionLabel"));
  description->setWordWrap(true);
  description->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  layout->addWidget(description);

  auto* license = new QLabel(
    tr("Genesis Plus GX is distributed under its existing non-commercial license. "
       "No games or proprietary firmware are included."),
    this);
  license->setObjectName(QStringLiteral("aboutLicenseLabel"));
  license->setWordWrap(true);
  license->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  layout->addWidget(license);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("aboutButtonBox"));
  buttons->button(QDialogButtonBox::Close)->setObjectName(QStringLiteral("aboutCloseButton"));
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

} // namespace genplusgx::ui
