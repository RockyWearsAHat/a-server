# AirPlay Receiver & NAS Browser Architecture

> **Last audited**: 2025-07-18

## AirPlay / Screen Mirror Receiver

### Architecture

Full AirPlay 2 screen mirroring receiver with:

- **Bonjour/mDNS** advertisement (`_airplay._tcp` + `_raop._tcp`)
- **HTTP/RTSP server** on port 7000 (fallback: 7100, 47000, 47001)
- **Ed25519 key pair** persistence (LTSK/LTPK) via QSettings
- **Transient pairing**: No PIN, no allowlist — any local Apple device can connect. The `/pair-setup` and `/pair-verify` endpoints exist because Apple's protocol requires them, but they just exchange keys without enforcing access control.

### State Machine

`Stopped → Advertising → Connected → Mirroring`

### Key Files

| File                                    | Purpose                                                                             |
| --------------------------------------- | ----------------------------------------------------------------------------------- |
| `src/screenmirror/AirPlayReceiver.cpp`  | Core receiver, HTTP server, mDNS                                                    |
| `src/screenmirror/AirPlayPairing.cpp`   | Transient pair-setup (Ed25519 key exchange) + pair-verify (X25519 ECDH + signature) |
| `src/screenmirror/AirPlayMirroring.cpp` | Screen mirror stream handler                                                        |
| `src/gui/ScreenMirrorPage.cpp`          | TV UI: waiting/connected/mirroring views                                            |
| `include/screenmirror/*.h`              | Headers                                                                             |

### Bonjour Records

**\_airplay.\_tcp**:

- `features`: `0x5A7FFFF7,0x1E` (AirPlay 2 screen mirroring — conservative highbits: only AudioRedundant|FPSAPv2pt5_AES_GCM|PhotoCaching|Buffering; higher bits caused iOS to attempt HAP/MFi auth flows we cannot handle)
- `model`: `AppleTV3,2`
- `pk`: Ed25519 public key (hex)
- `pi`: Persistent pairing UUID

**\_raop.\_tcp** (audio):

- `et`: `0,3,5` (none, FairPlay SAPv2.5, transient)
- `cn`: `0,1,2,3` (PCM, ALAC, AAC, AAC-ELD)
- `sr`: `44100`, `ss`: `16`, `ch`: `2`

### HTTP Endpoints

| Endpoint       | Method | Purpose                                  |
| -------------- | ------ | ---------------------------------------- |
| `/info`        | GET    | Device capabilities (binary plist)       |
| `/pair-setup`  | POST   | Transient key exchange (Ed25519 pubkeys) |
| `/pair-verify` | POST   | X25519 ECDH + Ed25519 signature verify   |
| `/fp-setup`    | POST   | FairPlay DRM setup                       |
| `/stream`      | POST   | RTSP-like stream negotiation             |
| `/feedback`    | POST   | Quality metrics from client              |
| `/action`      | POST   | Transport control (play/pause)           |

### Pairing Protocol (Transient)

Minimal handshake required by Apple's AirPlay 2 protocol (no PIN, no persistent device auth):

1. **pair-setup**: Client sends TLV8 M1 `{Method=0, State=1, Flags=0x10}` → server returns TLV8 M2 `{State=2, PublicKey=ltpk}`
2. **pair-verify M1**: Both sides generate ephemeral X25519 keypairs; server returns TLV8 M2 `{State=2, PublicKey=serverX25519}` — **no EncryptedData** (transient: server does not prove identity to client)
3. **pair-verify M3**: Client sends EncryptedData with its identity proof; server decrypts, verifies (or accepts for transient), returns M4 `{State=4}`
4. Connection proceeds to `/stream` — any client with a valid Ed25519 key passes (no allowlist)

**Key constraint**: For transient pairing, pair-verify M2 must contain ONLY `{State=2, PublicKey}`. Adding EncryptedData causes iOS to abort pairing without sending M3.

### Current State

- Discovery + pairing + pair-verify: working (mDNS, Ed25519, X25519 ECDH)
- Post-pairing ChaCha20-Poly1305 encryption: **implemented** (session key derived via HKDF-SHA512 from X25519 shared secret)
- SETUP response with stream ports: **implemented** (returns `eventPort`, `timingPort`, `dataPort`, `streams` array)
- Waiting/mirroring UI: working
- Home screen tile integrated
- **Not yet implemented**: FairPlay video stream decode (follow-on work) — connection negotiation completes but actual H.264 frames cannot be decrypted/displayed

## NAS Browser

### Architecture

Network media browser page for LAN file access.

### Key Files

| File                     | Purpose                             |
| ------------------------ | ----------------------------------- |
| `src/gui/NASPage.cpp`    | Browse UI with D-pad navigation     |
| `src/gui/NASAdapter.cpp` | NavigationAdapter for NAS file list |
| `assets/nas/`            | NAS-specific assets                 |

### Current State

- Basic browse/navigate functionality
- Accessed via Home Screen tile
- Uses NavigationAdapter pattern for D-pad navigation
