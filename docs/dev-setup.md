# Developer setup

Everything needed to build and test Flowstate v2 on a fresh machine. The versions below are the ones CI uses; newer minor versions are fine.

| Tool | Version | Used for |
| --- | --- | --- |
| Git | any recent | the repo |
| Node.js | 22 or newer (npm included) | schema, cloud, evals, generated files |
| CMake | 3.22 or newer | `core`, the bridge, the plugin |
| Ninja | any recent | the CMake generator every build uses |
| C++20 compiler | GCC 13+, Clang 16+, Apple Clang (Xcode 15+), or MSVC 2022 | `core`, the bridge, the plugin |
| ccache | optional | makes JUCE rebuilds near-instant |
| pluginval | v1.0.4 | plugin validation (download, no install) |

The first plugin build downloads JUCE 9.0.2, nlohmann/json and doctest (about 200 MB), so it needs network access once.

## Windows 10/11

1. Install the **Visual Studio 2022 Build Tools** (or Visual Studio 2022) with the **Desktop development with C++** workload, including the Windows SDK.
2. Install the other tools, from PowerShell:
   ```powershell
   winget install --id Git.Git
   winget install --id OpenJS.NodeJS.LTS
   winget install --id Kitware.CMake
   winget install --id Ninja-build.Ninja
   winget install --id Ccache.Ccache   # optional
   ```
3. WebView2: Windows 11 ships the runtime. On Windows 10, install the **Evergreen WebView2 Runtime** from Microsoft if Edge isn't installed. The build downloads the WebView2 SDK itself.
4. Build from an **x64 Native Tools Command Prompt for VS 2022** (or a shell where `cl` works). Git Bash works for the commands in `testing-plugin.md` if you start it from that prompt.

## macOS 11 or newer

1. Install Xcode (for AU validation and the full SDK), then run `xcode-select --install`.
2. Install the tools:
   ```sh
   brew install cmake ninja node ccache git
   ```
3. `auval` ships with macOS; nothing to install.

## Linux (headless builds only)

Linux builds use a native stand-in editor instead of the WebView. It's for running the tests and pluginval, not for daily use in a DAW.

Arch:
```sh
sudo pacman -S --needed base-devel git cmake ninja nodejs npm ccache \
  alsa-lib freetype2 fontconfig libx11 libxcomposite libxcursor libxext libxi \
  libxinerama libxrandr libxrender mesa
```

Ubuntu / Debian:
```sh
sudo apt-get install -y build-essential git cmake ninja-build nodejs npm ccache \
  libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxi-dev libxinerama-dev libxrandr-dev libxrender-dev \
  libgl1-mesa-dev xvfb
```

If the distribution's Ninja is missing or old, `uv tool install ninja` (or `pipx install ninja`) installs one for your user.

## pluginval

Download the release zip for your OS from https://github.com/Tracktion/pluginval/releases/tag/v1.0.4 (`pluginval_Windows.zip`, `pluginval_macOS.zip` or `pluginval_Linux.zip`) and unzip it anywhere. The binary is `pluginval.exe`, `pluginval.app/Contents/MacOS/pluginval` or `pluginval`.

## Check the setup

From the repo root:

```sh
npm install
npm run build:core && npm run test:core
npm run build:bridge && npm run test:bridge
npm run typecheck
npm test
```

All of these should pass. `testing-plugin.md` covers the plugin itself.

## Keeping the machine usable

JUCE's sources are large, and every plugin target compiles its own copy. On a machine with 4 cores:

- Run **one** build at a time in a build folder. Two builds in the same folder corrupt each other and trigger full rebuilds.
- Limit the jobs and lower the priority: `nice -n 19 cmake --build build/plugin -j2` (on Windows, `cmake --build build/plugin -j2`).
- Turn off link-time optimization locally with `-DFLOWSTATE_PLUGIN_LTO=OFF`. CI keeps it on.
- Use ccache: add `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` when configuring.
- Build only the target you're testing, e.g. `--target flowstate_plugin_tests`.
