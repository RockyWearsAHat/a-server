#pragma once

#include <QByteArray>
#include <QString>

namespace AIO::ScreenMirror {

/// AirPlay pairing handler for screen mirroring (transient mode).
///
/// Implements:
///   pair-setup  — Simple Ed25519 public key exchange (32 bytes each way).
///   pair-verify — Raw binary X25519 ECDH + Ed25519 signature handshake.
///
/// All request/response bodies are raw binary bytes (content-type
/// application/octet-stream).
///
/// Thread safety: not thread-safe.  Call from the Qt event loop only.
class AirPlayPairing {
public:
  AirPlayPairing();
  ~AirPlayPairing();

  AirPlayPairing(const AirPlayPairing &) = delete;
  AirPlayPairing &operator=(const AirPlayPairing &) = delete;

  void init(const QString &pairingId, const QByteArray &ltsk = {},
            const QByteArray &ltpk = {});

  QByteArray publicKey() const { return ltpk_; }
  QByteArray privateKey() const { return ltsk_; }
  QString pairingId() const { return pairingId_; }

  /// Process a /pair-setup request body (raw binary).
  /// Transient: client sends 32-byte Ed25519 pubkey, server returns its own.
  QByteArray handlePairSetup(const QByteArray &body);

  /// Process a /pair-verify request body (raw binary with 4-byte header).
  QByteArray handlePairVerify(const QByteArray &body);

  void reset();

  bool isSetupComplete() const { return setupComplete_; }
  bool isVerifyComplete() const { return verifyComplete_; }

  /// Returns the 32-byte shared secret derived during pair-verify (X25519
  /// ECDH). Empty if pair-verify has not completed.
  QByteArray sharedSecret() const { return sharedSecret_; }

  /// Derive a 32-byte encryption key from the shared secret using HKDF-SHA512.
  /// \a salt and \a info must match the AirPlay 2 protocol specification.
  QByteArray deriveKey(const QByteArray &salt, const QByteArray &info) const;

private:
  QByteArray hkdfSha512(const QByteArray &ikm, const QByteArray &salt,
                        const QByteArray &info, int outLen);
  QByteArray ed25519Sign(const QByteArray &message);
  bool ed25519Verify(const QByteArray &pk, const QByteArray &message,
                     const QByteArray &signature);
  QByteArray chacha20Poly1305Encrypt(const QByteArray &key,
                                     const QByteArray &nonce,
                                     const QByteArray &plaintext);
  QByteArray chacha20Poly1305Decrypt(const QByteArray &key,
                                     const QByteArray &nonce,
                                     const QByteArray &ciphertext);

  QString pairingId_;
  QByteArray ltsk_;
  QByteArray ltpk_;

  QByteArray clientEd25519Pk_;

  QByteArray pvPeerX25519_;
  QByteArray pvMyX25519Pub_;
  QByteArray sharedSecret_;
  QByteArray pvSessionKey_;

  bool setupComplete_ = false;
  bool verifyComplete_ = false;
  int verifyStep_ = 0;
};

} // namespace AIO::ScreenMirror
