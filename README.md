# Concurrency Primer

This repository contains the LaTeX source for a pretentiously-named,
but hopefully concise,
introduction to low-level concurrency.

## How do I build it?

Install a modern, Unicode-aware LaTeX, such as LuaLaTeX.
On Linux, this is usually as simple as installing the system TeX Live package, e.g., `texlive-base` or `texlive-core`.
The same package should also provide the `latexmk` script. (See below)

Install [pygments](http://pygments.org/), a Python syntax highlighter.
This is used by the LaTeX package [minted](https://ctan.org/tex-archive/macros/latex/contrib/minted/) to handle our syntax highlighting.

Build the document using
```shell
$ make
```

Note that `latexmk` will run LuaLaTeX multiple times, since TeX generates cross references in one pass, then links them in a second.

If you can't use `latexmk` for some reason, you can manually invoke
```shell
$ lualatex -halt-on-error -shell-escape concurrency-primer.tex
```
until it no longer warns, "Label(s) may have changed. Rerun to get cross-references right."

Enjoy a typeset `concurrency-primer.pdf`.
