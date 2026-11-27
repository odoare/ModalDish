#!/usr/bin/env bash
# Prints the section of CHANGELOG.md for version $1 (X.Y.Z), without its
# "## [X.Y.Z] - date" heading: the release notes of the GitHub release.
# Fails, with a GitHub Actions error, when there is no such section or it is
# empty, so a tag pushed without its changelog entry stops the workflow.
#
#   .github/changelog-notes.sh 0.2.0 [CHANGELOG.md]
set -euo pipefail

version="$1"
file="${2:-CHANGELOG.md}"

notes=$(awk -v heading="## [$version]" '
    index($0, heading) == 1 { found = 1; next }
    found && /^## \[/       { exit }
    found                   { print }
' "$file")

if [[ -z "${notes//[[:space:]]/}" ]]; then
    echo "::error::$file has no section for $version (expected a heading \"## [$version] - YYYY-MM-DD\")" >&2
    exit 1
fi
printf '%s\n' "$notes"
