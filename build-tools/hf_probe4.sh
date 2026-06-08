#!/usr/bin/env bash
set -uo pipefail
# Find the mod-ui port and dump the effect.parameters ORDER for cab + hexforge.
PORT=""
for p in 80 8888 8080 18181; do
  if curl -s -m 2 "http://127.0.0.1:$p/system/info" >/dev/null 2>&1 || \
     curl -s -m 2 "http://127.0.0.1:$p/" >/dev/null 2>&1; then PORT=$p; break; fi
done
echo "mod-ui port guess: ${PORT:-none}"
for uri in https://rpowell5064.github.io/guitaramp-suite/cab https://rpowell5064.github.io/guitaramp-suite/hexforge; do
  echo "=== $uri parameters (in array order) ==="
  curl -s -m 5 "http://127.0.0.1:${PORT:-80}/effect/get?uri=$uri" \
    | python3 -c "import sys,json
d=json.load(sys.stdin)
for i,p in enumerate(d.get('parameters',[])):
    print(i, p.get('uri'), '| fileTypes=', p.get('fileTypes'), '| ranges/path=', 'path' in p or p.get('ranges'))" 2>&1 | head -20
done
