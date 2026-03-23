#include "screenmirror/AirPlayPairing.h"
#include "screenmirror/AirPlayTlv.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

#include <cstring>

namespace AIO::ScreenMirror {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AirPlayPairing::AirPlayPairing() = default;
AirPlayPairing::~AirPlayPairing() = default;

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

void AirPlayPairing::init(const QString &pairingId, const QByteArray &ltsk,
                          const QByteArray &ltpk) {
  pairingId_ = pairingId;

  if (ltsk.size() == 32 && ltpk.size() == 32) {
    ltsk_ = ltsk;
    ltpk_ = ltpk;
    return;
  }

  // Generate a fresh Ed25519 key pair.
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  EVP_PKEY_keygen_init(pctx);
  EVP_PKEY *pkey = nullptr;
  EVP_PKEY_keygen(pctx, &pkey);
  EVP_PKEY_CTX_free(pctx);

  ltpk_.resize(32);
  ltsk_.resize(32);
  size_t pubLen = 32, privLen = 32;
  EVP_PKEY_get_raw_public_key(
      pkey, reinterpret_cast<unsigned char *>(ltpk_.data()), &pubLen);
  EVP_PKEY_get_raw_private_key(
      pkey, reinterpret_cast<unsigned char *>(ltsk_.data()), &privLen);
  EVP_PKEY_free(pkey);
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------

void AirPlayPairing::reset() {
  clientEd25519Pk_.clear();
  pvPeerX25519_.clear();
  pvMyX25519Pub_.clear();
  sharedSecret_.clear();
  pvSessionKey_.clear();
  verifyStep_ = 0;
  verifyComplete_ = false;
  // Don't reset setupComplete_ — pairing identity persists.
}

// ---------------------------------------------------------------------------
// handlePairSetup() — Ed25519 public key exchange
//
// Supports two encodings:
//   1. Raw:  32-byte Ed25519 public key (older AirPlay clients)
//   2. TLV8: HAP pair-setup M1 {State=1, Method=0} (iOS 14+)
//            Response: TLV8 {State=2, PublicKey=ltpk_}
// ---------------------------------------------------------------------------

QByteArray AirPlayPairing::handlePairSetup(const QByteArray &body) {
  // ── Raw 32-byte Ed25519 public key ──
  if (body.size() == 32) {
    clientEd25519Pk_ = body;
    setupComplete_ = true;
    qDebug(
        "AirPlayPairing: pair-setup — stored client Ed25519 pk (%lld bytes), "
        "returning ours",
        static_cast<long long>(body.size()));
    return ltpk_;
  }

  // ── TLV8-encoded pair-setup ──
  const auto tlv = Tlv8::decode(body);
  if (tlv.contains(TlvType::State)) {
    const uint8_t state = tlv[TlvType::State].isEmpty()
                              ? 0
                              : static_cast<uint8_t>(tlv[TlvType::State][0]);
    qInfo("AirPlayPairing: pair-setup TLV8 M%d (%lld bytes)", state,
          static_cast<long long>(body.size()));

    if (state == 1) {
      // M1: client initiates. If a PublicKey is included, store it.
      if (tlv.contains(TlvType::PublicKey) &&
          tlv[TlvType::PublicKey].size() == 32) {
        clientEd25519Pk_ = tlv[TlvType::PublicKey];
        setupComplete_ = true;
      }
      // Respond with M2: {State=2, PublicKey=our_ltpk}
      QHash<quint8, QByteArray> resp;
      resp[TlvType::State] = QByteArray(1, 0x02);
      resp[TlvType::PublicKey] = ltpk_;
      return Tlv8::encode(resp);
    }
    if (state == 3) {
      // M3: client sends proof/encrypted data. For transient pairing
      // (no long-term storage) we accept and respond with M4.
      if (tlv.contains(TlvType::PublicKey) &&
          tlv[TlvType::PublicKey].size() == 32) {
        clientEd25519Pk_ = tlv[TlvType::PublicKey];
      }
      setupComplete_ = true;
      QHash<quint8, QByteArray> resp;
      resp[TlvType::State] = QByteArray(1, 0x04);
      return Tlv8::encode(resp);
    }
  }

  qWarning("AirPlayPairing: pair-setup unexpected body size %lld (expected 32)",
           static_cast<long long>(body.size()));
  return ltpk_;
}

// ---------------------------------------------------------------------------
// handlePairVerify() — X25519 ECDH + Ed25519 signatures
//
// Supports two encodings:
//   1. Raw binary: [flag,0,0,0] + payload (original implementation)
//   2. TLV8: HAP pair-verify M1/M3 (iOS 14+)
//      M1: {State=1, PublicKey=32-byte X25519}
//      M3: {State=3, EncryptedData or Signature=64-byte Ed25519}
// ---------------------------------------------------------------------------

QByteArray AirPlayPairing::handlePairVerify(const QByteArray &body) {
  if (body.size() < 4)
    return {};

  // ── Check for TLV8 encoding ──
  // TLV8 pair-verify: first byte is a TLV type (0x06=State), not 0x00 or 0x01.
  // Also detect by trying TLV8 decode and seeing if State tag is present.
  const auto tlv = Tlv8::decode(body);
  if (tlv.contains(TlvType::State)) {
    const uint8_t state = tlv[TlvType::State].isEmpty()
                              ? 0
                              : static_cast<uint8_t>(tlv[TlvType::State][0]);

    if (state == 1 && tlv.contains(TlvType::PublicKey)) {
      // ── TLV8 M1 — X25519 key exchange ──
      const QByteArray clientX25519 = tlv[TlvType::PublicKey];
      if (clientX25519.size() != 32) {
        qWarning("AirPlayPairing: pair-verify TLV8 M1 bad PublicKey size %lld",
                 static_cast<long long>(clientX25519.size()));
        return {};
      }

      verifyStep_ = 0;
      verifyComplete_ = false;
      pvPeerX25519_ = clientX25519;

      EVP_PKEY_CTX *keyCTX = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
      EVP_PKEY_keygen_init(keyCTX);
      EVP_PKEY *myKey = nullptr;
      EVP_PKEY_keygen(keyCTX, &myKey);
      EVP_PKEY_CTX_free(keyCTX);

      pvMyX25519Pub_.resize(32);
      size_t pl = 32;
      EVP_PKEY_get_raw_public_key(
          myKey, reinterpret_cast<unsigned char *>(pvMyX25519Pub_.data()), &pl);

      EVP_PKEY *peerKey = EVP_PKEY_new_raw_public_key(
          EVP_PKEY_X25519, nullptr,
          reinterpret_cast<const unsigned char *>(clientX25519.constData()),
          32);

      EVP_PKEY_CTX *dCTX = EVP_PKEY_CTX_new(myKey, nullptr);
      EVP_PKEY_derive_init(dCTX);
      EVP_PKEY_derive_set_peer(dCTX, peerKey);
      size_t sharedLen = 32;
      sharedSecret_.resize(32);
      EVP_PKEY_derive(dCTX,
                      reinterpret_cast<unsigned char *>(sharedSecret_.data()),
                      &sharedLen);

      EVP_PKEY_CTX_free(dCTX);
      EVP_PKEY_free(peerKey);
      EVP_PKEY_free(myKey);

      // Derive session key for encrypting pair-verify sub-TLV data.
      pvSessionKey_ = hkdfSha512(
          sharedSecret_, QByteArrayLiteral("Pair-Verify-Encrypt-Salt"),
          QByteArrayLiteral("Pair-Verify-Encrypt-Info"), 32);

      // Transient pairing M2: send only our X25519 public key.
      // iOS does not expect EncryptedData in M2 for transient mode; including
      // it causes iOS to abort pairing without sending M3.  pvSessionKey_ is
      // still derived above so we can decrypt the client's EncryptedData in M3.

#if 0 // HAP full pair-verify M2 — preserved for non-transient pairing if needed
      const QByteArray accessoryId = pairingId_.toUtf8();
      const QByteArray accessoryInfo =
          pvMyX25519Pub_ + accessoryId + clientX25519;
      const QByteArray signature = ed25519Sign(accessoryInfo);

      QHash<quint8, QByteArray> subTlv;
      subTlv[TlvType::Identifier] = accessoryId;
      subTlv[TlvType::Signature] = signature;
      const QByteArray subTlvEncoded = Tlv8::encode(subTlv);

      QByteArray nonce(4, '\0');
      nonce.append("PV-Msg02");
      const QByteArray encryptedData =
          chacha20Poly1305Encrypt(pvSessionKey_, nonce, subTlvEncoded);
      resp[TlvType::EncryptedData] = encryptedData;
#endif // HAP full pair-verify M2

      verifyStep_ = 1;
      qInfo("AirPlayPairing: pair-verify TLV8 M1 — X25519 exchange, "
            "sending M2 (transient, no EncryptedData)");

      QHash<quint8, QByteArray> resp;
      resp[TlvType::State] = QByteArray(1, 0x02);
      resp[TlvType::PublicKey] = pvMyX25519Pub_;
      return Tlv8::encode(resp);
    }

    if (state == 3 && verifyStep_ == 1) {
      // ── TLV8 M3 — client sends encrypted proof ──
      QByteArray encData;
      if (tlv.contains(TlvType::EncryptedData))
        encData = tlv[TlvType::EncryptedData];

      if (encData.isEmpty()) {
        qWarning("AirPlayPairing: pair-verify TLV8 M3 — no EncryptedData");
        // For transient pairing, accept anyway.
        verifyComplete_ = true;
        verifyStep_ = 2;
        QHash<quint8, QByteArray> resp;
        resp[TlvType::State] = QByteArray(1, 0x04);
        return Tlv8::encode(resp);
      }

      // Decrypt with ChaCha20-Poly1305, nonce="PV-Msg03"
      QByteArray nonce(4, '\0');
      nonce.append("PV-Msg03");
      const QByteArray decrypted =
          chacha20Poly1305Decrypt(pvSessionKey_, nonce, encData);

      if (decrypted.isEmpty()) {
        qWarning("AirPlayPairing: pair-verify TLV8 M3 — decryption failed");
        QHash<quint8, QByteArray> resp;
        resp[TlvType::State] = QByteArray(1, 0x04);
        resp[TlvType::Error] = QByteArray(1, TlvError::Authentication);
        verifyStep_ = 0;
        return Tlv8::encode(resp);
      }

      // Decode sub-TLV: {Identifier, Signature}
      const auto subTlv = Tlv8::decode(decrypted);
      const QByteArray clientId = subTlv.contains(TlvType::Identifier)
                                      ? subTlv[TlvType::Identifier]
                                      : QByteArray();
      const QByteArray clientSig = subTlv.contains(TlvType::Signature)
                                       ? subTlv[TlvType::Signature]
                                       : QByteArray();

      qInfo("AirPlayPairing: pair-verify TLV8 M3 — decrypted, "
            "clientId=%s sig=%lld bytes",
            clientId.constData(), static_cast<long long>(clientSig.size()));

      // iOSDeviceInfo = clientX25519 + clientId + myX25519Pub
      if (!clientEd25519Pk_.isEmpty() && clientSig.size() == 64) {
        const QByteArray iOSDeviceInfo =
            pvPeerX25519_ + clientId + pvMyX25519Pub_;
        if (!ed25519Verify(clientEd25519Pk_, iOSDeviceInfo, clientSig)) {
          qWarning("AirPlayPairing: pair-verify TLV8 M3 — signature FAILED");
          QHash<quint8, QByteArray> resp;
          resp[TlvType::State] = QByteArray(1, 0x04);
          resp[TlvType::Error] = QByteArray(1, TlvError::Authentication);
          verifyStep_ = 0;
          return Tlv8::encode(resp);
        }
      }

      verifyComplete_ = true;
      verifyStep_ = 2;
      qInfo("AirPlayPairing: pair-verify TLV8 M3 — verified, pairing "
            "complete");

      QHash<quint8, QByteArray> resp;
      resp[TlvType::State] = QByteArray(1, 0x04);
      return Tlv8::encode(resp);
    }

    qWarning("AirPlayPairing: pair-verify TLV8 — unexpected state=%d step=%d",
             state, verifyStep_);
    return {};
  }

  // ── Raw binary format (legacy) ──
  const uint8_t flag = static_cast<uint8_t>(body[0]);

  if (flag == 1 && body.size() >= 68) {
    // ── Step 1 ──────────────────────────────────────────────────────────
    verifyStep_ = 0;
    verifyComplete_ = false;

    const QByteArray clientX25519 = body.mid(4, 32);
    const QByteArray clientEd25519 = body.mid(36, 32);

    // Store client Ed25519 key if we didn't get one from pair-setup.
    if (clientEd25519Pk_.isEmpty())
      clientEd25519Pk_ = clientEd25519;

    pvPeerX25519_ = clientX25519;

    // Generate ephemeral X25519 keypair.
    EVP_PKEY_CTX *keyCTX = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    EVP_PKEY_keygen_init(keyCTX);
    EVP_PKEY *myKey = nullptr;
    EVP_PKEY_keygen(keyCTX, &myKey);
    EVP_PKEY_CTX_free(keyCTX);

    pvMyX25519Pub_.resize(32);
    size_t pl = 32;
    EVP_PKEY_get_raw_public_key(
        myKey, reinterpret_cast<unsigned char *>(pvMyX25519Pub_.data()), &pl);

    // ECDH shared secret (needed for session encryption after handshake).
    EVP_PKEY *peerKey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr,
        reinterpret_cast<const unsigned char *>(clientX25519.constData()), 32);

    EVP_PKEY_CTX *dCTX = EVP_PKEY_CTX_new(myKey, nullptr);
    EVP_PKEY_derive_init(dCTX);
    EVP_PKEY_derive_set_peer(dCTX, peerKey);
    size_t sharedLen = 32;
    sharedSecret_.resize(32);
    EVP_PKEY_derive(dCTX,
                    reinterpret_cast<unsigned char *>(sharedSecret_.data()),
                    &sharedLen);

    EVP_PKEY_CTX_free(dCTX);
    EVP_PKEY_free(peerKey);
    EVP_PKEY_free(myKey);

    // Sign (serverX25519 || clientX25519) with our Ed25519 long-term key.
    const QByteArray sigMessage = pvMyX25519Pub_ + clientX25519;
    const QByteArray signature = ed25519Sign(sigMessage);

    verifyStep_ = 1;
    qInfo("AirPlayPairing: pair-verify step 1 — X25519 exchange + signature");

    // Response: 32-byte server X25519 pubkey + 64-byte Ed25519 signature.
    return pvMyX25519Pub_ + signature;
  }

  if (flag == 0 && body.size() >= 68 && verifyStep_ == 1) {
    // ── Step 2 ──────────────────────────────────────────────────────────
    const QByteArray clientSignature = body.mid(4, 64);

    // Client signed (clientX25519 || serverX25519).
    const QByteArray verifyMessage = pvPeerX25519_ + pvMyX25519Pub_;

    if (!ed25519Verify(clientEd25519Pk_, verifyMessage, clientSignature)) {
      qWarning("AirPlayPairing: pair-verify step 2 — signature verification "
               "FAILED");
      verifyStep_ = 0;
      return {};
    }

    verifyComplete_ = true;
    verifyStep_ = 2;
    qInfo("AirPlayPairing: pair-verify step 2 — verified, pairing complete");

    // Response: empty body (caller sends 200 OK).
    return {};
  }

  qWarning("AirPlayPairing: pair-verify — unexpected flag=%d size=%lld step=%d",
           flag, static_cast<long long>(body.size()), verifyStep_);
  return {};
}

// ===========================================================================
// ── Crypto helpers ────────────────────────────────────────────────────────
// ===========================================================================

QByteArray AirPlayPairing::hkdfSha512(const QByteArray &ikm,
                                      const QByteArray &salt,
                                      const QByteArray &info, int outLen) {
  QByteArray result(outLen, '\0');

  EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
  EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);

  // digest, key, salt, info
  const char *digestName = "SHA2-512";
  OSSL_PARAM params[5];
  int idx = 0;
  params[idx++] = OSSL_PARAM_construct_utf8_string(
      "digest", const_cast<char *>(digestName), 0);
  params[idx++] = OSSL_PARAM_construct_octet_string(
      "key", const_cast<char *>(ikm.constData()),
      static_cast<size_t>(ikm.size()));
  params[idx++] = OSSL_PARAM_construct_octet_string(
      "salt", const_cast<char *>(salt.constData()),
      static_cast<size_t>(salt.size()));
  params[idx++] = OSSL_PARAM_construct_octet_string(
      "info", const_cast<char *>(info.constData()),
      static_cast<size_t>(info.size()));
  params[idx] = OSSL_PARAM_construct_end();

  EVP_KDF_derive(kctx, reinterpret_cast<unsigned char *>(result.data()),
                 static_cast<size_t>(outLen), params);
  EVP_KDF_CTX_free(kctx);
  return result;
}

QByteArray AirPlayPairing::ed25519Sign(const QByteArray &message) {
  EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(
      EVP_PKEY_ED25519, nullptr,
      reinterpret_cast<const unsigned char *>(ltsk_.constData()),
      static_cast<size_t>(ltsk_.size()));

  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey);

  size_t sigLen = 0;
  EVP_DigestSign(mdctx, nullptr, &sigLen,
                 reinterpret_cast<const unsigned char *>(message.constData()),
                 static_cast<size_t>(message.size()));

  QByteArray sig(static_cast<int>(sigLen), '\0');
  EVP_DigestSign(mdctx, reinterpret_cast<unsigned char *>(sig.data()), &sigLen,
                 reinterpret_cast<const unsigned char *>(message.constData()),
                 static_cast<size_t>(message.size()));

  EVP_MD_CTX_free(mdctx);
  EVP_PKEY_free(pkey);
  return sig;
}

bool AirPlayPairing::ed25519Verify(const QByteArray &pk,
                                   const QByteArray &message,
                                   const QByteArray &signature) {
  if (pk.size() != 32)
    return false;

  EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(
      EVP_PKEY_ED25519, nullptr,
      reinterpret_cast<const unsigned char *>(pk.constData()),
      static_cast<size_t>(pk.size()));

  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pkey);

  int ok = EVP_DigestVerify(
      mdctx, reinterpret_cast<const unsigned char *>(signature.constData()),
      static_cast<size_t>(signature.size()),
      reinterpret_cast<const unsigned char *>(message.constData()),
      static_cast<size_t>(message.size()));

  EVP_MD_CTX_free(mdctx);
  EVP_PKEY_free(pkey);
  return ok == 1;
}

QByteArray AirPlayPairing::deriveKey(const QByteArray &salt,
                                     const QByteArray &info) const {
  if (sharedSecret_.isEmpty())
    return {};
  // const_cast is safe — HKDF doesn't modify the inputs.
  return const_cast<AirPlayPairing *>(this)->hkdfSha512(sharedSecret_, salt,
                                                        info, 32);
}

QByteArray
AirPlayPairing::chacha20Poly1305Encrypt(const QByteArray &key,
                                        const QByteArray &nonce,
                                        const QByteArray &plaintext) {
  if (key.size() != 32 || nonce.size() != 12)
    return {};

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(
      ctx, EVP_chacha20_poly1305(), nullptr,
      reinterpret_cast<const unsigned char *>(key.constData()),
      reinterpret_cast<const unsigned char *>(nonce.constData()));

  QByteArray ciphertext(plaintext.size() + 16, '\0'); // +16 for auth tag
  int outLen = 0;
  EVP_EncryptUpdate(
      ctx, reinterpret_cast<unsigned char *>(ciphertext.data()), &outLen,
      reinterpret_cast<const unsigned char *>(plaintext.constData()),
      plaintext.size());
  int totalLen = outLen;

  EVP_EncryptFinal_ex(
      ctx, reinterpret_cast<unsigned char *>(ciphertext.data()) + totalLen,
      &outLen);
  totalLen += outLen;

  // Append 16-byte auth tag
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16,
                      reinterpret_cast<unsigned char *>(ciphertext.data()) +
                          totalLen);
  totalLen += 16;

  EVP_CIPHER_CTX_free(ctx);
  ciphertext.resize(totalLen);
  return ciphertext;
}

QByteArray
AirPlayPairing::chacha20Poly1305Decrypt(const QByteArray &key,
                                        const QByteArray &nonce,
                                        const QByteArray &ciphertext) {
  if (key.size() != 32 || nonce.size() != 12 || ciphertext.size() < 16)
    return {};

  const int ctLen = ciphertext.size() - 16; // last 16 bytes are auth tag

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  EVP_DecryptInit_ex(
      ctx, EVP_chacha20_poly1305(), nullptr,
      reinterpret_cast<const unsigned char *>(key.constData()),
      reinterpret_cast<const unsigned char *>(nonce.constData()));

  QByteArray plaintext(ctLen, '\0');
  int outLen = 0;
  EVP_DecryptUpdate(
      ctx, reinterpret_cast<unsigned char *>(plaintext.data()), &outLen,
      reinterpret_cast<const unsigned char *>(ciphertext.constData()), ctLen);

  // Set expected auth tag
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16,
                      const_cast<char *>(ciphertext.constData() + ctLen));

  int finalLen = 0;
  int ok = EVP_DecryptFinal_ex(
      ctx, reinterpret_cast<unsigned char *>(plaintext.data()) + outLen,
      &finalLen);
  EVP_CIPHER_CTX_free(ctx);

  if (ok != 1) {
    qWarning("AirPlayPairing: ChaCha20-Poly1305 decrypt auth tag mismatch");
    return {};
  }
  plaintext.resize(outLen + finalLen);
  return plaintext;
}

} // namespace AIO::ScreenMirror
