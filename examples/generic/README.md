# Direct-authoring generic example

This project is intentionally handwritten. It needs no scaffold and gives
Confit no knowledge of its C sources or bmake graph. The only boundary between
configuration and ordinary build is the verified `values.mk`/`values.h` data in
the selected immutable snapshot.

Prepare two writable output directories, then run:

```sh
bmake \
  CONFIT=/absolute/path/to/confit \
  CONFIT_CC=/absolute/path/to/clang \
  CONFIG_OUTPUT=/absolute/path/to/config-output \
  EXAMPLE_OBJROOT=/absolute/path/to/example-output \
  configure

bmake \
  CONFIT=/absolute/path/to/confit \
  CONFIT_CC=/absolute/path/to/clang \
  CONFIG_OUTPUT=/absolute/path/to/config-output \
  EXAMPLE_OBJROOT=/absolute/path/to/example-output \
  all
```

`all` never runs `configure`. If the selected snapshot is missing or stale it
stops with an instruction to run `bmake menuconfig` or `bmake configure`.
`Makefile` alone owns the `src/metrics.c` selection. Confit follows only the
literal TOML `source` edges. The invalid TOML, invalid C source, and make
fragment under the poison paths are deliberately unreferenced and must remain
irrelevant.

The resulting hosted executable is only a generic configuration-consumption
example. It is not an operating-system, kernel-image, boot, emulator, or
hardware result.
