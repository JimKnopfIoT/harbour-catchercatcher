# Catcher Catcher

**An IMSI-catcher / cell-site anomaly detector for Sailfish OS.**

Catcher Catcher watches the cellular state of the phone and flags the classic
fingerprints of a fake base station ("IMSI catcher" / "StingRay"): forced
downgrades, unknown cells, missing neighbours, ciphering downgrades and silent
SMS. It is a **defensive, on-device research tool** — everything runs locally,
there is no telemetry and no cloud component.

> **Language:** the app UI is in **German**. The core detection is
> country-agnostic, but the licensed-transmitter overlay uses the German
> regulator's (Bundesnetzagentur) data, so the app is most useful in Germany.
> The code is fully `qsTr()`/`tr()`-wrapped, so English/other translations can
> be added later without touching the logic.

> ⚠️ **Scope & legality.** This tool only *observes* your own device's radio
> state and reads your own baseband diagnostics. It does not transmit, jam or
> interfere with any network. Reading the diagnostic interface requires root on
> your own phone. Whether and how you may use radio-analysis tools can depend on
> your jurisdiction — you are responsible for using it lawfully on hardware you
> own.

---

## How it works — two tiers

| Tier | Component | Privilege | Source |
|------|-----------|-----------|--------|
| **1** | `CellMonitor` (C++/Qt) | none | ofono over D-Bus — behavioural heuristics |
| **2** | `diag-helper` (Python 3) | root via pkexec/polkit | Qualcomm `/dev/diag` — baseband signalling |

**Tier 1** needs no root. It polls ofono every 5 s and runs behavioural
heuristics on the cooked cellular state:

- forced downgrade to 2G/3G (4G→2G = alert, 4G→3G = warning),
- hopping to a never-before-seen cell,
- a location-area change without an operator change,
- a serving cell that reports no visible neighbours (debounced over several
  polls),
- **ghost-site** heuristic: a strong serving cell with no licensed transmitter
  site nearby (see *Data sources*),
- **identity baseline**: ICCID / IMSI / modem-firmware are baselined in local
  settings, so a SIM swap or firmware change between launches is flagged.

**Tier 2** is optional and started only on explicit user request. A small
dependency-free Python helper reads the Qualcomm `/dev/diag` interface as root
(through a pkexec/polkit action) and surfaces signals the cooked layer hides:

- **A5/0 cipher downgrade** (GSM RR Cipher Mode Command),
- **silent / type-0 SMS** and MM Identity Requests,
- an LTE PDCP EEA0 (null-cipher) detector (fail-safe: alarms only on the
  explicit "no encryption" algorithm id).

Tier 2 streams NDJSON alarms back to the app, which merges them into the event
log and the SMS view.

## Map & data sources

The app draws a self-contained radar-style map (custom `Canvas`, no QtLocation)
and can overlay:

- **OpenStreetMap** silhouettes via the Overpass API (streets / plots /
  buildings, three zoom tiers),
- **OpenCellID** known-tower positions — requires your own free API key, entered
  in-app (never stored in this repository),
- **Bundesnetzagentur (BNetzA) EMF** licensed transmitter sites (Germany) —
  fetched and cached on-device.

## Supported device

Developed and tested on the **Sony Xperia 10 III** (`pdx213`, Qualcomm SD690)
running **Sailfish OS 5**. Tier 1 (ofono) is fairly generic; **Tier 2 is
Qualcomm-specific** and the `/dev/diag` ABI was reverse-engineered against this
device's modem — other basebands will need adaptation.

## Building

Requires the [Sailfish OS SDK](https://sailfishos.org/develop/) (build engine +
a target for your device). From the project root:

```sh
mb2 -t SailfishOS-<version>-aarch64 build
```

This produces an RPM under `RPMS/`. The build links `libcrypto` (OpenSSL) for the
BNetzA response decryption — see `BuildRequires: pkgconfig(libcrypto)` in the
spec.

## Installing

Copy the RPM to the device and install it:

```sh
scp RPMS/harbour-catchercatcher-*.aarch64.rpm <device>:/tmp/
ssh <device> 'pkcon install-local -y /tmp/harbour-catchercatcher-*.aarch64.rpm'
```

Tier 1 works immediately after launch. Tier 2 is enabled from the app's SMS page
pulley menu; the first start shows a polkit authentication dialog because the
diag helper runs as root.

## Privacy

- All processing is **on-device**. No analytics, no crash reporting, no upload.
- External APIs (OpenStreetMap / OpenCellID / BNetzA) are queried **only on your
  action**, and your OpenCellID key lives only in the device's local settings.
- The Tier-2 helper is **read-only** on `/dev/diag` and has no network access.

## Status & responsible use

**Proof of concept / work in progress.** This is a hobby project, shared **as is**
with **no warranty** of any kind (see the GPLv3). It may be incomplete, rough
around the edges, or change without notice — use it at your own risk. The
detections are **heuristic**: expect false positives, and never treat a quiet
screen as proof that nothing is wrong. It is an aid, not a guarantee.

Catcher Catcher is a **passive, defensive** tool — it only observes your own
device and reads your own baseband diagnostics; it never transmits, jams, attacks
or interferes with any network. How you use it is your responsibility.

AI assisted development

## License

This project is licensed under the **GNU General Public License v3.0 or later**
(see [`LICENSE`](LICENSE)).

Data © the respective providers: OpenStreetMap contributors (ODbL),
OpenCellID, and the Bundesnetzagentur (EMF site register). Please observe each
provider's terms of use.
