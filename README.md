![Azahar Emulator](https://azahar-emu.org/resources/images/logo/azahar-name-and-logo.svg)

![GitHub Release](https://img.shields.io/github/v/release/azahar-emu/azahar?label=Current%20Release)
![GitHub Downloads](https://img.shields.io/github/downloads/azahar-emu/azahar/total?logo=github&label=GitHub%20Downloads) <!-- ![Flathub Downloads](https://img.shields.io/flathub/downloads/org.azahar-emu.azahar?logo=Flathub&label=Flathub%20Downloads) -->
![CI Build Status](https://github.com/azahar-emu/azahar/actions/workflows/build.yml/badge.svg)

<b>Azahar</b> is an open-source 3DS emulator project based on Citra.

It was created from the merging of PabloMK7's Citra fork and the Lime3DS project, both of which emerged shortly after Citra was taken down.

The goal of this project is to be the de-facto platform for future development.

# Installation

### Windows

Download the latest release from [Releases](https://github.com/azahar-emu/azahar/releases).

If you are unsure of whether you want to use MSYS2 or MSVC, use MSYS2.

---

### MacOS

To download a build that will work on all Macs, you can download the `macos-universal` build on the [Releases](https://github.com/azahar-emu/azahar/releases) page.

Alternatively, if you wish to download a build specifically for your Mac, you can choose either:

- `macos-arm64` for Apple Silicon Macs
- `macos-x86_64` for Intel Macs

---
### Android
The recommended method of downloading Azahar on Android is via the Google Play store:

<a href='https://play.google.com/store/apps/details?id=io.github.lime3ds.android'><img width='180' alt='Get it on Google Play' src='https://raw.githubusercontent.com/pioug/google-play-badges/06ccd9252af1501613da2ca28eaffe31307a4e6d/svg/English.svg'/></a>

Alternatively, you can install the app using Obtainium:
1. Download and install Obtainium from [here](https://github.com/ImranR98/Obtainium/releases) (use the file named `app-release.apk`)
2. Open Obtainium and click 'Add App'
3. Type `https://github.com/azahar-emu/azahar` into the 'App Source URL' section
4. Click 'Add'
5. Click 'Install'

If you wish, you can also simply install the latest APK from the [Releases](https://github.com/azahar-emu/azahar/releases) page.

Keep in mind that you will not recieve automatic updates when installing via the APK.

---
### Linux

The recommended format for using Azahar on Linux is the Flatpak available on Flathub:

<a href='https://flathub.org/apps/org.azahar_emu.Azahar'><img width='180' alt='Download on Flathub' src='https://dl.flathub.org/assets/badges/flathub-badge-en.png'/></a>

Azahar is also available as an AppImage on the [Releases](https://github.com/azahar-emu/azahar/releases) page.


# Build instructions

Please refer this repository's [wiki](https://github.com/azahar-emu/azahar/wiki/Building-From-Source) for build instructions

# How can I contribute?

### Pull requests

If you want to implement a change and have the technical capability to do so, we would be happy to accept your contributions.

If you are contributing a new feature, it is highly suggested that you first make a Feature Request issue to discuss the addition before writing any code. This is to ensure that your time isn't wasted working on a feature which isn't deemed appropriate for the project.

After creating a pull request, please don't repeatedly merge `master` into your branch. A maintainer will update the branch for you if/ when it is appropriate to do so.

### Language translations

Additionally, we are accepting language translations on [Transifex](https://app.transifex.com/azahar/azahar). If you know a non-english language listed on our Transifex page, please feel free to contribute.

> [!NOTE]
> We are not currently accepting new languages for translation. Please do not request for new languages or language variants to be added.

### Compatibility reports

Even if you don't wish to contribute code or translations, you can help the project by reporting game compatibility data to our compatibility list.

To do so, simply read https://github.com/azahar-emu/compatibility-list/blob/master/CONTRIBUTING.md and follow the instructions.

Contributing compatibility data helps more accurately reflect the current capabilities of the emulator, so it would be highly appreciated if you could go through the reporting process after completing a game.

# Minimum requirements
Below are the minimum requirements to run Azahar:

### Desktop
```
Operating System: Windows 10 (64-bit), MacOS 13.4 (Ventura), or modern 64-bit Linux
CPU: x86-64/ARM64 CPU (Windows for ARM not supported). Single core performance higher than 1,800 on Passmark
GPU: OpenGL 4.3 or Vulkan 1.1 support
Memory: 2GB of RAM. 4GB is recommended
```
### Android
```
Operating System: Android 9.0+ (64-bit)
CPU: Snapdragon 835 SoC or better
GPU: OpenGL ES 3.2 or Vulkan 1.1 support
Memory: 2GB of RAM. 4GB is recommended
```

# What's next?

We share public roadmaps for upcoming releases in the form of GitHub milestones.

You can find these at https://github.com/azahar-emu/azahar/milestones.

# Join the conversation
We have a community Discord server where you can chat about the project, keep up to date with the latest announcements, or coordinate emulator development.

[![](https://dcbadge.vercel.app/api/server/4ZjMpAp3M6)](https://discord.gg/4ZjMpAp3M6)
