# KOSMOS — Cinematic Demonstration Reel

## Your assignment

Create a professional, cinematic 90-second demonstration reel for the Kosmos space flight simulator.

The finished video will be used on the Kosmos website, GitHub project page, YouTube and social media to introduce the simulator to potential users, developers and contributors.

**Creative title:** KOSMOS — Beyond the Horizon

**Tagline:** The Universe Is Yours to Explore.

The objective is to demonstrate the capabilities, scale, realism and atmosphere of Kosmos through a visually impressive journey from Earth to interstellar space.

This should look like a cinematic trailer for a serious space flight simulator, rather than a conventional game advertisement.

## 1. Inspect the existing codebase

Before implementing anything, examine the Kosmos source code and existing resources.

Identify:

- Camera systems, including external, chase, cockpit and free cameras.
- Spacecraft and vessel definitions.
- Launch facilities and planetary environments.
- Orbital mechanics and spacecraft control.
- Time acceleration and simulation stepping.
- Existing cinematic or automated camera functionality.
- Rendering and framebuffer capture capabilities.
- Audio playback, synthesis and mixing facilities.
- Lua scripting and mission automation.
- Interstellar navigation and quantum jump systems.
- Existing video recording or screenshot facilities.

Also inspect the underlying simlib functionality where appropriate.

**Reuse existing functionality wherever possible.**

Do not create parallel systems when the existing engine can be extended cleanly.

Do not assume that particular vessels, locations, assets or APIs exist. Discover the actual implementations before designing each scene.

## 2. Technical implementation

I want the demonstration reel produced directly from Kosmos.

Ideally, implement a reusable cinematic recording system that can be driven by a script or timeline.

The system should support:

- Precisely timed camera positions and orientations.
- Smooth camera movement using spline interpolation.
- Camera tracking of moving spacecraft.
- Scene transitions.
- Automated vessel control where necessary.
- Simulation time control.
- Deterministic frame stepping where practical.
- High-resolution frame capture.
- Synchronisation of visual events with music.
- On-screen titles and text overlays.

Prefer an offline frame-rendering approach rather than relying on real-time screen recording.

For example, at 30 fps, a 90-second reel requires 2,700 frames.

Each frame should represent a precisely determined point on the cinematic timeline, regardless of how long it takes to render.

Be careful to distinguish between simulation time and presentation time. Orbital manoeuvres, launches and interplanetary journeys may require substantial simulation-time progression while occupying only seconds of video.

If existing simulation systems cannot support deterministic offline rendering without significant changes, implement the simplest reliable alternative and document the limitations.

Use FFmpeg to assemble the final video if appropriate.

Target output:

- Resolution: 1920 × 1080.
- Frame rate: 30 fps.
- Duration: approximately 90 seconds.
- Format: MP4, H.264.
- Audio: stereo AAC, 48 kHz.
- High-quality encoding suitable for YouTube.

Keep cinematic recording functionality separate from ordinary gameplay so it does not introduce unnecessary runtime overhead.

## 3. Creative direction

The video should progress from realistic spaceflight to increasingly ambitious exploration.

The opening should feel restrained, quiet and authentic.

As the video progresses, introduce greater scale, more dramatic spacecraft and increasingly impressive environments.

The final section should reveal Kosmos's interstellar capabilities.

Avoid excessive camera shake, artificial lens effects, rapid cuts or distracting transitions.

Prefer slow, elegant camera movements that allow viewers to appreciate the spacecraft and scenery.

The visual style should evoke the atmosphere of 2001: A Space Odyssey, Interstellar and high-quality aerospace documentaries.

Do not use copyrighted film footage or soundtrack material. NASA should have public domain sound we can use for launches, radio chatter, stage seperation, ignition. If NASA don't have them look elsewhere.

## 4. Scene-by-scene storyboard

### Scene 1 — The Beginning (00:00–00:12)

**Setting:** A launch facility on Earth.

Begin with a black screen.

Fade into an atmospheric shot of a spacecraft standing on its launch pad.

Use the best available launch facility and rocket assets.

Camera movement:

1. Establishing wide shot.
2. Slow lateral movement around the spacecraft.
3. Low-angle view emphasising its size.

Audio:

- Low atmospheric synthesizer drone.
- Subtle mechanical ambience.
- Distant wind.
- Sparse piano notes.

On-screen title:

"EVERY JOURNEY BEGINS SOMEWHERE"

Fade the title in and out gently.

### Scene 2 — Ascent (00:12–00:28)

**Setting:** Rocket launch and atmospheric ascent.

Show ignition, liftoff and ascent.

Suggested camera sequence:

1. Low-angle view at ignition.
2. Wide shot as the vehicle clears the pad.
3. Tracking camera during ascent.
4. Stage separation or another significant flight event.

Synchronise ignition with the first major musical impact.

Use actual Kosmos spacecraft propulsion and flight behaviour wherever possible.

Do not fake physical events by moving static models if the simulator can perform them.

Audio:

- Powerful engine ignition.
- Deep launch rumble.
- Rising orchestral strings.
- Low rhythmic percussion.

On-screen title:

"REAL PHYSICS. REAL CHALLENGES."

### Scene 3 — Earth Orbit (00:28–00:43)

**Setting:** Low Earth orbit.

Transition from atmospheric ascent to a spectacular view of Earth from orbit.

Show:

- A spacecraft silhouetted against Earth.
- Planetary curvature and atmospheric effects.
- A close-up of a cockpit or instrument display.
- A short orbital manoeuvre or rendezvous sequence.

This section should demonstrate that Kosmos is a genuine flight simulator rather than simply a spacecraft viewer.

If possible, briefly show real orbital information on an MFD or instrument panel.

Audio:

- Music becomes expansive.
- Introduce the principal melodic theme.
- Reduce engine sounds.
- Use restrained cockpit or RCS sounds where appropriate.

On-screen title:

"A SOLAR SYSTEM WITHOUT BOUNDARIES"

### Scene 4 — Other Worlds (00:43–00:57)

**Setting:** The Moon and Mars.

Show a sequence of exploration shots.

Suggested shots:

1. A spacecraft approaching the Moon.
2. A lunar lander descending towards the surface.
3. A dramatic surface-level view.
4. Transition to Mars.
5. A spacecraft approaching or descending towards the Martian surface.

Use existing Kosmos spacecraft, terrain and planetary assets.

Select shots that demonstrate the strongest current visual capabilities of the simulator.

Audio:

- Broader orchestral harmonies.
- Warm brass and strings.
- Subtle percussion.
- Appropriate spacecraft sounds.

On-screen title:

"EXPLORE WORLDS BEYOND OUR OWN"

### Scene 5 — Beyond the Solar System (00:57–01:15)

**Setting:** Interstellar space.

This is the visual and musical climax.

Demonstrate the capabilities that distinguish Kosmos from conventional orbital simulators.

Suggested sequence:

1. Spacecraft departing the solar system.
2. Interstellar navigation or star-map view.
3. Quantum jump or equivalent interstellar travel mechanism.
4. Arrival near another star.
5. Dramatic spacecraft flyby against an unfamiliar planetary system.

Use the actual interstellar travel implementation and available effects.

If a quantum jump already has an established visual effect, use it rather than inventing an incompatible replacement.

Synchronise the jump with the strongest musical accent.

Audio:

- Full orchestral and synthesizer arrangement.
- Rising musical tension.
- Powerful transition impact.
- Brief reduction in music immediately after arrival.

On-screen title:

"THOUSANDS OF STARS. COUNTLESS DESTINATIONS."

Use an exact star count only if verified against the current simulation data.

### Scene 6 — The Reveal (01:15–01:30)

**Setting:** A distant world.

Show a spacecraft silhouetted against an unfamiliar planet or star.

The camera should move slowly, creating a sense of scale and discovery.

Allow the music to resolve.

Fade to black.

Display:

KOSMOS

THE UNIVERSE IS YOURS TO EXPLORE

Then:

FREE SPACE FLIGHT SIMULATOR

WINDOWS & LINUX

salyut.ddns.net/kosmos

Use the existing Kosmos logo or branding if available.

End with a gentle musical fade.

## 5. Original soundtrack

Create an original soundtrack specifically for the reel.

Target style:

Cinematic orchestral music combined with atmospheric electronic synthesis.

Musical references for mood only:

- Interstellar — scale and emotional progression.
- Blade Runner — atmospheric electronic textures.
- Mass Effect — futuristic exploration.
- Classical space-documentary soundtracks.

Do not copy existing compositions, melodies or recordings.

Suggested musical characteristics:

- Approximately 88 BPM.
- D minor or another suitably dramatic key.
- Sparse opening piano motif.
- Deep synthesizer drones.
- Warm sustained string harmonies.
- French horn or brass-like melodic elements.
- Restrained cinematic percussion.
- Electronic arpeggios during interstellar travel.
- Strong final resolution.

The score should contain a recognisable melodic motif that develops as the video progresses.

Musical structure:

00:00–00:12 — Mystery and anticipation.

00:12–00:28 — Ignition, power and ascent. Use Saturn V and/or N2 and/or Buran for this

00:28–00:43 — Wonder and orbital flight. Use DeltaIV Heavy and/or Atlas and/or Deltaglider for this

00:43–00:57 — Discovery and exploration. Use ShuttleA and/or Dragonfly for this

00:57–01:15 — Dramatic interstellar climax. Use Argo for this

01:15–01:30 — Emotional resolution. Use deltaglider in earth orbit approaching ISS for this

Investigate whether simlib's existing audio synthesis facilities can produce suitable musical elements.

If practical, use them to create an original score.

Otherwise, implement a separate offline audio-generation pipeline using appropriate locally available tools.

Avoid adding unnecessary dependencies to Kosmos itself.

The soundtrack must be original and suitable for unrestricted distribution with the demonstration reel.

## 6. Sound design

Create a separate sound-effects layer.

Potential sounds:

- Launch pad ambience.
- Rocket ignition and engine rumble.
- Stage separation.
- RCS thrusters.
- Cockpit instrumentation.
- Mechanical operations.
- Quantum jump charging and activation.
- Low atmospheric spacecraft ambience.

Use existing Kosmos sounds where suitable.

Any new effects should be original or properly licensed.

Remember that sound in space should be presented cinematically rather than implying realistic external acoustic propagation through a vacuum.

Sound effects should complement the soundtrack without overwhelming it.

## 7. Cinematic camera system

Where necessary, implement a lightweight cinematic camera controller.

Desired features:

- Keyframed camera positions.
- Keyframed camera orientations.
- Catmull-Rom or similar spline interpolation.
- Quaternion orientation interpolation.
- Target tracking.
- Configurable field of view.
- Smooth easing.
- Support for cameras attached to moving reference frames.

Pay particular attention to reference frames.

A camera following a spacecraft in orbit should not accidentally interpolate through planetary coordinates in an inappropriate coordinate system.

Likewise, planetary and interstellar scenes may require different spatial scales and coordinate handling.

Prefer a data-driven timeline that can be edited without recompiling Kosmos.

Lua would be a good choice if the existing scripting system is suitable.

## 8. Video production pipeline

Create a repeatable build and rendering workflow.

Suggested stages:

1. Load the cinematic timeline.
2. Initialise the required simulation scenario.
3. Advance simulation and camera state.
4. Render the current cinematic frame.
5. Save the frame to disk.
6. Repeat until the sequence is complete.
7. Generate or load the soundtrack.
8. Mix sound effects.
9. Assemble the final video.
10. Verify output duration, resolution, frame rate and audio synchronisation.

Prefer a lossless or high-quality intermediate frame format.

Provide a command or script that produces the finished video.

Also provide a fast preview mode that renders at reduced resolution or frame rate.

This is important because full-quality offline rendering may take considerable time.

## 9. Implementation constraints

- Do not break normal Kosmos operation.
- Do not significantly restructure the engine unnecessarily.
- Reuse existing simlib functionality.
- Keep dependencies minimal.
- Follow the existing C++ coding conventions.
- Keep the cinematic system modular.
- Avoid hardcoded paths.
- Use existing assets wherever possible.
- Preserve compatibility with Linux and Windows where practical.
- Make the reel reproducible.
- Do not modify unrelated systems.
- Do not substitute fabricated footage for actual Kosmos rendering.

If a requested scene cannot be implemented using current functionality, identify the limitation and propose the closest technically sound alternative.

## 10. Deliverables

Produce:

1. A cinematic timeline or script.
2. Any necessary camera and recording functionality.
3. A reproducible video rendering pipeline.
4. An original musical score.
5. Appropriate sound effects and audio mixing.
6. A final 90-second MP4 video.
7. A lower-resolution preview video.
8. A short README explaining how to regenerate or modify the reel.

If appropriate, also export individual scene clips for future promotional use.

## 11. Development workflow

Work incrementally.

**Phase 1 — Reconnaissance**

Inspect the codebase, available assets, camera systems and recording capabilities.

Identify the actual spacecraft and environments to be used.

Produce a technical plan and final shot list based on what Kosmos can currently do.

**Phase 2 — Minimal proof of concept**

Implement one short cinematic shot.

Demonstrate automated camera movement and frame capture.

Produce a short test video.

**Phase 3 — Complete cinematic timeline**

Implement all six scenes, with working transitions and titles.

**Phase 4 — Audio**

Create the soundtrack and sound effects.

Synchronise significant visual events with musical cues.

**Phase 5 — Final production**

Render the complete video, encode it and verify the results.

Check for camera glitches, clipping, discontinuities, dropped frames, incorrect titles and audio synchronisation.

## 12. Important instructions

You are working on a real, existing space flight simulator.

The primary objective is to showcase what Kosmos actually does.

Do not create a generic science-fiction animation.

Do not invent capabilities that are not implemented.

Do not replace genuine simulation footage with external generated visuals.

Use the strongest existing features and assets to make Kosmos look as impressive as possible.

Where possible, arrange the scenes so the viewer experiences a coherent journey:

**Earth → Launch → Orbit → Moon → Mars → Interstellar Space → Distant World**

The finished reel should leave viewers with three impressions:

1. Kosmos is a serious and technically capable space flight simulator.
2. Kosmos offers an enormous universe to explore.
3. Kosmos is an ambitious and exciting project worth trying.

Begin by inspecting the source tree and producing the reconnaissance report and implementation plan.

Then proceed with the minimal cinematic recording proof of concept before attempting the full reel.

Do not claim the finished video is complete until it has actually been rendered and inspected.
