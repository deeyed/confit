# Confit TUI Vendor Shim

This directory contains the small terminal UI shim used by the Confit frontend.
It is intentionally isolated from Confit core model, parser, resolver, and
generator code.

The shim owns only terminal rendering and keyboard tokenization:

- list-view rendering
- status-bar rendering
- simple key decoding for movement, edit, filter, save, and quit commands
- line prompts for text entry

Confit schema semantics stay outside this vendor directory.
