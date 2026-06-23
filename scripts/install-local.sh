#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PREFIX=
BUILD_DIR="${TMPDIR:-/tmp}/confit-install-build"

usage() {
  cat <<'USAGE'
Usage:
  install-local.sh --prefix <path> [--build-dir <path>]

Builds Confit from the local checkout and installs the single required
executable artifact:

  <prefix>/bin/confit

This script does not fetch network dependencies and does not edit project
config trees.
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
