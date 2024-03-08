SUBDIRS = lib src

man1_MANS = man/ugrep.1

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

UGREP = $(shell pwd)/$(top_builddir)/bin/ugrep$(EXEEXT)
CONFIGH = $(shell pwd)/$(top_builddir)/config.h

if ENABLE_BASH_COMPLETION
bashcompletiondir = $(BASH_COMPLETION_DIR)
bashcompletion_DATA = \
	completions/bash/ugrep \
	completions/bash/ug \
	completions/bash/ugrep+ \
	completions/bash/ug+
endif

if ENABLE_ZSH_COMPLETION
zshcompletiondir = $(ZSH_COMPLETION_DIR)
dist_zshcompletion_DATA = $(top_builddir)/completions/zsh/_ug
endif

all-local:	cp2bin

.PHONY:		cp2bin

# to copy the ugrep binary to the local ugrep/bin/ugrep and ugrep/bin/ug
cp2bin:		$(top_builddir)/src/ugrep$(EXEEXT)
		rm -f $(top_builddir)/bin/ugrep$(EXEEXT)
		mkdir -p $(top_builddir)/bin && \
		  cp -f $< $(top_builddir)/bin/ugrep$(EXEEXT) && \
		  cd $(top_builddir)/bin && \
		  rm -f ug$(EXEEXT) && \
		  cp -f ugrep$(EXEEXT) ug$(EXEEXT)
		@echo
		@echo "OK"
		@echo

install-exec-hook:
		cp -f $(top_builddir)/bin/ugrep+ $(top_builddir)/bin/ug+ $(DESTDIR)$(bindir)
		rm -f $(DESTDIR)$(bindir)/ug$(EXEEXT)
		cd $(DESTDIR)$(bindir) && cp -f ugrep$(EXEEXT) ug$(EXEEXT)

install-data-hook:
		mkdir -p $(DESTDIR)$(datadir)/ugrep && \
		  cp -rf $(top_srcdir)/patterns $(DESTDIR)$(datadir)/ugrep/
		cd $(DESTDIR)$(mandir)/man1 && \
		  rm -f ug.1 && \
		  $(LN_S) ugrep.1 ug.1
		@if [ "x$(zshcompletiondir)" != "x" ]; then \
		  cd $(zshcompletiondir) && \
		    sed -e 's/^#compdef ug/#compdef ug+/' _ug > _ug+ && \
		    sed -e 's/^#compdef ug/#compdef ugrep/' _ug > _ugrep && \
		    sed -e 's/^#compdef ug/#compdef ugrep+/' _ug > _ugrep+; \
		fi
		@echo " ______________________________________________________ "; \
		 echo "|                                                      |"; \
		 echo "| You have successfully built and installed ugrep.     |"; \
		 echo "|                                                      |"; \
		 echo "| You can use the ugrep & ug commands to search files. |"; \
		 echo "|                                                      |"; \
		 echo "| The ugrep command is for batch use, whereas the ug   |"; \
		 echo "| command is intended for interactive searches using   |"; \
		 echo "| a .ugrep configuration file with your preferences,   |"; \
		 echo "| located in the working directory or home directory.  |"; \
		 echo "| Run 'ug --save-config' to create a .ugrep file.      |"; \
		 echo "|                                                      |"; \
		 echo "| The ugrep+ and ug+ commands are the same as the      |"; \
		 echo "| ugrep and ug commands, but also use filters to       |"; \
		 echo "| search PDFs, documents, e-books, image metadata,     |"; \
		 echo "| when these filter tools are installed:               |"; \
		 echo "|                                                      |"; \
		 echo "|   pdftotext    https://pypi.org/project/pdftotext    |"; \
		 echo "|   antiword     https://github.com/rsdoiel/antiword   |"; \
		 echo "|   pandoc       https://pandoc.org                    |"; \
		 echo "|   exiftool     https://exiftool.sourceforge.net      |"; \
		 echo "|                                                      |"; \
		 echo "| Aliases to consider:                                 |"; \
		 echo "|   alias uq     = 'ug -Q'                             |"; \
		 echo "|   alias uz     = 'ug -z'                             |"; \
		 echo "|   alias ugit   = 'ug --ignore-files'                 |"; \
		 echo "|   alias grep   = 'ug -G'                             |"; \
		 echo "|   alias egrep  = 'ug -E'                             |"; \
		 echo "|   alias fgrep  = 'ug -F'                             |"; \
		 echo "|   alias pgrep  = 'ug -P'                             |"; \
		 echo "|   alias zgrep  = 'ug -zG'                            |"; \
		 echo "|   alias zegrep = 'ug -zE'                            |"; \
		 echo "|   alias zfgrep = 'ug -zF'                            |"; \
		 echo "|   alias zpgrep = 'ug -zP'                            |"; \
		 echo "|   alias ux     = 'ugrep -U --hexdump'                |"; \
		 echo "|   alias zxgrep = 'ugrep -zU --hexdump'               |"; \
		 echo "|   alias xdump  = 'ugrep --hexdump \"\"'                |"; \
		 echo "|   alias zmore  = 'ugrep+ -z -I -+ --pager \"\"'        |"; \
		 echo "|                                                      |"; \
		 echo "| Thank you for using ugrep!                           |"; \
		 echo "|                                                      |"; \
		 echo "|                             https://ugrep.com        |"; \
		 echo "|______________________________________________________|";

uninstall-hook:
		rm -f $(DESTDIR)$(bindir)/ug$(EXEEXT) $(DESTDIR)$(bindir)/ugrep$(EXEEXT)
		rm -f $(DESTDIR)$(bindir)/ug+ $(DESTDIR)$(bindir)/ugrep+
		rm -rf $(DESTDIR)$(datadir)/ugrep

.PHONY:		test

test:		${UGREP}
		@echo
		@echo "*** SINGLE-THREADED TESTS ***"
		@echo
		cd $(top_srcdir)/tests && env UGREP_ABS_PATH=${UGREP} CONFIGH_ABS_PATH=${CONFIGH} ./verify.sh -J1
		@echo
		@echo "*** MULTI-THREADED TESTS ***"
		@echo
		cd $(top_srcdir)/tests && env UGREP_ABS_PATH=${UGREP} CONFIGH_ABS_PATH=${CONFIGH} ./verify.sh
		@echo
		@echo "OK"
		@echo
