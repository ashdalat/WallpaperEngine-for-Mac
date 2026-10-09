#!/bin/bash
# Makes WallpaperEngineMac.app self-contained: copies every
# Homebrew dylib it links against into Contents/Frameworks,
# rewrites load paths to @rpath, and re-signs everything
# ad-hoc. After this the .app runs without Homebrew installed,
# so the folder can be shared with other people.
#
# Usage: bash bundle_dylibs.sh path/to/WallpaperEngineMac.app
set -e
APP="$1"
BIN="$APP/Contents/MacOS/WallpaperEngineMac"
FW="$APP/Contents/Frameworks"

[ -f "$BIN" ] || { echo "bundle_dylibs: no binary at $BIN" >&2; exit 1; }

rm -rf "$FW"
mkdir -p "$FW"

# Resolve a load path to a real file. Handles absolute
# paths (older Homebrew dylibs) and @rpath/ names (newer
# Homebrew dylibs reference their siblings this way).
resolve() {
    case "$1" in
        /opt/homebrew/*) echo "$1" ;;
        @rpath/*)
            name="${1#@rpath/}"
            for cand in /opt/homebrew/lib/"$name" \
                        /opt/homebrew/opt/*/lib/"$name"; do
                [ -f "$cand" ] && { echo "$cand"; return; }
            done
            ;;
    esac
    return 1
}

# Walk the dependency graph breadth-first, copying each
# Homebrew dylib we do not already have. A plain file
# stands in for a seen-set because macOS ships bash 3.2.
seen_list="$FW/.seen"
: > "$seen_list"
queue="$BIN"

while [ -n "$queue" ]; do
    current=$(printf '%s\n' "$queue" | head -1)
    queue=$(printf '%s\n' "$queue" | tail -n +2)
    [ -f "$current" ] || continue
    deps=$(otool -L "$current" | awk 'NR>1 {print $1}' \
        | grep -v '^/usr/lib\|^/System\|^@executable_path' || true)
    for dep in $deps; do
        resolved=$(resolve "$dep" || true)
        [ -n "$resolved" ] || continue
        base=$(basename "$resolved")
        if ! grep -qx "$base" "$seen_list"; then
            echo "$base" >> "$seen_list"
            cp "$resolved" "$FW/$base"
            queue=$(printf '%s\n%s\n' "$queue" "$FW/$base")
        fi
    done
done
rm -f "$seen_list"

# Point every dylib at its siblings via @rpath.
for f in "$FW"/*.dylib; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    install_name_tool -id "@rpath/$base" "$f" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path" "$f" 2>/dev/null || true
    for dep in $(otool -L "$f" | awk 'NR>1 {print $1}' | grep '^/opt/homebrew/' || true); do
        install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$f"
    done
    for dep in $(otool -L "$f" | awk 'NR>1 {print $1}' | grep '^@rpath/' || true); do
        base_dep=$(basename "$dep")
        [ -f "$FW/$base_dep" ] \
            && install_name_tool -change "$dep" "@rpath/$base_dep" "$f"
    done
done

# The main binary searches its own Frameworks folder.
install_name_tool -add_rpath "@loader_path/../Frameworks" "$BIN" 2>/dev/null || true
install_name_tool -delete_rpath /opt/homebrew/lib "$BIN" 2>/dev/null || true
for dep in $(otool -L "$BIN" | awk 'NR>1 {print $1}' | grep '^/opt/homebrew/' || true); do
    install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$BIN"
done
for dep in $(otool -L "$BIN" | awk 'NR>1 {print $1}' | grep '^@rpath/' || true); do
    base_dep=$(basename "$dep")
    [ -f "$FW/$base_dep" ] \
        && install_name_tool -change "$dep" "@rpath/$base_dep" "$BIN"
done

# install_name_tool invalidates the ad-hoc signature the
# linker produced; macOS kills apps whose signature does not
# match their content, so re-sign everything ad-hoc.
for f in "$FW"/*.dylib; do
    [ -f "$f" ] || continue
    codesign --force --sign - "$f" 2>/dev/null || true
done
codesign --force --sign - "$BIN"

echo "bundled $(ls "$FW" | wc -l) dylibs into $FW"
