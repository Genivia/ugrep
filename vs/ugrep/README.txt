How to build ugrep with Microsoft Visual Studio 2017
====================================================

Copy the ugrep\include, ugrep\lib, and ugrep\src directories to the Visual Studio ugrep project directory located below this directory to create ugrep\include, ugrep\lib, and ugrep\src.

Download Boost from:

	https://www.boost.org/doc/libs/1_72_0/more/getting_started/windows.html

Below we assume Boost version 1.72 is used.

Boost should be built as a static release library with static runtime linkage from the Visual Studio 2017 command prompt with:

> bootstrap vc141
> b2 -a toolset=msvc-14.1 address-model=32 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex
> b2 -a toolset=msvc-14.1 address-model=64 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex

After this completes, copy stage\lib\libboost_regex-vc141-mt-s-x32-1_72.lib and stage\lib\libboost_regex-vc141-mt-s-x64-1_72.lib here (in the directory of this README). Also copy the downloaded boost directory to boost_1_72_0 to create the boost_1_72_0\boost directory.

Download the zlib 1.2.11 source code from:

	https://www.zlib.net/

Copy the downloaded zlib-1.2.11 directory here (in the directory of this README).

Download the bzip2 1.0.5 "Sources" from:

	http://gnuwin32.sourceforge.net/packages/bzip2.htm

Copy the downloaded bzip2-1.0.5 directory located under bzip2-1.0.5-src\src\bzip2\1.0.5 here.

Download the liblzma 5.2.4 or later from:

	https://sourceforge.net/projects/lzmautils/files/

Change dir to xz-5.2.4/windows/vs2017.
Open xz_win.sln to open Visual Studio 2017.
Select ReleaseMT/Win32 and then build solution liblzma.
Copy ReleaseMT\Win32\liblzma.lib to liblzma-x32.lib here (in the directory of this README).
Select ReleaseMT/x64 and then build solution liblzma.
Copy ReleaseMT\x64\liblzma.lib to liblzma-x64.lib here (in the directory of this README).

Copy directory xz-5.2.4\src\liblzma\api here.

In Visual Studio project properties:

Configuration Properties
	General:
		Use of MFC: Use Standard Windows Libraries
	C/C++
		General:
			Additional Include Directories: $(ProjectDir)\include;$(ProjectDir)\..\boost_1_72_0;$(ProjectDir)\..\zlib-1.2.11;$(ProjectDir)\..\bzip2-1.0.5;$(ProjectDir)\..\api
		Preprocessor: 
			Preprocessor Definitions: WIN32;NDEBUG;_CONSOLE;WITH_NO_INDENT;HAVE_AVX;HAVE_BOOST_REGEX;HAVE_LIBZ;HAVE_LIBBZ2;HAVE_LIBLZMA;WITH_COLOR;ZLIB_WINAPI;NO_GZCOMPRESS;LZMA_API_STATIC;_CRT_NONSTDC_NO_DEPRECATE;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_WARNINGS
		Code Generation:
			Runtime Library:
				Multi-threaded (/MT)
		Precompiled Headers:
			Precompiled Header: Not Using Precompiled Headers
	Linker:
		General:
			Additional Library Directories: $(ProjectDir)\..
		Input:
			Additional Dependencies: setargv.obj;liblzma-x32.lib
