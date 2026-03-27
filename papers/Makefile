.PHONY: all whitepaper yellowpaper clean

all: whitepaper yellowpaper

# ── Whitepaper ─────────────────────────────────────────────────────────
# 1. Concatenate .md chapters into one markdown file
# 2. Pandoc converts to body.tex (LaTeX fragment, no preamble)
# 3. XeLaTeX compiles main.tex which \input{body.tex}

whitepaper: whitepaper/crucible-whitepaper.pdf

whitepaper/combined.md: $(sort $(wildcard whitepaper/[0-9]*.md))
	cat $^ > $@

whitepaper/body.tex: whitepaper/combined.md
	pandoc $< -o $@ --from=markdown --to=latex --wrap=preserve --columns=999

whitepaper/crucible-whitepaper.pdf: whitepaper/main.tex whitepaper/body.tex crucible-preamble.sty
	cd whitepaper && xelatex -interaction=nonstopmode main.tex; xelatex -interaction=nonstopmode main.tex; xelatex -interaction=nonstopmode main.tex
	mv whitepaper/main.pdf $@

# ── Yellowpaper ────────────────────────────────────────────────────────

yellowpaper: yellowpaper/crucible-yellowpaper.pdf

yellowpaper/crucible-yellowpaper.pdf: yellowpaper/main.tex crucible-preamble.sty
	cd yellowpaper && xelatex -interaction=nonstopmode main.tex; xelatex -interaction=nonstopmode main.tex
	mv yellowpaper/main.pdf $@

# ── Clean ──────────────────────────────────────────────────────────────

clean:
	rm -f whitepaper/combined.md whitepaper/body.tex whitepaper/crucible-whitepaper.pdf
	rm -f yellowpaper/crucible-yellowpaper.pdf
	rm -f */main.{aux,log,out,toc,fls,fdb_latexmk}
