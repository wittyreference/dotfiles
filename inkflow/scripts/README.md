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
cd ~/inkflow/inkflow && ./scripts/setup-host.sh
```

`setup-host.sh` puts esptool, espefuse and PlatformIO in venvs under `~/.inkflow-tools`
so nothing touches system python. They are deliberately **not** added to `PATH` — every
script here looks in that directory itself, so there is nothing to export and nothing to
remember. Setting `$ESPTOOL`, `$ESPEFUSE` or `$PIO` overrides the search if you keep
your own build elsewhere.

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

Also read-only against the device. Prompts first, takes about a minute at 460800 baud
over native USB, then pushes the manifest.

**Step 3 — keep the image.** The `.bin` is gitignored deliberately: 16 MB of
device-specific firmware doesn't belong in a repo. Nothing moves it off that machine
except you, and **no public archive of a stock X4 image exists**, so this file is the
only restore path that is definitely yours.

```sh
cp hardware-notes/x4-stock-golden-*.bin* ~/somewhere-durable/
```

Copy the `.sha256` sidecar with it. `03-restore.sh` refuses to write an image it cannot
check, and an unverified golden image is a guess rather than a restore path.

**Step 4 — flash.** Writes the reader firmware to app0 at `0x10000` and verifies it:

```sh
./scripts/02-flash.sh                      # the reader's own build output
./scripts/02-flash.sh path/to/firmware.bin
```

The bootloader, partition table, nvs and spiffs are never addressed, so a bad
application image costs a reflash and nothing more. It refuses any file that doesn't
start with the `0xE9` ESP image magic, and asks before writing.

**Step 5 — restore, if needed.** Puts the golden stock image back:

```sh
./scripts/03-restore.sh                    # newest golden image found
```

Verifies the image against its `.sha256` sidecar *before* writing, and refuses on a
mismatch — writing an unverified image to `0x0` would take the bootloader with it.

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
| `setup-host.sh` | Installs esptool, espefuse and PlatformIO into `~/.inkflow-tools` | **No** |
| `00-probe.sh` | Detects the port; reads chip id, flash id, MAC, and the eFuse summary | **No** |
| `01-backup.sh` | Golden 16 MB flash dump plus checksum and manifest | **No** |
| `02-flash.sh` | Writes an application image to app0 at `0x10000`, then verifies | **Yes**, app0 only |
| `03-restore.sh` | Writes a checksum-verified golden image back over the whole flash | **Yes**, all of it |
| `04-read-session.sh` | Drives one reading session: build, flash, transfer, watch, publish | **Yes**, via `02-flash.sh` |

Run them in order. `00-probe.sh` first is not ceremony: it tells you whether the unit
is locked, and a locked unit must not be flashed with anything before you have read
`docs/PLATFORM-MATRIX.md` §2 — it cannot be backed up over USB and can be stranded
permanently.

`00-probe.sh` and `01-backup.sh` are strictly read-only against the device. Every
`esptool` subcommand they use reads; none erase, write, or burn an eFuse. Don't run
either of the writing scripts until `01-backup.sh` has produced an image and you have
copied it somewhere durable.

No script here burns an eFuse, so nothing here is one-way.

## The USB trap

**Stock firmware drops USB-Serial/JTAG the moment it boots.** Measured, not inferred:
after a command that ended in a hard reset, the device vanished from USB entirely — not
just the serial node — and did not come back until it was unplugged and power-cycled.

So every `esptool` and `espefuse` invocation in these scripts passes `--after no-reset`,
which leaves the chip sitting in the bootloader with the connection alive. That is why
a probe, a backup and an eFuse read can run back-to-back without touching the device in
between. If you run esptool by hand, pass it yourself or budget a power-cycle.

Custom firmware built from `firmware/reader` keeps USB up after boot, so once you have
flashed your own image the power-cycle dance stops. `04-read-session.sh flash` relies on
that: it ends on `--after hard-reset`, which boots the freshly written application with
USB surviving, so a flash-and-watch loop needs no hands on the device at all.

**Never hold the serial port open across a flash.** esptool and a reader on the same port
produce `device reports readiness to read but returned no data`, and the device comes back
up in download mode (`boot:0x7 DOWNLOAD`) rather than running the application. It looks
exactly like a failed flash and is not one. `04-read-session.sh flash` checks for this and
refuses rather than letting you find out.

## A reading session

`04-read-session.sh` is the whole device session behind five subcommands:

```sh
./scripts/04-read-session.sh build      # pio run, print size and sha256
./scripts/04-read-session.sh flash      # write app0, then boot it
./scripts/04-read-session.sh watch      # capture serial to hardware-notes/session-<ts>.log
./scripts/04-read-session.sh transfer   # the phone-sized transfer instructions
./scripts/04-read-session.sh publish    # push-results.sh
```

Run `watch` in its own window and leave it there for the whole read: it timestamps every
line, so pacing, refresh counts and the battery reading land in one timeline. Stop it
before flashing again.

`transfer` prints instructions rather than uploading anything, deliberately. This Mac's
VPN pins `192.168.4.1` into a tunnel, so it cannot reach the device's access point at all.
A phone sidesteps that entirely and needs no privileges, which beats editing a VPN's
routing table for a file copy.

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
