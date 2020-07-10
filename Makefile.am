SUBDIRS = lib src

man1_MANS = man/ugrep.1

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

all-local:	cp2bin

.PHONY:		cp2bin

# to copy the ugrep binary to the local ugrep/bin/ugrep and link ugrep/bin/ug
cp2bin:		$(top_builddir)/src/ugrep$(EXEEXT)
		mkdir -p $(top_builddir)/bin; \
		  cp -f $< $(top_builddir)/bin/ugrep$(EXEEXT); \
		  cd $(top_builddir)/bin; \
		  rm -f ug$(EXEEXT); \
		  $(LN_S) ugrep$(EXEEXT) ug$(EXEEXT)
		@echo
		@echo "OK"
		@echo

install-exec-hook:
		rm -f $(DESTDIR)$(bindir)/ug$(EXEEXT)
		cd $(DESTDIR)$(bindir) && $(LN_S) ugrep$(EXEEXT) ug$(EXEEXT)

install-data-hook:
		mkdir -p $(DESTDIR)$(datadir)/ugrep && \
		  cp -rf patterns $(DESTDIR)$(datadir)/ugrep/
		cd $(DESTDIR)$(mandir)/man1; \
		  rm -f ug.1; \
		  $(LN_S) ugrep.1 ug.1
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
		 echo "| Aliases to consider:                                 |"; \
		 echo "|   alias uq     = 'ug -Q'                             |"; \
		 echo "|   alias ux     = 'ug -UX'                            |"; \
		 echo "|   alias uz     = 'ug -z'                             |"; \
		 echo "|   alias grep   = 'ugrep -G'                          |"; \
		 echo "|   alias egrep  = 'ugrep -E'                          |"; \
		 echo "|   alias fgrep  = 'ugrep -F'                          |"; \
		 echo "|   alias pgrep  = 'ugrep -P'                          |"; \
		 echo "|   alias xgrep  = 'ugrep -UX'                         |"; \
		 echo "|   alias zgrep  = 'ugrep -zG'                         |"; \
		 echo "|   alias zegrep = 'ugrep -zE'                         |"; \
		 echo "|   alias zfgrep = 'ugrep -zF'                         |"; \
		 echo "|   alias zpgrep = 'ugrep -zP'                         |"; \
		 echo "|   alias zxgrep = 'ugrep -zUX'                        |"; \
		 echo "|   alias xdump  = 'ugrep -X \"\"'                       |"; \
		 echo "|                                                      |"; \
		 echo "| Thank you for using ugrep!                           |"; \
		 echo "|                                                      |"; \
		 echo "|             https://github.com/Genivia/ugrep         |"; \
		 echo "|______________________________________________________|";

uninstall-hook:
		rm -f $(DESTDIR)$(bindir)/ug$(EXEEXT)
		rm -rf $(DESTDIR)$(datadir)/ugrep

.PHONY:		test

test:		$(top_builddir)/src/ugrep
		@echo
		@echo "*** SINGLE-THREADED TESTS ***"
		@echo
		cd tests && ./verify.sh -J1
		@echo
		@echo "*** MULTI-THREADED TESTS ***"
		@echo
		cd tests && ./verify.sh
		@echo
		@echo "OK"
		@echo
