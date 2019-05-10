SUBDIRS = src

man1_MANS = man/ugrep.1

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

all-local:	cp2bin

.PHONY:		cp2bin

# to copy the ugrep binary to bin
cp2bin:		$(abs_top_builddir)/src/ugrep
		-mkdir -p $(top_builddir)/bin
		-cp -f $< $(top_builddir)/bin

install-data-hook:
		@echo " ______________________________________________________ "; \
		 echo "|                                                      |"; \
		 echo "| You have successfully built and installed ugrep.     |"; \
		 echo "|                                                      |"; \
		 echo "| You can use the ugrep command to search files.       |"; \
		 echo "|                                                      |"; \
		 echo "| Thanks for using ugrep.                              |"; \
		 echo "|                                                      |"; \
		 echo "|             https://github.com/Genivia/ugrep         |"; \
		 echo "|______________________________________________________|";
