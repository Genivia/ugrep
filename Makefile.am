SUBDIRS = src patterns tests/dist

if ENABLE_MANPAGE
man1_MANS = man/ug.1 man/ugrep.1
endif

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

UGREP = $(shell pwd)/$(top_builddir)/src/ugrep$(EXEEXT)
CONFIGH = $(shell pwd)/$(top_builddir)/config.h

if ENABLE_COMPLETIONS
if ENABLE_BASH_COMPLETION
bashcompletiondir = $(BASH_COMPLETION_DIR)
bashcompletion_DATA = \
	completions/bash/ugrep \
	completions/bash/ug \
	completions/bash/ugrep+ \
	completions/bash/ug+
endif
endif


install-data-hook:
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
