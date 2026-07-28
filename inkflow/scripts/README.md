# Operator scripts

Scripts you run on a machine with the X4 physically attached. They are written for a
specific situation: **the operator is at a terminal with no Claude session available**,
reading instructions from a phone. So each one is a single short command to invoke,
safe to re-run, and prints a compact digest that is realistic to paste into a chat
from a phone. Full detail always goes to a file as well.

## How this works

The machine with the device attached has a terminal and git, and nothing else — no
agent session, no copying output by hand. So results travel through the repository:
each script writes its findings, commits them, and pushes. Whoever is working on the
project reads them from GitHub.

**You run one command per step. The script publishes its own results.**

## Setup, once

```sh
git clone https://github.com/wittyreference/dotfiles.git ~/inkflow
pipx install esptool          # or: pip3 install esptool
```

`main` is fine to sit on — the scripts move their own results onto a `hw-results`
branch rather than committing to the default branch, so you never have to think about
branch hygiene while standing at a bench with hardware plugged in. Review and merge
that branch like any other change.

Push access is what closes the loop. If this machine can't push, the scripts still
work and still print a short digest — see *If pushing fails* below.

## Each session

```sh
cd ~/inkflow && git checkout main && git pull
```

**Step 1 — probe.** Plug the X4 in with a **data** USB-C cable, then:

```sh
cd inkflow && ./scripts/00-probe.sh
```

Read-only. Detects the port, reads chip, flash and eFuse state, writes a report, and
pushes it. Takes seconds.

**Step 2 — golden backup.** Only once the probe confirms the unit is *unlocked*:

```sh
./scripts/01-backup.sh
```

Also read-only against the device. Prompts first, takes ~25 minutes, then pushes the
manifest.

**Step 3 — keep the image.** The `.bin` is gitignored deliberately: 16 MB of
device-specific firmware doesn't belong in a repo. Nothing moves it off that machine
except you, and **no public archive of a stock X4 image exists**, so this file is the
only restore path that is definitely yours.

```sh
cp hardware-notes/x4-stock-golden-*.bin ~/somewhere-durable/
```

## If pushing fails

Every script also prints a compact block between `--------- PASTE THIS ---------`
markers, sized to be pasted from a phone. That's the fallback, not the primary path.
`./scripts/push-results.sh` can be re-run on its own once credentials are sorted —
nothing is lost, the reports sit in `hardware-notes/` either way.

Set `NO_PUSH=1` to skip publishing entirely.

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
