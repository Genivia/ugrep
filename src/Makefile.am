CLEANFILES =
dist_noinst_SCRIPTS =

bin_PROGRAMS   = ugrep
ugrep_CPPFLAGS = -I$(top_srcdir)/include $(EXTRA_CFLAGS) $(SIMD_FLAGS) $(PTHREAD_CFLAGS) -DPLATFORM=\"$(PLATFORM)\" -DGREP_PATH=\"$(GREP_PATH)\" -DWITH_NO_INDENT
ugrep_SOURCES  = ugrep.cpp cnf.hpp cnf.cpp flag.hpp glob.hpp glob.cpp mmap.hpp output.hpp output.cpp query.hpp query.cpp screen.hpp screen.cpp stats.hpp stats.cpp vkey.hpp vkey.cpp zstream.hpp zopen.h zopen.c
ugrep_LDADD    = $(PTHREAD_LIBS) $(top_builddir)/lib/libreflex.a

# ############################################################
#                       Shell completions                    #
# ############################################################

if ENABLE_FISH_COMPLETION
fishcompletiondir = $(FISH_COMPLETION_DIR)
fishcompletion_DATA = \
	ugrep.fish \
	ug.fish \
	ugrep+.fish \
	ug+.fish
CLEANFILES += $(fishcompletion_DATA)
endif


# This is a GNU-make feature
.DELETE_ON_ERROR:

dist_noinst_SCRIPTS += $(top_srcdir)/completions/fish/compgen
$(fishcompletion_DATA): ugrep
	target=$@ ; cmdname=$${target##*/} ; cmdname=$${cmdname%%.fish} ; \
	$(top_srcdir)/completions/fish/compgen ./$< $$cmdname > $@
