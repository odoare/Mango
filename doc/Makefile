# Renders the markdown docs in this folder to PDF.
#
#   make                 build minilanguage.pdf (the handout)
#   make architecture.pdf   build any other doc here, same way
#   make all             build every .md in the folder
#   make clean           remove the generated PDFs
#
# Needs pandoc and a LaTeX engine:
#   Debian/Ubuntu  sudo apt install pandoc texlive-xetex texlive-fonts-recommended
#   macOS          brew install pandoc && brew install --cask basictex

PANDOC ?= pandoc
ENGINE ?= xelatex

SOURCES := $(wildcard *.md)
PDFS    := $(SOURCES:.md=.pdf)

# A4, comfortable margins, coloured links, no LaTeX section numbering
# (the handout's headings are labels, not a numbered hierarchy).
PANDOC_FLAGS := \
	--pdf-engine=$(ENGINE) \
	--highlight-style=tango \
	-V geometry:a4paper \
	-V geometry:margin=18mm \
	-V fontsize=10pt \
	-V colorlinks=true \
	-V linkcolor=RoyalBlue \
	-V urlcolor=RoyalBlue

# Default: the handout only. `make all` does the rest of the folder.
minilanguage.pdf: minilanguage.md

all: $(PDFS)

%.pdf: %.md
	$(PANDOC) $(PANDOC_FLAGS) -o $@ $<

clean:
	rm -f $(PDFS)

.PHONY: all clean
.DEFAULT_GOAL := minilanguage.pdf
