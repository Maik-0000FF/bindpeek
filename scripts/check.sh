#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Every gate that can run locally, in one command, so nothing broken gets
# committed. Mirrors what a CI would do: formatting, licence headers, the
# static checks for C++ and QML, the build and the tests.
#
# Usage (re-enters the Nix dev shell automatically if needed):
#   ./scripts/check.sh          # fast gates
#   ./scripts/check.sh --nix    # also `nix flake check` and `nix build`
set -euo pipefail

# Re-exec inside the dev shell so clang-tidy, qmllint, reuse and the Qt
# headers are all on PATH.
if [ -z "${IN_NIX_SHELL:-}" ]; then
    exec nix develop --command "$0" "$@"
fi

RUN_NIX=0
[ "${1:-}" = "--nix" ] && RUN_NIX=1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR=build-check # gitignored via build*/

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }

# Every file this repository would carry, which is what the gates below have to
# see: the tracked ones plus any newly written one that is not ignored.
#
# Plain `git ls-files` lists only what is already tracked. A file written but
# not yet added therefore slipped past every gate here while the run still
# reported green, which is how a formatting error and a clang-tidy error once
# reached a commit: the gates ran before the file was added and never looked at
# it. --exclude-standard keeps .gitignore honoured, so the build directories
# stay out.
#
# Names that no longer exist on disk are dropped. A tracked file is still
# listed once it has been deleted, and a rename in progress would otherwise
# fail the gates on the old name rather than on anything wrong with the code.
sources() {
    git ls-files --cached --others --exclude-standard --deduplicate -- "$@" \
        | while IFS= read -r file; do
            if [ -e "$file" ]; then printf '%s\n' "$file"; fi
        done
}

step "clang-format (dry run, warnings are errors)"
sources '*.cpp' '*.h' | xargs -r clang-format --dry-run --Werror

step "REUSE compliance"
reuse lint

step "desktop-file-validate"
# The desktop database is unforgiving and silent: a malformed entry is dropped
# without a word, and the program simply never shows up in any menu.
# shellcheck disable=SC2046  # deliberate splitting into one argument per file
desktop-file-validate $(sources '*.desktop')

step "workflow"
# The file that says what runs on every push is shell inside YAML inside a
# schema, and none of the three is checked by anything else here. Discovered
# rather than named, like the sources above.
workflows=$(sources '.github/workflows/*.yml' '.github/workflows/*.yaml')
if [ -n "$workflows" ]; then
    # shellcheck disable=SC2086  # deliberate splitting into one argument per file
    actionlint $workflows
else
    echo "no workflow files"
fi

step "shellcheck (warnings and above)"
# Discovered rather than listed, so a newly added script cannot slip past the
# gate by not being named here.
# shellcheck disable=SC2046  # deliberate splitting into one argument per file
shellcheck -S warning $(sources '*.sh')

step "the install pair can read the build files"
# Everything install.sh and uninstall.sh work out before they touch anything:
# the two program names, the entry, the icon. All of it is read out of the
# build files rather than written down twice, which is right, and it means a
# line written differently there stops the pair. That happened once, and
# nothing here noticed, because no gate ever asked.
(
    PROJECT_ROOT="$ROOT"
    export PROJECT_ROOT
    # shellcheck source=scripts/_style.sh
    . "$ROOT/scripts/_style.sh"
    # shellcheck source=scripts/_paths.sh
    . "$ROOT/scripts/_paths.sh"
    printf '  %s\n' "$PANEL" "$SETTINGS" "$DESKTOP_ID.desktop" "$ICON_NAME.svg"
)

step "configure and build"
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"

step "tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

step "translation catalogue is up to date"
# Refreshing is a build target; here it only has to be shown that running it
# would change nothing. A catalogue left behind is not a cosmetic problem: the
# old translation stays in the file and keeps matching, while the sentence it
# was written for has already moved on, so the interface silently falls back to
# English. The check below measures against this file and would be worthless
# without this one.
catalogue=translations/bindpeek_de.ts
before="$BUILD_DIR/catalogue.before"
cp "$catalogue" "$before"
# The target rewrites a tracked file where it stands. Restored whatever
# happens, or a failing lupdate or an interrupted run leaves somebody's
# catalogue changed behind with nothing to say so.
trap 'cp -f "$before" "$catalogue"' EXIT INT TERM
cmake --build "$BUILD_DIR" --target update_translations >/dev/null
if ! diff -q "$before" "$catalogue" >/dev/null; then
    echo "$catalogue is out of date"
    echo "run: cmake --build $BUILD_DIR --target update_translations"
    exit 1
fi
trap - EXIT INT TERM

step "desktop entry says what the program says"
# The entry carries its own copy of the sentence the program gives to --help,
# because the desktop database reads a static file and cannot ask the
# catalogue. Both halves are measured against the catalogue rather than against
# the source: lupdate writes the sentence there unbroken and as one line, while
# clang-format is free to split the C++ literal across lines the moment it
# grows, which would leave the two gates unable to be satisfied at once.
#
# The German half is measured against the translation belonging to that very
# source, not merely against some translation in the file. Otherwise a changed
# English sentence with an untouched catalogue passes, which is the drift this
# exists to catch.
entry=assets/bindpeek.desktop

# The two sides escape different things: the entry is a desktop file, the
# catalogue is XML. Each is brought to the form of the other rather than hoping
# the sentence stays free of the characters involved.
#
# First back out what the desktop entry escapes, and refuse what this field
# cannot mean. A localestring knows five: \s for a space, \n, \t, \r, and \\ for
# a backslash. \s and \\ are resolved; \s produces an ordinary space and a
# description may well carry one written that way.
#
# The other three are refused rather than resolved. A menu description is one
# line, so a control character in it is wrong to begin with, and refusing the
# escape is the only place the refusal holds: resolved into the value, a
# trailing newline is stripped again by the command substitution that catches
# the result, and every check downstream would look at a value the desktop
# database never sees.
#
# A backslash before anything else is no escape at all. GLib then refuses the
# whole value, the entry loads with no description and nothing says why, and
# desktop-file-validate lets it pass; measured against g_key_file_get_string,
# which reads "a\\b" as a backslash and rejects "a\b" outright.
#
# Walked left to right rather than replaced form after form: doing \\ and then
# \s would read the second half of "\\s" as an escape of its own.
# The label is handed over through the environment for the same reason as the
# lookup further down: awk resolves escape sequences in a -v value.
unescape() {
    label="$1" awk '
        {
            out = ""
            for (i = 1; i <= length($0); ++i) {
                c = substr($0, i, 1)
                if (c != "\\") { out = out c; continue }
                e = substr($0, ++i, 1)
                if (e == "s") out = out " "
                else if (e == "\\") out = out "\\"
                else if (e == "n" || e == "t" || e == "r") {
                    print ENVIRON["label"] " carries \\" e \
                        ", and this field is one line" > "/dev/stderr"
                    exit 1
                }
                else {
                    print ENVIRON["label"] " carries \\" e \
                        ", which spells no desktop entry escape" > "/dev/stderr"
                    exit 1
                }
            }
            print out
        }'
}

# Then forward into what the .ts writer escapes, which is these five and no
# others. The apostrophe is the one most likely to turn up in an English
# sentence, and leaving it out rejected a perfectly correct pair. The ampersand
# goes first, or the escapes of the others would be escaped again.
escape() {
    sed -e 's/&/\&amp;/g' -e "s/'/\&apos;/g" -e 's/</\&lt;/g' \
        -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'
}
# The same for a control character sitting in the line as itself rather than as
# an escape, which the three refused above cannot produce but a stray tab can.
oneline() {
    if [ "$(printf '%s' "$2" | wc -l)" -ne 0 ] \
        || printf '%s' "$2" | LC_ALL=C grep -q '[[:cntrl:]]'; then
        echo "$1 must be one line and carry no control character"
        exit 1
    fi
}
# Present and not empty, both of them. An absent key reads as an empty string
# here, and an empty string is what an untranslated catalogue entry holds too,
# so the two would agree with each other while the menu shows nothing. The
# value is measured on its own rather than joined to its key: a sentence that
# ends in an equals sign would read as an empty one.
present() {
    if [ -z "$2" ]; then
        echo "$1 is missing or empty"
        exit 1
    fi
}

english=$(sed -n 's/^Comment=//p' "$entry")
german=$(sed -n 's/^Comment\[de\]=//p' "$entry")

present "$entry: Comment=" "$english"
present "$entry: Comment[de]=" "$german"

# Read the way GLib reads them, and assigned here rather than inside the
# conditions below: neither set -e nor pipefail acts on a command that forms an
# if condition, so a refused escape would go to stderr and then be compared as
# an empty string, which against an unfinished translation matches.
english_plain=$(printf '%s' "$english" | unescape "$entry: Comment=")
german_plain=$(printf '%s' "$german" | unescape "$entry: Comment[de]=")
oneline "$entry: Comment=" "$english_plain"
oneline "$entry: Comment[de]=" "$german_plain"

source_line="<source>$(printf '%s' "$english_plain" | escape)</source>"

if ! grep -qF "$source_line" "$catalogue"; then
    echo "$entry: Comment= is no source string in $catalogue"
    exit 1
fi
# Read out of the same <message> block rather than from the next line: a
# translator note puts an <extracomment> between the two, and the entry would
# then be reported as wrong while being right.
# Handed over through the environment, not with -v: awk processes escape
# sequences in a -v value, so a backslash in the sentence would arrive as
# whatever it happened to spell, and the lookup would silently find nothing.
translated=$(want="$source_line" awk '
    index($0, ENVIRON["want"]) { inside = 1; next }
    inside && /<\/message>/ { exit }
    inside && /<translation/ {
        line = $0
        sub(/.*<translation[^>]*>/, "", line)
        sub(/<\/translation>.*/, "", line)
        print line
        exit
    }' "$catalogue")
if [ "$(printf '%s' "$german_plain" | escape)" != "$translated" ]; then
    # Both in the form the comparison used, or the two lines a reader holds
    # side by side would be spelled differently from each other.
    echo "$entry: Comment[de]= is not the translation of Comment="
    echo "  entry:     $(printf '%s' "$german_plain" | escape)"
    echo "  catalogue: $translated"
    exit 1
fi

step "the docs name the environments the program names"
# The list of environments lives in main.cpp and every text in the program is
# built from it. The documentation cannot read a C++ function, so it carries
# its own copies, and a copy is a thing that falls behind: it did once already.
#
# Read out of the program rather than out of its source, so what is compared is
# what a reader is actually told. Out of the refusal and not out of --help: Qt
# wraps an option's text at the width of the terminal, and the list is the
# first thing to be broken across lines once a fourth name is in it.
#
# The name asked for has to be one no environment will ever have, because the
# refusal is what is being read.
#
# LC_ALL and LANGUAGE both, because Qt picks its catalogue through
# QLocale::uiLanguages(), which weighs LANGUAGE above LC_ALL: with only the
# first set, a German session reads a German sentence and finds no list in it.
#
# The refusal is a failure, so the program leaves with a non-zero status and
# the pipeline has to be allowed to: without this the run stops here rather
# than reading what it came for.
refusal=$(LC_ALL=C LANGUAGE=C "$BUILD_DIR/src/bindpeek" \
    --environment 'not-an-environment' --list 2>&1 | head -n 1 || true)
environments=$(sed -n 's/.*: \(.*\)\./\1/p' <<<"$refusal" | tr -d ' ' | tr ',' ' ')
if [ -z "$environments" ]; then
    echo "could not read the environments out of: $refusal" >&2
    exit 1
fi

# Two questions, because the documentation names them in two ways.
#
# First, every place that lists them has to list all of them. A listing is a
# mention of the option that names more than one session; a mention that names
# exactly one is an example of the option, not a list of what it takes, and
# there are several of those.
#
# Asked per listing and not per file: several listings in one file would
# otherwise cover for each other, and one of them could lose a name unnoticed.
# The line after is taken along, because a sentence wraps.
#
# Whole words only. Unanchored, "sway" is inside "swaymsg", and a page that
# merely mentions the tool would pass for one that lists the session.
#
# Files discovered rather than named, like everything else here: a page added
# later is asked the same question without anyone remembering to add it.
listing_fails=0
while IFS= read -r file; do
    [ -n "$file" ] || continue
    while IFS= read -r listing; do
        [ -n "$listing" ] || continue
        named=0
        for name in $environments; do
            grep -qwF -- "$name" <<<"$listing" && named=$((named + 1))
        done
        [ "$named" -gt 1 ] || continue
        for name in $environments; do
            grep -qwF -- "$name" <<<"$listing" || {
                echo "$file lists the environments without '$name': $listing" >&2
                listing_fails=1
            }
        done
    done < <(grep -A1 -- '--environment' "$file" | sed '/^--$/d' \
                 | paste -d' ' - - 2>/dev/null || true)
done < <(sources '*.md')
[ "$listing_fails" = 0 ] || exit 1

# Second, the two pages that introduce the program have to know every session
# at all, wherever they say so: the readme lists them among the features and
# the how-it-works page gives each one a row saying where its shortcuts come
# from. Neither spells the option, so the question above passes them by, and
# both are places a new backend is easy to forget.
#
# Case is ignored here, and only here: prose names a session the way its own
# project writes it, "Hyprland" and "KDE Plasma", while the option takes the
# lower-case word. Both are right where they stand.
#
# What this catches is a page that does not know a session at all, which is
# what a forgotten backend looks like. It cannot catch a page that names it in
# one paragraph and forgets it in the next: prose carries no mark saying which
# sentence is meant to be a complete list, and guessing at that produced more
# false alarms than finds.
for file in README.md docs/HOW-IT-WORKS.md; do
    for name in $environments; do
        grep -qwiF -- "$name" "$file" || {
            echo "$file never names the environment '$name'" >&2
            exit 1
        }
    done
done
echo "$environments"

step "qmlformat (dry run)"
# qmlformat has no check mode, so its output is compared with the file. Without
# this gate a QML reindent goes unnoticed: clang-format does not touch .qml.
while read -r f; do
    if ! qmlformat "$f" | diff -q - "$f" >/dev/null; then
        echo "not formatted: $f"
        exit 1
    fi
done < <(sources "*.qml")

step "qmllint"
# The QML imports live in the Qt package; the first entry of the import path is
# the one the dev shell exports for exactly this.
# shellcheck disable=SC2046  # deliberate splitting into one argument per file
qmllint -I "${QML2_IMPORT_PATH%%:*}" -I src -I src/editor \
    $(sources '*.qml')

step "clang-tidy"
# The compile database drives it, so every file is checked with the flags it is
# really built with. Header warnings are limited to this project's own headers
# (HeaderFilterRegex in .clang-tidy), or Qt's would drown everything.
sources 'src/*.cpp' 'tests/*.cpp' \
    | xargs -r clang-tidy -p "$BUILD_DIR" --quiet

if [ "$RUN_NIX" = 1 ]; then
    step "nix flake check"
    nix flake check

    step "nix build"
    nix build
fi

printf '\n\033[1;32m==> all gates passed\033[0m\n'
