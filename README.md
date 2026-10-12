<div align="center">

<img src="docs/assets/damzagram_logo.png" width="128" height="128" alt="DamZaGram logo">

# DamZaGram

**A Telegram client you can program.**

Everything [AyuGram](https://github.com/AyuGram/AyuGramDesktop) has — ghost mode, anti-recall, deep customization —
plus a full **JavaScript plugin system** with hot reload, built-in plugins and a DevTools console.

by [**@wetakush**](https://t.me/wetakush)

[ English | [Русский](README-RU.md) ]

</div>

## Features

### Everything from AyuGram

- Full ghost mode (flexible): no read receipts, no online, no typing, stories viewed in secret
- Message history: deleted and edited messages are kept and marked
- Anti-recall
- Message filters (regex, per chat, shared filter lists)
- Font customization, app icons, streamer mode
- Local Telegram Premium
- Translator
- Enhanced appearance and lots of small tweaks

AyuGram's own documentation still applies to all of these: [docs.ayugram.one](https://docs.ayugram.one/desktop/).

### JavaScript plugins

- **Modern JavaScript.** Plugins run on [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (ES2023):
  `async`/`await`, ES modules, classes, `import`/`export`, top-level `await` in the console.
- **Access to everything.** Chats, messages (send, edit, delete, forward, react, search, download),
  users and accounts, events (`message.new`, `message.edited`, `message.deleted`, `chat.opened`, …),
  message filters, pre-send hooks and `/commands`, context-menu items, buttons in the UI, boxes, toasts,
  native widgets, theme colors, HTTP, files, clipboard.
- **Hot reload.** Save `main.js` and the plugin restarts instantly, no client restart.
- **DevTools console.** Run JavaScript against the live client and watch plugin logs
  (Settings → DamZaGram Preferences → Plugins → DevTools, `Ctrl+Enter` to run).
- **Settings for free.** Describe settings in `manifest.json` and DamZaGram draws the UI.
- **Typed API.** TypeScript definitions in [`docs/plugins/types`](docs/plugins/types) give autocompletion in any editor.

Full documentation: [`docs/plugins`](docs/plugins).

### Built-in plugins

Shipped with the client and enabled in one click (Settings → DamZaGram Preferences → Plugins):

| Plugin | What it does |
|---|---|
| Command palette | `Ctrl+Shift+K`: fuzzy search over chats, plugin commands and client actions |
| Deleted messages | Tells you who deleted what and where; journal of deleted messages |
| Keyword alerts | Notifies when a word or `/regex/` appears in any chat |
| Reminders | `/remind 10m buy bread`, `/remind 18:30 call`, "Remind me…" in the message menu |
| Auto-reply | "Away" mode for private chats |
| Chat stats | Who writes the most, active hours and days, top words and emoji |
| Text tools | `/calc`, wrong-layout fix ("ghbdtn" → "привет"), `/upper` `/mock` `/b64`…, `:shrug:` auto-replace |
| AI digest | "What did I miss": a summary of the latest messages of a chat |

The source of each one lives in [`plugins/builtin`](plugins/builtin) — a good place to learn the API.

### Your first plugin in 30 seconds

Open **Settings → DamZaGram Preferences → Plugins → Open plugins folder** (`<data>/tdata/plugins/`),
create a folder `hello` with two files:

`manifest.json`
```json
{
  "id": "hello",
  "name": "Hello",
  "version": "1.0.0",
  "author": "you",
  "settings": [
    { "key": "word", "type": "string", "title": "Word to watch", "default": "hello" }
  ]
}
```

`main.js`
```js
dzg.on('message.new', (m) => {
  if (!m.out && m.text.includes(dzg.settings.get('word'))) {
    dzg.ui.toast(`${m.fromName} in «${m.chat.name}»: ${m.text}`);
  }
});
```

Turn it on in **Plugins**. Edit `main.js`, save — it reloads by itself.

> Plugins get **full access** to the client, your account, files and network. Install only code you trust.

## Data folder

DamZaGram keeps its data separately from Telegram Desktop and AyuGram, so all three can be installed side by side:

| OS | Folder |
|---|---|
| Linux | `~/.local/share/DamZaGramDesktop` |
| Windows | `%APPDATA%\DamZaGram Desktop` |
| macOS | `~/Library/Application Support/DamZaGram Desktop` |

Use `-workdir <path>` to run with any other folder. Linux application id: `com.damzagram.desktop`.

## Building on Arch Linux

### 1. Dependencies

```bash
sudo pacman -S --needed base-devel git cmake ninja python mold ccache \
  qt6-base qt6-svg qt6-wayland qt6-imageformats qt6-declarative qt6-shadertools qt6-5compat qt6-tools \
  abseil-cpp ada boost fmt range-v3 tl-expected microsoft-gsl extra-cmake-modules wayland-protocols \
  ffmpeg openal openh264 openssl protobuf lz4 xxhash minizip hunspell rnnoise pipewire \
  glibmm-2.68 libsigc++-3.0 gobject-introspection kcoreaddons jemalloc \
  libjxl libavif libheif libvpx libxcomposite libxdamage libxrandr libxtst libxkbcommon libdrm \
  webkit2gtk-4.1 python-packaging
# not in the official repos — take them from the AUR:
yay -S --needed libtg_owt tlottie qrcodegencpp-cmake
```

### 2. tde2e (end-to-end encryption library from TDLib)

```bash
git clone https://github.com/tdlib/td.git ../DamZaGram-deps/tde2e
cmake -S ../DamZaGram-deps/tde2e -B ../DamZaGram-deps/tde2e/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DTD_E2E_ONLY=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/../DamZaGram-deps/tde2e/install"
cmake --build ../DamZaGram-deps/tde2e/build --target install
```

### 3. Configure and build

Get your own `api_id` / `api_hash` at [my.telegram.org](https://my.telegram.org) (see [docs/api_credentials.md](docs/api_credentials.md)).

```bash
git clone --recursive <this repository> DamZaGram && cd DamZaGram
cmake -S . -B out -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTDESKTOP_API_ID=YOUR_API_ID \
  -DTDESKTOP_API_HASH=YOUR_API_HASH \
  -Dtde2e_DIR="$PWD/../DamZaGram-deps/tde2e/install/lib/cmake/tde2e" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold
cmake --build out --target Telegram
./out/bin/DamZaGram
```

Use `-DCMAKE_BUILD_TYPE=Release` for a fast binary (the build takes much longer).

### 4. Add it to the app menu (optional)

```bash
tools/damza/install-desktop-entry.sh            # registers out/bin/DamZaGram for your user
tools/damza/install-desktop-entry.sh --uninstall
```

Or install system-wide with `sudo cmake --install out`.

### Developer tools

- `tools/damza/check.py <file.cpp>` — syntax-only compile of a single file with the real flags, in seconds.
- `tools/damza/sandbox.py <plugin/main.js> [test.js]` — runs a plugin against the real JS API in plain QuickJS with a mocked client.

## Credits

DamZaGram is made by [@wetakush](https://t.me/wetakush). It stands on the shoulders of:

- [**AyuGram Desktop**](https://github.com/AyuGram/AyuGramDesktop) by Radolyn Labs — the base of DamZaGram and all of its ghost-mode
  and customization features. Please support the original authors: [docs.ayugram.one/donate](https://docs.ayugram.one/donate/).
- [**Telegram Desktop**](https://github.com/telegramdesktop/tdesktop) by Telegram FZ-LLC.
- [**QuickJS-ng**](https://github.com/quickjs-ng/quickjs) — the JavaScript engine behind the plugins (MIT).
- Clients AyuGram credits: [Kotatogram](https://github.com/kotatogram/kotatogram-desktop),
  [64Gram](https://github.com/TDesktop-x64/tdesktop), [Forkgram](https://github.com/forkgram/tdesktop).
- Libraries: [JSON for Modern C++](https://github.com/nlohmann/json), [SQLite](https://github.com/sqlite/sqlite),
  [sqlite_orm](https://github.com/fnc12/sqlite_orm), [androidx sources](https://github.com/androidx/androidx).
- Icons: [Solar Icon Set](https://www.figma.com/community/file/1166831539721848736).

## License

DamZaGram, like Telegram Desktop and AyuGram, is free software licensed under the
[GNU GPL version 3](LICENSE) with the OpenSSL exception. See [LEGAL](LEGAL).
DamZaGram is an unofficial client and is not affiliated with Telegram or Radolyn Labs.
