How to build ugrep with Microsoft Visual Studio 2017
====================================================

Copy the ugrep/include, ugrep/lib, and ugrep/src directories to the Visual Studio ugrep project directory located below this directory.

Download Boost from:

	https://www.boost.org/doc/libs/1_72_0/more/getting_started/windows.html

Below we assume Boost version 1.72 is used.

Boost should be built as a static release library with static runtime linkage from the Visual Studio 2017 command prompt with:

> bootstrap vc141
> b2 -a toolset=msvc-14.1 address-model=32 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex
> b2 -a toolset=msvc-14.1 address-model=64 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex

After this completes, copy stage\lib\libboost_regex-vc141-mt-s-x32-1_72.lib and stage\lib\libboost_regex-vc141-mt-s-x64-1_72.lib here (in the directory of this README). Also copy the downloaded boost directory to the boost_1_72_0\boost directory.

Download Zlib 1.2.11 from:

	https://www.zlib.net/

Then place the zlib-1.2.11 directory here (in the directory of this README).

In Visual Studio project properties:

Configuration Properties
	General:
		Use of MFC: Use Standard Windows Libraries
	C/C++
		General:
			Additional Include Directories: $(ProjectDir)\include;$(ProjectDir)\..\boost_1_72_0;$(ProjectDir)\..\zlib-1.2.11
		Preprocessor: 
			Preprocessor Definitions: WIN32;NDEBUG;_CONSOLE;HAVE_BOOST_REGEX;HAVE_LIBZ;ZLIB_WINAPI;NO_GZCOMPRESS;_CRT_NONSTDC_NO_DEPRECATE;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_WARNINGS
		Code Generation:
			Runtime Library:
				Multi-threaded (/MT)
		Precompiled Headers:
			Precompiled Header: Not Using Precompiled Headers
	Linker:
		General:
			Additional Library Directories: $(ProjectDir)\..
		Input:
			Additional Dependencies: setargv.obj
