#!/bin/sh
# A planted script that reads the undefined symbols of an archive as the text of nm.
nm -A --undefined-only build/libcrucible.a
