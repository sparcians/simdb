# SimDB Book

The SimDB documentation book, written in [AsciiDoc](https://asciidoc.org/) and
built with [Asciidoctor](https://asciidoctor.org/).

## Why this lives here

The repository's `README.md` files are excellent at *piquing interest* in SimDB,
but a brand-new user still needs a guided path: where to start, what can be
built, and how the SQLite interface, concurrent pipelines, app framework, and
Argos fit together. This book provides that path.

## Layout

```
docs/book/
  book.adoc              # master document (doctype: book); includes each part
  parts/
    part1-why-simdb.adoc
    part2-getting-started.adoc
    part3-sqlite-interface.adoc
    part4-concurrent-pipelines.adoc
    part5-app-framework.adoc
    part6-advanced-pipelines.adoc
    part7-argos-case-study.adoc
    part8-patterns-cookbook.adoc
    part9-reference.adoc
  images/                # figures/diagrams (created as needed)
```

Each part file begins with a level-0 `= Part ...` title (rendered as a book
"part") and contains one `==` section per chapter. Chapters are currently
*outlines*: each lists its learning objectives, key topics, and the existing
sources/snippets to draw from. Prose and worked examples are filled in over
subsequent iterations.

## Tooling decision

- **Format:** AsciiDoc (`.adoc`), `:doctype: book`.
- **Builder:** Asciidoctor (single-source, multi-file via `include::`).
- **Layout:** one master `book.adoc` including one file per part. This keeps the
  file count manageable during the outline phase; individual chapters can be
  split into their own files later if any part grows large.
- **Location:** `docs/book/` at the repo root, alongside (not replacing) the
  existing component `README.md` files.

## Building

Install Asciidoctor (Ruby gem), then render from this directory:

```bash
# HTML (single page)
asciidoctor book.adoc            # -> book.html

# PDF (requires the asciidoctor-pdf gem)
asciidoctor-pdf book.adoc        # -> book.pdf
```

Install options:

```bash
gem install asciidoctor asciidoctor-pdf   # rubygems
# or
sudo apt-get install -y asciidoctor       # Debian/Ubuntu (HTML backend)
```

## Table of contents

The full chapter list is generated automatically from the headings (the
`:toc:` attribute in `book.adoc`). See [book.adoc](book.adoc) for the master
outline and each `parts/*.adoc` file for chapter-level detail.
