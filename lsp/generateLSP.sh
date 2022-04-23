#!/bin/bash

set -euo pipefail
set -x

# CHANGES="$(git diff --name-only --cached | grep lsp/)";
# readarray -t CHANGES <<<"$CHANGES";

elementIn () {
  local e match="$1";
  shift;
  for e; do [[ "$e" == "$match" ]] && return 0; done;
  return 1;
}

echo "Minimizing LSP..";

#if elementIn "lsp/plugins/md5.js" "${CHANGES[@]}" || elementIn "lsp/plugins/cattablesort.js" "${CHANGES[@]}" || elementIn "lsp/mist.js" "${CHANGES[@]}" ; then
  echo "  Generating minified.js.."
  tmp=$(mktemp)
  java -jar closure-compiler.jar --language_in=ECMASCRIPT6 --warning_level QUIET  plugins/md5.js plugins/cattablesort.js mist.js > $tmp
  mv $tmp minified.js
#fi

echo "Done.";
