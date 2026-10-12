# Установка DamZaGram

Коротко по дистрибутивам. Репозиторий: https://github.com/wetakush/DamZaGram

## Arch Linux и производные (Manjaro, EndeavourOS, CachyOS)

### Готовый пакет

```bash
sudo pacman -U damzagram-7.2.9-1-x86_64.pkg.tar.zst
```

Пакет ставит `/usr/bin/DamZaGram`, ярлык в меню приложений, иконки и
D-Bus-сервис. Все зависимости pacman подтянет из официальных репозиториев.

Удалить пакет: `sudo pacman -R damzagram`. Данные остаются в
`~/.local/share/DamZaGramDesktop`.

### Собрать пакет самому

```bash
# 1. Зависимости
sudo pacman -S --needed base-devel git cmake ninja python gperf mold ccache \
  qt6-base qt6-declarative qt6-imageformats qt6-svg qt6-wayland qt6-shadertools \
  abseil-cpp ada boost cmark-gfm ffmpeg glib2 glib2-devel gobject-introspection \
  hunspell kcoreaddons libavif libfido2 libheif libjpeg-turbo libjxl libpipewire \
  libsrtp libvpx libxcomposite libxdamage libxrandr libxtst lz4 microsoft-gsl \
  minizip openal openh264 openssl opus pipewire range-v3 rnnoise tl-expected \
  tlottie vulkan-headers xxhash libtg_owt

# 2. Исходники
git clone --recursive https://github.com/wetakush/DamZaGram.git
cd DamZaGram

# 3. tde2e (шифрование звонков из TDLib)
git clone https://github.com/tdlib/td.git ../tde2e && git -C ../tde2e checkout 51743df
cmake -S ../tde2e -B ../tde2e/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTD_E2E_ONLY=ON -DCMAKE_INSTALL_PREFIX="$PWD/../tde2e/install"
cmake --build ../tde2e/build && cmake --install ../tde2e/build

# 4. Клиент (Debug-сборка, около 30 минут при первом запуске)
cmake -B out -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTDESKTOP_API_ID=2040 -DTDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627 \
  -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON \
  -Dtde2e_DIR="$PWD/../tde2e/install/lib/cmake/tde2e" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build out --target Telegram

# 5. Пакет и установка
tools/damza/make-arch-package.sh
sudo pacman -U dist/damzagram-*.pkg.tar.zst
```

Запустить без установки: `out/bin/DamZaGram` или `tools/damza/run-dev.sh`.

## Ubuntu, Debian, Linux Mint, Fedora, openSUSE и остальные

Готового пакета нет: бинарник, собранный на Arch, требует свежую glibc и на
этих системах не запустится. Переносимую сборку дают официальный Docker-образ
Telegram Desktop (CentOS, glibc 2.28) и Qt, собранный статически. Получается
один бинарник, который работает на Ubuntu 20.04+, Debian 11+, Fedora,
openSUSE и любом другом современном Linux.

```bash
# Нужен docker (для своего образа ещё docker-buildx и poetry)
git clone --recursive https://github.com/wetakush/DamZaGram.git
cd DamZaGram

# Образ с зависимостями: готовый от Telegram Desktop (быстро)...
docker pull ghcr.io/telegramdesktop/tdesktop/centos_env:latest
IMAGE=ghcr.io/telegramdesktop/tdesktop/centos_env:latest
# ...или свой, собранный из этого репозитория (долго, ~1–3 ч):
#   ./Telegram/build/prepare/linux.sh && IMAGE=tdesktop:centos_env

docker run --rm -it -u $(id -u) -v "$PWD:/usr/src/tdesktop" -e CONFIG=Debug \
  "$IMAGE" /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
  -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

Docker-сборку DamZaGram пока никто не проверял. Если готовый образ
не подойдёт по версиям библиотек, собери свой командой `prepare/linux.sh`.

Бинарник появится в `out/Debug/DamZaGram`. Для релизной сборки убери
`-e CONFIG=Debug` (бинарник будет в `out/Release/`). Положи его куда угодно,
например в `~/.local/bin/`, и запусти. Ярлык в меню при первом запуске клиент
создаёт сам.

Нативно, на системных библиотеках, DamZaGram собирается так же, как на Arch.
Нужны Qt 6 свежей версии, `tg_owt`, `tde2e` и тот же набор библиотек (список
в разделе про Arch). На стабильных Ubuntu и Debian Qt обычно слишком старый,
поэтому там надёжнее Docker.

## Первый запуск

- Данные клиента лежат в `~/.local/share/DamZaGramDesktop`. Это отдельная
  папка от AyuGram и Telegram, войти в аккаунт нужно заново. Другую папку
  можно задать так: `DamZaGram -workdir /путь`.
- Не запускай одну и ту же папку `tdata` одновременно в DamZaGram и в другом
  клиенте: Telegram может сбросить сессию.
- Плагины лежат в Настройки → Плагины. Встроенные (экспорт чата, статистика,
  тема) включены сразу. Свои ставятся оттуда же или кликом по файлу плагина в
  чате.
- Автообновления нет: новые версии ставь пакетом или собирай заново.
- Если раньше ярлык ставился через `tools/damza/install-desktop-entry.sh`, он
  перекрывает ярлык из пакета. Убрать его:
  `tools/damza/install-desktop-entry.sh --uninstall`.
