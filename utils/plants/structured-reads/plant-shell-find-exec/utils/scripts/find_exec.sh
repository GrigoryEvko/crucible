#!/bin/bash
find src -name '*.cpp' -exec grep -l foo {} +
