#!/bin/sh
# Install libctrpf and 3gxtool in the devkitpro/devkitarm container (CI only).
#
# Usage: tools/ci/install-deps.sh PKGDIR
#
# Both packages come from the ThePixellizerOSS repos and need nothing that the
# image lacks, so this never contacts pkg.devkitpro.org (whose `-Sy` answered
# HTTP 403 on some runs): it refreshes only the ThePixellizerOSS dbs, with a
# separate pacman.conf, retrying with backoff. If that still fails it installs
# the copies in PKGDIR (restored by actions/cache from an earlier run). On
# success PKGDIR holds just the installed package files, for the cache to save.
set -eu

PKGDIR=$1
PKGS="libctrpf 3gxtool"
CONF=/opt/devkitpro/pacman/etc/pacman.conf
TPO_CONF=/tmp/pacman-tpo.conf
mkdir -p "$PKGDIR"

# The image's [options] (paths, arch) without its repos, plus the two ThePixellizerOSS repos.
awk '/^\[/ { keep = ($0 == "[options]") } keep' "$CONF" > "$TPO_CONF"
printf '\n[thepixellizeross-lib]\nServer = https://thepixellizeross.gitlab.io/packages/any\nSigLevel = Optional\n' >> "$TPO_CONF"
printf '\n[thepixellizeross-linux]\nServer = https://thepixellizeross.gitlab.io/packages/x86_64/linux\nSigLevel = Optional\n' >> "$TPO_CONF"

from_repo() {
    dkp-pacman --config "$TPO_CONF" -Sy --noconfirm &&
    dkp-pacman --config "$TPO_CONF" -Sl thepixellizeross-lib thepixellizeross-linux &&
    dkp-pacman --config "$TPO_CONF" --cachedir "$PKGDIR" -S --noconfirm --needed $PKGS
}

delay=5
attempt=1
while ! from_repo; do
    if [ $attempt -ge 4 ]; then
        echo "::warning::ThePixellizerOSS repo failed $attempt times; installing the cached packages"
        ls "$PKGDIR"/*.pkg.tar.* > /dev/null 2>&1 || { echo "::error::no cached packages in $PKGDIR"; exit 1; }
        ls -l "$PKGDIR"
        dkp-pacman -U --noconfirm --needed "$PKGDIR"/*.pkg.tar.*
        exit 0
    fi
    echo "attempt $attempt failed; retrying in ${delay}s"
    sleep $delay
    attempt=$((attempt + 1))
    delay=$((delay * 3))
done

# Keep only the installed versions for the cache.
for f in "$PKGDIR"/*.pkg.tar.*; do
    keep=
    for p in $PKGS; do
        case "$(basename "$f")" in "$(dkp-pacman -Q "$p" | awk '{ print $1 "-" $2 }')"-*) keep=1 ;; esac
    done
    [ -n "$keep" ] || rm -f "$f"
done
ls -l "$PKGDIR"
