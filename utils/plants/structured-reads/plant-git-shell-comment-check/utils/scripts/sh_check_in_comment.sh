#!/bin/bash
unset GIT_DIR \
      GIT_WORK_TREE \
      GIT_INDEX_FILE \
      GIT_OBJECT_DIRECTORY \
      GIT_ALTERNATE_OBJECT_DIRECTORIES \
      GIT_COMMON_DIR \
      GIT_NAMESPACE \
      GIT_CEILING_DIRECTORIES
git -C "$tmp" commit -q -m x  # --absolute-git-dir
