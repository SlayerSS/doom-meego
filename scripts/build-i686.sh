#!/bin/bash
# Build doom-meego for Hyundai Genesis (LG DHAVN MeeGo, i686) as AppUpgrade.
# Ubuntu 12.04 i386 chroot (EGLIBC 2.15, GCC 4.6, Qt 4.8).
#
#   sudo ./scripts/build-i686.sh --bootstrap
#   sudo ./scripts/build-i686.sh
#       -> bin/doom-meego-i686
#       -> ../builds/DH_2015/{AppUpgrade,.lge.upgrade.xml}
#       -> ../builds/DHPE_2018/{AppUpgrade,.lge.upgrade.xml}
#       prints DOOM_OK
set -e
CHROOT=${CHROOT:-/opt/precise}
MIRROR=${MIRROR:-https://old-releases.ubuntu.com/ubuntu}
SRC=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$SRC/.." && pwd)
[ "$(id -u)" = 0 ] || { echo "run as root (chroot/mount)"; exit 1; }

if [ "$1" = "--bootstrap" ] || [ ! -x "$CHROOT/usr/bin/qmake-qt4" ]; then
  command -v debootstrap >/dev/null || { echo "install debootstrap"; exit 1; }
  if [ ! -x "$CHROOT/usr/bin/qmake-qt4" ]; then
    debootstrap --arch=i386 --variant=minbase --no-check-gpg \
      --include=build-essential,libqt4-dev,pkg-config,file precise "$CHROOT" "$MIRROR"
  fi
fi

W=$(mktemp -d /tmp/doom-meego-build.XXXXXX)
cp -a "$SRC/doomgeneric" "$SRC/src" "$SRC/doom-meego.pro" "$W/"
mkdir -p "$CHROOT/work" "$CHROOT/proc"
umount "$CHROOT/work" 2>/dev/null || true
mount --bind "$W" "$CHROOT/work"
mountpoint -q "$CHROOT/proc" || mount --bind /proc "$CHROOT/proc"
cleanup() { umount "$CHROOT/work" 2>/dev/null || true; }
trap cleanup EXIT

chroot "$CHROOT" /bin/bash -lc 'cd /work && /usr/bin/qmake-qt4 CONFIG+=meego doom-meego.pro && make -j"$(nproc 2>/dev/null || echo 4)" >/work/make.log 2>&1 || (tail -40 /work/make.log; exit 1)'
strip "$W/doom-meego"
mkdir -p "$SRC/bin"
cp "$W/doom-meego" "$SRC/bin/doom-meego-i686"

# Two card variants: same Doom binary, different .lge.upgrade.xml version strings
for variant in DH_2015 DHPE_2018; do
  mkdir -p "$ROOT/builds/$variant"
  cp "$W/doom-meego" "$ROOT/builds/$variant/AppUpgrade"
  chmod +x "$ROOT/builds/$variant/AppUpgrade"
  if [ -f "$SRC/card/$variant/.lge.upgrade.xml" ]; then
    cp -a "$SRC/card/$variant/.lge.upgrade.xml" "$ROOT/builds/$variant/.lge.upgrade.xml"
  else
    echo "WARN: missing $SRC/card/$variant/.lge.upgrade.xml" >&2
  fi
done

file "$ROOT/builds/DH_2015/AppUpgrade"
md5sum "$ROOT/builds/DH_2015/AppUpgrade" "$ROOT/builds/DHPE_2018/AppUpgrade"
rm -rf "$W"
echo DOOM_OK
