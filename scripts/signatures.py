"""Masked byte signatures for the RVA derivation scripts.

The signatures are prologue bytes of Subnautica 2's own functions, so they are
game code and are deliberately not committed. Generate them locally from your
own copy of the game with `make_signatures.py`; this module loads that file and
refuses to run without it.
"""
import json
import os

SIGNATURE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "signatures.local.json")

_MISSING = """\
{path} not found.

The derivation scripts match against prologue bytes of the game's own
functions. Those bytes are game code, so they are not stored in this
repository - generate them from your own installed copy:

    py -3 scripts/make_signatures.py <path-to-known-good-Subnautica2-exe>

Use the last build the mod already supports as the reference; the resulting
signatures are what relocate the same functions in a newer, patched EXE."""


def load():
    """Return {name: [int|None]} for every signature, or raise."""
    if not os.path.exists(SIGNATURE_FILE):
        raise SystemExit(_MISSING.format(path=SIGNATURE_FILE))
    with open(SIGNATURE_FILE) as fh:
        raw = json.load(fh)
    return {name: parse(sig) for name, sig in raw.items()}


def parse(sig):
    """'48 89 ?? ff' -> [0x48, 0x89, None, 0xff]."""
    return [None if tok == "??" else int(tok, 16) for tok in sig.split()]


def get(sigs, name):
    if name not in sigs:
        raise SystemExit(
            "signature '%s' missing from %s - regenerate it with "
            "make_signatures.py" % (name, SIGNATURE_FILE))
    return sigs[name]
