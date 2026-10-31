# Building mxCut

This is for working on mxCut itself. To just use it, see [INSTALL.md](INSTALL.md).

## Linux (Debian / Ubuntu)

```sh
sudo apt install cmake pkg-config ffmpeg libmpv-dev qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick-controls qml6-module-qtquick-dialogs qml6-module-qtquick-layouts \
  qml6-module-qtquick-window qml6-module-qtqml-workerscript qml6-module-qtquick-shapes
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/mxcut
```

## Nix

```sh
nix run github:xnmrph/mxCut   # run it
nix develop                   # shell with every build dependency
```

## Windows (MSYS2 UCRT64)

From an MSYS2 UCRT64 shell (install [MSYS2](https://www.msys2.org) first):

```sh
pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,qt6-base,qt6-declarative,mpv,ffmpeg}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mxcut.exe
# Optional: a self-contained folder and an installer, as CI makes them.
pacman -S mingw-w64-ucrt-x86_64-{qt6-tools,nsis}
bash .github/scripts/package-windows.sh build dist/mxCut
makensis -DVERSION=0.1.0 -DDIST="$(cygpath -w "$PWD/dist/mxCut")" packaging/windows/installer.nsi
```

## How the code is laid out

- `src/MpvItem.*` is the video preview, a Qt Quick item drawn by libmpv's OpenGL render API.
- `src/TrimJob.*` runs one FFmpeg trim and reports its progress.
- `src/ExportQueue.*` is the export queue of TrimJobs, run a few at a time.
- `src/VideoFolderModel.*` lists a folder's subfolders and videos for the file browser: sorted, filtered,
  grouped by day, with thumbnails and durations made by FFmpeg, and each video's queue state.
- `src/Settings.*` holds the saved choices; `src/Encoders.*` maps each encoder to FFmpeg arguments, and
  `src/EncoderProbe.*` finds FFmpeg and tests which encoders work.
- `src/MediaPreview.*` makes the timeline's thumbnails and waveform with FFmpeg and caches them;
  `src/WaveformView.*` draws the waveform.
- `qml/Main.qml` is the window, `qml/Timeline.qml` the ruler, thumbnail and audio tracks with the
  in/out handles, `qml/FileSidebar.qml` the folder-mode file browser, `qml/QueuePanel.qml` the export queue with its
  file names and save folder, and
  `qml/SettingsDialog.qml` the Settings window. `qml/Theme.qml` holds the colours and
  `qml/Icons.qml` the icon shapes.
- `packaging/` has the icon, the Linux menu entry and the Windows installer script;
  `.github/scripts/` builds the Windows folder and the AppImage.
- `tests/` holds tests for the C++ classes, which generate clips and check real FFmpeg output.

Set `MXCUT_MPV_LOG=v` to print mpv's log when debugging playback.

## Releasing

1. Bump the version in `CMakeLists.txt` (`project(mxcut VERSION x.y.z`) and
   `flake.nix` (`version = "x.y.z";`), and push that to `main`.
2. On GitHub, open **Actions > Build > Run workflow**, tick **release**, and
   click **Run workflow**.

When it finishes, the release appears on the Releases page with the Windows
installer, the Windows zip and the AppImage. Every CI run also uploads these as
build artifacts, so a pull request's builds can be tried before a release.
