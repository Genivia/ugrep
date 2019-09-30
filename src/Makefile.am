bin_PROGRAMS   = ugrep
ugrep_CPPFLAGS = $(PTHREAD_CFLAGS) -DPLATFORM=\"$(PLATFORM)\" -DGREP_PATH=\"$(GREP_PATH)\"
ugrep_SOURCES  = ugrep.cpp glob.cpp
ugrep_LDADD    = $(PTHREAD_LIBS)
