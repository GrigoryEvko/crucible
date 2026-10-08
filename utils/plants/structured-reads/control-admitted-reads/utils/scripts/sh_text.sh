#!/bin/bash
# git commit -m example
git log --format=%s | rg -q '^Give'
cat <<'EOF'
rg -n x include
git commit -m fixture
EOF
grep -q 'include/planted/Moved.h:5' "$log"
awk '/include\/x.h/ {print}' "$out"
git log --grep init
