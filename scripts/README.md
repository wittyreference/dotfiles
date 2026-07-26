# Operator scripts

Scripts you run on a machine with the X4 physically attached. They are written for a
specific situation: **the operator is at a terminal with no Claude session available**,
reading instructions from a phone. So each one is a single short command to invoke,
safe to re-run, and prints a compact digest that is realistic to paste into a chat
from a phone. Full detail always goes to a file as well.

## Getting them

```sh
git clone https://github.com/wittyreference/dotfiles.git
cd dotfiles && git checkout claude/xteink-x4-rsvp-reader-212m18 && cd inkflow
```

## Prerequisites

```sh
pipx install esptool     # or: pip3 install esptool
```

Nothing else for Phase 0. No USB driver is needed — the ESP32-C3's USB Serial/JTAG is
native CDC-ACM, so macOS enumerates it without help. **Use a data USB-C cable**; a
charge-only cable is indistinguishable by eye and is the most common way to waste an
hour here.

## Order

| Script | What it does | Writes to device? |
|---|---|:-:|
| `00-probe.sh` | Detects the port; reads chip id, flash id, MAC, and the eFuse summary | **No** |
| `01-backup.sh` | Golden 16 MB flash dump plus checksum and manifest | **No** |

Run them in order. `00-probe.sh` first is not ceremony: it tells you whether the unit
is locked, and a locked unit must not be flashed with anything before you have read
`docs/PLATFORM-MATRIX.md` §2 — it cannot be backed up over USB and can be stranded
permanently.

Both scripts are strictly read-only against the device. Every `esptool` subcommand
they use reads; none erase, write, or burn an eFuse. Nothing here can brick anything.

## What comes back

Each script prints a block between `PASTE THIS` markers — a dozen lines at most,
sized for a phone. Paste that. The full report lands in `hardware-notes/` and can be
committed if you have push access from that machine; if not, the digest is enough to
carry on with.

## Why the eFuse read matters

`00-probe.sh` reads the eFuse summary because it settles three things this project has
only ever *inferred* from behaviour, and that nobody has published for this device:

- whether **secure boot** is actually disabled (assumed, never confirmed),
- whether **flash encryption** is actually disabled (same),
- and, on a locked unit, whether the lock is a **burned eFuse** or a firmware-level
  USB disable — a question the community has speculated about without testing.

Those answers are a contribution to the ecosystem regardless of what happens to
inkflow.
