# Getting an APO loaded by Windows

Notes from getting DreamDSP's APO into `audiodg.exe`. Written down because the
failure mode is *completely silent* — a misregistered APO is indistinguishable
from an endpoint that has no effects at all — and because most of the advice
online is about the wrong layer.

## The failure that started this

The APO was registered as a COM server, its CLSID was written into an endpoint's
`FxProperties`, and nothing happened. No sound changed. No event log entry. The
DLL was never even mapped into a process. Days of endpoint-hopping and
slot-shuffling followed, all of it aimed at the wrong layer.

## Three requirements, all mandatory

### 1. Register the CLSID in HKLM

```
HKLM\SOFTWARE\Classes\CLSID\{clsid}\              (default)      = friendly name
HKLM\SOFTWARE\Classes\CLSID\{clsid}\InprocServer32 (default)     = path to the DLL
HKLM\SOFTWARE\Classes\CLSID\{clsid}\InprocServer32 ThreadingModel = Both
```

HKCU is useless here: `audiodg.exe` runs as `NT AUTHORITY\LOCAL SERVICE`, so the
per-user half of `HKEY_CLASSES_ROOT` it sees is not the logged-in user's.

### 2. Register with the audio engine's object database

**This is the one that is easy to miss and produces no diagnostic whatsoever.**

```
HKLM\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\{clsid}
```

with values that mirror `APO_REG_PROPERTIES`:

| Value | Type | DreamDSP |
|---|---|---|
| `FriendlyName` | REG_SZ | `DreamDSP Effects` |
| `Copyright` | REG_SZ | `DreamDSP` |
| `MajorVersion` / `MinorVersion` | REG_DWORD | 0 / 1 |
| `Flags` | REG_DWORD | 13 |
| `MinInputConnections` … `MaxOutputConnections` | REG_DWORD | 1 |
| `MaxInstances` | REG_DWORD | `0xFFFFFFFF` |
| `NumAPOInterfaces` | REG_DWORD | 1 |
| `APOInterface0` | REG_SZ | `{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}` (`IID_IAudioProcessingObject`) |

When the engine builds an endpoint's effect chain it reads the CLSIDs out of
that endpoint's `FxProperties` and looks each one up here. A CLSID that is
absent is skipped without a word: the DLL is never loaded, `DllGetClassObject`
is never called, nothing is logged.

Every APO on a Windows machine has an entry here — Microsoft's own, Realtek's,
Equalizer APO's, ViPER4Windows'. Enumerate them to sanity-check your own:

```powershell
Get-ChildItem 'Registry::HKEY_CLASSES_ROOT\AudioEngine\AudioProcessingObjects'
```

APOs built on the WDK base classes get this for free from
`CRegAPOProperties::Register()`. Hand-rolled COM has to write it explicitly —
see `scripts/apo-register-engine.ps1`.

Two field values are traps:

- `MaxInstances = 0` does **not** mean "no limit". It means no instance may ever
  be created. Use `0xFFFFFFFF`.
- `APOInterface0` is `IAudioProcessingObject`, not `IAudioSystemEffects`.

`Flags = 13` is `APO_FLAG_INPLACE | APO_FLAG_FRAMESPERSECOND_MUST_MATCH |
APO_FLAG_BITSPERSAMPLE_MUST_MATCH`, which is what Equalizer APO registers.
Note that `APO_FLAG_DEFAULT` additionally sets `SAMPLESPERFRAME_MUST_MATCH`,
which needlessly refuses channel counts a general-purpose APO can handle.

### 3. Support COM aggregation

The audio engine creates APOs **aggregated**: it passes its own controlling
`IUnknown` to `IClassFactory::CreateInstance`. A factory that answers
`CLASS_E_NOAGGREGATION` is dropped, silently again.

So the object needs the standard split identity: a delegating `IUnknown` (the
one the APO interfaces inherit, which forwards to the outer object) and a
separate non-delegating one. `apo/DreamApo.h` declares `INonDelegatingUnknown`
for this; the factory returns `NonDelegatingQueryInterface(riid, ppv)`.

The rule inside `NonDelegatingQueryInterface`: `IID_IUnknown` resolves to the
*non-delegating* interface and counts this object; every other interface belongs
to the outer identity and must `AddRef` through the pointer just handed out.

The tracing added for this prints `ctor aggregated 1` — the `1` is a non-null
outer `IUnknown`, i.e. confirmation the engine really does aggregate.

## Attaching to an endpoint

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{endpoint}\FxProperties
```

Legacy slots, which is what Equalizer APO uses on this machine:

| Value | Meaning |
|---|---|
| `{d04e05a6-…},0` | association (GUID_NULL is fine) |
| `{d04e05a6-…},1` | LFX / pre-mix |
| `{d04e05a6-…},2` | GFX / post-mix |

Modern slots are `,5` (SFX) `,6` (MFX) `,7` (EFX), each optionally paired with a
processing-mode list under `{d3993a3f-…},N`; `,13/,14/,15` are the `REG_MULTI_SZ`
list forms. Both slot families work; a device usually uses one or the other.

`Set-ItemProperty` **cannot** write these values. It opens the key
`ReadWriteSubTree`, which requests `CreateSubKey` among other rights, and on
these keys even Administrators hold only `SetValue` and `ReadKey` (the owner is
SYSTEM). Open the key with exactly the rights needed instead:

```powershell
$rights = [System.Security.AccessControl.RegistryRights]'SetValue,QueryValues'
$check  = [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree
$key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($subKey, $check, $rights)
```

`BUILTIN\Users` is granted `SetValue` on these keys, so attaching an APO to an
endpoint needs **no elevation**. Registering it (requirements 1 and 2) does.

## Things that are not the problem

Worth recording, because each cost time:

- **Code signing.** `EqualizerAPO.dll` is unsigned and loads fine.
  `DisableProtectedAudioDG=1` is set on this machine.
- **The DLL's location or ACLs.** `C:\ProgramData\DreamDSP` with `LOCAL SERVICE`
  granted Modify works; Equalizer APO's own DLL has plainer ACLs than that.
- **Which slot.** Both pid 1 and pid 2 were proven to load Equalizer APO on the
  same endpoint. Neither loaded an APO missing requirement 2.
- **The event log.** There are no entries for a rejected APO. Do not wait for one.

## Things that *are* worth knowing

- **Virtual audio devices do not run system effects.** Steam Streaming Speakers
  is enumerated under `ROOT`, and is missing the entire `{1da5d803-…}`
  `PKEY_AudioEngine_*` group that real endpoints have. Choosing it as a "safe"
  test target wasted a lot of time: it was safe precisely because nothing
  happens there. Compare property counts against a real device before trusting
  an endpoint as a test target.
- **`FxProperties` changes need an `audiosrv` restart** (or a device
  disable/enable) before they take effect. Rewriting the registry and playing
  audio is not enough — the previously built chain keeps running.
- **A render stream is required.** Loopback capture taps the mix but does not
  build the effect chain, so it cannot tell you whether an APO loaded.
  `DreamDSP.exe --playtone <endpointId> <seconds>` exists for this.

## How to tell what actually happened

Two techniques, both independent of the event log:

**Is the DLL mapped into a process?** A mapped DLL cannot be opened for writing.

```powershell
try   { [System.IO.File]::Open($dll,'Open','ReadWrite','None').Close(); 'not loaded' }
catch { 'loaded' }
```

Note that enumerating `audiodg.exe`'s modules does *not* work — it is a
protected process and returns zero modules.

**What did it do once loaded?** `apo/DreamApo.cpp` has `trace()`, which appends
to `C:\ProgramData\DreamDSP\apo.log`. It is called from the DLL entry points and
from every COM method except `APOProcess` (which must not open files). A healthy
load looks like:

```
DllGetClassObject
ctor aggregated  1
Initialize bytes  56
IsFormatSupported ok tag/ch/rate  65534 2 96000
LockForProcess ch/rate/maxFrames  2 96000 1056
UnlockForProcess frames/peakIn/peakOut(1000x)  384000 200 273
UnlockForProcess/dtor
```

Trace from the *constructor* onwards, not just from `LockForProcess`. An APO
that is created and then rejected during format negotiation never reaches
`LockForProcess`, and an absent log would otherwise be indistinguishable from
never having been created at all.

The frame count and peaks on `UnlockForProcess` separate two things that look
identical from outside: being instantiated, and being in the signal path.

## Scripts

| Script | Elevation | What it does |
|---|---|---|
| `scripts/apo-register-engine.ps1` | yes | requirement 2 (and `-Remove` to undo) |
| `scripts/apo-install.ps1` | yes (`-AttachOnly`: no) | stages the DLL, requirements 1 and 2, attaches to an endpoint |
| `scripts/apo-uninstall.ps1` | no | detaches from an endpoint |
| `scripts/apo-rollback-headphones.ps1` | prompts | restores this machine's headphone endpoint to Equalizer APO |
