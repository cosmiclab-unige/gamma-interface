#include "serialreader.h"
#include <QtEndian>

SerialReader::SerialReader(QObject* parent) : QObject(parent)
{
    connect(&serial, &QSerialPort::readyRead, this, &SerialReader::onReadyRead);
}

void SerialReader::connectPort(const QString& portName, int baudRate)
{
    if (serial.isOpen())
        serial.close();

    serial.setPortName(portName);
    serial.setBaudRate(baudRate);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::ReadWrite)) {
        emit error("Open failed: " + serial.errorString());
        return;
    }

    buffer.clear();
}

void SerialReader::disconnectPort()
{
    serial.close();
}

void SerialReader::sendLine(const QString& line)
{
    if (!serial.isOpen()) {
        emit error("Serial not open");
        return;
    }

    QByteArray data = line.toUtf8();
    if (!data.endsWith('\n'))
        data.append('\n');

    serial.write(data);
}

// -------- Frame format (from Linux) --------
static constexpr int HEADER_SIZE = 3;
static constexpr int TIMESTAMP_SIZE = 4;
static constexpr int NUM_CH = 64;
static constexpr int BYTES_PER_CH = 2;
static constexpr int PAYLOAD_SIZE = NUM_CH * BYTES_PER_CH;   // 128
static constexpr int FRAME_SIZE = HEADER_SIZE + TIMESTAMP_SIZE + PAYLOAD_SIZE; // 135

static const QByteArray HDR = QByteArray::fromHex("3c3e00");

// little-endian u32
static inline quint32 readLE32(const char* p)
{
    return quint32(quint8(p[0])) |
           (quint32(quint8(p[1])) << 8) |
           (quint32(quint8(p[2])) << 16) |
           (quint32(quint8(p[3])) << 24);
}

// big-endian u16
static inline quint16 readBE16(const char* p)
{
    return (quint16(quint8(p[0])) << 8) | quint16(quint8(p[1]));
}

void SerialReader::onReadyRead()
{
    buffer.append(serial.readAll());

    static bool haveT0 = false;
    static quint32 t0 = 0;

    while (true) {

        // Find first header
        int idx = buffer.indexOf(HDR);
        if (idx < 0) {
            // keep last few bytes in case header splits
            if (buffer.size() > 2048)
                buffer = buffer.right(256);
            return;
        }

        if (idx > 0)
            buffer.remove(0, idx);

        // Need full frame
        if (buffer.size() < FRAME_SIZE)
            return;

        // 🔥 LINUX-STYLE VALIDATION
        // Check next header position
        int next = buffer.indexOf(HDR, FRAME_SIZE);
        if (next != -1 && next != FRAME_SIZE) {
            // Corrupt frame → drop 1 byte and resync
            buffer.remove(0, 1);
            continue;
        }

        // Now frame is trusted
        const char* frame = buffer.constData();

        quint32 ts = readLE32(frame + HEADER_SIZE);
        if (!haveT0) { haveT0 = true; t0 = ts; }
        double tSec = double(ts - t0) / 1e6;

        const char* payload = frame + HEADER_SIZE + TIMESTAMP_SIZE;

        QVector<double> volts(NUM_CH);
        for (int ch = 0; ch < NUM_CH; ++ch) {
            quint16 raw = readBE16(payload + ch * 2);
            volts[ch] = double(raw) * 4.09 / 65535.0;   // Linux scaling
        }

        emit newFrame(tSec, volts);
        emit rawLine(QString("ts=%1 v0=%2").arg(ts).arg(volts[0],0,'f',3));

        buffer.remove(0, FRAME_SIZE);
    }
}
