# Sound credits

## XRSound

The following files are byte-identical copies of sound effects in the XRSound 3.0
distribution for Orbiter. They are renamed as shown; the original files are in
`XRSound/Default/`. XRSound 3.0 is copyright (c) 2018-2021 Douglas Beachy and
its distribution is licensed under the MIT license. The original license and
dependency notice are included in [sounds/LICENSE-XRSound.txt](sounds/LICENSE-XRSound.txt).
Source: [XRSound 3.0 in the Orbiter repository](https://github.com/orbitersim/orbiter/tree/90f13a7c2269/Sound/XRSound/assets/XRSound).

| Kosmos file in `sounds/` | XRSound file in `Default/` |
|---|---|
| `main_engines.wav` | `Main Engines.wav` |
| `rcs_sustain.wav` | `RCS Sustain.wav` |
| `hover.wav` | `Hover.wav` |
| `touchdown.wav`, `crash.wav`, `contact.wav` | `Touchdown.wav`, `Crash.wav`, `Contact.wav` |
| `docking.wav`, `undocking.wav` | `Docking.wav`, `Undocking.wav` |
| `gear_up.wav`, `gear_down.wav` | `Gear Up.wav`, `Gear Down.wav` |
| `thump.wav`, `metal_crunch.wav` | `Thump.wav`, `Metal Crunch.wav` |
| `warning_beep.wav`, `warning_decompression_danger.wav` | `Warning Beep.wav`, `Warning Decompression Danger.wav` |
| `beephigh.wav`, `beeplow.wav` | `BeepHigh.wav`, `BeepLow.wav` |
| `switchon1.wav`, `switchoff1.wav` | `SwitchOn1.wav`, `SwitchOff1.wav` |
| `liftoff.wav`, `sonic_boom.wav` | `Liftoff.wav`, `Sonic Boom.wav` |
| `cabin/amb0.wav`–`amb6.wav`, `amb8.wav`–`amb12.wav` | `Cabin Ambience/`, same filenames |

## Synthesised sounds

Bomb and nuclear explosions, beam weapons and thunder are synthesised by Kosmos at runtime;
they do not use separately sourced samples.

Other sounds may be supplied by add-ons. If an effect file is missing, Kosmos falls back to
synthesised engine sounds or stays silent for that event.
