# Argos User Guide — build with Asciidoctor (see docs/book/README.md for install notes).
#
#   make        # same as make html
#   make html   # argos-user-guide.html

DOC      := argos-user-guide.adoc
HTML     := argos-user-guide.html
IMAGES   := $(wildcard images/*)
SRCS     := $(DOC) $(IMAGES)

ASCIIDOCTOR      := asciidoctor
ASCIIDOCTOR_OPTS := --failure-level=WARN

.DEFAULT_GOAL := html

.PHONY: html check-asciidoctor

html: check-asciidoctor $(HTML)

$(HTML): $(SRCS)
	$(ASCIIDOCTOR) $(ASCIIDOCTOR_OPTS) $(DOC)

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
