#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PREFIX=
BUILD_DIR="${TMPDIR:-/tmp}/confit-install-build"

usage() {
  cat <<'USAGE'
Usage:
  install-local.sh --prefix <path> [--build-dir <path>]

Builds Confit from the local checkout and installs the macOS/Linux local
package surface:

  <prefix>/bin/confit
  <prefix>/share/man/man1/confit.1

This script does not fetch network dependencies and does not edit project
config trees.

Windows is a CLI-only preview lane and does not use this POSIX installer.
Build with GNU-style clang/Ninja and copy the single executable to:

  <prefix>/bin/confit.exe

Windows docs and the manpage are provided from the repository checkout until a
dedicated Windows installer is introduced.
USAGE
}

die() {
  echo "install-local.sh: $*" >&2
  exit 1
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --prefix)
      [ "$#" -ge 2 ] || die "missing value for --prefix"
      PREFIX=$2
      shift 2
      ;;
    --build-dir)
      [ "$#" -ge 2 ] || die "missing value for --build-dir"
      BUILD_DIR=$2
      shift 2
      ;;
    --help | -h)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

[ -n "$PREFIX" ] || die "missing --prefix"

HOST_SYSTEM=$(uname -s 2>/dev/null || echo unknown)
case "$HOST_SYSTEM" in
  MINGW* | MSYS* | CYGWIN*)
    die "Windows preview installs confit.exe by manual copy, not install-local.sh; see docs/local-build-and-test.md"
    ;;
esac

case "$PREFIX" in
  / | "$ROOT_DIR" | "$ROOT_DIR"/*)
    die "refusing unsafe install prefix: $PREFIX"
    ;;
esac

case "$BUILD_DIR" in
  "" | / | "$ROOT_DIR" | "$ROOT_DIR"/*)
    die "refusing unsafe build directory: $BUILD_DIR"
    ;;
esac

rm -rf "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target confit
cmake --install "$BUILD_DIR" --prefix "$PREFIX"

"$PREFIX/bin/confit" --version >/dev/null
echo "installed $PREFIX/bin/confit"
if [ -f "$PREFIX/share/man/man1/confit.1" ]; then
  echo "installed $PREFIX/share/man/man1/confit.1"
fi
