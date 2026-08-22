#!/usr/bin/env bash
#
# Checks that help/ has not drifted from what it copies.
#
# Appendix A is a verbatim copy of the Shalimar app's SHALIMAR_LANGUAGE.md,
# carried here because this is a separate repository and a reader of the manual
# has no ../Shalimar to follow. Copying a specification is only defensible with
# something that notices when the two stop matching - otherwise there are two
# specifications and nothing says which one anybody read.
#
# The original is not in this repository, so this is not a suite check: it
# passes with a word when the app's checkout is not on this machine, and only
# fails when the file is there and differs.
#
#   ./tools/check-help.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

APPENDIX=help/appendix-a-shalimar-language.md
SOURCE="${SHALIMAR_SPEC:-../Shalimar/SHALIMAR_LANGUAGE.md}"

[ -f "$APPENDIX" ] || { echo "no $APPENDIX"; exit 2; }

if [ ! -f "$SOURCE" ]; then
    echo "no $SOURCE on this machine - Appendix A is not checked here"
    echo "(name it with \$SHALIMAR_SPEC if the app's checkout is somewhere else)"
    exit 0
fi

# The copy carries a header of its own, ending at the first horizontal rule on
# a line by itself. Everything after that rule must match the original byte for
# byte; the header is this repository's and is not compared. Blank lines
# between the rule and the first real line are the header's too - markdown
# wants one there and the original does not have it.
carried=$(awk 'found && !started && NF == 0 {next}
               found {started = 1; print}
               /^---$/ && !found {found = 1}' "$APPENDIX")

if [ "${1:-}" = "--update" ]; then
    header=$(awk '{print} /^---$/ && !found {found = 1; exit}' "$APPENDIX")
    { printf '%s\n\n' "$header"; cat "$SOURCE"; } > "$APPENDIX.new" && mv "$APPENDIX.new" "$APPENDIX"
    echo "re-copied $SOURCE into $APPENDIX (its header kept)"
    echo "check the 'at commit' line in that header is still right"
    exit 0
fi

if diff -u <(printf '%s\n' "$carried") "$SOURCE" > /tmp/help-drift.diff 2>&1; then
    echo "Appendix A matches $SOURCE"
    exit 0
fi

echo "Appendix A has drifted from $SOURCE"
echo
sed -n '1,40p' /tmp/help-drift.diff
echo
echo "The original wins. Re-copy it:"
echo "  tools/check-help.sh --update"
exit 1
