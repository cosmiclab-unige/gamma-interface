#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class SerialReader;

class QComboBox;
class QLineEdit;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshPorts();
    void connectClicked();
    void sendClicked();

private:
    Ui::MainWindow *ui;
    SerialReader *reader = nullptr;

    // controls (created in code, not in .ui)
    QComboBox *comboPort = nullptr;
    QComboBox *comboBaud = nullptr;
    QLineEdit *editCmd = nullptr;
    QPushButton *btnRefresh = nullptr;
    QPushButton *btnConnect = nullptr;
    QPushButton *btnSend = nullptr;

    bool isConnected = false;
};

#endif // MAINWINDOW_H
