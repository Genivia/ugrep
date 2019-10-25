SUBDIRS = lib src

man1_MANS = man/ugrep.1

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

all-local:	cp2bin

.PHONY:		cp2bin

# to copy the ugrep binary to ugrep/bin (joins ugrep.exe that's already there)
cp2bin:		$(abs_top_builddir)/src/ugrep
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
		 echo "| Here are some useful aliases to consider for ugrep:  |"; \
		 echo "|   alias ug    = 'ugrep --color --pager'              |"; \
		 echo "|   alias ux    = 'ugrep --color --pager -UX'          |"; \
		 echo "|   alias grep  = 'ugrep --color --pager -G'           |"; \
		 echo "|   alias egrep = 'ugrep --color --pager -E'           |"; \
		 echo "|   alias fgrep = 'ugrep --color --pager -F'           |"; \
		 echo "|   alias xgrep = 'ugrep --color --pager -W'           |"; \
		 echo "|   alias xdump = 'ugrep --color --pager -Xo \"\"'       |"; \
		 echo "|                                                      |"; \
		 echo "| Thanks for using ugrep.                              |"; \
		 echo "|                                                      |"; \
		 echo "|             https://github.com/Genivia/ugrep         |"; \
		 echo "|______________________________________________________|";
