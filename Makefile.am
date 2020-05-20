SUBDIRS = lib src

man1_MANS = man/ugrep.1

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

all-local:	cp2bin

.PHONY:		cp2bin

# to copy the ugrep binary to ugrep/bin (joins ugrep.exe that's already there)
cp2bin:		$(top_builddir)/src/ugrep
		-mkdir -p $(top_builddir)/bin
		-cp -f $< $(top_builddir)/bin
		@echo
		@echo "OK"
		@echo

install-data-hook:
		(mkdir -p $(datadir)/ugrep; \
		 cp -rf patterns $(datadir)/ugrep/)
		@echo " ______________________________________________________ "; \
		 echo "|                                                      |"; \
		 echo "| You have successfully built and installed ugrep.     |"; \
		 echo "|                                                      |"; \
		 echo "| You can use the ugrep command to search files.       |"; \
		 echo "|                                                      |"; \
		 echo "| Useful aliases to consider:                          |"; \
		 echo "|   alias ug     = 'ugrep'                             |"; \
		 echo "|   alias uq     = 'ugrep -Q'                          |"; \
		 echo "|   alias ux     = 'ugrep -UX'                         |"; \
		 echo "|   alias uz     = 'ugrep -z'                          |"; \
		 echo "|   alias grep   = 'ugrep -G'                          |"; \
		 echo "|   alias egrep  = 'ugrep -E'                          |"; \
		 echo "|   alias fgrep  = 'ugrep -F'                          |"; \
		 echo "|   alias xgrep  = 'ugrep -W'                          |"; \
		 echo "|   alias zgrep  = 'ugrep -zG'                         |"; \
		 echo "|   alias zegrep = 'ugrep -zE'                         |"; \
		 echo "|   alias zfgrep = 'ugrep -zF'                         |"; \
		 echo "|   alias xdump  = 'ugrep -X \"\"'                       |"; \
		 echo "|                                                      |"; \
		 echo "| Thank you for using ugrep!                           |"; \
		 echo "|                                                      |"; \
		 echo "|             https://github.com/Genivia/ugrep         |"; \
		 echo "|______________________________________________________|";

.PHONY:		test

test:		$(top_builddir)/src/ugrep
		@echo
		@echo "*** SINGLE-THREADED TESTS ***"
		@echo
		-cd tests; ./verify.sh -J1
		@echo
		@echo "*** MULTI-THREADED TESTS ***"
		@echo
		-cd tests; ./verify.sh
		@echo
		@echo "OK"
		@echo
