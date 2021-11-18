# How to build ugrep with Microsoft Visual Studio

The following instructions apply to MSVC++ 2019 or greater.

## Base-UGREP

- Double-click on ugrep.sln to open in Visual Studio 2019 or later.
- You may be asked to upgrade the projects to the version of Visual Studio that
  you have installed.
- Once the project is open, select the desired platform (x86 or x64), select the
  desired configuration (Base-Debug or Base-Release).
  - Note that the Pcre-Debug and Pcre-Release configurations will not build
    without additional steps as described below.
- Build Solution.

The resulting binary will be in
`...\ugrep\msvc\bin\Base-(configuration)\ugrep.exe`.

## Pcre-UGREP

To add support for PCRE2 regular expressions (use PCRE2 for the `-P` option) and
for searching within compressed files, you'll need to download and extract
additional sources for the required external libraries. For each library
described below, you'll need to download the library, extract it, rename the
library folder to remove the library version number, and move the library folder
next to the ugrep folder.

For example, if my ugrep source code is in `D:\sources\ugrep`, I would download
`pcre2-10.39.tar.bz2`, use `tar xf pcre2-10.39.tar.bz2` to extract it (creating
a folder named `pcre2-10.39`), then I would run
`move pcre2-10.39 D:\sources\pcre2` to move and rename the folder.

- Download [PCRE2 from GitHub](https://github.com/PhilipHazel/pcre2/releases),
  e.g.
  [pcre2-10.39.tar.gz](https://github.com/PhilipHazel/pcre2/releases/download/pcre2-10.39/pcre2-10.39.tar.gz).
  Extract, then rename folder to `pcre2`.
- Download [ZLIB from zlib.net](https://zlib.net), e.g.
  [zlib-1.2.11.tar.gz](https://zlib.net/zlib-1.2.11.tar.gz). Extract, then
  rename folder to `zlib`.
- Download [BZIP2 from sourceware.org](https://sourceware.org/bzip2/), e.g.
  [bzip2-1.0.8.tar.gz](https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz).
  Extract, then rename folder to `bzip2`.
- Download
  [XZ Utils from SourceForge](https://sourceforge.net/projects/lzmautils/files/),
  e.g.
  [xz-5.2.5.tar.gz](https://sourceforge.net/projects/lzmautils/files/xz-5.2.5.tar.gz/download).
  Extract, then rename folder to `xz`.
- Download [LZ4 from GitHub](https://github.com/lz4/lz4/releases), e.g.
  [lz4-1.9.3.tar.gz](https://github.com/lz4/lz4/archive/refs/tags/v1.9.3.tar.gz).
  Extract, then rename folder to `lz4`.
- Download [ZSTD from GitHub](https://github.com/facebook/zstd/releases), e.g.
  [zstd-1.5.0.tar.gz](https://github.com/facebook/zstd/releases/download/v1.5.0/zstd-1.5.0.tar.gz).
  Extract, then rename folder to `zstd`.

You should now be able to build the Pcre-Debug and Pcre-Release configurations.
The resulting binary will be in
`...\ugrep\msvc\bin\Pcre-(configuration)\ugrep.exe`.

## Boost-UGREP

To add support for Boost regular expressions (use Boost-RegEx for the `-P`
option) and for searching within compressed files, you'll need to follow the
instructions for Pcre-UGREP and then use the following steps to enable Boost.

- Download [Boost](https://www.boost.org/users/download/), e.g.
  [boost_1_77_0.tar.gz](https://boostorg.jfrog.io/artifactory/main/release/1.77.0/source/boost_1_77_0.tar.gz).
  Extract this next to your ugrep directory, then rename folder to `boost`.
- Open the "Developer Command Prompt for Visual Studio" from the Start menu.
- CD to the boost directory.
- Run `bootstrap vcNNN`, where NNN corresponds to the toolset version being used
  to build your ugrep project. To determine this value, open ugrep.sln, go to
  properties of the ugrep project, look under Configuration Properties/General,
  and check the value of Platform Toolset. For example, if Platform Toolset is
  "Visual Studio 2019 (v142)", I would run `bootstrap vc142`.
- Build 32-bit and 64-bit libs with the following commands, replacing
  `msvc-14.2` with your toolset version as appropriate.
  - `b2 -a toolset=msvc-14.2 address-model=32 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex`
  - `b2 -a toolset=msvc-14.2 address-model=64 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex`

You should now be able to build the Boost-Debug and Boost-Release configurations.
The resulting binary will be in
`...\ugrep\msvc\bin\Boost-(configuration)\ugrep.exe`.
