# Run-Ahead

Run-ahead reduces controller-to-screen latency by displaying a bounded speculative
future frame while advancing the authoritative emulated machine by exactly one frame.
It is optional and disabled by default because every additional frame increases CPU
work.

## Using run-ahead

Load a cartridge game, then choose **Emulation → Run-Ahead**. Open
**Emulation → Run-Ahead Settings…** to select one through four speculative frames.
The setting is stored in `config/run-ahead-settings.json` beneath the normal application
data root and applies to future launches. Restore Defaults disables run-ahead and resets
the count to one.

Run-ahead is available for SG-1000, Mark III, Master System, Game Gear, Genesis/Mega
Drive, and Power Base Converter cartridge sessions. It is intentionally unavailable for
Sega CD because disc-drive/CDDA state has not yet met the same rollback determinism
gate. The preference remains saved while a Sega CD game is open, but the menu action is
disabled and Diagnostics reports support as unavailable.

Standard 2/3/6-button pads and pad-only multitaps have exact handshake rollback support.
Mouse, light-gun, analog, paddle, sports, drawing-tablet, Pico, and Activator sessions
temporarily disable run-ahead rather than risk advancing peripheral protocol state.

## Accuracy and data flow

All speculation stays on the existing emulation owner thread:

```text
input snapshot -> capture raw + transient rollback context
               -> execute 1-4 speculative frames (discard audio)
               -> keep final speculative native video
               -> restore exact rollback context and input
               -> execute one authoritative frame (keep audio)
               -> publish one video frame and one audio batch
```

The persistent Genesis Plus GX save-state payload is unchanged. A separate in-process
rollback context preserves transient 68000 execution, viewport, pause-input,
sound-filter, resampler, standard gamepad, Master Tap/Four Way Play, and Team Player
handshake state that is intentionally outside portable save states.
The first speculative frame is compared with the authoritative continuation. If their
raw states differ, run-ahead fails closed for that game session, publishes the normal
authoritative frame, and records the determinism failure in the log and Diagnostics.

Speculative audio never reaches the host ring, recordings receive one composed video
frame paired with authoritative audio, and speculative frames never enter rewind
history, save RAM, save states, screenshots, or frame counters. Rollback vectors reuse
their allocated storage; diagnostics show current/capacity bytes and speculation,
rollback, and determinism counters.

## Interactions and performance

- Fast-forward, slow motion, and rewind temporarily suspend run-ahead. It resumes when
  normal forward play returns.
- Pause performs no speculation. Frame Advance performs one host frame and applies the
  configured run-ahead count.
- Reset, state load, disc/settings changes, game replacement, and unload clear the
  verification session before speculation can resume.
- Selecting a specialized controller disables speculation; returning every connected
  player to a standard pad starts a fresh verification.
- Recording preserves normal one-frame cadence and never records speculative audio.
- Four-frame run-ahead can require roughly five times the normal core execution work.
  Reduce the count or disable the feature if the measured frame rate cannot remain at
  the target.

## Troubleshooting

Open **Tools → Log and Diagnostics…** and inspect Run-ahead support, active/verified
state, counters, and bounded state bytes. A saved enabled preference with an inactive
runtime normally means Sega CD is loaded or a mutually exclusive speed/rewind mode is
active. A nonzero determinism-failure count means the worker automatically disabled
speculation for the current session; normal authoritative emulation continues.

The required `integration.run_ahead` test uses generated CC0 Genesis and Sega CD
fixtures. It verifies state/video/audio/input equivalence, one-through-four-frame
limits, recording isolation, fast/slow/rewind transitions, unsupported-disc fallback,
120-frame maximum-depth stress, and stable rollback allocation.
It also directly mutates and restores the Team Player's private handshake sequence so
multitap support cannot pass merely because portable save states omit that protocol
counter.
