How to build ugrep with Microsoft Visual Studio
===============================================

The following instructions apply to MSVC++ 2017 or later.

There are two choices for ugreo option -P: PCRE2 or Boost.Regex.


When using PCRE2 for option -P (preferred)
------------------------------------------

Copy the ugrep\include, ugrep\lib, and ugrep\src directories to the Visual Studio ugrep project directory located below this directory to create ugrep\include, ugrep\lib, and ugrep\src.

Download the latest version of PCRE2 from:

	ftp://ftp.pcre.org/pub/pcre/

Below we assume PCRE2 version 10.34 is used.

Copy the downloaded pcre2-10.34 directory here.

Rename pcre2-10.34\src\config.h.generic as pcre2-10.34\src\config.h

Rename pcre2-10.34\src\pcre2.h as pcre2-10.34\src\pcre2.h

Rename pcre2-10.34\src\pcre2_chartables.c.dist as pcre2-10.34\src\pcre2_chartables.c

Create a new C++ Static Library project in Visual Studio named "pcre2".  This
should be an empty project (remove the .h and .cpp files when present).

Add all of the .h files and a select set of .c files in directory
pcre2-10.34/src to the project sources:

	pcre2_auto_possess.c
	pcre2_chartables.c
	pcre2_compile.c
	pcre2_config.c
	pcre2_context.c
	pcre2_convert.c
	pcre2_dfa_match.c
	pcre2_error.c
	pcre2_extuni.c
	pcre2_find_bracket.c
	pcre2_jit_compile.c
	pcre2_maketables.c
	pcre2_match.c
	pcre2_match_data.c
	pcre2_newline.c
	pcre2_ord2utf.c
	pcre2_pattern_info.c
	pcre2_script_run.c
	pcre2_serialize.c
	pcre2_string_utils.c
	pcre2_study.c
	pcre2_substitute.c
	pcre2_substring.c
	pcre2_tables.c
	pcre2_ucd.c
	pcre2_valid_utf.c
	pcre2_xclass.c

Select Release/x86 then Project Configuration Properties:
	General:
		Configuration Type: Static library (.lib)
		Use of MFC: Use Standard Windows Libraries
	C/C++:
		General:
			Additional Include Directories: C:\Users\<YOUR-PATH-HERE>\ugrep\vs\ugrep\pcre2-10.34\src
		Precompiled Headers:
			Precompiled Header: Not Using Precompiled Headers
		Preprocessor: 
			Preprocessor Definitions: WIN32;NDEBUG;_LIB;HAVE_CONFIG_H;PCRE2_STATIC;PCRE2_CODE_UNIT_WIDTH=8;SUPPORT_JIT
		Code Generation:
			Runtime Library:
				Multi-threaded (/MT)
		Advanced:
			Disable Specific Warnings: 4146;4703

Then build the pcre2.lib static library in Visual Studio.

Copy pcre2.lib to ugrep\vs\ugrep\pcre2-x32.lib.

To build pcre2-x64.lib, repeat the last three steps, selecting Release/x64.

Download zlib 1.2.11 source code from:

	https://www.zlib.net/

Copy directory zlib-1.2.11 with its contents here (in the directory of this README).

Download bzip2 1.0.5 "Sources" from:

	http://gnuwin32.sourceforge.net/packages/bzip2.htm

Copy directory bzip2-1.0.5 located under bzip2-1.0.5-src\src\bzip2\1.0.5 here.

Download liblzma 5.2.4 or later from:

	https://sourceforge.net/projects/lzmautils/files/

Change dir to xz-5.2.4/windows/vs2017.
Open xz_win.sln to open Visual Studio 2017.
Select ReleaseMT/Win32 and then build solution liblzma.
Copy ReleaseMT\Win32\liblzma.lib to liblzma-x32.lib here (in the directory of this README).
Select ReleaseMT/x64 and then build solution liblzma.
Copy ReleaseMT\x64\liblzma.lib to liblzma-x64.lib here (in the directory of this README).

Copy directory xz-5.2.4\src\liblzma\api with its contents here.

Clone lz4 1.9.2 or later from:

	https://github.com/lz4/lz4

Copy directory lz4-dev\lib with its contents here, although only lz4-dev\lib\lz4.h and lz4-dev\lib\lz4.c are required.

Open vs\ugrep\ugrep.sln in Visual Studio.  Upgrade the version if prompted.

Edit Visual Studio project properties to make sure these match the following:

Configuration Properties
	General:
		Use of MFC: Use Standard Windows Libraries
	C/C++
		General:
			Additional Include Directories: $(ProjectDir)\include;$(ProjectDir)\..\pcre2-10.34\src;$(ProjectDir)\..\zlib-1.2.11;$(ProjectDir)\..\bzip2-1.0.5;$(ProjectDir)\..\api;$(ProjectDir)\..\lz4-dev\lib
		Preprocessor: 
			Preprocessor Definitions: WIN32;NDEBUG;_CONSOLE;WITH_NO_INDENT;HAVE_AVX2;HAVE_PCRE2;PCRE2_STATIC;HAVE_LIBZ;HAVE_LIBBZ2;HAVE_LIBLZMA;HAVE_LIBLZ4;WITH_COLOR;ZLIB_WINAPI;NO_GZCOMPRESS;LZMA_API_STATIC;_CRT_NONSTDC_NO_DEPRECATE;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_WARNINGS
		Code Generation:
			Runtime Library:
				Multi-threaded (/MT)
		Precompiled Headers:
			Precompiled Header: Not Using Precompiled Headers
	Linker:
		General:
			Additional Library Directories: $(ProjectDir)\..
		Input:
			Additional Dependencies: setargv.obj;pcre2-x32.lib;liblzma-x32.lib

Then build ugrep in Visual Studio.


When using Boost.Regex for option -P
------------------------------------

Copy the ugrep\include, ugrep\lib, and ugrep\src directories to the Visual Studio ugrep project directory located below this directory to create ugrep\include, ugrep\lib, and ugrep\src.

Download Boost from:

	https://www.boost.org/doc/libs/1_72_0/more/getting_started/windows.html

Below we assume Boost version 1.72 is used.

Boost should be built as a static release library with static runtime linkage from the Visual Studio 2017 command prompt with:

> bootstrap vc141
> b2 -a toolset=msvc-14.1 address-model=32 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex
> b2 -a toolset=msvc-14.1 address-model=64 architecture=x86 runtime-link=static variant=release link=static threading=multi --with-regex

After this completes, copy stage\lib\libboost_regex-vc141-mt-s-x32-1_72.lib and stage\lib\libboost_regex-vc141-mt-s-x64-1_72.lib here (in the directory of this README). Also copy the downloaded boost directory to boost_1_72_0 to create the boost_1_72_0\boost directory.

Download zlib 1.2.11 source code from:

	https://www.zlib.net/

Copy directory zlib-1.2.11 with its contents here (in the directory of this README).

Download bzip2 1.0.5 "Sources" from:

	http://gnuwin32.sourceforge.net/packages/bzip2.htm

Copy directory bzip2-1.0.5 located under bzip2-1.0.5-src\src\bzip2\1.0.5 here.

Download liblzma 5.2.4 or later from:

	https://sourceforge.net/projects/lzmautils/files/

Change dir to xz-5.2.4/windows/vs2017.
Open xz_win.sln to open Visual Studio 2017.
Select ReleaseMT/Win32 and then build solution liblzma.
Copy ReleaseMT\Win32\liblzma.lib to liblzma-x32.lib here (in the directory of this README).
Select ReleaseMT/x64 and then build solution liblzma.
Copy ReleaseMT\x64\liblzma.lib to liblzma-x64.lib here (in the directory of this README).

Copy directory xz-5.2.4\src\liblzma\api with its contents here.

Clone lz4 1.9.2 or later from:

	https://github.com/lz4/lz4

Copy directory lz4-dev\lib with its contents here, although only lz4-dev\lib\lz4.h and lz4-dev\lib\lz4.c are required.

Open vs\ugrep\ugrep.sln in Visual Studio.  Upgrade the version if prompted.

Edit Visual Studio project properties to make sure these match the following:

Configuration Properties
	General:
		Use of MFC: Use Standard Windows Libraries
	C/C++
		General:
			Additional Include Directories: $(ProjectDir)\include;$(ProjectDir)\..\boost_1_72_0;$(ProjectDir)\..\zlib-1.2.11;$(ProjectDir)\..\bzip2-1.0.5;$(ProjectDir)\..\api;$(ProjectDir)\..\lz4-dev\lib
		Preprocessor: 
			Preprocessor Definitions: WIN32;NDEBUG;_CONSOLE;WITH_NO_INDENT;HAVE_AVX2;HAVE_BOOST_REGEX;HAVE_LIBZ;HAVE_LIBBZ2;HAVE_LIBLZMA;HAVE_LIBLZ4;WITH_COLOR;ZLIB_WINAPI;NO_GZCOMPRESS;LZMA_API_STATIC;_CRT_NONSTDC_NO_DEPRECATE;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_WARNINGS
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

Then build ugrep in Visual Studio.
