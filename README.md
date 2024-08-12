# TF15-Client [![Build Status](https://github.com/Velaron/tf15-client/actions/workflows/build.yml/badge.svg)](https://github.com/Velaron/tf15-client/actions) <img align="right" width="128" height="128" src="https://github.com/Velaron/tf15-client/raw/master/android/app/src/main/ic_launcher-playstore.png" alt="TF15-Client" />
This is my custom tf15client modification

## Download
You can download a build at the [Releases](https://github.com/Velaron/tf15-client/releases/tag/continuous) section, or use these links:
* [Android](https://github.com/Velaron/tf15-client/releases/download/continuous/tf15-client.apk)
* [Linux](https://github.com/Velaron/tf15-client/releases/download/continuous/tf15-client_linux.tar.gz) (not recommended, just use the Steam version)
* [Windows](https://github.com/Velaron/tf15-client/releases/download/continuous/tf15-client_win32.zip) (not recommended, same as above)

## Installation
To run TF15-Client you need the latest developer build of Xash3D FWGS, which you can get [here](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous).
You have to own the [game on Steam](https://store.steampowered.com/app/20/Team_Fortress_Classic/) and copy `valve` and `tfc` folders into your Xash3D FWGS directory.
After that, just install the APK and run.

## Building
Clone the source code:
```
git clone https://github.com/Velaron/tf15-client --recursive
```
### Windows
```
cmake -A Win32 -S . -B build
cmake --build build --config Release
```
### Linux
```
cmake -S . -B build
cmake --build build --config Release
```
### Android
```
cd android
./gradlew assembleRelease
```
