#pragma once

#include <QByteArray>
#include <QHash>
#include <cstdint>

namespace AIO::ScreenMirror {

// ---------------------------------------------------------------------------
// HAP TLV8 type codes
// ---------------------------------------------------------------------------
namespace TlvType {
static constexpr quint8 Method = 0x00;
static constexpr quint8 Identifier = 0x01;
static constexpr quint8 Salt = 0x02;
static constexpr quint8 PublicKey = 0x03;
static constexpr quint8 Proof = 0x04;
static constexpr quint8 EncryptedData = 0x05;
static constexpr quint8 State = 0x06;
static constexpr quint8 Error = 0x07;
static constexpr quint8 RetryDelay = 0x08;
static constexpr quint8 Certificate = 0x09;
static constexpr quint8 Signature = 0x0A;
static constexpr quint8 Permissions = 0x0B;
static constexpr quint8 FragmentData = 0x0C;
static constexpr quint8 FragmentLast = 0x0D;
static constexpr quint8 Separator = 0xFF;
} // namespace TlvType

// ---------------------------------------------------------------------------
// HAP TLV8 error codes
// ---------------------------------------------------------------------------
namespace TlvError {
static constexpr quint8 Unknown = 0x01;
static constexpr quint8 Authentication = 0x02;
static constexpr quint8 Backoff = 0x03;
static constexpr quint8 MaxPeers = 0x04;
static constexpr quint8 MaxTries = 0x05;
static constexpr quint8 Unavailable = 0x06;
static constexpr quint8 Busy = 0x07;
} // namespace TlvError

// ---------------------------------------------------------------------------
// TLV8 encode/decode (handles fragmentation for values > 255 bytes)
// ---------------------------------------------------------------------------
namespace Tlv8 {

inline QByteArray encode(const QHash<quint8, QByteArray> &items) {
  QByteArray out;
  for (auto it = items.cbegin(); it != items.cend(); ++it) {
    const quint8 type = it.key();
    const QByteArray &val = it.value();
    qsizetype offset = 0;
    // Empty value still emits a zero-length TLV entry.
    do {
      qsizetype chunk = qMin<qsizetype>(255, val.size() - offset);
      out.append(static_cast<char>(type));
      out.append(static_cast<char>(static_cast<quint8>(chunk)));
      out.append(val.mid(offset, chunk));
      offset += chunk;
    } while (offset < val.size());
  }
  return out;
}

inline QHash<quint8, QByteArray> decode(const QByteArray &data) {
  QHash<quint8, QByteArray> result;
  qsizetype i = 0;
  while (i + 1 < data.size()) {
    const quint8 type = static_cast<quint8>(data[i]);
    const quint8 length = static_cast<quint8>(data[i + 1]);
    i += 2;
    if (i + length > data.size())
      break;
    result[type] += data.mid(i, length); // fragments are concatenated
    i += length;
  }
  return result;
}

} // namespace Tlv8

} // namespace AIO::ScreenMirror
