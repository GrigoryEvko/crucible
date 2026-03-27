.PHONY: all whitepaper yellowpaper clean

all: whitepaper yellowpaper

whitepaper: whitepaper/crucible-whitepaper.pdf

whitepaper/crucible-whitepaper.pdf: $(wildcard whitepaper/src/*.tex)
	cd whitepaper/src && xelatex -interaction=nonstopmode main.tex; xelatex -interaction=nonstopmode main.tex
	mv whitepaper/src/main.pdf $@

yellowpaper: yellowpaper/crucible-yellowpaper.pdf

yellowpaper/crucible-yellowpaper.pdf: $(wildcard yellowpaper/src/*.tex)
	cd yellowpaper/src && xelatex -interaction=nonstopmode main.tex; xelatex -interaction=nonstopmode main.tex
	mv yellowpaper/src/main.pdf $@

clean:
	rm -f whitepaper/crucible-whitepaper.pdf yellowpaper/crucible-yellowpaper.pdf
	rm -f */src/main.{aux,log,out,toc,fls,fdb_latexmk,pdf}
