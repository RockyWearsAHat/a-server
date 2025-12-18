# NAS Server (HTTP)

This project includes a simple LAN-only HTTP NAS server with a web file browser UI.

## Run

From the project root:

```sh
./build/bin/AIOServer --headless --nas --nas-root "$PWD" --nas-port 8080
```

Then open from a device on your network:

- `http://<your-mac-ip>:8080/`

## Security model

- **LAN-only**: requests are accepted only from loopback + private subnets (10/8, 172.16/12, 192.168/16, 169.254/16, fc00::/7).
- **Optional auth**: add `--nas-token <token>` to require `Authorization: Bearer <token>`.

Example:

```sh
./build/bin/AIOServer --headless --nas --nas-root "$PWD" --nas-port 8080 --nas-token "change-me"
```

In the browser/devices, send header:

- `Authorization: Bearer change-me`

(If you want, I can add a tiny login prompt in the UI so you don’t need a header-manipulation tool.)

## Features

- Browse folders and files
- Upload files (raw POST)
- Create folders
- Rename files/folders
- Delete files/folders (recursive for folders)
- Preview common formats (image/audio/video/pdf/text)

## Implementation

- Server: [src/nas/NASServer.cpp](src/nas/NASServer.cpp)
- UI assets: [assets/nas/index.html](assets/nas/index.html), [assets/nas/app.js](assets/nas/app.js), [assets/nas/style.css](assets/nas/style.css)
