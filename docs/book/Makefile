# SimDB book — build with Asciidoctor (see README.md for install notes).
#
#   make        # same as make html
#   make html   # book.html
#   make pdf    # book.pdf (requires asciidoctor-pdf)

BOOK     := book.adoc
HTML     := book.html
PDF      := book.pdf
PARTS    := $(wildcard parts/*.adoc)
IMAGES   := $(wildcard images/*)
SRCS     := $(BOOK) $(PARTS) $(IMAGES)

ASCIIDOCTOR      := asciidoctor
ASCIIDOCTOR_PDF  := asciidoctor-pdf
ASCIIDOCTOR_OPTS := --failure-level=WARN

.DEFAULT_GOAL := html

.PHONY: html pdf check-asciidoctor check-asciidoctor-pdf

html: check-asciidoctor $(HTML)

pdf: check-asciidoctor-pdf $(PDF)

$(HTML): $(SRCS)
	$(ASCIIDOCTOR) $(ASCIIDOCTOR_OPTS) $(BOOK)

$(PDF): $(SRCS)
	$(ASCIIDOCTOR_PDF) $(ASCIIDOCTOR_OPTS) $(BOOK)

check-asciidoctor:
	@if ! command -v $(ASCIIDOCTOR) >/dev/null 2>&1; then \
		printf '\n*** Error: %s is not installed or not on PATH. ***\n\n' '$(ASCIIDOCTOR)'; \
		printf 'HTML builds need the Asciidoctor CLI.\n\n'; \
		printf 'Install (pick one):\n\n'; \
		printf '  Debian/Ubuntu package:\n'; \
		printf '    sudo apt-get install -y asciidoctor\n\n'; \
		printf '  Ruby gem (no sudo; installs under your home directory):\n'; \
		printf '    gem install --user-install asciidoctor\n'; \
		printf '    export PATH="$$(ruby -e '"'"'print Gem.user_dir'"'"')/bin:$$PATH"\n\n'; \
		printf '  Ruby gem (system-wide; needs sudo on most Linux setups):\n'; \
		printf '    sudo gem install asciidoctor\n\n'; \
		exit 1; \
	fi

check-asciidoctor-pdf:
	@if ! command -v $(ASCIIDOCTOR_PDF) >/dev/null 2>&1; then \
		printf '\n*** Error: %s is not installed or not on PATH. ***\n\n' '$(ASCIIDOCTOR_PDF)'; \
		printf 'PDF builds need the asciidoctor-pdf Ruby gem.\n'; \
		printf 'There is no asciidoctor-pdf package in Debian/Ubuntu apt — use gem.\n\n'; \
		printf 'Install (recommended; no sudo):\n\n'; \
		printf '  gem install --user-install asciidoctor-pdf\n'; \
		printf '  export PATH="$$(ruby -e '"'"'print Gem.user_dir'"'"')/bin:$$PATH"\n\n'; \
		printf 'Or system-wide:\n\n'; \
		printf '  sudo gem install asciidoctor-pdf\n\n'; \
		printf 'Plain "gem install ..." without --user-install often fails with:\n'; \
		printf '  Gem::FilePermissionError (cannot write to /var/lib/gems/...)\n\n'; \
		printf 'Then run: make pdf\n\n'; \
		exit 1; \
	fi
