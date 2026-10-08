#!/bin/bash
find include -name '*.h' | xargs grep -l foo
