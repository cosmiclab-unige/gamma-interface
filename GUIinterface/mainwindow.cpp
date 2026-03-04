#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "serialreader.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

#include <QSerialPortInfo>
#include <QTimer>
#include <QVector>
#include <QPointF>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    reader = new SerialReader(this);

    // ---------- Main layout ----------
    auto *mainLayout = new QVBoxLayout(ui->centralwidget);

    // ---------- Controls row ----------
    auto *controls = new QHBoxLayout();

    controls->addWidget(new QLabel("Port:"));
    comboPort = new QComboBox(this);
    controls->addWidget(comboPort);

    btnRefresh = new QPushButton("Refresh", this);
    controls->addWidget(btnRefresh);

    controls->addSpacing(10);

    controls->addWidget(new QLabel("Baud:"));
    comboBaud = new QComboBox(this);
    comboBaud->addItem("115200", 115200);
    comboBaud->addItem("921600", 921600);
    comboBaud->addItem("1000000", 1000000);
    comboBaud->addItem("2000000", 2000000);
    comboBaud->addItem("3000000", 3000000);
    comboBaud->setCurrentText("3000000");
    controls->addWidget(comboBaud);

    btnConnect = new QPushButton("Connect", this);
    controls->addWidget(btnConnect);

    controls->addSpacing(10);

    controls->addWidget(new QLabel("Cmd:"));
    editCmd = new QLineEdit(this);
    editCmd->setText("start");
    editCmd->setMinimumWidth(200);
    controls->addWidget(editCmd);

    btnSend = new QPushButton("Send", this);
    controls->addWidget(btnSend);

    controls->addStretch(1);
    mainLayout->addLayout(controls);

    // ---------- Chart ----------
    auto *chart = new QChart();
    chart->legend()->hide();
    chart->setTitle("64-channel Real-time Serial Data");

    auto *axisX = new QValueAxis();
    auto *axisY = new QValueAxis();
    axisX->setTitleText("Time (s)");
    axisY->setTitleText("Voltage (V)");

    const double windowSec = 1.0;      // <<< keep last 10 seconds
    axisX->setRange(0, windowSec);
    axisY->setRange(1.5, 2.2);

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);

    QVector<QLineSeries*> series(64);
    for (int ch = 0; ch < 64; ++ch) {
        series[ch] = new QLineSeries(this);
        series[ch]->setUseOpenGL(true);          // <<< BIG performance win
        chart->addSeries(series[ch]);
        series[ch]->attachAxis(axisX);
        series[ch]->attachAxis(axisY);
    }

    auto *view = new QChartView(chart, this);
    view->setRenderHint(QPainter::Antialiasing);
    mainLayout->addWidget(view);

    // ---------- Buffer incoming frames ----------
    struct Frame { double t; QVector<double> v; };
    auto *pending = new QVector<Frame>(); // owned by MainWindow via lambda captures

    connect(reader, &SerialReader::newFrame, this,
            [=](double t, const QVector<double>& v) mutable
            {
                // just buffer data; DO NOT touch the chart here
                pending->push_back(Frame{t, v});
                // prevent unlimited growth if GUI can't keep up
                if (pending->size() > 500)
                    pending->erase(pending->begin(), pending->begin() + 250);
            });

    // ---------- UI refresh timer (30 FPS) ----------
    auto *plotTimer = new QTimer(this);
    plotTimer->setInterval(33); // ~30 fps
    plotTimer->start();

    connect(plotTimer, &QTimer::timeout, this, [=]() mutable
            {
                if (pending->isEmpty())
                    return;

                // Move buffered frames to local copy (fast)
                QVector<Frame> frames = std::move(*pending);
                pending->clear();

                // Append in batches
                double tLast = frames.last().t;
                double tMin = tLast - windowSec;

                for (int ch = 0; ch < 64; ++ch) {
                    QVector<QPointF> pts;
                    pts.reserve(frames.size());
                    for (const auto &f : frames) {
                        if (f.v.size() == 64)
                            pts.append(QPointF(f.t, f.v[ch]));
                    }
                    if (!pts.isEmpty())
                        series[ch]->append(pts);

                    // drop points older than window (time-based)
                    while (series[ch]->count() > 0 && series[ch]->at(0).x() < tMin) {
                        series[ch]->remove(0);
                    }
                }

                // fixed-width scrolling axis
                if (tLast < windowSec)
                    axisX->setRange(0, windowSec);
                else
                    axisX->setRange(tMin, tLast);
            });

    // ---------- Status / errors ----------
    connect(reader, &SerialReader::rawLine, this, [=](const QString& s){
        statusBar()->showMessage("RX: " + s);
    });

    connect(reader, &SerialReader::error, this, [=](const QString& msg){
        statusBar()->showMessage("Serial error: " + msg);
        isConnected = false;
        btnConnect->setText("Connect");
    });

    // ---------- Buttons ----------
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(btnConnect, &QPushButton::clicked, this, &MainWindow::connectClicked);
    connect(btnSend, &QPushButton::clicked, this, &MainWindow::sendClicked);

    refreshPorts();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::refreshPorts()
{
    comboPort->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto &p : ports) {
        const QString label = p.portName() + " (" + p.description() + ")";
        comboPort->addItem(label, p.portName());
    }

    if (comboPort->count() == 0)
        statusBar()->showMessage("No serial ports found.");
    else
        statusBar()->showMessage("Ports refreshed.");
}

void MainWindow::connectClicked()
{
    if (!isConnected) {
        if (comboPort->count() == 0) {
            statusBar()->showMessage("No port selected.");
            return;
        }

        const QString portName = comboPort->currentData().toString();
        const int baud = comboBaud->currentData().toInt();

        reader->connectPort(portName, baud);

        isConnected = true;
        btnConnect->setText("Disconnect");
        statusBar()->showMessage("Connected: " + portName + " @ " + QString::number(baud));
    } else {
        reader->disconnectPort();

        isConnected = false;
        btnConnect->setText("Connect");
        statusBar()->showMessage("Disconnected.");
    }
}

void MainWindow::sendClicked()
{
    const QString cmd = editCmd->text().trimmed();
    if (cmd.isEmpty()) {
        statusBar()->showMessage("Command is empty.");
        return;
    }

    reader->sendLine(cmd);
    statusBar()->showMessage("TX: " + cmd);
}
