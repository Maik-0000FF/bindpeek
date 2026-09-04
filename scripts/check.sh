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

# How much the service that holds the keyboards may be allowed to do, on the
# scale systemd-analyze uses internally: ten times the number it prints. The
# unit scores 0.6 as it stands, and 15 leaves room for the scoring to shift
# between systemd versions without leaving room for a setting to go missing.
#
# A ceiling rather than the exact number on purpose. Pinning today's value
# fails on somebody else's systemd for no fault of the unit, and a gate that
# fails for the wrong reason is a gate that gets switched off.
WATCH_EXPOSURE_CEILING=15

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
#
# Read as git writes them with -z, one NUL apart, and with its quoting turned
# off. By default git wraps any path holding a byte outside ASCII, a quote or a
# backslash in double quotes and escapes what is inside, so a page called
# "Übersicht.md" arrives as a name no file on disk answers to and is dropped
# here without a word. Every gate below reads through this, so such a file
# passed all of them unchecked, and whether it did depended on a git setting
# rather than on anything in the repository.
#
# Handed on the same way, one NUL apart, and every reader below takes it that
# way, whether it gives the names to a tool, walks them one at a time or only
# counts them. A name rescued here and then written into a line is only
# rescued as far as the next reader. Plain xargs reads a backslash and a quote
# as syntax of its own, so it quietly handed clang-format the wrong name twice
# over and left the file it could not spell unchecked; an unquoted expansion
# splits the same names at their blanks into arguments naming nothing. A NUL is
# the one byte a path cannot hold, and a newline in a name is carried through
# as the character it is.
#
# Every name leaves here with "./" in front of it, which is what makes it a
# path to whatever opens it and nothing else. Without it a file called
# "-lead.qml" is a bundle of options to qmllint and to diff, and a file called
# "a=b.md" is a setting to awk, which then reads standard input instead and
# swallows the rest of the list. Put here rather than at each reader that opens
# a file, because the next such reader would be written without it.
sources() {
    git -c core.quotePath=false ls-files --cached --others --exclude-standard \
        --deduplicate -z -- "$@" \
        | while IFS= read -r -d '' file; do
            if [ -e "$file" ]; then printf './%s\0' "$file"; fi
        done
}

step "clang-format (dry run, warnings are errors)"
sources '*.cpp' '*.h' | xargs -0 -r clang-format --dry-run --Werror

step "REUSE compliance"
reuse lint

step "desktop-file-validate"
# The desktop database is unforgiving and silent: a malformed entry is dropped
# without a word, and the program simply never shows up in any menu.
sources '*.desktop' | xargs -0 -r desktop-file-validate

step "workflow"
# The file that says what runs on every push is shell inside YAML inside a
# schema, and none of the three is checked by anything else here. Discovered
# rather than named, like the sources above.
#
# Counted through a pipe rather than gathered into an array: a list read out of
# a process substitution arrives with the reader's own status, always zero, so
# a git that failed left an empty list behind and this step said there were no
# workflow files and went on. Counting the separators keeps the failure where
# it can be seen. The patterns are named once and used twice.
workflow_globs=('.github/workflows/*.yml' '.github/workflows/*.yaml')
workflow_count=$(sources "${workflow_globs[@]}" | tr -cd '\0' | wc -c)
if [ "$workflow_count" -gt 0 ]; then
    sources "${workflow_globs[@]}" | xargs -0 -r actionlint
else
    echo "no workflow files"
fi

step "shellcheck (warnings and above)"
# Discovered rather than listed, so a newly added script cannot slip past the
# gate by not being named here.
sources '*.sh' | xargs -0 -r shellcheck -S warning


step "a script that can be run says so, and only those"
# ./install.sh and ./uninstall.sh are how the readme, the installation page and
# the workflow all name them. Without the executable bit that is "Permission
# denied" for everybody, and the bit goes missing quietly: a file rewritten by
# a tool that writes a new one and moves it into place comes back with the
# default permissions, and a diff of the contents shows nothing at all. That
# happened twice here before this gate existed.
#
# The rule is the shebang rather than a list of names: a file that names an
# interpreter on its first line is meant to be started, one that does not is
# meant to be sourced. Both directions, so a helper that grows a bit it has no
# use for is caught as well.
mode_wrong=0
while IFS= read -r -d '' file; do
    IFS= read -r first_line < "$file" || true
    case "$first_line" in
        '#!'*)
            if [ ! -x "$file" ]; then
                echo "starts with a shebang but is not executable: $file"
                mode_wrong=1
            fi
            ;;
        *)
            if [ -x "$file" ]; then
                echo "is executable but names no interpreter: $file"
                mode_wrong=1
            fi
            ;;
    esac
done < <(sources '*.sh')
if [ "$mode_wrong" != 0 ]; then
    exit 1
fi

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
    printf '  %s\n' "$PANEL" "$SETTINGS" "$WATCH" \
        "$DESKTOP_ID.desktop" "$ICON_NAME.svg" "$WATCH_SOCKET_UNIT"
)

step "configure and build"
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"

step "the keyboard service is allowed no more than it says"
# The one unit here where a setting quietly dropped is a real loss, and
# nothing else would notice: it is the program that reads the event devices.
#
# Read offline and against the file the build just wrote. Without --offline
# the tool asks the running service manager, which knows nothing about a unit
# that has not been installed, and in a sandbox there is no manager to ask at
# all.
systemd-analyze security --offline=true \
    --threshold="$WATCH_EXPOSURE_CEILING" \
    "$BUILD_DIR/src/bindpeek-watch.service" | tail -n 1

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

step "every marked listing names every environment"
# The environments live in main.cpp and every text the program prints is built
# from that one list. A page cannot read a C++ function, so it carries its own
# copy, and a copy is a thing that falls behind: it did once already.
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
# The line is found by the word that introduces the list rather than taken as
# the first one printed. Standard error is folded in here, so a word from Qt
# about a runtime directory it did not like would otherwise be read as the
# list. The refusal is a failure, so the program leaves with a non-zero status
# and the pipeline has to be allowed to.
refusal=$(LC_ALL=C LANGUAGE=C "$BUILD_DIR/src/bindpeek" \
    --environment 'not-an-environment' --list 2>&1 | grep -m1 'Allowed:' || true)
environments=$(sed -n 's/.*Allowed: \(.*\)\./\1/p' <<<"$refusal" \
    | tr -d ' ' | tr ',' ' ')
if [ -z "$environments" ]; then
    echo "could not read the environments out of: $refusal" >&2
    exit 1
fi

# A listing says so itself, between two marker lines, and every one of them has
# to name every environment.
#
# Declared rather than found. An earlier gate searched the prose for what
# looked like a listing, and over four rounds every rule it used had a
# plausible sentence that slipped past it or that it flagged wrongly: how far a
# wrapped sentence reaches, whether the next table row belongs to it, whether a
# dash or a dot ends a word. Searching prose means guessing what a listing is,
# and the guess is what kept being wrong. A marker cannot be guessed at.
#
# The two words of a marker are joined here rather than written whole anywhere
# in this file, because every tracked file is scanned and this one is no
# exception: spelled out, the sentence you are reading would open a listing of
# its own and never end it. Every file, so that a marker in a flake, a module
# or a script is read like one in a page. An earlier draft named three
# suffixes, which left a marker anywhere else not merely unchecked but silently
# so: no listing counted, no complaint, and whoever wrote it believing it
# watched.
marker_word=environments
marker_begin="$marker_word:begin"
marker_end="$marker_word:end"

# A listing that is complete without one of them says so on its opening line,
# as "except kde". Some sentences are like that and would otherwise have to
# stay unmarked: the one naming what falls outside every heading, where KDE has
# no such case because a shortcut there always belongs to a component, and the
# ones about autostart, which name the bare compositors precisely because KDE
# is on the other side of the sentence. Excepting a name that is no environment
# is an error, and so is excepting all of them: a typo would otherwise quietly
# widen the hole it was meant to be. So is excepting a name the region goes on
# to use: the exception then covers a sentence that was being checked and
# passing, and the day someone rewrites that sentence the cover is all that is
# left. Written as a rule and not as advice, because advice in a comment is
# what the third case was until it stopped holding.
#
# Contained, not whole words, so that "kde" answers for "src/SourceKde.*".
# Measured over the marked listings, one of them rests on that, the row of
# backend files, and there for all four names at once; every other listing
# writes the names it has to name as words. Case is ignored for a related
# reason: the option's vocabulary is lower case and prose writes each session
# the way its own project does. The exception on the opening line is read
# that way too, both the keyword and the name after it, so that "Except KDE"
# at the start of a sentence excepts what "except kde" excepts. A complaint
# about one quotes the name as it was written, which is what the reader has to
# find on the line.
#
# The words of the exception are cut at whitespace, and each word then loses
# what is neither a letter nor a digit at either end: a comment closing the
# line, "-->", falls away entirely, while a name keeps whatever it holds
# inside. Cutting at every such byte instead would take a name apart, and
# "sway-kde" would read as two names the program does know: a listing excepting
# it would then quietly except both while naming neither.
#
# The price is known and taken deliberately. A name sitting inside a longer
# word answers just as well, "swayed" for sway, so a listing written that way
# would pass while naming nothing. None does today, measured: every match
# inside a longer word is one of the four backend file names. Reading whole
# words instead would cost that row, which is the one listing with no other
# spelling to fall back on.
#
# The rule about a useless exception reads the region the same way, and
# deliberately so: both answer one question, whether the check would have
# passed without the exception, and two measures would have them disagree. So
# an exception for sway beside that "swayed" is called useless, which is the
# truth about such a region rather than a fault of the rule.
#
# Markers go around a paragraph, a table or a list, never inside one: a comment
# between two rows ends the table, and one between two items splits the list.
# That is why a region may hold more than the listing itself.
#
# LC_ALL for awk, because every tracked file is offered to it and one of them
# is a picture: a byte that spells no character is not an error to be reported
# but a byte to be passed over.
regions_seen=0
region_fails=0
regions=""
while IFS= read -r -d '' file; do
    [ -n "$file" ] || continue
    while IFS= read -r result; do
        [ -n "$result" ] || continue
        case "$result" in
        region\ *)
            regions_seen=$((regions_seen + 1))
            regions="$regions
  ${file#./}, line ${result#region }"
            ;;
        *)
            echo "${file#./} $result" >&2
            region_fails=1
            ;;
        esac
    done < <(LC_ALL=C awk -v wanted="$environments" \
        -v opens="$marker_begin" -v closes="$marker_end" '
        BEGIN { split(wanted, names, " ") }
        index($0, opens) && index($0, closes) {
            print "opens and ends a listing on one line, line " NR
            next
        }
        index($0, opens) {
            if (inside) {
                print "opens a listing at line " NR \
                    " inside the one opened at line " start
                next
            }
            inside = 1
            start = NR
            text = ""
            delete skipped
            skipping = 0
            rest = substr($0, index($0, opens) + length(opens))
            count = split(rest, word, /[ \t]+/)
            for (j = 1; j <= count; j++) {
                gsub(/^[^a-zA-Z0-9]+|[^a-zA-Z0-9]+$/, "", word[j])
                if (word[j] == "") continue
                if (tolower(word[j]) == "except") { skipping = 1; continue }
                if (skipping) skipped[tolower(word[j])] = word[j]
            }
            next
        }
        index($0, closes) {
            if (!inside) {
                print "ends a listing that was never opened, line " NR
                next
            }
            inside = 0
            print "region " start
            lower = tolower(text)
            asked = 0
            for (i = 1; i in names; i++) {
                if (tolower(names[i]) in skipped) continue
                asked++
                if (index(lower, tolower(names[i])) == 0)
                    print "listing at line " start " does not name \x27" \
                        names[i] "\x27"
            }
            for (name in skipped) {
                written = skipped[name]
                known = 0
                for (i = 1; i in names; i++)
                    if (tolower(names[i]) == name) known = 1
                if (!known)
                    print "listing at line " start " excepts \x27" written \
                        "\x27, which is no environment"
                else if (index(lower, name) != 0)
                    print "listing at line " start " excepts \x27" written \
                        "\x27 and names it anyway"
            }
            if (asked == 0)
                print "listing at line " start " excepts every environment"
            next
        }
        inside { text = text " " $0 }
        END {
            if (inside)
                print "opens a listing at line " start " and never ends it"
        }' "$file")
done < <(sources)
[ "$region_fails" = 0 ] || exit 1

# How many listings there are, written down so that losing one is a failure
# rather than a silent pass.
#
# A guard that only fires once every marker in the repository is gone guards
# nothing: markers are dropped a pair at a time, and a listing whose pair is
# gone is back to being unwatched with the gate still green. Counted, so the
# one that went missing has to be accounted for.
#
# Writing a new listing means changing this number in the same breath. The gate
# says which way it moved, so neither direction can happen by accident.
#
# Every marked listing is named when the count is wrong, because the number
# alone leaves nowhere to look. A file written but never added is read like any
# other here, on purpose, so a copy of a page left lying in the working tree
# brings its listings along and the count says so without saying where.
expected_regions=18
if [ "$regions_seen" != "$expected_regions" ]; then
    echo "$regions_seen listings are marked, $expected_regions were expected" >&2
    printf '%s\n' "$regions" | sed '/^[[:space:]]*$/d' >&2
    exit 1
fi

step "qmlformat (dry run)"
# qmlformat has no check mode, so its output is compared with the file. Without
# this gate a QML reindent goes unnoticed: clang-format does not touch .qml.
#
# Read through a pipe rather than out of a process substitution, so that a
# git that failed halfway takes the step down with it: a substitution hands
# the loop the reader's own status, always zero, and this gate has no count
# downstream that would notice the files that never arrived.
sources '*.qml' | while IFS= read -r -d '' f; do
    if ! qmlformat "$f" | diff -q - "$f" >/dev/null; then
        echo "not formatted: ${f#./}"
        exit 1
    fi
done

step "qmllint"
# The QML imports live in the Qt package; the first entry of the import path is
# the one the dev shell exports for exactly this.
sources '*.qml' \
    | xargs -0 -r qmllint -I "${QML2_IMPORT_PATH%%:*}" -I src -I src/editor

step "clang-tidy"
# The compile database drives it, so every file is checked with the flags it is
# really built with. Header warnings are limited to this project's own headers
# (HeaderFilterRegex in .clang-tidy), or Qt's would drown everything.
sources 'src/*.cpp' 'tests/*.cpp' \
    | xargs -0 -r clang-tidy -p "$BUILD_DIR" --quiet

if [ "$RUN_NIX" = 1 ]; then
    step "nix flake check"
    nix flake check

    step "nix build"
    nix build
fi

printf '\n\033[1;32m==> all gates passed\033[0m\n'
