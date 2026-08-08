#!/bin/sh
# Confit의 local installer는 반드시 caller가 제공한 pinned bmake만 사용한다.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PREFIX=
OBJROOT=
BMAKE=${CONFIT_BMAKE:-}

usage() {
  cat <<'USAGE'
Usage: install-local.sh --prefix <absolute-path> --objroot <absolute-path> \
       --bmake <pinned-bmake-path>

Builds only the Confit host binary with the supplied pinned BSD bmake and
installs:
  <prefix>/bin/confit
  <prefix>/share/man/man1/confit.1

The script neither fetches dependencies nor writes project configuration.
USAGE
}

die() {
  echo "install-local.sh: $*" >&2
  exit 1
}

require_absolute() {
  case "$1" in
    /*) ;;
    *) die "$2 must be an absolute path" ;;
  esac
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --prefix) [ "$#" -ge 2 ] || die "missing --prefix value"; PREFIX=$2; shift 2 ;;
    --objroot) [ "$#" -ge 2 ] || die "missing --objroot value"; OBJROOT=$2; shift 2 ;;
    --bmake) [ "$#" -ge 2 ] || die "missing --bmake value"; BMAKE=$2; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

[ -n "$PREFIX" ] || die "missing --prefix"
[ -n "$OBJROOT" ] || die "missing --objroot"
[ -n "$BMAKE" ] || die "missing --bmake (or CONFIT_BMAKE)"
require_absolute "$PREFIX" "--prefix"
require_absolute "$OBJROOT" "--objroot"
require_absolute "$BMAKE" "--bmake"

case "$PREFIX" in
  /|"$ROOT_DIR"|"$ROOT_DIR"/*) die "refusing unsafe install prefix: $PREFIX" ;;
esac
case "$OBJROOT" in
  /|"$ROOT_DIR"|"$ROOT_DIR"/*) die "objroot must be outside the source tree" ;;
esac

[ -x "$BMAKE" ] || die "bmake is not executable: $BMAKE"
make_version=$("$BMAKE" -r -f /dev/null -V MAKE_VERSION 2>/dev/null || true)
[ "$make_version" = "20240909" ] ||
  die "requires pinned bmake 20240909; found ${make_version:-unknown}"

"$BMAKE" -r -C "$ROOT_DIR" -f Makefile CONFIT_OBJROOT="$OBJROOT" all
[ -x "$OBJROOT/bin/confit" ] || die "built binary is missing"

mkdir -p "$PREFIX/bin" "$PREFIX/share/man/man1"
install -m 0755 "$OBJROOT/bin/confit" "$PREFIX/bin/confit"
install -m 0644 "$ROOT_DIR/man/confit.1" "$PREFIX/share/man/man1/confit.1"
"$PREFIX/bin/confit" --version >/dev/null
echo "installed $PREFIX/bin/confit"
