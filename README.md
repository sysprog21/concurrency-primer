# Concurrency Primer

This repository contains the LaTeX source for a pretentiously-named,
but hopefully concise,
introduction to low-level concurrency.

## Prebuilt PDF

If you just want to read the primer, grab the latest typeset PDF from the
[Releases page](https://github.com/sysprog21/concurrency-primer/releases).
Building from source (below) is only necessary if you intend to modify the text.

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

The build fetches `lstlangarm.sty` with `wget`, so the first run needs network access.

## Building the examples

The programs under `examples/` are listed in the text and can be built and run on their own:
```shell
$ make -C examples check     # build all three and run them
$ make -C examples format    # reformat to examples/.clang-format
```

They use C11 threads (`<threads.h>`), so they need a libc that provides them, such as glibc 2.28 or newer.
They do not build on macOS, whose libc omits C11 threads.

`rmw_example_aba` claims a job with a double-width compare-and-swap.
On x86-64 the Makefile passes `-mcx16` to enable `cmpxchg16b`.
It also links against libatomic whenever the toolchain has it, on any architecture,
because some compilers still route 16-byte atomic loads and stores through libatomic even with `-mcx16`.
