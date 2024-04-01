lstlangarm.sty:
	wget https://raw.githubusercontent.com/sysprog21/arm-assembler-latex-listings/master/lstlangarm.sty

concurrency-primer.pdf: lstlangarm.sty concurrency-primer.tex
	latexmk -lualatex -latexoption=-shell-escape concurrency-primer.tex

# Clear the .DEFAULT_GOAL special variable, so that the following turns
# to the first target after .DEFAULT_GOAL is not set.
.DEFAULT_GOAL :=

all: concurrency-primer.pdf

clean:
	rm -f *.dvi *.aux *.log *.ps *.pdf *.out *.bbl *.blg *.lof *.toc *.fdb_latexmk *.fls
	rm -rf _minted-concurrency-primer

distclean: clean
	rm -f lstlangarm.sty
