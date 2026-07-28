## How Equalizer APO hosts VST plugins (VST2/AEffect), and what that means for PeaceQT
- [high] Equalizer APO hosts VST2 only (Steinberg AEffect C API, VST 2.4 era). It resolves exactly one export, "VSTPluginMain", via GetProcAddress. There is no VST3, CLAP, or LV2 support of any kind.
  evidence: Binary string scan of the installed D:\Program Files\EqualizerAPO\EqualizerAPO.dll (v1.4.1) — ASCII strings contain exactly one hit for "VSTPluginMain" and ZERO hits for GetPluginFactory, clap_entry, CLAP, LV2, VST3, InitDll, ExitDll. Same DLL leaks its build paths: C:\Users\x221\Desktop\EqualizerAPO-src-1.4.1\filters\VSTPluginFilter.cpp and ...\VSTPluginFilterFactory.cpp, plus RTTI names .?AVVSTPluginFilter@@, .?AVVSTPluginFilterFactory@@, .?AVVSTPluginLibrary@@, .?AV?$_Ref_count@VVSTPluginLibrary@@@std@@. Source: https://raw.githubusercontent.com/mirror/equalizerapo/master/helpers/VSTPluginLibrary.h — "typedef AEffect* (* vstPluginMain)(audioMasterCallback audioMaster);" and it includes "aeffectx.h"; https://raw.githubusercontent.com/mirror/equalizerapo/master/helpers/VSTPluginLibrary.cpp — VSTPluginMain = (vstPluginMain)GetProcAddress(module, "VSTPluginMain");
  -> Do not plan around APO's VST hosting as a modern-plugin path. If PeaceQT ever wants to ship its own DSP as a plugin loadable by stock Equalizer APO, it must export VSTPluginMain and implement the VST2 AEffect struct — nothing else will load. Conversely, if PeaceQT is going to do its own DSP anyway, hosting-inside-APO buys you nothing that in-process DSP wouldn't.
- [high] Equalizer APO does NOT use the Steinberg VST2 SDK. It vendors the GPLv2 clean-room "VeSTige" header from LMMS (Javier Serrano Polo, 2006) as helpers/aeffectx.h. This is how a 2025-era project still legally ships a VST2 host after Steinberg pulled the SDK.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/helpers/aeffectx.h — file header verbatim: "aeffectx.h - simple header to allow VeSTige compilation and eventually work / Copyright (c) 2006 Javier Serrano Polo <jasp00/at/users.sourceforge.net> / This file is part of Linux MultiMedia Studio - http://lmms.sourceforge.net", licensed GPLv2-or-later. Struct AEffect fields: magic; dispatcher; process; setParameter; getParameter; numPrograms; numParams; numInputs; numOutputs; flags; ptr1, ptr2; initialDelay; empty3a, empty3b; unkown_float; ptr3, user; uniqueID, version; processReplacing. Directory listing: https://github.com/mirror/equalizerapo/tree/master/helpers
  -> If PeaceQT wants to host VST2 plugins itself, this is the proven, copyable pattern: vendor the VeSTige/aeffectx.h header rather than seeking a VST2 SDK license (unobtainable — Steinberg removed VST2 from the SDK in Oct 2018 and terminated licences effective 2024). BUT: VeSTige is GPLv2, and Steinberg has issued DMCA takedowns against repos carrying it under the name aeffectx.h. Treat this as a licensing decision for PeaceQT, not a technical one. The clean alternative is VST3 (dual-licensed GPLv3 / free proprietary licence) — which is what the third-party fork chose.
- [high] The config command is `VSTPlugin:` and its argument list is a flat sequence of space-separated key/value PAIRS parsed by StringHelper::splitQuoted(parameters, ' '). Recognised keys are the case-sensitive literals `Library` and `ChunkData`; every other key is treated as a VST parameter NAME and its value converted with wcstof.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/filters/VSTPluginFilterFactory.cpp — createFilter(): `for (unsigned i = 0; i + 1 < parts.size(); i += 2) { wstring key = parts[i]; wstring value = parts[i + 1]; if (key == L"Library") {...} else if (key == L"ChunkData") { chunkData = value; } else { float f = wcstof(value.c_str(), NULL); paramMap[key] = f; } }`. Literals "ChunkData" and "Library" confirmed present as UTF-16 strings in the installed 1.4.1 EqualizerAPO.dll.
  -> If PeaceQT ever generates VSTPlugin: lines, emit strict key/value pairs, quote anything containing spaces, and never emit a trailing lone token (it is silently dropped by the `i + 1 < parts.size()` guard). Note `library`/`chunkdata` in the wrong case become parameter names, not keywords — a silent misconfiguration.
- [high] Malformed VSTPlugin input fails silently, never loudly. Odd token counts drop the last token; unparseable numbers become 0.0 via wcstof; unbalanced quotes do not error; a parameter name that the plugin does not report is silently ignored.
  evidence: VSTPluginFilterFactory.cpp loop guard `i + 1 < parts.size()` and `wcstof(value.c_str(), NULL)` (returns 0.0 on failure, end pointer discarded). https://raw.githubusercontent.com/mirror/equalizerapo/master/helpers/StringHelper.cpp — splitQuoted() verbatim: it toggles a bool `inQuotes` on each quoteChar, doubles (`""`) collapse to one literal quote via `if (inQuotes && i > 0 && s[i - 1] == quoteChar) current += quoteChar;`, and there is no unbalanced-quote validation at all. VSTPluginInstance.cpp parameter loop: `auto it = paramMap.find(name); if (it != paramMap.end()) effect->setParameter(effect, i, it->second);` — no else branch.
  -> Any PeaceQT feature that round-trips VST settings through config.txt will silently corrupt to 0.0 rather than report an error. This is a strong argument for PeaceQT owning its own DSP state instead of shipping it through APO's text config.
- [high] Parameters are addressed BY NAME in config.txt but set BY INDEX at load time. APO enumerates 0..numParams-1, asks the plugin for each name via effGetParamName into a 256-byte buffer, converts it from CP_UTF8, and looks that name up in the paramMap. Values are passed raw to setParameter with no clamping — VST2 convention is 0.0–1.0 normalised.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/helpers/VSTPluginInstance.cpp — `for (int i = 0; i < effect->numParams; i++) { char buf[256]; effect->dispatcher(effect, effGetParamName, i, 0, buf, 0.0f); buf[255] = '\0'; wstring name = StringHelper::toWString(buf, CP_UTF8); auto it = paramMap.find(name); if (it != paramMap.end()) effect->setParameter(effect, i, it->second); }`
  -> Name-keyed addressing means two parameters sharing a name both receive the same value, and renaming a parameter across plugin versions silently drops the setting. Note this is exactly the fragility PeaceQT would inherit if it drove effects through APO's VST path — another point for native DSP.
- [high] Plugin state that cannot be expressed as name/value pairs is stored as `ChunkData` — a Base64 blob applied via effSetChunk (isPreset=1). Users cannot meaningfully hand-edit it.
  evidence: VSTPluginInstance.cpp: `effect->dispatcher(effect, effSetChunk, 1, bufSize, buf, 0.0f)` with `new BYTE[bufSize]` after Base64 decode (the file #includes <wincrypt.h>, i.e. CryptStringToBinary). "ChunkData" present as a UTF-16 literal in the installed EqualizerAPO.dll 1.4.1. Forum confirmation: https://sourceforge.net/p/equalizerapo/discussion/general/thread/fc7e51287e/ — "you can't set the VST parameters by editing the chunkdata stored in config.txt because of it has no standard formation and can be also packed (unreadable format)".
  -> PeaceQT cannot offer a usable GUI over a chunk-based plugin without embedding the plugin's own editor. This kills the idea of PeaceQT presenting ViPER-like knobs on top of a third-party VST.
- [high] Default plugin directory is <InstallPath>\VSTPlugins, read from the registry. Relative Library paths are resolved against it via PathIsRelativeW; absolute paths are used as-is. On this machine that folder DOES NOT EXIST.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/helpers/VSTPluginLibrary.cpp verbatim: `wstring VSTPluginLibrary::getDefaultPluginPath() { if (defaultPluginPath == L"") { wstring installPath = RegistryHelper::readValue(APP_REGPATH, L"InstallPath"); defaultPluginPath = installPath + L"\\VSTPlugins"; } return defaultPluginPath; }`. VSTPluginFilterFactory.cpp: `if (PathIsRelativeW(value.c_str()))` prefixes getDefaultPluginPath(). Literal "\\VSTPlugins" confirmed as a UTF-16 string in the installed EqualizerAPO.dll. Local check: Get-ChildItem "D:\Program Files\EqualizerAPO" shows only config\ and qt\ subfolders — no VSTPlugins directory exists.
  -> Any relative VSTPlugin: Library line on this machine resolves to D:\Program Files\EqualizerAPO\VSTPlugins\<name>.dll and will fail with "File %s not found" until that folder is created. The commonly cited `Library ..\config\plugin.dll` trick works only because it walks up from that non-existent folder into the real config dir.
- [high] Plugin architecture must match the host: 64-bit APO loads only 64-bit VST2 DLLs. There is no bit-bridge. Failure modes are logged as four distinct messages.
  evidence: Verbatim format strings extracted from the installed EqualizerAPO.dll 1.4.1: "File %s not found", "Library %s could not be loaded", "Library %s does not contain needed functions", "Library %s has wrong architecture, must be %d-bit", "Adding VST plugin %s", "Loaded library %s", "Unloaded library %s". Corroborated by https://sourceforge.net/p/equalizerapo/tickets/275/ and the 1.2 release notes ("The plugin's architecture has to match the OS architecture").
  -> Nothing to build here, but it explains a large share of user-reported 'VST doesn't work' cases and is a support burden PeaceQT would inherit if it recommended the VST route.
- [high] The buffer contract: APO gives the plugin DEINTERLEAVED float32, per-channel pointers, and calls effSetSampleRate + effSetBlockSize(maxFrameCount) once at initialize. IFilter::process is `void process(float** output, float** input, unsigned frameCount)` and VSTPluginFilter declares itself NOT in-place. Actual frameCount per callback can be less than the declared maxFrameCount.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/IFilter.h — `std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)` and `void process(float** output, float** input, unsigned frameCount)`; the in-place hook is documented as "return false to request that output and input do not point to the same memory locations". VSTPluginInstance.cpp verbatim: `void VSTPluginInstance::prepareForProcessing(float sampleRate, int blockSize) { if (effect == NULL) return; this->sampleRate = sampleRate; effect->dispatcher(effect, effSetSampleRate, 0, 0, NULL, sampleRate); effect->dispatcher(effect, effSetBlockSize, 0, blockSize, NULL, 0.0f); }` called from VSTPluginFilter.cpp as `effect->prepareForProcessing(sampleRate, maxFrameCount)`. The APO shim itself (EqualizerAPO/EqualizerAPO.cpp) passes interleaved float* from APOProcess into engine.process and does no format validation; FilterEngine deinterleaves via currentConfig->read(input, frameCount).
  -> This is the contract PeaceQT's own DSP would have to live inside if it ships as an APO/sAPO: deinterleaved float32, variable frameCount up to maxFrameCount, hard real-time. A reverb with pre-delay and a compressor with lookahead must size their own internal delay lines — you cannot ask the host for more latency (VST2 initialDelay is not honoured by APO; nothing in VSTPluginFilter reads effect->initialDelay).
- [high] Channel handling: APO instantiates MULTIPLE copies of the same plugin to cover the channel count, and pads with silent dummy channels. A stereo plugin on 5.1 becomes three independent instances — which destroys any cross-channel or multiband-linked processing (e.g. stereo-linked compression across a surround mix).
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/filters/VSTPluginFilter.cpp — initialize(): channelCount = channelNames.size(); a first instance is created with 2 channels; effectChannelCount = max(firstEffect->numInputs(), firstEffect->numOutputs()); `effectCount = (channelCount + (effectChannelCount - 1)) / effectChannelCount`; then `emptyChannelCount = 2 * (effectCount * effectChannelCount - channelCount)` zeroed float buffers are allocated and wired into the pointer arrays. process() loops per effect over the prepared float** arrays calling processReplacing() or process() depending on canReplacing().
  -> Directly relevant to the user's goal: a ViPER-style psychoacoustic bass or a stereo reverb hosted this way on a surround endpoint would be silently replicated N times with wrong channel pairing. PeaceQT doing its own DSP can define correct channel semantics; the VST-in-APO route cannot.
- [high] APO reloads the whole filter chain (and therefore fully re-instantiates every VST plugin) on any change under the config directory. The reload runs on a background notification thread, and the swap is covered by a ~10 ms crossfade — so it should not click, but plugin construction cost is paid on every edit.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/FilterEngine.cpp — notification thread created with `threadHandle = CreateThread(NULL, 0, notificationThread, this, 0, NULL);` watching `FindFirstChangeNotificationW(engine->configPath.c_str(), true, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE)`; `loadConfig()` takes `EnterCriticalSection(&loadSection)` and stages into `nextConfig`; process() runs `transitionCounter = currentConfig->doTransition(nextConfig, frameCount, transitionCounter, transitionLength);` and on completion `previousConfig = currentConfig; currentConfig = nextConfig; nextConfig = NULL; transitionCounter = 0; ReleaseSemaphore(loadSemaphore, 1, NULL);` with `this->transitionLength = (unsigned)(sampleRate / 100);` i.e. 10 ms.
  -> PeaceQT writing config.txt on every slider move is already triggering a full chain rebuild every time. With a VST in the chain that means a full plugin destroy/construct per keystroke. If PeaceQT adds real-time-adjustable effects, it must NOT drive them by rewriting config.txt.
- [medium] Independent corroboration of the reload cost: Peter Verbeek (Peace author) states every interface change re-creates the VST plugin, causing significant delays with complex plugins.
  evidence: https://sourceforge.net/p/equalizerapo/discussion/general/thread/9526d91f79/ — quoted: "anything you do on the interface VST 2 plugin in the Configuration Editor will trigger a creation of the VST plugin...constantly a sequence of removing the former plugin and creating a new one."
  -> Confirms the architectural conclusion above from a second, independent source (the author of the app PeaceQT replaces).
- [high] APO wraps VST calls in SEH and degrades to passthrough on crash, with three distinct crash messages. This is defensive precisely because VST-in-audiodg is unstable.
  evidence: Verbatim UTF-16 strings in the installed EqualizerAPO.dll 1.4.1: "The VST plugin %s crashed during initialization.", "The VST plugin %s crashed while preparing for processing.", "The VST plugin %s crashed during audio processing." Source: VSTPluginFilter.cpp — `__try { ... } __except (EXCEPTION_EXECUTE_HANDLER) { LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str()); skipProcessing = true; }`; process() then does `memcpy(output[i], input[i], frameCount * sizeof(float))` passthrough when skipProcessing is set, and logs the audio-processing crash once via a reportCrash flag.
  -> Note the failure mode is SILENT bypass, not an error dialog: the user hears unprocessed audio and must read the log. If PeaceQT does its own DSP it should adopt the same fail-safe passthrough discipline — but surface it in the GUI, which APO cannot.
- [high] The single biggest practical limitation is not the API — it is that APO code runs inside audiodg.exe under a LocalService-equivalent restricted token with no access to the user profile. Plugins cannot read their own license files, presets, or IRs from C:\Users\<you>, and cannot show a GUI.
  evidence: https://github.com/dechamps/APO/blob/master/README.md — APOs "run inside the Windows audio graph process (audiodg.exe) which is itself managed by the Windows Audio service (audiosrv)"; its "access token gives it permissions that are roughly equivalent to the LocalService account" and "does not have access to your user directory". https://sourceforge.net/p/equalizerapo/tickets/228/ ("Paid VST plugins run in trial mode", still Open as of May 2026) — "the APO itself does not run under the same user as the Editor (this is because of the way the Windows Audio Engine is structured)"; workarounds are copying licences into C:\Windows\ServiceProfiles\LocalService\AppData or re-importing HKCU plugin keys under HKEY_USERS\S-1-5-19. https://sourceforge.net/p/equalizerapo/discussion/general/thread/a4594bf7d5/ — Etienne Dechamps: "The Equalizer APO Editor doesn't actually see the audio flowing through the APO, therefore the VST running in the editor can't see it either."
  -> This is decisive for PeaceQT's design. Any DSP that runs in the APO must be self-contained: no user-profile file reads, no licence servers, no GUI, no metering feedback to the UI. If PeaceQT wants live meters or GUI-driven effects, it needs an explicit IPC channel (shared memory / named pipe with a DACL granting LOCAL SERVICE) between its APO-side DSP and its Qt UI — the same problem the VST3 fork solved with an out-of-process host.
- [high] Known-broken real-world cases are well documented: Redline Monitor 112 crashes the Editor on load; ToneBoosters Morphit loads but has no audible effect; Accusonus ERA Noise Reducer kills all audio; Soothe2 crashes the Editor with "File ??? not found". The feature has been requested to be renamed "VST plugin (Experimental)".
  evidence: https://sourceforge.net/p/equalizerapo/discussion/general/thread/89a7d21b7e/ ("VST Issues") — Redline Monitor 112 "crashes Equalizer apo the second you try to load it"; Morphit "You can change plugin settings but they have no effect"; ERA "whenever i try to activate it...every sound stops coming out"; a user notes "for this and other reasons I made a ticket to rename the option to 'VST plugin (Experimental)'". https://sourceforge.net/p/equalizerapo/tickets/221/ — Soothe2, log error "File ??? not found", status Open, unresolved. https://sourceforge.net/p/equalizerapo/discussion/general/thread/75dec7d932/ — "Configuration Editor crashes if the VST plugin is added to the chain".
  -> Recommending 'just use a VST in APO' to PeaceQT users is a support liability. Whatever fraction of plugins work is unpredictable per-plugin.
- [high] VSTPlugin is undocumented in Equalizer APO's official Configuration Reference wiki. It exists only as an Editor-generated command.
  evidence: https://sourceforge.net/p/equalizerapo/wiki/Configuration%20reference/ documents Preamp, Filter, Delay, Copy, GraphicEQ, Convolution, Include, Device, Channel, Stage, If/ElseIf/Else/EndIf, Eval — and contains no VSTPlugin entry. https://sourceforge.net/p/equalizerapo/wiki/Documentation/ likewise has no VST mention.
  -> There is no stable documented contract for the VSTPlugin: syntax — it can change between releases without notice. Do not have PeaceQT hard-code generation of these lines as a supported feature.
- [medium] A third-party fork (EqAPO64 with VST3 support, by Ezequiel Casas, v2.0.4, AGPL/GPLv3) already does what the user is contemplating: native VST3 via the Steinberg VST3 SDK, a 64-bit double-precision internal pipeline, and an `OutProcVSTPlugin:` command that runs plug-ins out-of-process in EqApoOutProcHost.exe so crashes and GUIs are isolated.
  evidence: https://sourceforge.net/projects/eqapo64-with-vst3-support/ — "Native VST3 hosting through the Steinberg VST3 SDK", "Double procession processing (64 bit internal pipeline)", "OutProcVSTPlugin: mode, which runs plug-ins in EqApoOutProcHost.exe"; licensed "Affero GNU Public License, GNU General Public License version 3.0 (GPLv3)"; based on a restructured fork of github.com/TheFireKahuna/equalizerAPO64. Discussion: https://sourceforge.net/p/equalizerapo/discussion/general/thread/9526d91f79/
  -> This is the closest existing prior art and it is GPL/AGPL — readable as a reference for how to solve the GUI-isolation and out-of-process-host problem, but its licence may be incompatible with PeaceQT's intended distribution. Worth reading before designing PeaceQT's own DSP host.
- [high] Official Equalizer APO has not gained VST3/CLAP/LV2 and the maintainer has not responded to the request. Ticket #275 ("Support for VST3 plugins", opened 2024-03-28) is still Open as of April 2026.
  evidence: https://sourceforge.net/p/equalizerapo/tickets/275/ — status Open; Peter Verbeek 2026-04-23: "We don't know what the VST3 limits are for Equalizer APO. It would be nice when the developer responds." Latest official release is 1.4.2 (2025-03-21) per https://sourceforge.net/projects/equalizerapo/files/ ; this machine runs 1.4.1.
  -> Do not architect PeaceQT on the assumption that upstream APO will add VST3. The VST route in stock APO is frozen at VST2 for the foreseeable future.

### code
```
# --- Minimal working VSTPlugin: line (absolute path, no parameters) ---
# Plugin state comes entirely from the plugin's own defaults.
VSTPlugin: Library "C:\VST64\ReaComp.dll"

# Path without spaces does not need quotes:
VSTPlugin: Library C:\VST64\ReaComp.dll

```

```
# --- Relative path form ---
# PathIsRelativeW() -> prefixed with <InstallPath>\VSTPlugins
# On THIS machine that is: D:\Program Files\EqualizerAPO\VSTPlugins\ReaComp.dll
# NOTE: that folder does not currently exist -> logs 'File %s not found'
VSTPlugin: Library ReaComp.dll

# The widely-cited config-folder trick walks up out of VSTPlugins:
# resolves to D:\Program Files\EqualizerAPO\config\ReaComp.dll
VSTPlugin: Library ..\config\ReaComp.dll

```

```
# --- Named-parameter form ---
# Keys are matched case-sensitively against effGetParamName(i) for i in [0, numParams).
# Values are raw floats passed straight to setParameter() -- VST2 convention is 0.0-1.0 normalised.
# Names containing spaces MUST be quoted; "" inside quotes is a literal quote.
VSTPlugin: Library "C:\Program Files\VSTPlugins\ReaComp.dll" Thresh 0.35 Ratio 0.25 "Attack ms" 0.1 "Release ms" 0.4

```

```
# --- ChunkData form (opaque serialized plugin state, Base64) ---
# Applied via dispatcher(effect, effSetChunk, 1 /*isPreset*/, bufSize, buf, 0.0f)
# Written by the Configuration Editor; not hand-editable.
VSTPlugin: Library "C:\VST64\SomeReverb.dll" ChunkData "Q0NuSwAAAAEAAAAAAAAAAgAAAAEAAAAAAAAAAgAAAAE="

```

```
// --- The load path, condensed from Equalizer APO 1.4.x source ---
// helpers/VSTPluginLibrary.h
typedef AEffect* (*vstPluginMain)(audioMasterCallback audioMaster);

// helpers/VSTPluginLibrary.cpp  -- ONLY this export is probed. No VST3/CLAP/LV2.
VSTPluginMain = (vstPluginMain)GetProcAddress(module, "VSTPluginMain");

// helpers/VSTPluginLibrary.cpp  -- default search dir
wstring VSTPluginLibrary::getDefaultPluginPath()
{
    if (defaultPluginPath == L"")
    {
        wstring installPath = RegistryHelper::readValue(APP_REGPATH, L"InstallPath");
        defaultPluginPath = installPath + L"\\VSTPlugins";
    }
    return defaultPluginPath;
}

// filters/VSTPluginFilterFactory.cpp  -- the whole argument grammar
for (unsigned i = 0; i + 1 < parts.size(); i += 2)
{
    wstring key = parts[i];
    wstring value = parts[i + 1];
    if (key == L"Library")
    {
        // if (PathIsRelativeW(value.c_str())) prefix with getDefaultPluginPath()
    }
    else if (key == L"ChunkData")
    {
        chunkData = value;
    }
    else
    {
        float f = wcstof(value.c_str(), NULL);   // parse failure -> 0.0f, silently
        paramMap[key] = f;
    }
}

// helpers/VSTPluginInstance.cpp  -- name->index resolution, no clamping
for (int i = 0; i < effect->numParams; i++)
{
    char buf[256];
    effect->dispatcher(effect, effGetParamName, i, 0, buf, 0.0f);
    buf[255] = '\0'; // just to be sure
    wstring name = StringHelper::toWString(buf, CP_UTF8);
    auto it = paramMap.find(name);
    if (it != paramMap.end())
        effect->setParameter(effect, i, it->second);
}

// helpers/VSTPluginInstance.cpp  -- the entire format negotiation
void VSTPluginInstance::prepareForProcessing(float sampleRate, int blockSize)
{
    if (effect == NULL)
        return;
    this->sampleRate = sampleRate;
    effect->dispatcher(effect, effSetSampleRate, 0, 0, NULL, sampleRate);
    effect->dispatcher(effect, effSetBlockSize, 0, blockSize, NULL, 0.0f);
}
// ... then effOpen, effMainsChanged(1), effSetChunk, and per-callback:
//     effect->processReplacing(effect, inputArray, outputArray, frameCount);

```

```
// --- helpers/StringHelper.cpp : the exact quoting rules, verbatim ---
vector<wstring> StringHelper::splitQuoted(const wstring& s, wchar_t splitChar, wchar_t quoteChar)
{
    vector<wstring> result;
    bool inQuotes = false;
    wstring current;
    for (size_t i = 0; i < s.length(); i++)
    {
        wchar_t c = s[i];
        if (c == splitChar && !inQuotes)
        {
            if (current != L"")
            {
                result.push_back(current);
                current = L"";
            }
        }
        else if (c == quoteChar)
        {
            inQuotes = !inQuotes;
            if (inQuotes && i > 0 && s[i - 1] == quoteChar)
                current += quoteChar;   // "" -> literal "
        }
        else
        {
            current += c;
        }
    }
    if (current != L"")
        result.push_back(current);
    return result;
}
// No validation of unbalanced quotes -- parsing just ends in whatever state it is in.

```

```
// --- The contract PeaceQT's own DSP would have to satisfy (IFilter.h) ---
std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount,
                                    std::vector<std::wstring> channelNames);
void process(float** output, float** input, unsigned frameCount);
// DEINTERLEAVED float32, one pointer per channel. frameCount <= maxFrameCount, varies.
// VSTPluginFilter overrides the in-place hook to return false:
//   "return false to request that output and input do not point to the same memory locations"

```

UNKNOWNS: Whether the github.com/mirror/equalizerapo tree I read is byte-identical to 1.4.1/1.4.2. The installed EqualizerAPO.dll's embedded build paths (EqualizerAPO-src-1.4.1\filters\VSTPluginFilter.cpp) and its literal strings all match, but I could not diff the actual 1.4.x tarball. Verbatim quotes above came through WebFetch's summarizing model, so exact whitespace/brace formatting may differ slightly even where the semantics are right.; Whether APO clamps or validates parameter values before setParameter. The code passes it->second raw; nothing I read clamps to 0.0-1.0. Unverified whether a value like 5.0 or -1.0 is rejected somewhere upstream or simply handed to the plugin (which may then behave undefined).; Whether APO ever calls effClose / effMainsChanged(0) on teardown, and whether VSTPluginLibrary's instanceMap (static unordered_map<wstring, weak_ptr<VSTPluginLibrary>>) keeps the DLL resident across a config reload. If it does, reload cost is effOpen/effSetChunk only, not LoadLibrary -- but I did not read the destructor.; Whether effect->initialDelay (plugin-reported latency) is read anywhere. I saw no reference to it, which would mean hosted plugins with lookahead introduce uncompensated latency, but I did not exhaustively grep the source.; The exact frameCount APO receives on this machine. maxFrameCount comes from IAudioProcessingObject::LockForProcess (u32MaxFrameCount) and WASAPI shared-mode period is typically 10 ms (480 frames @ 48 kHz), but I did not measure it. Also unverified whether frameCount is constant per callback or varies -- APO's own code treats it as variable.; Whether the 10 ms crossfade (transitionLength = sampleRate/100) actually covers a VST reload cleanly. FilterConfiguration::doTransition's blending math was not retrieved; if the new config's plugin is constructed lazily on the audio thread rather than in loadConfig on the notification thread, a heavy plugin could still glitch. I confirmed loadConfig runs on the notification thread but did not confirm where VSTPluginFilter::initialize is called from.; Whether APO handles the VST2 audioMasterGetTime / tempo opcodes with real values or stubs. VSTPluginInstance.cpp's audioMaster handles audioMasterGetTime, but what it returns (valid VstTimeInfo vs NULL) was not retrieved -- relevant because tempo-sync'd effects (delays, LFO-based) would break.; The precise legal status in 2026 of shipping the VeSTige aeffectx.h header in a non-GPL product. Steinberg has issued DMCA takedowns against repos carrying it; clean-room reverse engineering for interoperability is widely held legal in the EU/US but this is contested and I am not qualified to advise. Equalizer APO gets away with it partly because it is itself GPLv2.; Whether ViPER4Windows' DSP is itself GPL/openly licensed and thus reimplementable by PeaceQT -- out of scope for this topic, but it directly affects whether 'PeaceQT does its own DSP' can legally copy ViPER's algorithms.; Whether the EqAPO64-with-VST3 fork's out-of-process host design (EqApoOutProcHost.exe) is usable as a template for PeaceQT given its AGPL/GPLv3 licence. I read only the project description, not the source.

---

## Writing PeaceQT's own Audio Processing Object (APO) DSP DLL registered in MMDevices on Windows 11 (2026)
- [high] An APO is an in-process COM DLL that must expose four interfaces beyond IUnknown: IAudioProcessingObject (Initialize + IsInputFormatSupported — setup and format negotiation), IAudioProcessingObjectConfiguration (LockForProcess + UnlockForProcess — where you allocate/free per-stream state), IAudioProcessingObjectRT (APOProcess — the DSP, called on the RT pump thread), and IAudioSystemEffects (a marker interface: 'makes the audio engine recognize a DLL as a systems effects APO'). Custom APOs must NOT expose IAudioProcessingObjectVBR.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects — 'Design Considerations for Custom APO Development' and 'Replacing System-supplied APOs' sections
  -> PeaceQT's DSP DLL needs exactly one COM coclass per pipeline slot implementing these four. Put all allocation (delay lines, reverb buffers, FFT plans, compressor state) in LockForProcess, keyed off the negotiated format; free in UnlockForProcess.
- [high] Microsoft's CBaseAudioProcessingObject base class (baseaudioprocessingobject.h, WDK) implements almost everything; if you accept float32 you only need to write IsInputFormatSupported, APOProcess and ValidateAndCacheConnectionInfo. But Equalizer APO does NOT use it — EqualizerAPO.cpp implements IUnknown/IAudioProcessingObject/IAudioProcessingObjectRT/IAudioProcessingObjectConfiguration/IAudioSystemEffects by hand, with its own ClassFactory.cpp and DllMain.cpp.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects (CBaseAudioProcessingObject) and https://sourceforge.net/p/equalizerapo/code/ci/main/tree/EqualizerAPO/ (ClassFactory.cpp, DllMain.cpp, EqualizerAPO.cpp, EqualizerAPO.def)
  -> Two viable routes. Hand-rolled COM (Equalizer APO style, ~4 files, no WDK dependency beyond audioenginebaseapo.h) is the lower-friction one for a Qt/CMake project; CBaseAudioProcessingObject drags in the WDK and MSVC-specific build settings.
- [high] The FxProperties registry slots are exactly: {d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1 = PKEY_FX_PreMixEffectClsid (legacy LFX), ,2 = PKEY_FX_PostMixEffectClsid (legacy GFX), ,5 = PKEY_FX_StreamEffectClsid (SFX), ,6 = PKEY_FX_ModeEffectClsid (MFX), ,7 = PKEY_FX_EndpointEffectClsid (EFX). ,0 = PKEY_FX_Association. All CLSID values are REG_SZ containing a braced GUID string. The processing-mode lists (PKEY_SFX/MFX/EFX_ProcessingModes_Supported_For_Streaming = {d3993a3f-99c2-4402-b5ec-a92a0367664b},5/6/7) are REG_MULTI_SZ. If both modern (SFX/MFX/EFX) and legacy (LFX/GFX) are configured, Windows uses the modern ones.
  evidence: https://github.com/dechamps/APO/blob/master/README.md (PID table + 'If both modern (SFX/MFX/EFX) and legacy (LFX/GFX) sAPOs are configured, then Windows will use the modern ones') and INF strings in https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects ('PKEY_FX_StreamEffectClsid = "{D04E05A6-594B-4fb6-A80D-01AF5EED7D1D},5"')
  -> The machine currently has Equalizer APO on pid 1 and 2 (legacy LFX/GFX). PeaceQT's installer must pick one scheme and be consistent — writing PeaceQT into pid 5/6/7 while Equalizer APO sits on 1/2 would silently disable Equalizer APO, because modern slots win.
- [high] Pipeline placement: SFX runs per-stream before the mix (only SFX may change channel count); MFX runs after the mix for a given signal-processing mode; EFX runs after the mix of all modes and is applied even to RAW streams. Windows 10+ does not load RAW SFX (it does load RAW MFX). Legacy LFX == pre-mix, GFX == post-mix.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture — 'Placement of Audio Effects' section ('Windows 10 loads RAW MFX but not RAW SFX')
  -> For PeaceQT's target effects (bass harmonics, exciter, tube, compressor, reverb) the right slot is MFX (post-mix, one instance, mode-aware, power-efficient) — same place Equalizer APO's GFX/post-mix sits. EFX is for speaker protection/compensation and would also hit RAW/exclusive-ish paths; avoid it.
- [high] APOProcess real-time rules, verbatim from MS: it 'is called from a real-time processing thread. The implementation of this method must not touch paged memory and it should not call any system blocking routines.' More broadly: 'All methods that are members of real-time interfaces must be implemented as nonblocking members. They must not block, use paged memory, or call any blocking system routines. All buffers that are processed by the APO must be nonpageable. All code and data in the process path must be nonpageable. APOs should not introduce significant latency.' APOProcess must not change the ppOutputConnections array itself but must set the output connection properties after processing.
  evidence: https://learn.microsoft.com/en-us/windows/win32/api/audioenginebaseapo/nf-audioenginebaseapo-iaudioprocessingobjectrt-apoprocess (Remarks) and https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects (Design Considerations)
  -> Concretely: no new/delete/malloc, no std::mutex/CriticalSection, no file or registry I/O, no logging, no COM calls, no std::string, no exceptions, no C++ static-local initialization (thread-safe statics take a lock) inside APOProcess. Page-lock the code+data path with the AVRT pragmas the way Equalizer APO does (#pragma AVRT_CODE_BEGIN / AVRT_CODE_END). Denormals will also wreck you: reverb tails and long IIR states need FTZ/DAZ set (_MM_SET_FLUSH_ZERO_MODE) — but set it in LockForProcess, and restore it, since you are a guest in audiodg.
- [high] Format negotiation protocol: the audio service sets the pre-mix APO's output to the default float32 format, then calls IsInputFormatSupported with a suggested format. Return S_OK (with the same format echoed) if you accept, S_FALSE plus a closest-match format in ppSupportedInputFormat if you don't, or APOERR_FORMAT_NOT_SUPPORTED if you have no close match. The post-mix (GFX) APO 'is not involved in the format negotiation process' — it simply works with whatever the pre-mix APO's output format is.
  evidence: https://learn.microsoft.com/en-us/windows/win32/api/audioenginebaseapo/nf-audioenginebaseapo-iaudioprocessingobject-isinputformatsupported (Remarks + return-value table)
  -> If PeaceQT installs post-mix/MFX only, format negotiation is nearly free: accept whatever arrives, read the actual sample rate / channel count / channel mask from the APO_CONNECTION_DESCRIPTOR passed to LockForProcess, and size all DSP state there. That is exactly what Equalizer APO does: engine.initialize(outFormat.fFramesPerSecond, inFormat.dwSamplesPerFrame, realChannelCount, outFormat.dwSamplesPerFrame, channelMask, maxFrameCount).
- [high] Registration is two registry writes, both under HKLM, both needing elevation: (a) normal in-proc COM registration HKLM\SOFTWARE\Classes\CLSID\{your-clsid}\InProcServer32 = full path to the DLL, ThreadingModel = "Both"; plus a descriptor block under HKCR\AudioEngine\AudioProcessingObjects\{clsid} (FriendlyName, Flags, Min/MaxInputConnections, Min/MaxOutputConnections, MaxInstances, NumAPOInterfaces, APOInterface0). (b) the per-endpoint FxProperties value. Verified on this machine: HKLM\SOFTWARE\Classes\CLSID\{EC1CC9CE-FAED-4822-828A-82A81A6F018F}\InProcServer32 = 'D:\Program Files\EqualizerAPO\EqualizerAPO.dll', ThreadingModel = 'Both'.
  evidence: Local registry read (PowerShell Get-ItemProperty on HKLM:\SOFTWARE\Classes\CLSID\{EC1CC9CE-FAED-4822-828A-82A81A6F018F}\InProcServer32); AudioEngine\AudioProcessingObjects key layout from the [Apo_AddReg] sample at https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects; HKEY_CLASSES_ROOT\AudioEngine\AudioProcessingObjects path used by Equalizer APO per https://raw.githubusercontent.com/mirror/equalizerapo/master/DeviceAPOInfo.cpp
  -> PeaceQT's DLL does NOT need to live in System32 or DriverStore — Equalizer APO registers its DLL in place under Program Files and audiodg loads it plus its dependencies (fftw3f.dll, sndfile.dll, msvcp140.dll) from that same directory. That means PeaceQT can ship the DSP DLL alongside the Qt app.
- [high] The MMDevices key is owned by TrustedInstaller: 'The only user with full control of that key is TrustedInstaller. Even Administrators have restricted permissions: they can add, modify and remove registry values, but they cannot change the keys.' Changes take effect after restarting the Windows Audio service (Restart-Service audiosrv) or disabling/re-enabling the endpoint.
  evidence: https://github.com/dechamps/APO/blob/master/README.md — Permissions and 'Applying Changes' sections
  -> PeaceQT's installer can write the FxProperties *values* as Administrator without taking ownership (values are writable; only creating/deleting subkeys is blocked). Plan on an elevated helper process, and on bouncing audiosrv — which momentarily kills audio for every app.
- [high] Windows tracks APO failures and will disable all system effects on an endpoint after 10 failures. It counts failing HRESULTs from CoCreateInstance, IsInputFormatSupported, IsOutputFormatSupported and LockForProcess; a successful LockForProcess resets the count. On reaching the limit 'the SFX, MFX and EFX APOs are disabled by setting the PKEY_Endpoint_Disable_SysFx registry key to 1'.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects — 'Troubleshooting APO Load Failures'
  -> A buggy PeaceQT APO won't just fail to load — it will get *all* effects on that endpoint switched off, and the user then sees 'Enhancements' greyed out with no explanation. PeaceQT should detect PKEY_AudioEndpoint_Disable_SysFx ({1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5, DWORD) and offer to clear it.
- [high] An APO DLL does NOT need to be signed for normal playback, but audiodg.exe runs as a protected process when the graph carries protected (DRM) content, and there an unsigned/improperly-signed APO fails to load: MS lists 'The graph is running protected content, and the APO is not properly signed' as a primary CoCreateInstance failure cause. Equalizer APO sidesteps this by setting HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\DisableProtectedAudioDG = 1. Verified on this machine: that value is present and = 1, and D:\Program Files\EqualizerAPO\EqualizerAPO.dll reports Get-AuthenticodeSignature Status = NotSigned.
  evidence: Local: Get-AuthenticodeSignature → 'NotSigned'; Get-ItemProperty HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio → DisableProtectedAudioDG : 1. Docs: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects ('Troubleshooting APO Load Failures'); Equalizer APO writes to that key per https://raw.githubusercontent.com/mirror/equalizerapo/master/DeviceAPOInfo.cpp
  -> PeaceQT can ship an unsigned APO today because Equalizer APO already set DisableProtectedAudioDG=1 on this box. But that is a test/debug key, it is global, and it is the reason some users find DRM audio (Netflix in Edge, etc.) misbehaving. If PeaceQT ships this to other people, it must own that decision explicitly, tell the user, and restore the key on uninstall — and it must not silently set it if another product already did.
- [high] You must disable the embedded manifest in the APO project: 'If you have an embedded manifest, this triggers the use of certain APIs which are forbidden within a protected environment. This means that your APO will run with DisableProtectedAudioDG=1, but when this test key is removed, your APO will fail to load, even if it is WHQL-signed.'
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects — 'Working with Visual Studio and APOs' / 'Disable Use of an Embedded Manifest'
  -> In PeaceQT's CMake target for the APO DLL: /MANIFEST:NO. Also consider /MT static CRT to avoid depending on vcruntime resolution inside audiodg (Equalizer APO chose dynamic and ships the redist).
- [high] audiodg.exe hosts the APO and runs as LocalService — it has no access to the user profile. dechamps: 'the service does not have access to your user directory'. Equalizer APO's own log therefore lands in C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log, and its config lives under Program Files.
  evidence: https://github.com/dechamps/APO/blob/master/README.md — 'Runtime Environment' / permissions section
  -> PeaceQT cannot have the APO read presets from %APPDATA%. Either keep DSP state in a machine-wide location the service can read, or (better) pass parameters via shared memory from the GUI — see the ViPER4Windows pattern below.
- [high] A crash inside APOProcess takes down audiodg.exe and kills audio for every application until it restarts; repeated crashes mean effectively no system audio. This is a well-populated failure class for Equalizer APO specifically (faulting modules libfftw3f-3.dll, VST plugins, third-party APOs like THXSYSVAD2APO.dll), and version 1.2 had to fix an FFTW concurrency bug that manifested as audiodg crashes when GraphicEQ ran on both an output and an input device.
  evidence: https://sourceforge.net/p/equalizerapo/tickets/42/ (Crashing, faulting module libfftw3f-3.dll); https://sourceforge.net/p/equalizerapo/tickets/244/ (audiodg.exe crash after some seconds); https://sourceforge.net/p/equalizerapo/discussion/general/thread/75408993/ (audiodg 100% CPU, no sound)
  -> Two instances of PeaceQT's APO can be live simultaneously (render + capture, or multiple endpoints) inside one audiodg. Any shared global state — FFT plans, scratch buffers, wisdom tables — must be per-instance or genuinely thread-safe. And PeaceQT needs an out-of-band recovery path (a 'disable PeaceQT DSP' entry that works with no audio, ideally a .reg file and a Safe Mode note), because when this breaks the user cannot hear the app tell them it broke.
- [high] Windows 11 build 22000+ adds the CAPX API set: IAudioSystemEffects3 (implicitly signals support for the APO Settings framework), APOInitSystemEffects3, IAudioProcessingObjectNotifications/2 (volume, endpoint and effects-property-store change notifications, delivered on a serial queue via GetApoNotificationRegistrationInfo/HandleNotification), IAudioSystemEffectsPropertyStore with default/user/volatile substores, and IAudioProcessingObjectLoggingService ETW logging (provider {8b4a0b51-5dcf-5a9c-2817-95d0ec876a87}). MS: 'Any new APOs that ship on a device for Windows 11 are required to be compliant with the APIs listed in this topic, validated via HLK.' Windows 10 has no support for these APIs. Logging APIs must not be called from the RT streaming thread.
  evidence: https://raw.githubusercontent.com/MicrosoftDocs/windows-driver-docs/staging/windows-driver-docs-pr/audio/windows-11-apis-for-audio-processing-objects.md
  -> The HLK/CAPX requirement applies to APOs that 'ship on a device' (OEM/IHV preload), not to a user-installed third-party DLL — PeaceQT is not gated by it. But IAudioSystemEffectsPropertyStore's user substore is genuinely useful: it is 'persisted by the OS across upgrades and migrations', unlike the default substore which is repopulated from the INF and reset on OS upgrade. That is the one officially-blessed place to survive feature updates.
- [high] Microsoft's supported path for new APOs is the componentized model, and it explicitly excludes what PeaceQT wants to do: 'The registration of the APO with the audio engine is done using a newly created APO device. For the audio engine to make use of the new APO device it must be a PNP child of the audio device, sibling of the audio endpoints. The new componentized APO design does not allow for an APO to be registered globally and used by multiple different drivers. Each driver must register its own APOs.' Componentized registration uses HKR under an AudioProcessingObject-class device with the DLL run from DriverStore, and requires an INF and a driver package.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects — 'Componentized APO Installation'
  -> PeaceQT has no audio driver, so it cannot use the supported model. It must do what Equalizer APO and ViPER4Windows do: global HKCR/HKLM CLSID registration plus a direct write to another vendor's endpoint FxProperties. That is unsupported-but-functional territory, and it is why this whole approach is structurally fragile. Frame it to the user that way.
- [high] Why reinstallation after Windows feature updates is the perennial support burden: the per-endpoint FxProperties values live in registry keys owned by the audio driver's installation. dechamps: 'such custom configuration will be overwritten every time the audio driver is reinstalled, and some Windows updates have a tendency to reinstall audio drivers.' Endpoint GUIDs themselves also change when drivers reinstall, so the old endpoint key is orphaned (device shows as disabled/disconnected) and a brand-new endpoint key appears with default FxProperties. Users hit exactly this on Windows 11 24H2.
  evidence: https://github.com/dechamps/APO/blob/master/README.md ('Driver Reinstallation Effects'); https://sourceforge.net/p/equalizerapo/discussion/general/thread/cd88fee23e/ ('Equalizer APO keeps losing devices after Windows updates'); https://sourceforge.net/p/equalizerapo/discussion/general/thread/7df05ab8b2/ (no longer working after 24H2)
  -> PeaceQT must ship a watchdog: a user-session service/tray component that periodically enumerates HKLM\...\MMDevices\Audio\Render\*, checks whether its CLSID is still in FxProperties on the endpoints the user selected, and re-applies (elevated) when it isn't. Peace already does a version of this ('does show a notification if Windows updates' and can 'automatically repair things'). This is not optional polish — it is the single largest ongoing cost of shipping an APO.
- [high] Equalizer APO's install/uninstall logic is a directly reusable blueprint. DeviceAPOInfo.cpp enumerates HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render (and \Capture) subkeys, reads the DeviceState DWORD, and on install() backs up the *existing* CLSID in each slot under its own APP_REGPATH L"\\Child APOs\\<deviceGuid>", using sentinel strings APOGUID_NOKEY / APOGUID_NOVALUE when nothing was there. uninstall() restores originalApoGuids[i] or deletes the value entirely. It supports three install modes: LFX_GFX, SFX_MFX, and SFX_EFX. At runtime EqualizerAPO::Initialize CoCreateInstances the saved original APO as a child and chains to it — that is how 'Use original APO' works.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/DeviceAPOInfo.cpp (install()/uninstall()/loadAllInfos()); https://raw.githubusercontent.com/mirror/equalizerapo/master/EqualizerAPO/EqualizerAPO.cpp (Initialize does registry access + CoCreateInstance of child APO; IsInputFormatSupported delegates to child APO)
  -> Copy this design wholesale: back up before you overwrite, chain the displaced OEM APO instead of destroying it, and support multiple slot modes because different drivers (Realtek UAD in particular) only cooperate with some. Note this also means PeaceQT would be displacing Equalizer APO on this machine unless it chains it as a child.
- [high] There is a current, working, open-source APO that already implements precisely the effect set the user asked for: likelikeslike/ViPER4Windows (created 2026-04-09, last pushed 2026-06-07, 41 stars, primary language Dart for the Flutter UI). Its ViPER4WindowsAPO/ directory contains DllMain.cpp, ViPER4WindowsAPO.cpp/.h/.def/.rgs, SharedParams.h, ViPERLog.h, and the DSP lives in a ViPERDSP submodule. README lists bass enhancement (Natural/Pure Bass/Subwoofer), Clarity/Exciter (Natural/OZone/XHiFi), full-room-modeling reverb, a FET compressor, tube simulator, spectrum extension, surround, convolver. It requires Windows 10 1809+ x64 and a reboot for registration to take effect. CRITICAL: the GitHub API reports license: null — there is no license file.
  evidence: https://api.github.com/repos/likelikeslike/ViPER4Windows (created_at, pushed_at, stargazers_count, license: None); https://api.github.com/repos/likelikeslike/ViPER4Windows/contents/ViPER4WindowsAPO; https://raw.githubusercontent.com/likelikeslike/ViPER4Windows/main/README.md
  -> Read it as a reference implementation — it proves the whole approach works in 2026 with a modern GUI toolkit. Do NOT copy code: with no license, all rights are reserved and PeaceQT cannot lawfully vendor any of it. Reimplement the algorithms from DSP literature instead.
- [high] ViPER4Windows solves the GUI-to-RT-thread parameter problem with a lock-free named shared-memory block plus a change event: #define VIPER_SHM_NAME L"Global\\ViPER4Windows_Params", #define VIPER_EVENT_NAME L"Global\\ViPER4Windows_ParamsChanged", and a #pragma pack(push,1) POD struct ViPERSharedParams whose first two fields are uint32_t version; uint32_t sequenceNumber; followed by flat scalar parameters (fetCompressorThreshold/Ratio/Knee/Attack/Release/Gain, reverberationRoomSize/RoomWidth/RoomDampening/WetSignal/DrySignal, spectrumExtensionExciter, equalizerBands[31], etc.).
  evidence: https://raw.githubusercontent.com/likelikeslike/ViPER4Windows/main/ViPER4WindowsAPO/SharedParams.h
  -> This is the pattern PeaceQT should adopt and it is a much better fit than Equalizer APO's file-watching config directory. Qt GUI opens the Global\\ section (needs SeCreateGlobalPrivilege / a DACL that grants LocalService read, since audiodg is LocalService), bumps sequenceNumber before and after writing, and signals the event; APOProcess does a seqlock read of a plain POD struct with zero allocation and zero locks. Fixed-size arrays, no pointers, no strings in the struct.
- [high] An APO 'must have one input and one output connection', it 'can modify only the audio data that is passed to it through APOProcess', and it 'cannot change the settings of the underlying logical device, including its KS topology'. Only SFX APOs may change the channel count. Equalizer APO's IsInputFormatSupported explicitly rejects downmixing (input channels > output channels).
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects (Design Considerations); https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture (SFX 'can be used for changing channel count before the mixer'); https://raw.githubusercontent.com/mirror/equalizerapo/master/EqualizerAPO/EqualizerAPO.cpp
  -> PeaceQT's reverb and stereo-widening effects can't upmix from an MFX slot. If PeaceQT ever wants channel-count changes (e.g. 2->5.1 upmix), that has to be an SFX APO, which means per-application instances and RAW-mode streams bypassing it entirely.
- [medium] An APO never sees exclusive-mode (WASAPI exclusive / ASIO) streams at all — those bypass the audio engine's shared-mode graph — and Windows 10+ skips SFX APOs entirely in RAW mode.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture — 'Software based effects are inserted in the software device pipe on stream initialization' and 'Windows 10 loads RAW MFX but not RAW SFX'
  -> Set expectations in PeaceQT's UI: DSP will not apply to exclusive-mode players (foobar2000 WASAPI exclusive, ASIO games/DAWs). This is the same limitation Equalizer APO has and users will report it as a bug otherwise.
- [high] Microsoft's alternative to writing an APO is not a nicer user-mode API — it is shipping an audio driver. The supported options are (a) a real/virtual audio driver with a componentized APO (SysVAD sample), or (b) a proxy APO advertising hardware DSP (MsApoFxProxy.dll + KSPROPSETID_AudioEffectsDiscovery). The user-facing UI is supposed to be a Hardware Support App (HSA). Windows.Media.Effects.AudioRenderEffectsManager is discovery-only: it lets apps ask which effects are active, not add them.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture ('Proxy APO for Hardware DSP', 'Custom APO Support App', 'Application Audio Effect Awareness'); https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/
  -> There is no modern sanctioned path for a driverless third-party effects app. The realistic non-APO alternative for PeaceQT is a virtual audio device (VB-Cable / Voicemeeter style) plus a user-mode processing app — which trades unsigned-DLL-inside-audiodg risk for kernel-driver signing (EV cert + attestation/WHQL submission), plus the UX cost of the user having to switch their default playback device.
- [high] BLUNT RISK ASSESSMENT — the realistic failure mode for a hobby APO is: it works on your machine, and on other people's machines it produces silent-and-unattributable system-wide audio loss. The chain is: your DLL crashes or hangs inside audiodg.exe (LocalService, no debugger attached, no console) -> all audio for all applications stops -> the user has no working sound to be told why -> Windows may then set PKEY_Endpoint_Disable_SysFx=1 and permanently kill all effects on that endpoint -> and the user blames Windows or their headphones, not PeaceQT. Layered on top: a Windows feature update reinstalls the audio driver, the endpoint GUID changes, your FxProperties entry evaporates, and every user files the same 'stopped working' report. Equalizer APO has been shipping since 2012 with a full-time maintainer and this is still its dominant support load.
  evidence: Composite of: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects (failure count -> Disable_SysFx at 10); https://sourceforge.net/p/equalizerapo/tickets/244/; https://sourceforge.net/p/equalizerapo/discussion/general/thread/7df05ab8b2/; https://github.com/dechamps/APO/blob/master/README.md (driver reinstall wipes config)
  -> If PeaceQT does this, budget for the infrastructure, not the DSP: (1) an offline recovery .reg + documented Safe Mode procedure shipped in the install dir, (2) a watchdog that re-applies registration after updates, (3) a kill-switch flag the APO reads before doing anything so a bad preset can be neutralized without touching audiodg, (4) an ETW/file log written from LockForProcess (never APOProcess), (5) a bit-exact offline harness (Equalizer APO ships Benchmark.exe for exactly this) so DSP is validated outside audiodg before it ever runs inside it.
- [medium] A pragmatic intermediate exists that avoids writing an APO at all: Equalizer APO's config language includes VSTPlugin and Convolution, and Equalizer APO is already registered on the default endpoint on this machine as both pre-mix and post-mix.
  evidence: Local: D:\Program Files\EqualizerAPO contains EqualizerAPO.dll plus fftw3f.dll/libfftw3f-3.dll/sndfile.dll; project context states VSTPlugin and Convolution are among APO's 16 commands and both pid1/pid2 are registered
  -> PeaceQT could ship its nonlinear DSP (harmonic bass, exciter, tube saturation, compressor, algorithmic reverb) as a VST2/VST3 plugin loaded by Equalizer APO's VSTPlugin command, and keep generating config text as it does today. That gets nonlinear processing into the pipeline with zero registry surgery, zero audiodg-crash liability owned by PeaceQT, and no signing question — at the cost of depending on Equalizer APO being installed and inheriting its host's crash behaviour. This is the lowest-risk first milestone and it de-risks the DSP itself before any APO work begins.

### code
```
// Minimal APO coclass shape (Equalizer APO does exactly this set, by hand,
// in EqualizerAPO/EqualizerAPO.h + ClassFactory.cpp + DllMain.cpp)
class PeaceAPO : public IAudioProcessingObject,
                 public IAudioProcessingObjectRT,
                 public IAudioProcessingObjectConfiguration,
                 public IAudioSystemEffects        // marker: "I am a system effect"
{
    // IAudioProcessingObject
    STDMETHOD(Initialize)(UINT32 cbDataSize, BYTE* pbyData);        // reads APOInitSystemEffects*
    STDMETHOD(IsInputFormatSupported)(IAudioMediaType* opposite,
                                      IAudioMediaType* requested,
                                      IAudioMediaType** supported);
    STDMETHOD(IsOutputFormatSupported)(...);
    STDMETHOD(GetLatency)(HNSTIME* pTime);
    // IAudioProcessingObjectConfiguration  -- ALL ALLOCATION HAPPENS HERE
    STDMETHOD(LockForProcess)(UINT32 nIn,  APO_CONNECTION_DESCRIPTOR** ppIn,
                              UINT32 nOut, APO_CONNECTION_DESCRIPTOR** ppOut);
    STDMETHOD(UnlockForProcess)();
    // IAudioProcessingObjectRT  -- REAL-TIME, NOTHING MAY ALLOCATE OR BLOCK
    STDMETHOD_(void, APOProcess)(UINT32 nIn,  APO_CONNECTION_PROPERTY** ppIn,
                                 UINT32 nOut, APO_CONNECTION_PROPERTY** ppOut);
    STDMETHOD_(UINT32, CalcInputFrames)(UINT32 n);
    STDMETHOD_(UINT32, CalcOutputFrames)(UINT32 n);
};
```

```
; ---- Registration, part 1: ordinary in-proc COM (elevated, HKLM) ----
; Verified layout of the existing Equalizer APO entry on this machine:
[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{EC1CC9CE-FAED-4822-828A-82A81A6F018F}\InProcServer32]
@="D:\\Program Files\\EqualizerAPO\\EqualizerAPO.dll"
"ThreadingModel"="Both"

; Plus the audio-engine descriptor block (from the MS [Apo_AddReg] sample):
[HKEY_CLASSES_ROOT\AudioEngine\AudioProcessingObjects\{your-clsid}]
"FriendlyName"="PeaceQT Effects (MFX)"
"Flags"=dword:00000001
"MinInputConnections"=dword:00000001
"MaxInputConnections"=dword:00000001
"MinOutputConnections"=dword:00000001
"MaxOutputConnections"=dword:00000001
"MaxInstances"=dword:ffffffff
"NumAPOInterfaces"=dword:00000001
"APOInterface0"="{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}"
```

```
; ---- Registration, part 2: per-endpoint slot. REG_SZ, braced GUID string. ----
; HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{endpoint}\FxProperties
;
; LEGACY (what Equalizer APO uses on this machine today):
;   "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1" = pre-mix  (LFX) = {EACD2258-...}
;   "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2" = post-mix (GFX) = {EC1CC9CE-...}
;
; MODERN (wins over legacy if both are present):
;   "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5" = SFX  PKEY_FX_StreamEffectClsid
;   "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6" = MFX  PKEY_FX_ModeEffectClsid   <-- target this
;   "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7" = EFX  PKEY_FX_EndpointEffectClsid
;   "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},0" = PKEY_FX_Association (KSNODETYPE GUID)
;
; Mode lists are REG_MULTI_SZ, not REG_SZ:
;   "{d3993a3f-99c2-4402-b5ec-a92a0367664b},6" = MFX modes, e.g.
;        {C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}  (AUDIO_SIGNALPROCESSINGMODE_DEFAULT)
;
; Escape hatch (DWORD): "{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5" = PKEY_AudioEndpoint_Disable_SysFx
```

```
// Lock-free GUI -> RT parameter handoff. Verbatim shape from ViPER4Windows
// (ViPER4WindowsAPO/SharedParams.h) -- reference only, that repo has NO LICENSE.
#define VIPER_SHM_NAME   L"Global\\ViPER4Windows_Params"
#define VIPER_EVENT_NAME L"Global\\ViPER4Windows_ParamsChanged"

#pragma pack(push, 1)
struct ViPERSharedParams {
    uint32_t version;
    uint32_t sequenceNumber;      // seqlock: bump before AND after a GUI write
    uint32_t masterEnabled;
    // ... flat PODs only, no pointers, no strings, fixed-size arrays:
    int32_t  fetCompressorThreshold, fetCompressorRatio, fetCompressorKnee;
    int32_t  fetCompressorAttack,    fetCompressorRelease, fetCompressorGain;
    int32_t  reverberationRoomSize,  reverberationRoomWidth,
             reverberationRoomDampening, reverberationWetSignal,
             reverberationDrySignal;
    int32_t  spectrumExtensionExciter;
    int32_t  equalizerBands[31];
};
#pragma pack(pop)

// APOProcess side: read seq -> memcpy struct -> read seq again -> retry if changed.
// No CreateFileMapping, no WaitForSingleObject, no allocation on that path.
```

```
// Real-time discipline inside APOProcess. Equalizer APO brackets it with the
// AVRT pragmas so the code pages are non-pageable, per the MS requirement that
// "All code and data in the process path must be nonpageable."
#pragma AVRT_CODE_BEGIN
STDMETHODIMP_(void) PeaceAPO::APOProcess(
        UINT32 nIn, APO_CONNECTION_PROPERTY** ppIn,
        UINT32 nOut, APO_CONNECTION_PROPERTY** ppOut)
{
    // FORBIDDEN in here: new/delete/malloc, std::mutex/EnterCriticalSection,
    // any file/registry/COM call, logging, std::string, exceptions,
    // function-local statics (their guard takes a lock), Sleep/Wait*.
    switch (ppIn[0]->u32BufferFlags) {
    case BUFFER_VALID:  /* run DSP in-place or in->out */ break;
    case BUFFER_SILENT: /* still run the reverb tail, or emit silence */ break;
    }
    // Must NOT modify the ppOutputConnections array itself,
    // but MUST set the output connection properties after processing:
    ppOut[0]->u32ValidFrameCount = ppIn[0]->u32ValidFrameCount;
    ppOut[0]->u32BufferFlags     = BUFFER_VALID;
}
#pragma AVRT_CODE_END
```

```
# Apply registration changes (elevated). Values under FxProperties are writable
# by Administrators; only creating/deleting the KEYS is blocked (TrustedInstaller).
Restart-Service -Name audiosrv -Force
# ...or disable + re-enable the endpoint in Device Manager.

# Unsigned-APO escape hatch that Equalizer APO's installer sets.
# CONFIRMED PRESENT AND = 1 on this machine:
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio' |
    Select-Object DisableProtectedAudioDG      # -> 1
Get-AuthenticodeSignature 'D:\Program Files\EqualizerAPO\EqualizerAPO.dll' |
    Select-Object Status                        # -> NotSigned
```

UNKNOWNS: Equalizer APO's exact source license was not verified — I did not open License.txt in the repo. Before reusing any of DeviceAPOInfo.cpp / ClassFactory.cpp logic in PeaceQT, read https://sourceforge.net/p/equalizerapo/code/ci/main/tree/License.txt. (ViPER4Windows I did verify: no license at all, so it is reference-only.); I read the mirror/equalizerapo GitHub mirror, not the current SourceForge 1.4.2 tree, for EqualizerAPO.cpp and DeviceAPOInfo.cpp contents. The current tree (last touched 2025-11-28, and now including ARM64 .rc files) may differ; the file inventory I confirmed on SourceForge matches, but the code quotes are from the mirror.; Whether Windows 11 25H2/26xx has tightened anything about globally-registered third-party APOs (Smart App Control, core isolation / memory integrity, HVCI interaction with audiodg) — I found no source either way. Not tested on this machine.; Whether DisableProtectedAudioDG still works on the newest Windows 11 builds, and whether Microsoft has ever documented it (I only found it in Equalizer APO's source and in forum/community posts, not in Microsoft Learn). Treat it as an undocumented test key that could be removed.; Whether an ARM64 build is needed for PeaceQT's target users, and whether an x64 APO loads under an ARM64 audiodg (Equalizer APO ships EqualizerAPO-ARM64.rc, implying they build a native ARM64 DLL, but I did not confirm the loading rules).; The exact ACL required on the Global\ shared-memory section so that audiodg.exe (LocalService, and a protected process when DisableProtectedAudioDG is absent) can open it — a default-DACL section created by an elevated GUI will likely NOT be readable by LocalService. This needs a hand-built SECURITY_ATTRIBUTES and empirical testing.; Whether an APO can safely call _MM_SET_FLUSH_ZERO_MODE / change MXCSR inside audiodg without upsetting other APOs sharing the same thread — I inferred this is needed for reverb denormals but found no Microsoft guidance on it.; Sonarworks' published notes on their Windows APO insert (a commercial precedent with the same architecture) — their support page returned HTTP 403 and I could not read it.; Whether Equalizer APO's Benchmark.exe harness is reusable as an offline DSP test rig for PeaceQT, or is too tightly coupled to ParametricEQ/FilterEngine.; How Windows behaves when TWO third-party APOs both want the same slot on the same endpoint — I know Equalizer APO chains the displaced APO as a child via CoCreateInstance, but I did not verify whether PeaceQT chaining Equalizer APO (which itself chains the Realtek APO) actually works, or how deep that nesting can go before format negotiation breaks.

---

## DSP algorithms behind the ViPER4Windows effects (psychoacoustic bass, exciter/clarity, tube saturation, compressor, algorithmic reverb, convolution reverb) — enough detail to implement from scratch in C++ inside PeaceQT
- [high] ViPER4Windows' effect list is now open source and reverse-engineered: the DSP lives in the ViPERDSP submodule, one .cpp/.h pair per effect. The effects the user screenshotted map to concrete files: ViPERBass.cpp, ViPERBassMono.cpp, PsychoacousticBass.cpp, ViPERClarity.cpp, TubeSimulator.cpp, Reverberation.cpp, DynamicSystem.cpp, FETCompressor.cpp, MultibandCompressor.cpp, SpectrumExtend.cpp, Convolver.cpp, DynamicEQ.cpp, VHE_L0..L4, DiffSurround, StereoImager, ColorfulMusic, Cure, AnalogX, SpeakerCorrection, ViPERDDC, LUFSTargeting, SoftwareLimiter, PlaybackGain, IIRFilter.
  evidence: https://github.com/likelikeslike/ViPERDSP/tree/main/viper/effects (directory listing, 46 files); parent repo https://github.com/likelikeslike/ViPERWindows and https://github.com/AndroidAudioMods/ViPERFX_RE (DSP re-implemented from a decompilation of libv4a_fx.so, float32)
  -> Before writing anything from scratch, clone https://github.com/likelikeslike/ViPERDSP and read viper/effects/*.cpp. It is a working, buildable C++ reference for exactly the effects the user wants, licensed as an open repo. PeaceQT can either port these files or use them as a correctness oracle for its own implementations.
- [high] ViPER's own 'Tube Simulator' does no distortion at all. TubeSimulator.cpp contains no nonlinearity, no oversampling and no DC blocker — the entire per-sample operation is acc[ch] = (acc[ch] + x[ch]) / 2.0, i.e. a fixed one-pole lowpass at fs/2-ish smoothing, written back to the buffer.
  evidence: https://raw.githubusercontent.com/likelikeslike/ViPERDSP/main/viper/effects/TubeSimulator.cpp — only numeric constant is the divisor 2.0, initial accumulator {0.0, 0.0}
  -> Do not port ViPER's tube. Implement a real one (see the asymmetric-bias tanh + ADAA + DC blocker sample). PeaceQT can honestly claim to be better than ViPER here, and it is a cheap win.
- [high] ViPER's 'Reverberation' is Freeverb. Reverberation.cpp is a thin wrapper over a model_ object whose API is exactly Freeverb's revmodel: SetRoomSize, SetWidth, SetDamp, SetWet, SetDry (dry initialised to 0.5f), and Process() calls model_.ProcessReplace(buffer, buffer+1, size) — the classic Freeverb interleaved-stereo signature.
  evidence: https://raw.githubusercontent.com/likelikeslike/ViPERDSP/main/viper/effects/Reverberation.cpp
  -> Freeverb is the minimum bar to match ViPER. But Freeverb has no pre-delay, no bandwidth control, no density control and no early-reflection mix — so if the user's screenshot shows those knobs, they come from a different (Dattorro-style) engine, not from Freeverb. Implement Dattorro to cover the full knob set.
- [high] Freeverb's exact structure and tuning constants (verified from source): 8 parallel lowpass-feedback comb filters + 4 series allpasses per channel, tuned at 44100 Hz. Comb delays L = 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617; allpass delays L = 556, 441, 341, 225; stereospread = 23 samples added to every one of the 12 delays for the right channel. Scale factors: fixedgain = 0.015, scalewet = 3, scaledry = 2, scaledamp = 0.4, scaleroom = 0.28, offsetroom = 0.7, initialroom = 0.5, initialdamp = 0.5, initialwet = 1/3, initialdry = 0, initialwidth = 1, freezemode = 0.5. Allpass feedback is fixed at 0.5.
  evidence: https://raw.githubusercontent.com/sinshu/freeverb/master/Components/tuning.h (all constants listed verbatim); structural description at https://ccrma.stanford.edu/~jos/pasp/Freeverb.html ('four Schroeder allpasses in series and eight parallel Schroeder-Moorer filtered-feedback comb-filters', stereospread default 23)
  -> Scale all 12 delay lengths by fs/44100.0 when running at 48k/96k, otherwise the reverb time and colour shift. Parameter mapping: comb feedback = roomsize*0.28 + 0.7 (so roomsize 0..1 -> feedback 0.70..0.98); comb damp1 = damping*0.4, damp2 = 1-damp1; wet1 = wet*(width/2 + 0.5), wet2 = wet*((1-width)/2); input is scaled by fixedgain = 0.015 before the combs.
- [high] Dattorro's plate reverb ('Effect Design Part 1', JAES 45(9), 1997) is the topology whose knobs match the user's screenshot one-for-one: pre-delay, bandwidth (input LP), input diffusion 1/2 (= density), decay, damping, decay diffusion 1/2. Defaults from Table 1: Fs = 29761 Hz, EXCURSION = 16 samples, decay = 0.50, decay diffusion 1 = 0.70, decay diffusion 2 = 0.50 (rule: decay+0.15, clamped to [0.25, 0.50]), input diffusion 1 = 0.750, input diffusion 2 = 0.625, bandwidth = 0.9995 (full bandwidth = 0.9999999). Damping and bandwidth coefficients are recommended in the range 0.0 to 0.9999999, and the three one-pole lowpasses must be implemented direct-form I so internal nodes do not clip prematurely.
  evidence: Dattorro, 'Effect Design Part 1: Reverberator and Other Filters', https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf — Table 1 (p. 662) and surrounding text, extracted with pdftotext
  -> Use Dattorro as PeaceQT's main reverb, not Freeverb. Map the user's knobs: room size -> scale factor on the four tank delay lengths (672/4453/1800/3720 and 908/4217/2656/3163); damping -> tank one-pole damping coefficient; density -> input diffusion 1/2 coefficients; bandwidth -> input one-pole LP coefficient; decay -> tank decay coefficient; pre-delay -> the z^-N block before the bandwidth filter; early-reflections mix -> weight on the first output taps vs the later ones.
- [high] Dattorro's delay lengths and output taps are recoverable exactly. Delay lengths in samples at 29761 Hz: input diffusers 142, 107, 379, 277; left tank branch 672(+EXCURSION), 4453, 1800, 3720; right tank branch 908(+EXCURSION), 4217, 2656, 3163. Output taps (Table 2, verbatim): YL = 0.6*node48_54[266] + 0.6*node48_54[2974] - 0.6*node55_59[1913] + 0.6*node59_63[1996] - 0.6*node24_30[1990] - 0.6*node31_33[187] - 0.6*node33_39[1066]; YR = 0.6*node24_30[353] + 0.6*node24_30[3627] - 0.6*node31_33[1228] + 0.6*node33_39[2673] - 0.6*node48_54[2111] - 0.6*node55_59[335] - 0.6*node59_63[121]. The taps at nodes 24 and 48 are the modulating ones (LFO ~1 Hz, peak excursion ~8-16 samples, LFO updated at the audio rate to avoid aliasing artifacts).
  evidence: Table 2 and Fig. 1 caption of https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf (extracted text); independently cross-checked against https://github.com/khoin/DattorroReverbNode which stores the same values in seconds — e.g. 0.004771345*29761 = 142.0, 0.012734787*29761 = 379.0, 0.149625349*29761 = 4453.0, 0.008937872*29761 = 266.0, 0.121870905*29761 = 3627.0 — all 12 delays and all 14 taps match to the integer
  -> Multiply every length by fs/29761.0 and round; allocate the delay lines at the next power of two for cheap masked indexing. Keep the 0.6 tap weights and the +/- signs exactly — the alternating signs are what decorrelate L and R.
- [high] The 'missing fundamental' basis of psychoacoustic bass is real and patented in an implementable form. Waves' MaxxBass patent US5930373A: split off a low band (low frequency range of interest typically 20-300 Hz; practical desktop case 40-120 Hz), synthesise a residue signal containing at least three consecutive harmonics of the fundamental (2nd-4th harmonic for fundamentals 60-120 Hz; 3rd-5th for 40-60 Hz), high-pass it at the speaker's usable cutoff (120 Hz in the worked example), scale it by a 'Residue Expansion Ratio' so the residue is loudness-matched to the fundamental it replaces (~1.34 when the 2nd harmonic is the first dominant residue harmonic, ~1.74 when the 3rd is), then sum with the unmodified high band. The original MaxxBass patents expired 2006-2008.
  evidence: https://patents.google.com/patent/US5930373A/en (claims and worked example); AES paper 'The Effect of MaxxBass Psychoacoustic Bass Enhancement on Loudspeaker Design' https://secure.aes.org/forum/pubs/conventions/?elib=8288 — extension of ~1.5 octaves below the loudspeaker cutoff
  -> The 'speaker size' knob IS the cutoff fc. Small laptop speaker ~ 150-200 Hz, laptop/phone ~ 250-300 Hz, bookshelf ~ 80-120 Hz, headphones ~ 40-60 Hz. Below fc you delete the fundamental (or leave it, cheaper) and inject harmonics at 2fc..5fc. Ship fc as a combo box labelled by speaker type, mapped to Hz internally, with an expert Hz spinbox.
- [high] Two competing harmonic-generation methods, plus the state-of-the-art hybrid. NLD (non-linear device): waveshape the low band with a memoryless nonlinearity — half-wave rectifier, full-wave rectifier, hard/soft clipper, exponential, or a Chebyshev polynomial designed to emit chosen harmonic orders — cheap, sample-accurate, excellent on transients, but intermodulation-distorts polyphonic bass. PV (phase vocoder): STFT the low band, for each detected partial write energy into bins at 2f, 3f, ... with the correct phase advance — clean on sustained/pitched material, but smears transients and adds a frame of latency. Hill & Hawksford's hybrid crossfades between them using a transient-content detector built on a constant-Q transform (CQT), favouring NLD on transients and PV on steady pitched signals.
  evidence: https://adamjhill.com/AJHILL/wp-content/uploads/2017/05/Hill-Hawksford-AES-129.pdf and https://adamjhill.com/AJHILL/wp-content/uploads/2017/05/Hill-Hawksford-CEEC-2010.pdf; Aalto DAFx-20 'Virtual Bass System with Fuzzy Separation of Tones and Transients' http://research.spa.aalto.fi/publications/papers/dafx20-vbs/media/DAFx2020_vbs.pdf; 'Synthesis and Implementation of Virtual Bass System with a Phase-Vocoder Approach' (MaxxBass block diagram incl. upward-compressor loop) https://www.researchgate.net/publication/228362764
  -> Ship NLD only for v1 — it is 40 lines, zero latency, and RT-safe. Structure: LR4 crossover at fc; lowband -> NLD -> bandpass [fc, ~5fc] -> level-match against the pre-NLD lowband envelope -> sum with highband. Add a compressor on the harmonic branch (MaxxBass uses an upward compressor there) so quiet bass still gets audible harmonics. Only add the PV path if the user complains about muddy polyphonic bass.
- [high] ViPER's own ViPERBass is much simpler than MaxxBass, and useful as a floor. Two identical biquad lowpasses at 60 Hz default, Q = 0.53, cutoff settable via SetFrequency(). Three modes: NATURAL_BASS (lowpassed content added back with smoothed gain), PURE_BASS_PLUS (polyphase upsample -> same biquads -> mix resampled bass back), SUBWOOFER (dedicated subwoofer_ object, gain = bassFactor * 2.5, with an anti-pop ramp of 1.0/fs per sample). The only nonlinearity is a soft-saturation 'sat_mix' with knee 0.5: for drive > 0.5, shaped = 0.5 + (drive-0.5)/sqrt(1+(drive-0.5)^2), and both channels are scaled by shaped/drive. Bass-factor smoothing coefficient is 1.0 - exp(-1.0/(0.030*fs)) (a 30 ms one-pole).
  evidence: https://raw.githubusercontent.com/likelikeslike/ViPERDSP/main/viper/effects/ViPERBass.cpp
  -> Note that this is bass BOOST plus a limiter, not true harmonic virtual bass — it will not help a speaker that physically cannot move air at 40 Hz. PeaceQT should implement the real NLD/missing-fundamental path and keep a 'ViPER-compatible' mode that just does the 60 Hz Q=0.53 lowpass + sat_mix, so presets ported from ViPER sound the same. Reuse the sat_mix formula and the 30 ms gain smoother verbatim — both are sensible.
- [high] Exciter architecture (Aphex Aural Exciter) is a parallel sidechain, not an insert: split the signal; the main path passes through unmodified; the sidechain is high-passed with a tunable filter (the 'tune' knob, typically 700 Hz to 5 kHz, with the classic reference around 3 kHz), fed through an amplitude-dependent harmonic generator, then mixed back at a much lower level than the dry path. Later Aphex units replaced the fixed generator with a 'Transient Discriminate Harmonics Generator' that detects transients over a wide dynamic range and generates harmonics on them, giving level-independent, more natural enhancement.
  evidence: Aphex Exciter owner's manual http://cdn.aphex.com/assets/pdf/Aphex_Exciter_OM.pdf ('tunable high pass filter and a harmonics generator', 'frequency dependent phase shift and transient discriminate harmonics', sidechain mixed back 'much lower in level'); https://patents.google.com/patent/US4150253A/en (Aphex signal distortion circuit); 'Modeling the Harmonic Exciter' https://www.researchgate.net/publication/258333577
  -> Implement as: HP(fc) -> [optional envelope/transient gate] -> nonlinearity -> optional LP to tame the very top -> gain -> sum with dry. Expose fc (700 Hz - 8 kHz), amount (0-100%), and a mode selector for the nonlinearity. Keep the dry path bit-exact so 'amount = 0' is transparent.
- [high] The 'natural / 醇氧+ / X-HiFi' clarity variants in ViPER are not three different exciters — they are three different processors sharing one gain knob. ViPERClarity.cpp: NATURAL calls noise_sharpening_.Process() with gain_ (a sharpening/pre-emphasis filter, essentially y[n] = x[n] + g*(x[n]-x[n-1])-style HF emphasis); OZONE (醇氧+) applies a high-shelf biquad per channel with SetGain(gain_ + 1.0f) — default cutoff 12000 Hz at construction, reset to 8250 Hz; XHIFI calls hifi_.SetClarity(gain_ + 1.0f) on a separate hi-fi module. So conceptually: NATURAL = transient/edge sharpening, OZONE = pure linear high-shelf EQ, XHIFI = a distinct multi-filter chain.
  evidence: https://raw.githubusercontent.com/likelikeslike/ViPERDSP/main/viper/effects/ViPERClarity.cpp
  -> Only the NATURAL and XHIFI modes need new DSP — OZONE is a high-shelf that Equalizer APO can already do losslessly, so PeaceQT should keep emitting a config line for it rather than processing audio. That is a good argument for a hybrid architecture: PeaceQT processes only what APO cannot express, and keeps generating APO config for everything linear.
- [high] Tube/valve saturation: the standard recipe is (a) DC-bias a symmetric soft-clipper so it becomes asymmetric, which is what produces even-order harmonics (2nd, 4th) rather than only odd; (b) run it oversampled; (c) DC-block afterwards, because an asymmetric shaper always emits DC. A convenient shaper is y = tanh(k*x + b) - tanh(b): the subtraction removes the operating-point DC, the bias b sets the even/odd balance (b in 0.1..0.5 is a usable range), k is drive. tanh has a closed-form antiderivative log(cosh(.)), which makes it directly compatible with antiderivative antialiasing.
  evidence: Parker, Zavalishin & Le Bivic, 'Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution', DAFx-16 https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf; Bilbao, Esqueda, Parker & Välimäki, 'Antiderivative Antialiasing for Memoryless Nonlinearities', IEEE SPL 24(7), 2017; reference implementations https://github.com/jatinchowdhury18/ADAA and https://github.com/julian-parker/DAFX-AntiAliasing; practical notes https://jatinchowdhury18.medium.com/practical-considerations-for-antiderivative-anti-aliasing-d5847167f510
  -> Use ADAA order 1 combined with 2x oversampling — the DAFx-16 paper's explicit finding is that the technique 'markedly reduces aliasing distortion, especially in combination with low order oversampling', which is far cheaper than the 8x-16x oversampling naive waveshaping needs. ADAA order 1 costs half a sample of group delay, which is inaudible and does not need reporting as latency.
- [high] Compressor gain computer (log-domain feedforward), exact equations. With input level x_dB, threshold T, knee width W, ratio R, the static characteristic is: x_sc = x_dB when 2(x_dB - T) < -W; x_sc = x_dB + (1/R - 1)*(x_dB - T + W/2)^2 / (2W) when 2|x_dB - T| <= W; x_sc = T + (x_dB - T)/R when 2(x_dB - T) > W. The control signal is c_dB = x_dB - x_sc (the required attenuation, always >= 0).
  evidence: MathWorks 'Dynamic Range Control' https://www.mathworks.com/help/audio/ug/dynamic-range-control.html (equations reproduced verbatim); original source Giannoulis, Massberg & Reiss, 'Digital Dynamic Range Compressor Design — A Tutorial and Analysis', JAES 60(6):399-408, 2012, https://secure.aes.org/forum/pubs/journal/?ID=174
  -> Implement exactly this. Typical ranges to expose: T in [-60, 0] dB, R in [1, 20] plus an inf/limiter setting, W in [0, 24] dB (6 dB is a good default), attack [0.1, 100] ms, release [20, 2000] ms, makeup [-12, +24] dB.
- [high] Compressor level detector: put the smoothing AFTER the gain computer, in the log domain. g_s[n] = aA*g_s[n-1] + (1-aA)*g_c[n] when g_c[n] <= g_s[n-1] (attack), else aR*g_s[n-1] + (1-aR)*g_c[n] (release), with aA = exp(-ln(9)/(fs*T_A)) and aR = exp(-ln(9)/(fs*T_R)). The ln(9) makes T_A/T_R the standard 10%-to-90% rise time. Giannoulis et al. specifically recommend this placement because it 'generates a smooth envelope, no attack lag, and a variable knee width', and recommend feedforward over feedback because it is 'stable and predictable'.
  evidence: https://www.mathworks.com/help/audio/ug/dynamic-range-control.html (alpha formulas and branching smoother verbatim); Giannoulis/Massberg/Reiss JAES 2012 abstract and findings via https://secure.aes.org/forum/pubs/journal/?ID=174 and https://scispace.com/papers/digital-dynamic-range-compressor-design-a-tutorial-and-4kkirmhnyx
  -> Use the simple branching smoother in the log domain (code sample 1). If the user wants a more 'analogue' feel, upgrade to the smooth decoupled peak detector: a release-only one-pole feeding a full attack/release one-pole, which gives the two-stage release of classic optical compressors.
- [medium] Automatic makeup gain has a closed form: M = -x_sc(0), i.e. M = 0 if W/2 < T; M = -(1/R - 1)*(T - W/2)^2/(2W) if -W/2 <= T <= W/2; M = -T + T/R if -W/2 > T. Automatic attack/release/knee is program-dependent and comes from side-chain feature extraction: Giannoulis, Massberg & Reiss (JAES 61, Oct 2013) drive attack and release from the short-term crest factor (peak/RMS, with the RMS measured over roughly a 200 ms window) and use spectral flux as a transient cue.
  evidence: Auto-makeup formula verbatim from https://www.mathworks.com/help/audio/ug/dynamic-range-control.html; 'Parameter Automation in a Dynamic Range Compressor', Giannoulis, Massberg & Reiss, JAES 61, 2013, https://www.eecs.qmul.ac.uk/~josh/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf (indexed abstract; PDF itself failed TLS verification from this machine); also 'Automatic Control of the Dynamic Range Compressor' https://dafx17.eca.ed.ac.uk/papers/DAFx17_paper_44.pdf
  -> Ship auto-makeup immediately (it is exact and free). For auto attack/release, implement crest-factor tracking: maintain peak^2 and rms^2 with ~200 ms one-poles, C = peak/rms; high C (transient content) -> short attack, long release; low C (sustained) -> longer attack, shorter release. Fetch the 2013 PDF over a browser to get the exact scaling constants before shipping — see unknowns.
- [medium] ViPER ships two dynamics processors and the user probably wants both: FETCompressor.cpp (a 1176-style fast FET compressor) and MultibandCompressor.cpp, plus DynamicSystem.cpp which is a two-band 'Dynamic System' whose real math lives in an opaque dynamic_bass_ class (the .cpp only exposes SetFilterXPassFrequency/SetFilterYPassFrequency and SetSideGain(side_gain_low_, side_gain_high_)) — so it is a two-band split with independent low/high side gains, i.e. a dynamic loudness/headphone compensation, not a general compressor.
  evidence: https://github.com/likelikeslike/ViPERDSP/tree/main/viper/effects (file listing); https://raw.githubusercontent.com/likelikeslike/ViPERDSP/main/viper/effects/DynamicSystem.cpp
  -> Implement one good full-featured compressor (the Giannoulis design) and expose it both as a single-band and as an N-band version over a Linkwitz-Riley tree. Do not try to reverse-engineer DynamicSystem's dynamic_bass_ without reading its header — it is a headphone-compensation curve, not the compressor the user asked for.
- [high] Convolution reverb: the right structure is non-uniform partitioned overlap-add FFT convolution. Gardner's classic result is that you can get zero input-output latency by processing the head of the impulse response with direct (time-domain) convolution and the tail with progressively larger FFT blocks; the recommended even-CPU-load partition is that if the first FIR block is 2N, the following FFT block sizes are N, N, 2N, 2N, 4N, 4N, ... Uniform partitioning is much simpler but costs one block of latency.
  evidence: Gardner, 'Efficient Convolution without Input-Output Delay', JAES 43(3):127-136, 1995, https://secure.aes.org/forum/pubs/journal/?elib=7957; 'Implementing Real-Time Partitioned Convolution Algorithms on Conventional Operating Systems', DAFx-11, https://www.dafx.de/paper-archive/2011/Papers/90_e.pdf; 'Optimal Filter Partition for Efficient Convolution with Short Input/Output Delay' https://www.angelofarina.it/Public/AES-113/Garcia-PrePrint5660.pdf
  -> Equalizer APO already has a Convolution command, so PeaceQT gets convolution reverb for free by emitting a config line and shipping IRs — no DSP work at all. Only build an in-process convolver if PeaceQT needs to crossfade IRs or apply per-app IRs. If you do build it, use uniform partitioning first (simple, one block of latency) and only add the non-uniform head if latency becomes a complaint.
- [high] Convolution vs algorithmic, practically: convolution gives you an exact, unarguable room but the parameters the user screenshotted (room size, damping, density, decay, pre-delay) are not adjustable — changing them means swapping or synthesising a new IR, which is not a real-time knob. Algorithmic (Dattorro/FDN) gives continuous, zero-latency, low-CPU control of exactly those parameters. CPU: a 2-second stereo IR at 48 kHz is ~96k taps per channel and costs roughly 10-30x a Dattorro reverb even partitioned.
  evidence: Structural consequence of the Gardner/DAFx-11 partitioned-convolution cost model (https://www.dafx.de/paper-archive/2011/Papers/90_e.pdf) versus the Dattorro topology (https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf, ~22K words of delay memory at 30 kHz for the whole reverberator excluding predelay)
  -> Ship Dattorro as the interactive reverb and offer convolution via APO's existing Convolution command as a separate 'IR reverb' feature. Do not try to make one replace the other in the UI.
- [high] Oversampling requirement, by effect. NEEDS oversampling (or ADAA): tube saturation, exciter harmonic generator, virtual-bass NLD — every memoryless nonlinearity folds harmonics above Nyquist back into the audible band. 2x with ADAA-1, or 4x with a good polyphase halfband, is sufficient for musical drive levels. DOES NOT need oversampling: compressor (the gain signal is heavily smoothed and effectively band-limited by the attack/release one-poles; only a true zero-attack limiter needs it), all reverbs (linear, time-invariant delay networks), all EQ/shelving/crossovers.
  evidence: DAFx-16 antialiasing paper https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf (explicitly recommends the ADAA + low-order-oversampling combination); Giannoulis et al. topology, which places the nonlinearity only in the smoothed control path (https://www.mathworks.com/help/audio/ug/dynamic-range-control.html)
  -> Build one shared 2x/4x polyphase oversampler and wrap only the three nonlinear blocks in it. Do not oversample the whole chain — that would triple CPU for no benefit and add resampler latency to the reverbs.
- [high] Latency and real-time safety, by effect. Zero latency and fully RT-callback-safe (no allocation, bounded work per sample, no locks): compressor, tube saturation, NLD virtual bass, exciter, Freeverb, Dattorro, all IIR EQ. Latency-introducing: phase-vocoder virtual bass (one STFT hop, typically 512-2048 samples), uniform partitioned convolution (one partition block), linear-phase FIR EQ (N/2 samples), oversamplers (a few samples of halfband group delay). Non-RT-safe if done naively: IR loading, FFT plan creation, delay-line resizing on parameter change, and any std::vector growth — all must happen on a message thread with a lock-free handoff.
  evidence: Composite of the topologies cited above: Dattorro (pure delay network, https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf), Gardner partitioned convolution latency (https://secure.aes.org/forum/pubs/journal/?elib=7957), PV transient/latency tradeoff (http://research.spa.aalto.fi/publications/papers/dafx20-vbs/media/DAFx2020_vbs.pdf)
  -> Critical for PeaceQT specifically: if it becomes an APO, it runs inside the Windows audio engine's real-time thread. Pre-allocate every delay line at construction for the maximum supported room size and fs; never call new/malloc/free, never lock a mutex, never log, never touch the filesystem from Process(). Push all parameter changes through a single-producer single-consumer lock-free queue and smooth them per-sample (ViPER uses a 30 ms one-pole for exactly this, and an anti-pop ramp of 1.0/fs per sample when enabling an effect).
- [high] Denormals will destroy real-time performance in the reverbs specifically. Freeverb's original source contains an explicit 'undenormalise' step in the comb filter, and every feedback delay network decays exponentially toward denormal territory during silence, where x87/SSE denormal handling costs 100x a normal multiply.
  evidence: Freeverb comb filter structure documented at https://ccrma.stanford.edu/~jos/pasp/Freeverb.html and the tuning constants at https://raw.githubusercontent.com/sinshu/freeverb/master/Components/tuning.h; 'muted = 0' constant and the classic undenormalise macro in the Freeverb Components sources
  -> Set FTZ+DAZ once at the top of the audio callback via _mm_setcsr(_mm_getcsr() | 0x8040), and additionally add a tiny DC offset (1e-20 or so) into each feedback path as a belt-and-braces measure. This is a one-line fix that people forget and then blame the algorithm.

### code
```
// ============================================================================
// 1. LOG-DOMAIN FEED-FORWARD COMPRESSOR
// Topology: |x| -> dB -> gain computer -> level detector (log domain, AFTER the
// gain computer) -> makeup -> linear -> multiply.  Equations verbatim from
// Giannoulis/Massberg/Reiss JAES 60(6) 2012, as reproduced in
// https://www.mathworks.com/help/audio/ug/dynamic-range-control.html
// Real-time safe: no allocation, no branching on data size, no denormal risk.
// ============================================================================
#include <cmath>
#include <algorithm>

class Compressor {
public:
    // ---- user parameters -------------------------------------------------
    float thresholdDb = -18.0f;  // T  : typical range [-60 .. 0]
    float ratio       =   4.0f;  // R  : [1 .. 20], use 1e6 for a limiter
    float kneeDb      =   6.0f;  // W  : [0 .. 24], 0 = hard knee
    float attackMs    =  10.0f;  //      [0.1 .. 100]
    float releaseMs   = 120.0f;  //      [20 .. 2000]
    float makeupDb    =   0.0f;  //      [-12 .. +24], ignored if autoMakeup
    bool  autoMakeup  = true;

    void prepare(double sampleRate) { fs = (float) sampleRate; updateCoeffs(); reset(); }
    void reset() { yL = 0.0f; peakSq = rmsSq = 1e-12f; }

    void updateCoeffs() {
        // ln(9) makes attackMs/releaseMs the standard 10%->90% rise time.
        constexpr float LN9 = 2.1972245773362196f;
        aA = std::exp(-LN9 / (fs * attackMs  * 1e-3f));
        aR = std::exp(-LN9 / (fs * releaseMs * 1e-3f));
        // crest-factor trackers, ~200 ms window (Giannoulis et al. JAES 61, 2013)
        aCrest = std::exp(-1.0f / (fs * 0.200f));
        makeupAuto = -staticCurve(0.0f);   // M = -x_sc(0)
    }

    // ---- static characteristic (the "gain computer") ----------------------
    // Returns the TARGET OUTPUT LEVEL in dB for an input level xDb.
    float staticCurve(float xDb) const {
        const float T = thresholdDb, W = kneeDb, R = ratio;
        const float d = xDb - T;
        if (2.0f * d < -W)                 return xDb;                    // below knee
        if (2.0f * std::fabs(d) <= W) {                                   // inside knee
            const float t = d + 0.5f * W;
            return xDb + (1.0f / R - 1.0f) * (t * t) / (2.0f * W);
        }
        return T + d / R;                                                 // above knee
    }

    // ---- per-sample -------------------------------------------------------
    // sidechain: usually std::max(|L|,|R|) for linked stereo.
    // Returns the LINEAR gain to multiply every channel by.
    float computeGain(float sidechain) {
        const float xDb = 20.0f * std::log10(std::max(std::fabs(sidechain), 1e-9f));
        const float cDb = xDb - staticCurve(xDb);    // required attenuation, >= 0

        // Branching peak detector in the LOG domain, placed after the gain
        // computer: smooth envelope, no attack lag, variable knee width.
        if (cDb > yL) yL = aA * yL + (1.0f - aA) * cDb;   // attack (more reduction)
        else          yL = aR * yL + (1.0f - aR) * cDb;   // release

        const float m = autoMakeup ? makeupAuto : makeupDb;
        return std::pow(10.0f, (m - yL) * 0.05f);         // 10^(dB/20)
    }

    float gainReductionDb() const { return yL; }         // for a GR meter

    // ---- optional: program-dependent ("auto") attack / release ------------
    // Crest factor C = peak/rms drives the time constants: transient-rich
    // material (high C) wants a fast attack and slow release; sustained
    // material (low C) wants the opposite.  Call once per sample BEFORE
    // computeGain().  Constants below are a sane starting point, NOT the
    // published ones -- see "unknowns".
    void updateAutoTimes(float sidechain) {
        const float x2 = sidechain * sidechain;
        rmsSq  = aCrest * rmsSq  + (1.0f - aCrest) * x2;
        peakSq = (x2 > peakSq) ? x2 : aCrest * peakSq + (1.0f - aCrest) * x2;
        const float C = std::sqrt(std::max(peakSq, 1e-12f) / std::max(rmsSq, 1e-12f));
        const float C2 = std::max(C * C, 1.0f);
        attackMs  = std::clamp(2.0f * kAttackMaxMs  / C2, 0.1f,  100.0f);
        releaseMs = std::clamp(2.0f * kReleaseMaxMs / C2 - attackMs, 20.0f, 2000.0f);
        updateCoeffs();
    }

private:
    float fs = 48000.0f;
    float aA = 0.0f, aR = 0.0f, aCrest = 0.0f;
    float yL = 0.0f;                 // smoothed attenuation in dB
    float makeupAuto = 0.0f;
    float peakSq = 1e-12f, rmsSq = 1e-12f;
    static constexpr float kAttackMaxMs  = 80.0f;
    static constexpr float kReleaseMaxMs = 1000.0f;
};

```

```
// ============================================================================
// 2. FREEVERB (Schroeder-Moorer): 8 lowpass-feedback combs + 4 allpasses.
// Tuning constants verbatim from Components/tuning.h
// (https://raw.githubusercontent.com/sinshu/freeverb/master/Components/tuning.h)
// Structure per https://ccrma.stanford.edu/~jos/pasp/Freeverb.html
// Zero latency, RT-safe once prepare() has run. Delay lengths are TUNED FOR
// 44100 Hz -- scale by fs/44100 for other rates.
// ============================================================================
#include <vector>
#include <cmath>

namespace fv {
// --- tuning.h ---------------------------------------------------------------
constexpr int   numcombs = 8, numallpasses = 4, stereospread = 23;
constexpr int   combL[numcombs]  = {1116,1188,1277,1356,1422,1491,1557,1617};
constexpr int   apL  [numallpasses] = {556,441,341,225};
constexpr float fixedgain = 0.015f, scalewet = 3.0f, scaledry = 2.0f;
constexpr float scaledamp = 0.4f, scaleroom = 0.28f, offsetroom = 0.7f;
constexpr float initialroom = 0.5f, initialdamp = 0.5f;
constexpr float initialwet = 1.0f/3.0f, initialdry = 0.0f, initialwidth = 1.0f;

inline float undenorm(float v) { return (std::fabs(v) < 1e-18f) ? 0.0f : v; }

// Lowpass-feedback comb filter (Moorer): a comb whose feedback path contains
// a one-pole lowpass, so highs decay faster than lows -> "damping".
struct Comb {
    std::vector<float> buf; int idx = 0;
    float store = 0.0f, feedback = 0.5f, damp1 = 0.5f, damp2 = 0.5f;
    void setSize(int n) { buf.assign((size_t) n, 0.0f); idx = 0; }
    void setDamp(float d) { damp1 = d; damp2 = 1.0f - d; }
    void mute() { std::fill(buf.begin(), buf.end(), 0.0f); store = 0.0f; }
    inline float process(float in) {
        const float out = buf[(size_t) idx];
        store = undenorm(out * damp2 + store * damp1);
        buf[(size_t) idx] = undenorm(in + store * feedback);
        if (++idx >= (int) buf.size()) idx = 0;
        return out;
    }
};

// Freeverb's "allpass" (not a true allpass -- Schroeder's is y = -g*x + d + g*d;
// Freeverb uses this simplified form, which is what gives it its character).
struct Allpass {
    std::vector<float> buf; int idx = 0; float feedback = 0.5f;
    void setSize(int n) { buf.assign((size_t) n, 0.0f); idx = 0; }
    void mute() { std::fill(buf.begin(), buf.end(), 0.0f); }
    inline float process(float in) {
        const float bufout = buf[(size_t) idx];
        const float out = -in + bufout;
        buf[(size_t) idx] = undenorm(in + bufout * feedback);
        if (++idx >= (int) buf.size()) idx = 0;
        return out;
    }
};

class Reverb {
public:
    void prepare(double sampleRate) {
        const double k = sampleRate / 44100.0;          // rescale the tuning
        for (int i = 0; i < numcombs; ++i) {
            combsL[i].setSize((int) std::lround(combL[i] * k));
            combsR[i].setSize((int) std::lround((combL[i] + stereospread) * k));
        }
        for (int i = 0; i < numallpasses; ++i) {
            apsL[i].setSize((int) std::lround(apL[i] * k)); apsL[i].feedback = 0.5f;
            apsR[i].setSize((int) std::lround((apL[i] + stereospread) * k)); apsR[i].feedback = 0.5f;
        }
        setRoomSize(initialroom); setDamp(initialdamp);
        setWet(initialwet); setDry(initialdry); setWidth(initialwidth);
        mute();
    }
    void mute() { for (auto& c : combsL) c.mute(); for (auto& c : combsR) c.mute();
                  for (auto& a : apsL) a.mute();  for (auto& a : apsR) a.mute(); }

    // --- parameter mapping (this is the whole "how do knobs map" answer) ---
    void setRoomSize(float v) {                 // 0..1 -> feedback 0.70..0.98
        roomsize = v * scaleroom + offsetroom;
        for (auto& c : combsL) c.feedback = roomsize;
        for (auto& c : combsR) c.feedback = roomsize;
    }
    void setDamp(float v) {                     // 0..1 -> lowpass coeff 0..0.4
        const float d = v * scaledamp;
        for (auto& c : combsL) c.setDamp(d);
        for (auto& c : combsR) c.setDamp(d);
    }
    void setWet(float v)   { wet = v * scalewet; updateWet(); }
    void setDry(float v)   { dry = v * scaledry; }
    void setWidth(float v) { width = v; updateWet(); }

    void processReplace(float* L, float* R, int numFrames, int stride = 1) {
        for (int n = 0; n < numFrames; ++n) {
            const float inL = L[n * stride], inR = R[n * stride];
            const float input = (inL + inR) * fixedgain;
            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < numcombs; ++i) {           // parallel combs
                outL += combsL[i].process(input);
                outR += combsR[i].process(input);
            }
            for (int i = 0; i < numallpasses; ++i) {       // series allpasses
                outL = apsL[i].process(outL);
                outR = apsR[i].process(outR);
            }
            L[n * stride] = outL * wet1 + outR * wet2 + inL * dry;
            R[n * stride] = outR * wet1 + outL * wet2 + inR * dry;
        }
    }
private:
    void updateWet() { wet1 = wet * (width * 0.5f + 0.5f);
                       wet2 = wet * ((1.0f - width) * 0.5f); }
    Comb combsL[numcombs], combsR[numcombs];
    Allpass apsL[numallpasses], apsR[numallpasses];
    float roomsize = 0.5f, wet = 1.0f, wet1 = 1.0f, wet2 = 0.0f, dry = 0.0f, width = 1.0f;
};
} // namespace fv

```

```
// ============================================================================
// 3. DATTORRO PLATE REVERB -- constants table.
// Delay lengths in SAMPLES AT 29761 Hz, from Fig.1 / Table 1 / Table 2 of
// https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf
// (cross-verified against https://github.com/khoin/DattorroReverbNode).
// This is the topology whose knobs match the ViPER screenshot 1:1.
// ============================================================================
namespace dattorro {
constexpr double kTunedFs = 29761.0;
inline int scale(int n, double fs) { return (int) std::lround(n * fs / kTunedFs); }

// --- input chain: predelay -> bandwidth LP -> 4 diffusing allpasses ---------
constexpr int kInputDiffusion[4] = { 142, 107, 379, 277 };
constexpr float kInputDiffusion1 = 0.750f;   // allpass coeff for lengths 142,107
constexpr float kInputDiffusion2 = 0.625f;   // allpass coeff for lengths 379,277

// --- tank: figure-of-eight, two branches cross-feeding each other ----------
// branch A: ap(672 + modulation) -> delay(4453) -> damping LP -> *decay
//           -> ap(1800) -> delay(3720) -> *decay -> into branch B
// branch B: ap(908 + modulation) -> delay(4217) -> damping LP -> *decay
//           -> ap(2656) -> delay(3163) -> *decay -> into branch A
constexpr int kTankA[4] = {  672, 4453, 1800, 3720 };
constexpr int kTankB[4] = {  908, 4217, 2656, 3163 };
constexpr int kExcursion = 16;               // peak samples of delay modulation

// --- defaults, Table 1 ------------------------------------------------------
constexpr float kDecay          = 0.50f;     // rate of decay
constexpr float kDecayDiff1     = 0.70f;     // density of tail (ap 672 / 908)
// decay diffusion 2 = decay + 0.15, floored at 0.25, ceilinged at 0.50
constexpr float kDecayDiff2     = 0.50f;     // (ap 1800 / 2656)
constexpr float kBandwidth      = 0.9995f;   // input LP; 0.9999999 = full band
constexpr float kDamping        = 0.0005f;   // tank LP; range 0.0 .. 0.9999999

// --- output taps, Table 2 (verbatim, all-wet) ------------------------------
// yL = +0.6*n48_54[ 266] +0.6*n48_54[2974] -0.6*n55_59[1913] +0.6*n59_63[1996]
//      -0.6*n24_30[1990] -0.6*n31_33[ 187] -0.6*n33_39[1066]
// yR = +0.6*n24_30[ 353] +0.6*n24_30[3627] -0.6*n31_33[1228] +0.6*n33_39[2673]
//      -0.6*n48_54[2111] -0.6*n55_59[ 335] -0.6*n59_63[ 121]
// (node names are Dattorro's numbered taps into the tank delay lines; keep the
//  alternating signs -- they are what decorrelates L from R.)

// --- knob mapping for the user's UI ---------------------------------------
// room size   -> global multiplier on kTankA/kTankB lengths (0.5x .. 2x)
// damping     -> kDamping (tank one-pole; HIGH coeff = LOW cutoff = dark tail)
// bandwidth   -> kBandwidth (input one-pole; coeff tracks cutoff directly)
// density     -> kInputDiffusion1/2 (0 = none, ~0.5..0.75 optimal, ->1 buzzes)
// decay       -> kDecay (0.0 .. ~0.9999; 1.0 with damping off = infinite hold)
// pre-delay   -> length of the z^-N before the bandwidth filter (0 .. 200 ms)
// early mix   -> weight on the taps into node24_30/node31_33 vs the later taps
// wet mix     -> final dry/wet crossfade

// NOTE: the three one-pole lowpasses (input bandwidth + two tank damping)
// MUST be implemented DIRECT FORM I so internal nodes do not clip prematurely.
// One-pole: y[n] = (1-c)*x[n] + c*y[n-1], with c the coefficient above.
// Modulation: sine LFO ~1 Hz, peak excursion ~8-16 samples, updated EVERY
// SAMPLE (not per block) or you inject aliasing into the audio path.
} // namespace dattorro

```

```
// ============================================================================
// 4. TUBE / VALVE SATURATION
// Asymmetric soft clip (bias into tanh -> even-order harmonics) with
// antiderivative antialiasing order 1 (Parker/Zavalishin/Le Bivic DAFx-16,
// https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf)
// plus a DC blocker, because ANY asymmetric shaper emits DC.
// Run inside 2x oversampling for best results; ADAA-1 alone already removes
// most of the aliasing that would otherwise need 8x-16x.
// ============================================================================
#include <cmath>

class TubeStage {
public:
    float drive = 2.0f;      // k : [1 .. 20]. 1 = nearly clean.
    float bias  = 0.25f;     // b : [0 .. 0.6]. 0 = odd harmonics only (symmetric),
                             //     higher = more 2nd/4th order "tube" warmth.

    void prepare(double fs) {
        // DC blocker pole: fc ~= fs*(1-R)/(2*pi). R=0.9995 @48k -> ~3.8 Hz.
        dcR = 1.0f - 2.0f * 3.14159265f * 5.0f / (float) fs;
        x1 = 0.0f; dcX1 = 0.0f; dcY1 = 0.0f; F1 = 0.0f;
    }

    // f(x)  = tanh(k*x + b) - tanh(b)     <- the -tanh(b) removes the operating
    //                                        point offset; DC blocker mops up
    //                                        the signal-dependent remainder.
    // F(x)  = log(cosh(k*x + b))/k - tanh(b)*x   (antiderivative of f)
    static inline float f(float x, float k, float b) {
        return std::tanh(k * x + b) - std::tanh(b);
    }
    static inline float F(float x, float k, float b) {
        const float u = k * x + b;
        // log(cosh(u)) computed stably for large |u|
        const float au = std::fabs(u);
        const float lc = au + std::log1p(std::exp(-2.0f * au)) - 0.6931472f;
        return lc / k - std::tanh(b) * x;
    }

    inline float processSample(float x) {
        // --- ADAA order 1: y = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]) -------
        const float Fn = F(x, drive, bias);
        const float dx = x - x1;
        float y;
        if (std::fabs(dx) < 1e-5f)  y = f(0.5f * (x + x1), drive, bias); // fallback
        else                        y = (Fn - F1) / dx;
        x1 = x; F1 = Fn;

        // --- DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1] ---------------------
        const float out = y - dcX1 + dcR * dcY1;
        dcX1 = y; dcY1 = out;
        return out;
    }
private:
    float x1 = 0.0f, F1 = 0.0f;
    float dcR = 0.9995f, dcX1 = 0.0f, dcY1 = 0.0f;
};

// NOTE on ADAA-1: it introduces exactly 0.5 sample of group delay. That is
// inaudible and does not need to be reported as plugin latency, but it DOES
// mean a dry/wet blend is very slightly phase-shifted -- delay the dry path by
// half a sample (a 2-tap linear interpolator) if you care.

```

```
// ============================================================================
// 5. PSYCHOACOUSTIC / VIRTUAL BASS (NLD path)
// Missing-fundamental synthesis per Waves MaxxBass US5930373A:
// split at the speaker cutoff fc, synthesise 2nd..5th harmonics of the content
// BELOW fc, band-limit them to [fc, ~5*fc], loudness-match, sum with the band
// ABOVE fc.  Zero latency, RT-safe.  The NLD needs 2x oversampling (or feed
// it a signal already band-limited to fc, which makes aliasing negligible
// since 5*fc << Nyquist for any sane fc).
// ============================================================================
#include <cmath>
#include <algorithm>

struct OnePoleHP { // used only for the harmonic-branch cleanup
    float a = 0.0f, y = 0.0f, x1 = 0.0f;
    void set(float fc, float fs) { a = std::exp(-2.0f * 3.14159265f * fc / fs); }
    inline float process(float x) { y = a * (y + x - x1); x1 = x; return y; }
};

class VirtualBass {
public:
    // "Speaker size" knob -> fc.  This is the entire meaning of that parameter:
    // below fc the speaker cannot move air, so fundamentals there are replaced
    // by harmonics the speaker CAN reproduce, and the ear reconstructs the
    // missing fundamental (good for ~1.5 octaves below fc per the Waves AES paper).
    enum class Speaker { Headphones, Bookshelf, Laptop, Phone };
    static float cutoffFor(Speaker s) {
        switch (s) {
            case Speaker::Headphones: return  50.0f;
            case Speaker::Bookshelf:  return 100.0f;
            case Speaker::Laptop:     return 200.0f;
            case Speaker::Phone:      return 320.0f;
        }
        return 120.0f;
    }

    float amount   = 0.7f;   // 0..1, how much harmonic signal to inject
    float harmonicGain = 1.34f; // MaxxBass "Residue Expansion Ratio":
                                // ~1.34 when the 2nd harmonic dominates,
                                // ~1.74 when the 3rd does (fundamentals 40-60 Hz)

    void prepare(float sampleRate, float fc) {
        fs = sampleRate; cutoff = fc;
        setLR4(lpA, lpB, fc, fs, /*highpass=*/false);
        setLR4(hpA, hpB, fc, fs, /*highpass=*/true);
        bpHi.set(fc, fs);          // remove residual sub content from the branch
        envA = std::exp(-1.0f / (fs * 0.030f)); // 30 ms envelope, as ViPER uses
    }

    inline float processSample(float x) {
        const float low  = lpB.process(lpA.process(x));   // Linkwitz-Riley 4th
        const float high = hpB.process(hpA.process(x));

        // --- harmonic generator (NLD) ---------------------------------------
        // Full-wave rectifier -> strong 2nd + 4th; add a cubic term for 3rd+5th.
        // Both are memoryless, so the harmonic set is level-dependent, which is
        // exactly the "amplitude dependent harmonics" behaviour of the Aphex
        // patent and of real tube/transformer bass.
        const float rect = std::fabs(low) - 0.6366f * peakEnv; // remove DC (2/pi)
        const float cube = low * low * low;
        float h = 0.7f * rect + 0.3f * cube;

        // --- loudness match: track the low-band envelope and scale ----------
        peakEnv = envA * peakEnv + (1.0f - envA) * std::fabs(low);
        hEnv    = envA * hEnv    + (1.0f - envA) * std::fabs(h);
        h *= (hEnv > 1e-6f) ? (peakEnv / hEnv) * harmonicGain : 0.0f;

        // --- band-limit the harmonics to what the speaker can reproduce -----
        h = bpHi.process(h);                 // kill anything left below fc

        // MaxxBass optionally DELETES the fundamental here (low is dropped).
        // Keeping a little of it sounds better on headphones:
        const float keepFundamental = 0.0f;  // 0.0 = pure virtual bass, 1.0 = boost
        return high + amount * h + keepFundamental * low;
    }
private:
    struct Biquad {
        float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        inline float process(float x) {           // transposed direct form II
            const float y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y; return y;
        }
    };
    // Cascade two identical Butterworth Q=0.7071 sections -> Linkwitz-Riley 4th,
    // which sums flat in magnitude (with a polarity flip on one band for HP+LP).
    static void setLR4(Biquad& s1, Biquad& s2, float fc, float fs, bool hp);

    Biquad lpA, lpB, hpA, hpB;
    OnePoleHP bpHi;
    float fs = 48000.0f, cutoff = 120.0f;
    float envA = 0.0f, peakEnv = 0.0f, hEnv = 0.0f;
};

```

UNKNOWNS: Exact auto attack/release formulas from Giannoulis, Massberg & Reiss, 'Parameter Automation in a Dynamic Range Compressor', JAES 61 (Oct 2013). The PDF at https://www.eecs.qmul.ac.uk/~josh/documents/2013/... failed TLS certificate verification from this machine on two attempts, so I could only confirm from secondary indexing that the method uses short-term crest factor plus spectral flux. The crest-factor scaling in code sample 1 is a plausible reconstruction, NOT the published constants. Fetch that PDF in a browser before shipping auto-attack/release.; Giannoulis/Massberg/Reiss 2012 tutorial PDF likewise failed to fetch (same TLS error). The gain-computer and alpha equations in code sample 1 are verbatim from the MathWorks Dynamic Range Control documentation, which reproduces the same equations, but I did not read the JAES paper's own decoupled / smooth-decoupled peak detector equations. If PeaceQT wants the 'smooth decoupled' detector (two cascaded one-poles, giving the two-stage optical release), get the paper.; ViPERClarity's NATURAL mode calls noise_sharpening_.Process() and XHIFI calls hifi_.Process() -- I did not read NoiseSharpening.cpp or the hifi module, so the exact sharpening kernel and the X-HiFi filter chain are unverified. The conceptual split (sharpening vs high-shelf vs multi-filter) is confirmed from ViPERClarity.cpp itself.; ViPER's DynamicSystem crossover frequencies and dynamics math live in an opaque dynamic_bass_ class not visible in DynamicSystem.cpp. Same for FETCompressor.cpp and MultibandCompressor.cpp, which I did not open.; ViPER's SpectrumExtend.cpp and PsychoacousticBass.cpp (separate from ViPERBass.cpp) were not read -- PsychoacousticBass is likely the true missing-fundamental implementation while ViPERBass is the simpler boost, but I did not confirm this.; The exact license of likelikeslike/ViPERDSP was not checked. Since it is a reverse-engineered decompilation of a closed-source binary (libv4a_fx.so), PeaceQT should treat it as a reference for understanding rather than copy code from it without a legal review.; Whether ViPER's Reverberation model_ is literally the original Jezar Freeverb code or a reimplementation with different tuning constants -- the API is identical (SetRoomSize/SetWidth/SetDamp/SetWet/SetDry, ProcessReplace with interleaved pointers) but I did not read the model class, so I could not confirm the delay lengths match tuning.h.; The Aphex 'Transient Discriminate Harmonics Generator' circuit (US5424488A) was referenced in search results but I did not fetch the patent, so the exact transient-detection and harmonic-generation math is unverified beyond the manual's prose description.; Latency figures for a specific partitioned-convolution implementation: Gardner's zero-latency scheme and the N,N,2N,2N,4N,4N partition are confirmed from the JAES abstract and the DAFx-11 paper's indexing, but I did not read the full Gardner 1995 paper (AES paywall), so the exact crossover point between direct and FFT convolution is from secondary description.; Whether Equalizer APO's Convolution command can be driven fast enough for PeaceQT to hot-swap IRs, and what latency APO's own convolver adds -- not investigated; this determines whether PeaceQT needs its own convolver at all.

---

## Live parameter control: how a PeaceQT GUI pushes a DSP parameter into a running audio graph and hears it immediately, per candidate architecture (APO-hosted VST via config.txt, PeaceQT's own APO in audiodg.exe, and out-of-process alternatives)
- [high] Equalizer APO's config reload is NOT a stop/restart of the audio chain — it builds the new filter chain on a background thread and the RT thread crossfades from old to new over exactly sampleRate/100 samples (10 ms). There is no dropout, no silence gap, and the RT thread never takes a lock.
  evidence: https://raw.githubusercontent.com/mirror/equalizerapo/master/FilterEngine.cpp — line 150 `this->transitionLength = (unsigned)(sampleRate / 100);`; lines 391-407 in `FilterEngine::process()`: `if (nextConfig != NULL) { nextConfig->read(...); nextConfig->process(...); transitionCounter = currentConfig->doTransition(nextConfig, frameCount, transitionCounter, transitionLength); } ... if (nextConfig != NULL && transitionCounter >= transitionLength) { previousConfig = currentConfig; currentConfig = nextConfig; nextConfig = NULL; transitionCounter = 0; ReleaseSemaphore(loadSemaphore, 1, NULL); }`. Local copy: C:\Users\10678\AppData\Local\Temp\claude\I--Qt\fa517dc1-8998-4033-b663-a4285117eee5\scratchpad\FilterEngine.cpp
  -> The 'rewriting config.txt drops audio' assumption is wrong — the mechanism is a clean 10 ms equal-length crossfade. If PeaceQT stays on the config.txt channel, it will NOT click or gap. Copy this exact pattern (build off-thread, crossfade on-thread) in PeaceQT's own APO too; it is the single most important design element in Equalizer APO.
- [high] BUT the crossfade runs BOTH the old and the new chain in full for the 10 ms transition — instantaneous 2x DSP CPU. With a reverb + convolution + compressor chain that is a real glitch risk near the deadline, and it happens on every single parameter change.
  evidence: FilterEngine.cpp lines 388-396: `currentConfig->read(input, frameCount); currentConfig->process(frameCount);` then unconditionally `nextConfig->read(input, frameCount); nextConfig->process(frameCount);` inside the same APOProcess call.
  -> Do not use whole-chain crossfade for knob turns in PeaceQT's own DSP. Use it only for structural changes (adding/removing a filter). For scalar parameter changes use per-parameter smoothing inside the existing filter instances — no second chain, no 2x CPU.
- [high] Every config reload destroys and rebuilds every filter object, so all internal DSP state is lost: reverb tails, compressor envelopes, convolution overlap-add history, and VST plugin instances. This is the fatal flaw of the config.txt channel for the effects PeaceQT wants.
  evidence: FilterEngine.cpp lines 214-219 (`previousConfig->~FilterConfiguration(); MemoryHelper::free(previousConfig);`), 229-251: factories are re-run (`startOfConfiguration()`, `loadConfigFile()`, `endOfConfiguration()`) and a brand-new `FilterConfiguration` is placement-newed for every reload. Filter set in the installed binary confirmed via strings on D:\Program Files\EqualizerAPO\EqualizerAPO.dll: `Convolution`, `GraphicEQ`, `LoudnessCorrection`, `pVSTPlugin`.
  -> Rule out 'PeaceQT GUI -> config.txt -> VSTPlugin hosted by APO' as the live-control path for reverb/compressor/saturation. Turning a reverb decay knob would re-instantiate the VST and cut the tail every time. The config.txt channel is acceptable only for stateless linear filters (EQ, gain, delay) — i.e. exactly what Peace does today.
- [high] The whole live-update pipeline is: file write -> directory-change notification (measured 0.5-1.1 ms on this machine) -> a deliberate 10 ms debounce -> full config parse -> up to one buffer period -> 10 ms crossfade. Realistic end-to-end knob latency is ~25-40 ms plus parse time, which is fine for a slider but not for anything gesture-like.
  evidence: Measured on this machine via .NET FileSystemWatcher (same ReadDirectoryChanges kernel path): write->notification delivered = 7.751, 0.662, 0.739, 0.776, 1.12, 0.752, 0.488, 0.625 ms over 8 runs. Debounce is FilterEngine.cpp line 594-595: `// Wait for second event within 10 milliseconds to avoid loading twice` / `WaitForMultipleObjects(1, &notificationHandle, false, 10);`. Parse time is logged by the APO as `Finished loading configuration after %lf milliseconds` (string present in the installed EqualizerAPO.dll).
  -> If PeaceQT keeps the file channel, throttle GUI writes to ~20-30 Hz max; writing faster just queues reloads behind the `loadSemaphore` (FilterEngine.cpp line 598-599 blocks the loader thread until the previous crossfade completes). To measure real parse cost on this machine set HKLM\SOFTWARE\EqualizerAPO\EnableTrace=true (currently `false`) and read C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log.
- [high] Equalizer APO already ships a working second IPC channel between audiodg.exe and a normal user-mode GUI process: a named pipe. DeviceSelector.exe creates the pipe server; the APO inside audiodg.exe opens it as a client. This is direct proof that the audiodg security boundary is crossable by a named pipe today.
  evidence: strings on D:\Program Files\EqualizerAPO\DeviceSelector.exe: imports `CreateNamedPipeW`, `ConnectNamedPipe`, `DisconnectNamedPipe`, `ReadFile`, `WriteFile`; UTF-16 strings `EqualizerAPODeviceTest`, `\\.\pipe\`, `DeviceTestPipeName`, `Could not create named pipe: `. strings on D:\Program Files\EqualizerAPO\EqualizerAPO.dll: `"\\.\pipe\`, `DeviceTestPipeName`, `Could not connect to named pipe: %s`, `Could not write to pipe: %s`. The pipe name is passed through registry value `DeviceTestPipeName` under HKLM\SOFTWARE\EqualizerAPO (key confirmed present locally with InstallPath/ConfigPath/EnableTrace).
  -> This is the pattern PeaceQT should steal for its own APO: GUI = pipe SERVER (it owns the DACL), APO in audiodg = pipe CLIENT. Named pipes are the right primitive because they are NOT session-namespaced — see the SeCreateGlobalPrivilege finding below.
- [high] A non-elevated PeaceQT GUI CANNOT create a Global\ file-mapping (or named event/mutex/semaphore) for audiodg to open — that is a privileged operation requiring SeCreateGlobalPrivilege, which this user does not have. Opening an existing global object is NOT privileged.
  evidence: https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces : "The creation of a file-mapping object or symbolic link object in the global namespace... from a session other than session zero is a privileged operation. Because of this, an application must have SeCreateGlobalPrivilege enabled in order to create a file-mapping object... The privilege check is limited to the creation of these objects, and does not apply to opening existing ones." Also https://learn.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory : "Note: The code in this example will require administrative privileges at runtime." Verified locally: `whoami /priv` on the user's token lists only SeShutdown/SeChangeNotify/SeUndock/SeIncreaseWorkingSet/SeTimeZone — no SeCreateGlobalPrivilege; IsInRole(Administrator) = False.
  -> Invert the ownership: have PeaceQT's APO (which runs in session 0, where the global namespace is the default) CREATE the section and any event, and have the GUI OPEN it by `Global\PeaceQT_<endpointGuid>`. That needs zero elevation on the GUI side. Alternative: use only a named pipe (`\\.\pipe\...` is not session-namespaced and needs no privilege) and let the APO pull the parameter block through it. Do NOT design around 'GUI creates Global\ section' unless you are willing to ship an elevated helper service.
- [high] audiodg.exe runs in Session 0 under a filtered/restricted LOCAL SERVICE token and has no access to C:\Users\<username>. Any object PeaceQT shares with it must have a DACL granting NT SERVICE\Audiosrv (the service SID) or LOCAL SERVICE, and must not live under the user profile.
  evidence: Verified locally: audiodg.exe pid 5068, SessionId 0; Win32_Service Audiosrv StartName = NT AUTHORITY\LocalService. https://github.com/dechamps/APO (Etienne Dechamps, FlexASIO author): "the service does *not* have access to your user directory (i.e. C:\Users\<username>)" and "ensure the `NT SERVICE\Audiosrv` user principal has access. (This principal is the service SID. You can also use the local service account, but only allowing the Windows audio service is cleaner.)" Secondary source https://medium.com/@S.1.l.k.y/abusing-windows-audio-for-local-privilege-escalation-1d59440116cb describes audiodg as inheriting "a filtered LOCAL SERVICE token" with "write-restricted SIDs".
  -> Put PeaceQT's shared state under %ProgramData%\PeaceQT or Program Files, never %LOCALAPPDATA%. Build an explicit SECURITY_ATTRIBUTES for the pipe/section granting NT SERVICE\Audiosrv. Because the token is reportedly write-restricted, design the section so audiodg only needs READ access (GUI writes params, APO reads) — write-restriction does not affect read. If you want the APO to write back (meters), budget extra time to also grant NT AUTHORITY\WRITE RESTRICTED (S-1-5-33).
- [high] audiodg.exe is a protected process by default, but this specific machine has protection turned OFF via HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\DisableProtectedAudioDG = 1. Microsoft explicitly warns that an APO can work with that key set and then fail to load once it is removed.
  evidence: Verified locally: `DisableProtectedAudioDG : 1` under HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio. Direct NtQueryInformationProcess(ProcessProtectionInformation, class 61) on pid 5068 returned status 0x00000000, ProtectionByte 0x00 (unprotected) — while OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) on svchost and lsass failed with error 5. Microsoft: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects — "If you have an embedded manifest, this triggers the use of certain APIs which are forbidden within a protected environment. This means that your APO will run with DisableProtectedAudioDG=1, but when this test key is removed, your APO will fail to load, even if it is WHQL-signed." Larry Osterman (https://learn.microsoft.com/en-us/archive/blogs/larryosterman/what-is-audiodg-exe): "The DRM system in Vista requires that the audio samples be processed in a protected process."
  -> CRITICAL TEST TRAP: any shared-memory/pipe scheme PeaceQT builds will appear to work on this machine and may fail on a stock Windows 11 box. Before committing to a custom APO, temporarily set DisableProtectedAudioDG=0, restart audiosrv, and re-verify the IPC path. Also disable the embedded manifest in the APO DLL project (Manifest Tool > Embed Manifest = No), per the same MS doc.
- [high] Microsoft's actual documented, supported channel for a GUI to push live parameters into an APO exists and is not shared memory: the Windows 11 CAPX Settings + Notifications frameworks. The GUI writes an IPropertyStore obtained via IAudioSystemEffectsPropertyStore; the APO gets a HandleNotification callback with APO_NOTIFICATION_TYPE_AUDIO_SYSTEM_EFFECTS_PROPERTY_CHANGE. The doc states plain non-admin desktop apps may write it.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-11-apis-for-audio-processing-objects — "The IAudioSystemEffectsPropertyStore is readable and writable by an ISV/IHV service, a UWP store application, non-admin desktop applications, and APOs. Additionally, this can act as the mechanism for APOs to deliver messages back to a service or UWP store application." GUI side: `device->Activate(__uuidof(IAudioSystemEffectsPropertyStore), CLSCTX_INPROC_SERVER, activationParam /* = propertyStoreContext GUID */, ...)` then `effectsPropertyStore->OpenUserPropertyStore(STGM_READWRITE, userPropertyStore)`. APO side: `GetApoNotificationRegistrationInfo` returns `APO_NOTIFICATION_DESCRIPTOR` with `type = APO_NOTIFICATION_TYPE_AUDIO_SYSTEM_EFFECTS_PROPERTY_CHANGE` and `audioSystemEffectsPropertyChange.propertyStoreContext = m_propertyStoreContext`; OS calls `HandleNotification(APO_NOTIFICATION*)`. Three substores exist: Default (from INF), User (persisted across OS upgrades), Volatile ("lost upon device reboot and are cleared each time the endpoint transitions to active... expected to contain time variant properties"). Enum values: https://learn.microsoft.com/en-us/windows/win32/api/audioengineextensionapo/ne-audioengineextensionapo-apo_notification_type
  -> If PeaceQT ships its own APO, implement IAudioSystemEffects3 + IAudioProcessingObjectNotifications and use the Volatile substore for live knob values and the User substore for presets. This gets you a supported, non-elevated, DACL-free channel with no config file and no shared memory — and it is the only path that will survive Windows 11 HLK/CAPX scrutiny. Requires build 22000+; keep the file/pipe channel as the Windows 10 fallback (detect via APOInitSystemEffects2 vs APOInitSystemEffects3, per the same doc).
- [high] The CAPX notification callback is explicitly NOT real-time safe and is serialized — Microsoft warns not to block it. It is a message-delivery thread, not the APOProcess thread.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-11-apis-for-audio-processing-objects — "Notifications are delivered to an APO using a serial queue... Notifications stop once audiodg.exe stops intending to use an APO for streaming. APOs will stop receiving notifications after UnlockForProcess. It is still necessary to synchronize UnlockForProcess and any in-flight notifications." and "All notifications to the APO are serialized, and it is important to not block the notification callback thread for too long."
  -> In HandleNotification, do only: read the property, convert to a POD parameter struct, publish it to the RT thread with a lock-free handoff. Do zero allocation, zero filter construction there. And explicitly synchronize UnlockForProcess against in-flight notifications or you will get a use-after-free on device change.
- [high] Microsoft forbids paged memory and blocking calls in the APO real-time path outright. This directly constrains any shared-memory design: a MapViewOfFile view is pageable, so reading it from APOProcess is a spec violation and a potential hard-fault on the audio thread.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects — "All methods that are members of real-time interfaces must be implemented as nonblocking members. They must not block, use paged memory, or call any blocking system routines." / "All buffers that are processed by the APO must be nonpageable. All code and data in the process path must be nonpageable." Equalizer APO complies with this by allocating filter memory via AERT_Allocate and marking the process path with AVRT pragmas: FilterEngine.cpp line 373 `#pragma AVRT_CODE_BEGIN` immediately before `FilterEngine::process`; https://raw.githubusercontent.com/mirror/equalizerapo/master/helpers/MemoryHelper.cpp uses `AERT_Allocate(size, &memory)` with a malloc fallback.
  -> Do not have APOProcess read a MapViewOfFile view directly. Either (a) VirtualLock the view and mark it non-paged, or preferably (b) have a non-RT thread in the APO read the shared view / pipe / property store and publish into an AERT_Allocate'd, VirtualLock'd parameter block that the RT thread reads. Mark PeaceQT's process path with AVRT_CODE_BEGIN/END and allocate everything the RT path touches with AERT_Allocate.
- [high] ViPER4Windows — the reference implementation for exactly the effects PeaceQT wants — uses no shared memory and no pipe. Its APO watches a directory and re-reads a 2.7 KB binary file. Its live-control channel is architecturally identical to Equalizer APO's, just binary instead of text.
  evidence: strings on C:\Program Files\ViPER4Windows\ViPER4Windows.dll: imports `FindFirstChangeNotificationW`, `FindNextChangeNotification`, `FindCloseChangeNotification`, `CreateEventW`, `SetEvent`, `CreateThread`, `WaitForMultipleObjects`, `EnterCriticalSection`/`LeaveCriticalSection`, `RegOpenKeyExW`/`RegQueryValueExW` — and NO CreateFileMapping/MapViewOfFile/CreateNamedPipe/CreateMutex. It depends on ConfigProxy.dll, whose only file APIs are `CreateFileW`, `ReadFile`, `WriteFile`, `SetFilePointer`, `SetEndOfFile` (exports include `CreateProxy`, `GetConfigVersion`). Registry HKLM\SOFTWARE\ViPER4Windows: `ConfigPath = C:\Program Files\ViPER4Windows\DriverComm`, `ConfigFile = EffectConfig.bin`. That file exists, is 2700 bytes, and its mtime (Jul 28 22:40) matches Config.ini and LocalPreset.bin — i.e. the last GUI interaction.
  -> Two lessons. (1) Note the path is under Program Files, not the user profile — because audiodg cannot read C:\Users. Copy that. (2) ViPER uses EnterCriticalSection, not lock-free atomics, around its parameter swap — that means it takes a lock on or near the audio path, which violates the MS RT rule above. Do NOT copy that part; use a seqlock/double-buffer instead.
- [high] Recommended real-time-safe update pattern for PeaceQT: a POD parameter block published by pointer-swap or seqlock, plus per-parameter one-pole smoothing in the DSP to kill zipper noise. Equalizer APO's own swap is technically a data race and should not be copied verbatim.
  evidence: FilterEngine.h lines 101-108 declare `FilterConfiguration* currentConfig; FilterConfiguration* nextConfig; FilterConfiguration* previousConfig; unsigned transitionCounter; unsigned transitionLength; HANDLE loadSemaphore; CRITICAL_SECTION loadSection;` — plain non-atomic pointers. The loader thread writes `nextConfig` under `loadSection` (FilterEngine.cpp line 261) while `FilterEngine::process` (line 391) reads it without ever entering that critical section. Benign on x86-64 for an aligned pointer, but UB under the C++ memory model.
  -> For PeaceQT: `std::atomic<Params*> m_pending{nullptr};` — GUI/notification thread fills a preallocated (AERT_Allocate'd, VirtualLock'd) Params slot and does `m_pending.store(p, std::memory_order_release)`; APOProcess does `Params* p = m_pending.exchange(nullptr, std::memory_order_acquire)` and, if non-null, copies scalars into its live smoothers and returns the slot to a lock-free freelist. Never delete on the RT thread. For each continuous parameter (gain, threshold, ratio, wet mix, decay) run `cur += (target - cur) * coeff` per block or per sample; for reverb room-size/pre-delay (which resize buffers) do NOT smooth — cross-fade two reverb instances or accept the change only at a buffer boundary.
- [high] There is a hard reliability limit on APO experimentation: after 10 failures across CoCreateInstance / IsInputFormatSupported / IsOutputFormatSupported / LockForProcess, Windows sets PKEY_Endpoint_Disable_SysFx=1 and silently disables ALL system effects on that endpoint.
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects — "if the failure count value for an SFX, MFX or EFX APO reaches a system-specified limit, the SFX, MFX and EFX APOs are disabled by setting the PKEY_Endpoint_Disable_SysFx registry key to '1'. The system-specified limit is currently a value of 10."
  -> If PeaceQT ships its own APO, make the GUI detect and reset PKEY_AudioEndpoint_Disable_SysFx ({1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5 — a value string already present in the installed EqualizerAPO.dll) and surface a clear 'enhancements were auto-disabled' message. Otherwise a crash loop during development will silently take Equalizer APO down with it.
- [high] Peace itself does nothing cleverer than write a text file — confirmed on this machine's live config. So 'what do other APO-adjacent tools do' has a uniform answer: file + directory watch, for every tool in this ecosystem.
  evidence: D:\Program Files\EqualizerAPO\config\config.txt contains exactly two lines: `Include: peace.txt` and `Include: dreamdsp.txt`. peace.txt is 0 bytes (Peace currently inactive); the config dir contains Peace's .peace preset files and Peace.chm. There is no IPC binary, service, or driver from Peace in the install.
  -> PeaceQT gets no competitive pressure to be cleverer on the config channel — but also no prior art to borrow from for real live control. The genuinely novel piece would be the CAPX property-store path or an APO-created Global\ section; nothing in this ecosystem has done either.
- [medium] FxSound (GPL3, open source) sidesteps the whole problem by not being an APO: it ships a virtual audio driver/endpoint, so its DSP runs inside its own process and parameter updates are ordinary in-process atomics. This is a third architecture worth pricing.
  evidence: https://github.com/fxsound2/fxsound-app (application + DSP source) and https://github.com/fxsound2/fxsound-driver ("source code for FxSound Audio Enhancer driver, which is a Windows virtual audio driver"); FxSound forum announcement of open-sourcing under GPL 3 at https://forum.fxsound.com/t/fxsound-is-now-open-source/2554
  -> Consider a fourth option for PeaceQT: virtual endpoint + WASAPI loopback/render in PeaceQT's own process. You lose 'works on the default device with zero setup' and add latency, but you get unrestricted live control, a normal user token, a debugger, and no protected-process/AERT/non-paged constraints — which matters a lot for a reverb and a compressor you are writing from scratch.

### code
```
// VERIFIED SOURCE — Equalizer APO's zero-dropout swap (FilterEngine.cpp, mirror/equalizerapo@master)
// RT thread. Runs BOTH chains during the 10 ms crossfade, takes no lock.
// line 150:  this->transitionLength = (unsigned)(sampleRate / 100);   // 10 ms
#pragma AVRT_CODE_BEGIN
void FilterEngine::process(float* output, float* input, unsigned frameCount)
{
    currentConfig->read(input, frameCount);
    currentConfig->process(frameCount);

    if (nextConfig != NULL)
    {
        nextConfig->read(input, frameCount);
        nextConfig->process(frameCount);
        transitionCounter = currentConfig->doTransition(nextConfig, frameCount,
                                                       transitionCounter, transitionLength);
    }

    currentConfig->write(output, frameCount);

    if (nextConfig != NULL && transitionCounter >= transitionLength)
    {
        previousConfig = currentConfig;   // freed later, on the LOADER thread
        currentConfig  = nextConfig;
        nextConfig     = NULL;
        transitionCounter = 0;
        ReleaseSemaphore(loadSemaphore, 1, NULL);   // loader may now stage the next config
    }
}
```

```
// VERIFIED SOURCE — the watcher thread + 10 ms debounce + backpressure (FilterEngine.cpp lines 549-616)
unsigned long __stdcall FilterEngine::notificationThread(void* parameter)
{
    FilterEngine* engine = (FilterEngine*)parameter;
    HANDLE notificationHandle = FindFirstChangeNotificationW(
        engine->configPath.c_str(), true,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    HANDLE registryEvent = CreateEventW(NULL, true, false, NULL);
    HANDLE handles[3] = {engine->shutdownEvent, notificationHandle, registryEvent};

    while (true)
    {
        // ... RegNotifyChangeKeyValue(...) on each watched key ...
        DWORD which = WaitForMultipleObjects(3, handles, false, INFINITE);
        if (which == WAIT_OBJECT_0) break;                    // shutdown

        if (which == WAIT_OBJECT_0 + 1)
        {
            FindNextChangeNotification(notificationHandle);
            // Wait for second event within 10 milliseconds to avoid loading twice
            WaitForMultipleObjects(1, &notificationHandle, false, 10);
        }

        // BACKPRESSURE: block until the RT thread finished the previous crossfade
        HANDLE h2[2] = {engine->shutdownEvent, engine->loadSemaphore};
        if (WaitForMultipleObjects(2, h2, false, INFINITE) == WAIT_OBJECT_0) break;

        engine->loadConfig();          // full re-parse + full filter rebuild, OFF the RT thread
        FindNextChangeNotification(notificationHandle);
        ResetEvent(registryEvent);
    }
    FindCloseChangeNotification(notificationHandle);
    CloseHandle(registryEvent);
    return 0;
}
```

```
// MS DOC (windows-11-apis-for-audio-processing-objects) — GUI side, non-elevated, no file, no shared memory.
// PeaceQT GUI writes a live parameter into the endpoint's Volatile FX property store.
HRESULT GetPropertyStoreFromMMDevice(IMMDevice* device, REFGUID propertyStoreContext,
                                     IPropertyStore** userPropertyStore)
{
    *userPropertyStore = nullptr;
    wil::unique_prop_variant activationParam;
    RETURN_IF_FAILED(InitPropVariantFromCLSID(propertyStoreContext, &activationParam));

    wil::com_ptr_nothrow<IAudioSystemEffectsPropertyStore> effectsPropertyStore;
    RETURN_IF_FAILED(device->Activate(__uuidof(effectsPropertyStore), CLSCTX_INPROC_SERVER,
                                      activationParam.addressof(), effectsPropertyStore.put_void()));
    // Use OpenVolatilePropertyStore for live knob values; OpenUserPropertyStore for presets.
    RETURN_IF_FAILED(effectsPropertyStore->OpenUserPropertyStore(STGM_READWRITE, userPropertyStore));
    return S_OK;
}
```

```
// MS DOC — APO side: declare interest once, then receive HandleNotification on a serialized,
// NON-real-time thread. Never block here; never allocate here.
STDMETHODIMP SampleApo::GetApoNotificationRegistrationInfo(
    APO_NOTIFICATION_DESCRIPTOR** apoNotificationDescriptorsReturned, DWORD* count)
{
    wil::unique_cotaskmem_ptr<APO_NOTIFICATION_DESCRIPTOR[]> d;
    d.reset(static_cast<APO_NOTIFICATION_DESCRIPTOR*>(
        CoTaskMemAlloc(sizeof(APO_NOTIFICATION_DESCRIPTOR) * 1)));
    RETURN_IF_NULL_ALLOC(d);

    d[0].type = APO_NOTIFICATION_TYPE_AUDIO_SYSTEM_EFFECTS_PROPERTY_CHANGE;
    (void)m_device.query_to(&d[0].audioSystemEffectsPropertyChange.device);
    d[0].audioSystemEffectsPropertyChange.propertyStoreContext = m_propertyStoreContext;

    *apoNotificationDescriptorsReturned = d.release();
    *count = 1;
    return S_OK;
}
```

```
// RECOMMENDED FOR PeaceQT (not copied from anywhere) — RT-safe parameter handoff.
// Replaces both Equalizer APO's racy raw-pointer swap and ViPER4Windows' critical section.
struct Params {                 // POD only. No std::string, no heap-owning members.
    float bassHarmonics, exciterAmount, driveDb;
    float compThresholdDb, compRatio, compAttackMs, compReleaseMs, compKneeDb, compMakeupDb;
    float revRoomSize, revDamping, revDensity, revBandwidth, revDecay, revPreDelayMs, revWet;
};

class PeaceQtApo {
    // All three slots AERT_Allocate'd at LockForProcess time and VirtualLock'd: MS requires
    // "All code and data in the process path must be nonpageable."
    std::atomic<Params*> m_pending{nullptr};   // producer -> consumer
    std::atomic<Params*> m_recycle{nullptr};   // consumer -> producer
    Params               m_live{};             // RT thread's private copy (targets)
    Params               m_smooth{};           // RT thread's smoothed values (actually used by DSP)

public:
    // Called from HandleNotification / pipe reader thread. Never from APOProcess.
    void publish(const Params& p) {
        Params* slot = m_recycle.exchange(nullptr, std::memory_order_acquire);
        if (!slot) return;                     // RT hasn't returned a slot yet: coalesce, drop this update
        *slot = p;
        Params* old = m_pending.exchange(slot, std::memory_order_release);
        if (old) m_recycle.store(old, std::memory_order_release);   // superseded before RT saw it
    }

#pragma AVRT_CODE_BEGIN
    void APOProcess(float* buf, UINT32 frames) {
        if (Params* p = m_pending.exchange(nullptr, std::memory_order_acquire)) {
            m_live = *p;                                   // plain POD copy, no allocation
            m_recycle.store(p, std::memory_order_release); // hand the slot back; NEVER delete here
        }
        // Per-block one-pole smoothing kills zipper noise on continuous params.
        constexpr float k = 0.05f;
        m_smooth.compThresholdDb += (m_live.compThresholdDb - m_smooth.compThresholdDb) * k;
        m_smooth.revWet          += (m_live.revWet          - m_smooth.revWet)          * k;
        m_smooth.driveDb         += (m_live.driveDb         - m_smooth.driveDb)         * k;
        // Structural params (revRoomSize, revPreDelayMs) resize buffers -> do NOT smooth.
        // Cross-fade two reverb instances, or apply only at a block boundary.
        // ... run DSP using m_smooth ...
    }
#pragma AVRT_CODE_END
};
```

```
# VERIFIED ON THIS MACHINE — proof audiodg opens a named pipe created by a user-mode GUI,
# and proof of the ViPER file-watch channel. Reproduce with:
strings -a -n 5 "D:/Program Files/EqualizerAPO/DeviceSelector.exe" | grep -E '^(CreateNamedPipeW|ConnectNamedPipe|DisconnectNamedPipe)$'
strings -el -n 5 "D:/Program Files/EqualizerAPO/DeviceSelector.exe" | grep -iE 'pipe|EqualizerAPODeviceTest'
#   -> EqualizerAPODeviceTest / \\.\pipe\ / DeviceTestPipeName / "Could not create named pipe: "
strings -el -n 5 "D:/Program Files/EqualizerAPO/EqualizerAPO.dll" | grep -i pipe
#   -> "\\.\pipe\ / "Could not connect to named pipe: %s" / "Could not write to pipe: %s"

strings -a -n 5 "C:/Program Files/ViPER4Windows/ViPER4Windows.dll" \
  | grep -E '^(FindFirstChangeNotificationW|CreateFileMappingW|CreateNamedPipeW|EnterCriticalSection)$'
#   -> FindFirstChangeNotificationW, EnterCriticalSection   (no mapping, no pipe)

# PowerShell — the two facts that gate every shared-memory design:
whoami /priv | Select-String SeCreateGlobal      # -> (nothing): cannot create Global\ sections
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio' |
  Select-Object DisableProtectedAudioDG          # -> 1 on THIS machine; 0/absent on a stock box
Get-ItemProperty 'HKLM:\SOFTWARE\ViPER4Windows'  # -> ConfigPath=...\DriverComm, ConfigFile=EffectConfig.bin
```

UNKNOWNS: Whether the Windows 11 CAPX Settings/Notifications framework (IAudioSystemEffectsPropertyStore + IAudioProcessingObjectNotifications) works at all for an APO that is registered the Equalizer APO way — manually, by writing PKEY_FX_PostMixEffectClsid into MMDevices FxProperties — rather than via a componentized driver INF. The docs only ever describe the INF/HSA path, and the 'Default' substore is explicitly INF-populated. If CAPX requires a real driver package, the entire supported live-control path is closed to PeaceQT. THIS IS THE SINGLE HIGHEST-VALUE THING TO PROTOTYPE (a ~200-line APO that logs HandleNotification would settle it).; End-to-end latency of the CAPX property-store notification path. The doc only says notifications are serialized and must not be blocked; no timing figures are published anywhere I could find. Unmeasured.; Whether a genuinely non-elevated Win32 process can write the User/Volatile FX property store in practice. The doc says 'non-admin desktop applications' can, but MMDevices registry keys are TrustedInstaller-owned (per dechamps/APO: 'The only user with full control of that key is TrustedInstaller'), so the write must be brokered by audiosrv. Untested.; Whether audiodg's token is actually write-restricted, and if so exactly which restricting SIDs a mapping's DACL must satisfy for audiodg to get FILE_MAP_WRITE. My source for 'write-restricted SIDs' is a single security blog post, not Microsoft. Read-only access from audiodg is safe; write-back is the uncertain direction. Requires an elevated token dump of pid 5068 to settle.; Whether audiodg can successfully open a Global\ named section that an elevated helper creates, and whether it still can when DisableProtectedAudioDG is 0 (protected process re-enabled). This machine has protection disabled, so nothing observed here generalizes. Must be tested with the key set back to 0 and audiosrv restarted.; Why EqualizerAPO.dll and Editor.exe both import CreateFileMappingW/MapViewOfFileEx. MemoryHelper.cpp in the mirror source uses AERT_Allocate/malloc, not file mappings, so this is newer 1.3/1.4 code not present in the GitHub mirror. Editor.exe also references QSharedMemory and an AnalysisThread. I could not find a named-section string in either binary and could not rule out that it is Qt single-instance guard code or impulse-response file mapping. If it IS a live APO->GUI channel it would be the best prior art available — worth 30 minutes with a disassembler or Process Explorer's handle view on audiodg.; Actual measured cost of a real Equalizer APO loadConfig() on this machine with a heavy chain. HKLM\SOFTWARE\EqualizerAPO\EnableTrace is 'false' and the log directory C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp is not readable without elevation, so I have the crossfade length (10 ms, exact) and the notification latency (0.5-1.1 ms, measured) but not the parse+construct time. For a config with a large convolution IR or a VST this is likely the dominant term and could be 100 ms+.; Whether ViPER4Windows' ConfigProxy re-reads EffectConfig.bin on the notification thread or lazily on the audio thread. ViPER4Windows.dll imports EnterCriticalSection and the file APIs live in ConfigProxy.dll, so a lock could plausibly be taken on or near the RT path — but I did not disassemble to confirm. Relevant only as a cautionary example.; Whether Windows 11 24H2+ has added any new gate on non-INF, manually-registered third-party APOs (the CAPX/HLK requirements are documented for shipping IHV APOs, but their applicability to a sideloaded APO is unstated). Searches surfaced only 24H2 audio-compat safeguard holds (e.g. Dirac's cridspapo.dll), not a general block.

---

## Other ways to get custom system-wide DSP on Windows in 2026 — is anything better than "APO" or "VST inside APO"?
- [high] ViPER4Windows is NOT a driver — it is a plain user-mode COM APO DLL registered by writing PKEY_FX_* values into MMDevices FxProperties, exactly the same mechanism Equalizer APO uses. Verified on this machine: C:\Program Files\ViPER4Windows\Configurator\{30d0a993-116c-4954-b341-5de5dc17de1c}.reg writes [HKLM\...\MMDevices\Audio\Render\{guid}\FxProperties] with "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13" (PKEY_FX_StreamEffectClsid / SFX) and ",14" (PKEY_FX_ModeEffectClsid / MFX). No .sys, no .inf, no kernel component in the install dir. So ViPER4Windows proves the exact thing PeaceQT wants — psychoacoustic bass, tube sim, FET compressor, reverb, convolver — is fully achievable inside an ordinary APO DLL with zero driver work.
  evidence: C:\Program Files\ViPER4Windows\Configurator\{30d0a993-116c-4954-b341-5de5dc17de1c}.reg (read, header + key inventory); dir listing of C:\Program Files\ViPER4Windows (ViPER4Windows.dll 859,528 B, ConfigProxy.dll, Utils.dll, ViPER4WindowsCtrlPanel.exe, Batch\, Configurator\, ImpulseResponse\ — no .sys/.inf); https://github.com/likelikeslike/ViPER4Windows
  -> Stop treating "PeaceQT does DSP itself" as needing a driver. Ship a C++ APO DLL (IAudioProcessingObject + IAudioProcessingObjectConfiguration + IAudioProcessingObjectRT) that contains PeaceQT's own nonlinear DSP, and keep the Qt GUI as a separate process that talks to it via shared memory / named-pipe / registry-poll. That is a 1-person-scale project and is literally what V4W v1.0.5.4 (installed here) already is.
- [high] ViPER4Windows solves the APO code-signing problem by shipping its own private root CA and importing it into the machine's trusted root store. ViPER4Windows.dll is Authenticode-valid, signed by "CN=PureSoftApps Windows Hardware Signature 2019" chaining to "PureSoftApps Root Certificate 2019" — a self-issued root that C:\Program Files\ViPER4Windows\Batch\ImportCertificate.cmd base64-echoes to disk and installs. Equalizer APO by contrast is Status=NotSigned and relies purely on the DisableProtectedAudioDG=1 override, which is already set to 1 on this machine.
  evidence: Get-AuthenticodeSignature results: ViPER4Windows.dll Status=Valid Signer=CN=PureSoftApps Windows Hardware Signature 2019,OU=PureSoftApps,C=HK; D:\Program Files\EqualizerAPO\EqualizerAPO.dll Status=NotSigned. HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\DisableProtectedAudioDG = 1. C:\Program Files\ViPER4Windows\Batch\ImportCertificate.cmd lines 1-60 (embeds PureSoftApps Root + Intermediate certs as base64). https://sourceforge.net/p/equalizerapo/wiki/Documentation/
  -> PeaceQT needs NO signing at all on this machine — DisableProtectedAudioDG is already 1 because Equalizer APO set it. For shipping to others, do what Equalizer APO does (installer sets the key + reboot) and do NOT copy the V4W private-root-CA trick: injecting a root CA is an AV/EDR red flag and a real security regression for users.
- [high] There is a third, fully-legitimate APO shipping path that needs no registry override: package the APO as a signed Extension INF driver package. Bongiovi DPS on this machine does exactly this — BongioviAPO.inf declares Class=Extension, ClassGuid={e2f84ce7-8efa-411c-aa69-97454ca4cb57}, PnpLockdown=1, [SignatureAttributes] BongioviAPO.dll=SignatureAttributes.PETrust / PETrust=true, and matches by USB/Bluetooth VID+PID. BongioviAPO.cat is signed by an EV cert (CN="Bongiovi Acoustics, LLC", DigiCert Trusted G4 Code Signing RSA4096, Private Organization OID). So Bongiovi DPS is also an APO, not a virtual driver — just a professionally-packaged one.
  evidence: C:\Program Files\BongioviMBApp\BongioviAPO.inf lines 1-60 (read verbatim); Get-AuthenticodeSignature C:\Program Files\BongioviMBApp\BongioviAPO.cat -> Valid, Issuer=DigiCert Trusted G4 Code Signing RSA4096 SHA384 2021 CA1, NotAfter 2025/6/4. Files: BongioviAPO.dll 14.6 MB, BongioviSDK.dll 148 MB.
  -> Note the cost: this path needs an EV cert + Partner Center + a hardware-ID match list, and Bongiovi only targets USB-Audio-class and BT-stereo VID/PIDs. It is the right answer only if PeaceQT ever gets an OEM deal. For a solo dev, the Equalizer-APO-style FxProperties registration is the pragmatic choice; keep the Extension-INF path as a documented upgrade route.
- [high] The APO route costs essentially ZERO added latency, and when you do need lookahead (compressor, reverb pre-delay, linear-phase EQ) the platform has a first-class way to declare it. Microsoft's own DelayAPO sample allocates a delay buffer in LockForProcess and reports the added latency from GetLatency: `*pTime = (m_fEnableDelayMFX ? HNS_DELAY : 0);` with `m_nDelayFrames = FRAMES_FROM_HNS(HNS_DELAY); m_pf32DelayBuffer.Allocate(GetSamplesPerFrame() * m_nDelayFrames);`. The engine's own period is the baseline: shared-mode default is 10 ms (e.g. 448 frames ≈ 10.16 ms at 44.1 kHz), and IAudioClient3 can go to ~2.67 ms at 48 kHz.
  evidence: https://raw.githubusercontent.com/microsoft/Windows-driver-samples/main/audio/sysvad/APO/DelayAPO/DelayAPOMFX.cpp ; https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-getsharedmodeengineperiod
  -> Design PeaceQT's DSP to run in-place in APOProcess with a strict RT contract (no allocation, no locks, no paged memory, no file I/O). Allocate every buffer in LockForProcess. Implement GetLatency honestly so A/V sync stays correct when the reverb pre-delay / compressor lookahead is on. This is strictly better than every alternative on latency.
- [high] WASAPI loopback + exclusive-mode render is architecturally impossible on a single endpoint. Microsoft states verbatim: "A client can enable loopback mode only for a shared-mode stream (AUDCLNT_SHAREMODE_SHARED). Exclusive-mode streams cannot operate in loopback mode." And opening the endpoint in exclusive mode evicts the shared engine — which is the very thing producing the audio you wanted to capture. Even in shared+shared you get doubling: the original mix still reaches the speakers, and nothing in WASAPI lets you suppress it. Windows 10 20348+ process loopback (AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK with PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE) lets you capture everything except yourself, which avoids the feedback howl-round, but still does not mute the originals.
  evidence: https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording (fetched full text); https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-process_loopback_mode
  -> Reject the loopback route for the effects engine. PeaceQT already has src/platform/LoopbackCapture.cpp — keep it for what it is good at (the peak meter / spectrum display) and never promote it to the audio path. The only way loopback works is when the capture endpoint is a *different, virtual* device, which collapses into the virtual-driver route below.
- [medium] The virtual-audio-device route works but is the most expensive option and its cost is now dominated by driver signing, not code. A WDM/PortCls virtual audio device is kernel-mode: it must be Microsoft-signed. Microsoft is removing trust for the cross-signed driver program starting with the April 2026 servicing release (evaluation mode first, ~100 hours runtime + multiple restarts before enforcement), leaving WHCP/attestation via Partner Center as the only path — which requires an EV cert on the account (DigiCert ~$409/yr, Sectigo ~$290-499/yr), and from Feb 15 2026 code-signing certs are capped at 1-year lifetimes. Attestation-signed drivers also cannot be broadly published to retail via Windows Update.
  evidence: https://techcommunity.microsoft.com/blog/windows-itpro-blog/advancing-windows-driver-security-removing-trust-for-the-cross-signed-driver-pro/4504818 (title + coverage at https://windowsforum.com/threads/april-2026-windows-update-ends-cross-signed-kernel-driver-trust.410487/); https://sslinsights.com/disable-driver-signature-enforcement-in-windows-10/
  -> Do not write a virtual audio driver. Recurring EV-cert cost + Partner Center company vetting + the April 2026 trust purge + WHQL/HLK is a company-scale burden, and it buys you nothing PeaceQT needs that an APO doesn't already give. If you ever must, start from Microsoft's SysVAD or simpleaudiosample rather than from scratch.
- [high] FxSound is the canonical open-source example of the virtual-driver route, and it confirms the shape of the work: a kernel driver plus a user-space DSP. Its driver is explicitly "based on https://github.com/uri247/wdk81/tree/master/Microsoft Virtual Audio Device Driver Sample", built with VS2022 + WDK, and the app is three parts — a JUCE GUI, an "Audiopassthru" module that talks to the audio devices, and "DfxDsp", the actual DSP.
  evidence: https://raw.githubusercontent.com/fxsound2/fxsound-driver/main/README.md ; https://github.com/fxsound2/fxsound-app
  -> Read fxsound-app's DfxDsp module as a reference implementation of exactly the effect set PeaceQT wants (it is GPL/open) — but reuse the DSP ideas inside an APO rather than adopting their driver architecture. Note the split they chose (GUI framework separate from DSP core) matches PeaceQT's existing src/core vs src/app split.
- [medium] Virtual-device routing costs a full extra buffering stage in each direction, and VB-Audio's own numbers put that at roughly 14-21 ms for VB-CABLE alone. VB-Audio forum guidance: VB-CABLE's internal latency setting is 2048 samples ≈ 21.3 ms at 48 kHz, with real added latency "closer to 14.2 ms"; a user measuring a full chain with all cables at 2048 got ~47.66 ms round trip. Voicemeeter's own optimization page only commits to "a buffer size of 256 samples at 44.1 or 48 kHz results in a latency of 5 ms" and says MME "cannot deliver a low latency, like ASIO".
  evidence: https://forum.vb-audio.com/viewtopic.php?t=600 (search-result extraction; direct fetch returned HTTP 403); https://voicemeeter.com/how-to-optimize-your-latency-in-voicemeeter/ (fetched)
  -> Quote ~15-45 ms as the realistic penalty of any virtual-cable design and compare it to ~0 ms for an APO. That alone should settle the architecture decision for a general-purpose system-wide effect (it breaks lip sync in video and makes games feel mushy).
- [high] This machine already has an enormous number of virtual endpoints installed (VoiceMeeter VAIO / AUX VAIO / VAIO3, NVIDIA Virtual Audio 4.65, ToDesk Virtual Audio, MiPlay Virtual Audio, 网易虚拟音频设备, Voicemod Dummy Output, ASUS Utility Virtual Speaker, Steam Streaming Speakers). Voicemeeter Potato additionally ships a Virtual ASIO "insert" driver specifically so an external VST host can be spliced into its signal path (System Settings -> select Potato ASIO Insert -> tick PATCH INSERT).
  evidence: HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render enumeration on this machine (78 endpoints listed); uninstall registry shows "Voicemeeter, The Virtual Mixing Console" (VB-Audio) and "NVIDIA Virtual Audio 4.65.0.12"; https://voicemeeter.com/user-guide-connect-audio-apps-to-the-voicemeeter-insert-driver/ ; https://forum.vb-audio.com/viewtopic.php?t=742
  -> If you want a zero-driver escape hatch for power users, document "PeaceQT as a Voicemeeter Potato ASIO insert" instead of shipping your own cable. The user already has Voicemeeter installed, so this costs you an ASIO client implementation and nothing else. It is a nice-to-have, not the primary product.
- [high] CamillaDSP is the existing open-source DSP host that already solves "drop in DSP without writing an APO" — and its Windows docs confirm the constraints rather than escaping them: loopback capture "requires using Shared mode for the capture device", the config `samplerate` "must match the 'Default format' setting of the device", and its own example config captures from "CABLE Output (VB-Audio Virtual Cable)" with exclusive:false. I.e. even the best-in-class open-source solution ends up as virtual-cable + shared-mode capture + rate-locked config.
  evidence: https://raw.githubusercontent.com/HEnquist/camilladsp/master/backend_wasapi.md ; https://henquist.github.io/0.6.1/backend_wasapi.html
  -> Use CamillaDSP as the honest benchmark for the non-APO route. Its user-visible friction (pick a dummy default device, match sample rates manually, re-do it when the format changes) is exactly the friction PeaceQT would inherit. This is the concrete argument for staying in APO-land.
- [high] Windows 11's modern APO API surface (CAPX) does not open a new door for a third-party like PeaceQT — it mostly adds obligations. IAudioSystemEffects3 is what lets an APO expose effects to the OS via GetControllableSystemEffectsList / SetAudioSystemEffectState, and "Any new APOs that ship on a device for Windows 11 are required to be compliant with the APIs listed in this topic, validated via HLK." The settings API "is intended to support all OEMs and HSA developers" and is reached through the restricted capability "audioDeviceConfiguration" declared in a package manifest. It also assumes the audio driver declared support: the sample comments note "If we get here, the audio driver did not declare support for IAudioSystemEffects3."
  evidence: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-11-apis-for-audio-processing-objects (full text fetched; lines 74, 400, 639 of the retrieved markdown)
  -> Do not chase CAPX / Effects Discovery / the Windows 11 "Audio enhancements" Settings UI. It is gated behind an OEM driver declaring support and behind HLK validation. Implement the classic IAudioProcessingObject* trio only; optionally add IAudioSystemEffects3 later as a no-cost nicety, but do not architect around it.
- [high] The real risk of the APO route is not signing or latency, it is registration durability and OS gating. Known failure modes: (a) Windows feature updates re-create endpoints with new device IDs and silently detach the APO — reported across 23H2, 24H2 and 25H2, with the standard fix being reinstall-over-itself + Configurator + reboot; (b) the Windows 11 "Audio enhancements" selector must be Default / Device Default Effects or third-party APO effects are bypassed; (c) Windows spatial audio conflicts with third-party APOs; (d) "Windows 10 loads RAW MFX but not RAW SFX" while "EFX are always applied, even to raw streams"; (e) WASAPI exclusive-mode apps (many DAWs, some players) bypass the engine entirely so no APO runs; (f) DisableProtectedAudioDG=1 "means that the protected audio path is broken, making some software using DRM refuse to output any sound."
  evidence: https://sourceforge.net/p/equalizerapo/discussion/general/thread/3845eb142e/ and .../7df05ab8b2/ ; https://equalizerapoguide.com/ ; https://raw.githubusercontent.com/MicrosoftDocs/windows-driver-docs/staging/windows-driver-docs-pr/audio/audio-processing-object-architecture.md ; https://sourceforge.net/p/equalizerapo/wiki/Documentation/
  -> Build a health-check/self-repair subsystem into PeaceQT from day one — it is the single highest-value feature and Peace/Equalizer APO do it badly. On every launch: verify DisableProtectedAudioDG, verify the FxProperties CLSIDs still point at PeaceQT's DLL on the *current* default endpoint, detect endpoints whose DeviceState changed, detect the Audio-enhancements-off state, detect spatial audio, and offer one-click re-registration. src/platform/ApoLocator.cpp and AudioDevices.cpp are the natural homes.
- [medium] Equalizer APO currently occupies BOTH the pre-mix (SFX) and post-mix (MFX) slots on the default endpoint on this machine, but a third slot — EFX (endpoint effect) — is architecturally distinct and per Microsoft is applied "after the mix (render) or before the tee (capture) of all modes" and is "always applied, even to raw streams".
  evidence: Task-given: pid1 {EACD2258-...} pre-mix + pid2 {EC1CC9CE-...} post-mix both -> EqualizerAPO.dll. https://raw.githubusercontent.com/MicrosoftDocs/windows-driver-docs/staging/windows-driver-docs-pr/audio/audio-processing-object-architecture.md
  -> Consider registering PeaceQT's APO in the EFX slot so it can coexist with an existing Equalizer APO install during migration rather than fighting it. Verify empirically that Windows honours a third-party EFX CLSID written to FxProperties (PKEY_FX_EndpointEffectClsid) on a non-OEM endpoint — I did not test this.
- [high] Boom3D could not be verified on this machine — it is not present in Program Files, not in the uninstall registry, and no Boom-named render endpoint exists. The installed third-party audio DSP on this box is: EqualizerAPO 1.4.1.0, ViPER4Windows 1.0.5.4 (ViPERs Audio, Inc.), BongioviMBApp (ASUS-motherboard OEM build — it ships BongioviIsMotherboardAsus.exe), Voicemeeter, NVIDIA Virtual Audio 4.65, NVIDIA Broadcast, and iZotope RX 11.
  evidence: Get-ItemProperty over HKLM Uninstall + WOW6432Node Uninstall filtered on audio keywords; Get-Item probes for C:\Program Files*\Boom* and *\FxSound* returned nothing; MMDevices\Render enumeration contains no Boom/FxSound endpoint.
  -> Drop Boom3D from the comparison set — the parent brief's MMDevices sighting does not reproduce. Use Bongiovi (installed, INF-packaged APO) and ViPER4Windows (installed, registry-registered APO) as the two live reference implementations you can dissect on this very machine.

### code
```
// Latency declaration pattern PeaceQT must copy for reverb pre-delay / compressor lookahead.
// Source: Windows-driver-samples audio/sysvad/APO/DelayAPO/DelayAPOMFX.cpp
STDMETHODIMP CDelayAPOMFX::GetLatency(HNSTIME* pTime)
{
    if (IsEqualGUID(m_AudioProcessingMode, AUDIO_SIGNALPROCESSINGMODE_RAW))
        *pTime = 0;
    else
        *pTime = (m_fEnableDelayMFX ? HNS_DELAY : 0);
    return S_OK;
}

// ...and the allocation must happen in LockForProcess, never in APOProcess:
m_nDelayFrames = FRAMES_FROM_HNS(HNS_DELAY);
m_pf32DelayBuffer.Allocate((size_t)GetSamplesPerFrame() * m_nDelayFrames);
WriteSilence(m_pf32DelayBuffer, m_nDelayFrames, GetSamplesPerFrame());
```

```
; The entire registration mechanism ViPER4Windows uses -- identical to Equalizer APO.
; Verbatim head of C:\Program Files\ViPER4Windows\Configurator\{30d0a993-...}.reg (UTF-16LE)
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{30d0a993-116c-4954-b341-5de5dc17de1c}]
"DeviceState"=dword:00000001

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{30d0a993-116c-4954-b341-5de5dc17de1c}\FxProperties]
"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},0"="{00000000-0000-0000-0000-000000000000}"   ; PKEY_FX_Association
"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13"=hex(7): ... "{FA38205F-1C0A-40D4-BCBF-AEE89A9B00CB}" ; PKEY_FX_StreamEffectClsid (SFX / pre-mix)
"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14"=hex(7): ... "{ACE4B6FC-0E07-...}"                ; PKEY_FX_ModeEffectClsid  (MFX / post-mix)
```

```
; The "legitimate" alternative: ship the APO as a signed Extension INF.
; Verbatim head of C:\Program Files\BongioviMBApp\BongioviAPO.inf
[Version]
Signature="$Windows NT$"
Class=Extension
ClassGuid={e2f84ce7-8efa-411c-aa69-97454ca4cb57}
ExtensionId={F542CB95-4A23-4FBF-A05F-61F2D66BF16A}
Provider=%ProviderName%
CatalogFile=BongioviAPO.cat
DriverVer=01/27/2025,1.0.0.117
PnpLockdown=1

[SignatureAttributes]
BongioviAPO.dll=SignatureAttributes.PETrust

[SignatureAttributes.PETrust]
PETrust=true

[Bongiovi.NTamd64]
=BGVDeviceUSB,%VIDPID_USB%          ; matches by hardware ID -- not device-agnostic
=BGVDeviceBTHStereo,%VIDPID_BTH_Stereo_SRC%
```

```
# Already true on this machine -- PeaceQT's APO would load unsigned today, no cert needed.
PS> Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio' | fl DisableProtectedAudioDG
DisableProtectedAudioDG : 1

PS> Get-AuthenticodeSignature 'D:\Program Files\EqualizerAPO\EqualizerAPO.dll' | fl Status
Status : NotSigned

PS> Get-AuthenticodeSignature 'C:\Program Files\ViPER4Windows\ViPER4Windows.dll' | fl Status,Signer
Status : Valid
Signer : CN=PureSoftApps Windows Hardware Signature 2019, OU=PureSoftApps, C=HK   # self-issued root, imported by Batch\ImportCertificate.cmd
```

UNKNOWNS: Whether a third-party (non-OEM, registry-registered) APO CLSID written to PKEY_FX_EndpointEffectClsid (the EFX slot) is actually honoured by the Windows 11 audio engine on a consumer endpoint. Microsoft documents EFX as "always applied, even to raw streams", but every real-world third-party APO I found (Equalizer APO, ViPER4Windows) uses SFX+MFX only. Needs an empirical test on this machine before PeaceQT designs coexistence-with-EqualizerAPO around it.; Whether audiodg's APO signature check would accept a normal commercial Authenticode signature (e.g. Microsoft Trusted Signing at $9.99/mo) without DisableProtectedAudioDG=1, or whether it specifically demands a WHQL/PETrust catalog. ViPER4Windows' self-signed-root approach suggests a plain trusted chain suffices, but DisableProtectedAudioDG is already 1 on this machine so its private CA may be decorative. Untested — and it is the single fact that decides whether PeaceQT can ship without the registry hack.; Exact buffer/frame count the engine passes to APOProcess in practice on this machine, and whether it is stable across format changes. Docs give the engine period (10 ms default, ~2.67 ms minimum via IAudioClient3) but not the guaranteed APOProcess frame count. Matters for block-based DSP (FFT-based exciter, partitioned convolution).; VB-CABLE's exact added latency figures (2048 samples / 21.3 ms / "closer to 14.2 ms") come from a VB-Audio forum thread that returned HTTP 403 to direct fetch — I only have the search-engine extraction, not the primary text. Treat the numbers as order-of-magnitude, not quotable.; Whether Microsoft's April 2026 cross-signed-driver trust removal has any second-order effect on APO DLLs (they are user-mode, so it should not, but the same servicing wave has historically re-created audio endpoints and detached APOs). Not verified.; What ViPER4Windows' actual DSP quality/algorithms are — I confirmed the feature list from its README (bass/clarity, tube sim, FET compressor, reverb, convolver) and confirmed it is an APO, but did not disassemble ViPER4Windows.dll or evaluate the algorithms. If PeaceQT wants to match it, the DSP design work is still entirely ahead.; Whether Bongiovi's Extension-INF APO and Equalizer APO's FxProperties APO can coexist on the same endpoint, and what happens when both try to own MFX. Relevant because this machine has both installed.; Boom3D and FxSound were named in the brief as appearing in MMDevices on this machine; neither is present now (no install dir, no uninstall entry, no endpoint). Possibly stale/orphaned MMDevices GUIDs the brief saw, or a misattribution — unresolved.