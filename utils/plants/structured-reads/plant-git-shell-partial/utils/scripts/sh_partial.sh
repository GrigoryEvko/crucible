#!/bin/bash
unset GIT_DIR GIT_WORK_TREE
git -C "$tmp" commit -q -m x
