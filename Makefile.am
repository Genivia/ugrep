SUBDIRS = src

man1_MANS = man/ugrep.1

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

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
		 echo "|   alias ug    = 'ugrep --color'                      |"; \
		 echo "|   alias ux    = 'ugrep --color -UX'                  |"; \
		 echo "|   alias ugp   = 'ugrep --color --pager               |"; \
		 echo "|   alias uxp   = 'ugrep --color --pager -UX'          |"; \
		 echo "|   alias grep  = 'ugrep --color -G'                   |"; \
		 echo "|   alias egrep = 'ugrep --color -E'                   |"; \
		 echo "|   alias fgrep = 'ugrep --color -F'                   |"; \
		 echo "|   alias xgrep = 'ugrep --color -W'                   |"; \
		 echo "|   alias xdump = 'ugrep --color --pager -Xo \"\"'       |"; \
		 echo "|                                                      |"; \
		 echo "| Thanks for using ugrep.                              |"; \
		 echo "|                                                      |"; \
		 echo "|             https://github.com/Genivia/ugrep         |"; \
		 echo "|______________________________________________________|";
