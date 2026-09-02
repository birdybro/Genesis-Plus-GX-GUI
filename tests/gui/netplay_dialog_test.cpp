#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/netplay_dialog.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

#include <optional>

class NetplayDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void sessionFormAndDeterministicLockouts()
  {
    using namespace genplusgx;
    using namespace genplusgx::ui;
    MainWindow window;
    window.show();
    auto* action = window.findChild<QAction*>(QStringLiteral("netplayAction"));
    QVERIFY(action != nullptr);
    QVERIFY(!action->isEnabled());

    std::optional<NetplayUiRequest> request;
    window.setNetplayRequestSink([&request](NetplayUiRequest value) {
      request = std::move(value);
      return PersistenceStatus{};
    });
    window.setEmulationControlSink([](EmulationUiOperation, bool) {
      return true;
    });
    window.setAppearanceSettings({
      .theme = settings::ThemeMode::system,
      .developerToolsEnabled = true,
    });
    window.setRunAheadSettingsSink([](const RunAheadConfiguration&) {
      return PersistenceStatus{};
    });
    window.setGameLoaded(std::filesystem::path{"synthetic.md"});
    window.setRunAheadRuntimeState(true, false, false);
    window.setStateSessionReady(true);
    QVERIFY(action->isEnabled());
    action->trigger();
    auto* dialog = window.findChild<NetplayDialog*>(
      QStringLiteral("netplayDialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->isVisible());

    auto* mode = dialog->findChild<QComboBox*>(
      QStringLiteral("netplayModeCombo"));
    auto* host = dialog->findChild<QLineEdit*>(
      QStringLiteral("netplayHostEdit"));
    auto* code = dialog->findChild<QLineEdit*>(
      QStringLiteral("netplaySessionCodeEdit"));
    auto* port = dialog->findChild<QSpinBox*>(
      QStringLiteral("netplayPortSpin"));
    auto* connect = dialog->findChild<QPushButton*>(
      QStringLiteral("netplayConnectButton"));
    QVERIFY(mode != nullptr && host != nullptr && code != nullptr &&
      port != nullptr && connect != nullptr);
    QCOMPARE(code->echoMode(), QLineEdit::Password);
    mode->setCurrentIndex(1);
    QVERIFY(host->isEnabled());
    host->setText(QStringLiteral("localhost"));
    port->setValue(55'456);
    code->setText(QStringLiteral("private-session"));
    QTest::mouseClick(connect, Qt::LeftButton);
    QVERIFY(request.has_value());
    QCOMPARE(request->operation, NetplayUiOperation::join);
    QCOMPARE(request->host, std::string{"localhost"});
    QCOMPARE(request->port, 55'456U);
    QCOMPARE(request->sessionCode, std::string{"private-session"});
    QVERIFY(code->text().isEmpty());

    window.setNetplaySessionState(netplay::NetplaySessionState::listening);
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("resetAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("controllerConfigurationAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("saveStateAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("importStateAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("runAheadAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("overscanFullAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("emulationSpeed150Action"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("debugToolsAction"))->isEnabled());
    window.setRunAheadRuntimeState(true, false, false);
    window.setStateSessionReady(true);
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("runAheadAction"))->isEnabled());
    QVERIFY(!window.findChild<QAction*>(
      QStringLiteral("importStateAction"))->isEnabled());
    QVERIFY(action->isEnabled());

    window.setNetplaySessionState(netplay::NetplaySessionState::disconnected);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("resetAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(
      QStringLiteral("controllerConfigurationAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(
      QStringLiteral("overscanFullAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(
      QStringLiteral("emulationSpeed150Action"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(
      QStringLiteral("debugToolsAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(
      QStringLiteral("runAheadAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(
      QStringLiteral("importStateAction"))->isEnabled());
  }

  void validationAndDisconnect()
  {
    using namespace genplusgx;
    using namespace genplusgx::ui;
    MainWindow window;
    int calls = 0;
    NetplayUiOperation operation = NetplayUiOperation::host;
    window.setNetplayRequestSink([&](NetplayUiRequest request) {
      ++calls;
      operation = request.operation;
      return PersistenceStatus{};
    });
    window.setGameLoaded(std::filesystem::path{"synthetic.md"});
    window.showNetplay();
    auto* dialog = window.findChild<NetplayDialog*>(
      QStringLiteral("netplayDialog"));
    auto* code = dialog->findChild<QLineEdit*>(
      QStringLiteral("netplaySessionCodeEdit"));
    auto* connect = dialog->findChild<QPushButton*>(
      QStringLiteral("netplayConnectButton"));
    code->setText(QStringLiteral("short"));
    QTest::mouseClick(connect, Qt::LeftButton);
    QCOMPARE(calls, 0);
    QVERIFY(!dialog->findChild<QLabel*>(
      QStringLiteral("netplayValidationLabel"))->text().isEmpty());

    window.setNetplaySessionState(netplay::NetplaySessionState::connected);
    auto* disconnect = dialog->findChild<QPushButton*>(
      QStringLiteral("netplayDisconnectButton"));
    QVERIFY(disconnect->isEnabled());
    QTest::mouseClick(disconnect, Qt::LeftButton);
    QCOMPARE(calls, 1);
    QCOMPARE(operation, NetplayUiOperation::disconnect);
  }
};

QTEST_MAIN(NetplayDialogTest)
#include "netplay_dialog_test.moc"
