#!/bin/bash
g=(git -C "$tmp")
"${g[@]}" update-ref refs/heads/x HEAD
