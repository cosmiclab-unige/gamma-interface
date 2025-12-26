#pragma once

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QVector>

class SerialReader : public QObject
{
    Q_OBJECT
public:
    explicit SerialReader(QObject* parent = nullptr);

public slots:
    void connectPort(const QString& portName, int baudRate);
    void disconnectPort();
    void sendLine(const QString& line);

signals:
    void newFrame(double t, const QVector<double>& volts64);
    void rawLine(const QString& line);
    void error(const QString& msg);

private slots:
    void onReadyRead();

private:
    QSerialPort serial;
    QByteArray buffer;
};
