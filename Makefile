lstlangarm.sty:
	wget https://raw.githubusercontent.com/sysprog21/arm-assembler-latex-listings/master/lstlangarm.sty

# The examples are pulled in with \inputminted, which reaches them through
# pygmentize rather than through TeX, so latexmk never records them as
# dependencies. Listing them here is what keeps an edited example from
# silently shipping stale in the PDF.
#
# Read out of the document rather than globbed or spelled out here: the
# \inputminted directives are the only place that records which examples the
# book actually embeds, so deriving the list from them cannot drift when one
# is added, renamed, or dropped.
EXAMPLES := $(shell sed -n 's|.*\\inputminted{c}{\./\(.*\)}|\1|p' concurrency-primer.tex)

# -g because latexmk would otherwise consult its own dependency database and
# declare the PDF up to date. make already decided something changed, so force
# at least one pass; latexmk still handles the rerun-until-stable logic.
concurrency-primer.pdf: lstlangarm.sty lib/codeblock.tex concurrency-primer.tex $(EXAMPLES)
	latexmk -g -lualatex -latexoption=-shell-escape concurrency-primer.tex

# Clear the .DEFAULT_GOAL special variable, so that the following turns
# to the first target after .DEFAULT_GOAL is not set.
.DEFAULT_GOAL :=

all: concurrency-primer.pdf

clean:
	rm -f *.dvi *.aux *.log *.ps *.pdf *.out *.bbl *.blg *.lof *.toc *.fdb_latexmk *.fls
	rm -rf _minted-concurrency-primer

distclean: clean
	rm -f lstlangarm.sty
