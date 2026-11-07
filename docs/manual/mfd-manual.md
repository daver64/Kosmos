# MFD Operations Manual

Every multi-function display mode in Kosmos, with step-by-step guides for the [Transfer](#transfer-howto) and [OrbitComp](#orbitcomp-howto) orbital planners. Back to the [Flight manual](manual.md#mfds).

<a id="using"></a>

## Using the MFDs

A ship's panel carries two multi-function displays (MFDs), left and right. Each has six buttons down its left edge (buttons 1–6, top to bottom) and six down its right edge (7–12). Button labels change with the mode; a blank label means the button does nothing in that mode.

Key| Action  
---|---  
`F5`| Cycle the MFD layout (both, left only, right only, hidden)  
`F6` / `F7`| Next mode on the left / right display  
`F8` / `F9`| Power the left / right display on or off  
  
The see-through (heads-up) displays and the framed panels also have an **MNU** button that opens the mode list (long lists page with PREV / NEXT, and reopening the list returns to the page you were last on; the heads-up list shows short codes such as ENG, ALT and RCS) and a **close** button (a cross) that hides that display. `F5` brings it back.

In a virtual cockpit, click the buttons with the mouse; hovering shows a label. Launch options: `--mfd 0|1|2`, `--mfd-left ID`, `--mfd-right ID`.

Many modes work on a **target**. Set one with **TGT+** / **TGT-** (they cycle through the other ships, then the bodies). In OrbitComp the list is shorter: the central star, the planets and the quantum portal, plus the moons, stations and ships that orbit the same body you do. The target is shared by every MFD, so you can pick it once on Align Planes and read it on Transfer.

<a id="availability"></a>

## Which ships get which

  * **Spacecraft** get every general mode. Aircraft-style cockpits are filtered to atmosphere modes (the six-pack, VOR, HSI, ADF, COM/NAV, Surface, Map, Systems) unless the ship sets `mfd_filter: "none"`, as the Shuttle-A does so that it can use Transfer and OrbitComp.
  * A ship can hide individual modes with `cockpit.mfd_hide`. The Deltaglider hides the aircraft six-pack in its VC; the Shuttle-A hides the Deltaglider-specific AoA, slip and slope modes and the ICBM targeting display. Ship-specific modes (Deltaglider AoA, slip and slope; ICBM targeting; Shuttle-A levels) are offered only on their own ships.
  * Transfer and OrbitComp are _space_ modes: they only appear for ships that can fly orbits, and they show "No orbit" when you are landed or have no orbit around a body.



<a id="transfer-howto"></a>

## Transfer: step by step

The Transfer MFD plans a _Hohmann transfer_ : two burns that take you from your circular orbit to the orbit of a target around the same body. It assumes both orbits are near-circular and in nearly the same plane. For eccentric orbits, or when the planes differ, use [OrbitComp](#orbitcomp-howto).

![The Transfer display with the Moon as target. The diagram shows your orbit \(green\) with you as the white dot, and the target's orbit \(orange\).](img/mfd/transfer.png)The Transfer display with the Moon as target. The diagram shows your orbit (green) with you as the white dot, and the target's orbit (orange).

  1. **Get into a stable, near-circular orbit** around the body you share with the target (for example, a low Earth orbit when going to a station).
  2. **Display Transfer** on an MFD (`F6`/`F7` until its name shows).
  3. **Select the target** with **TGT+** / **TGT-**. Until you do it reads "No target". If it says "Target not in this orbit's frame", the target is not orbiting the body you orbit, so you cannot use a Hohmann transfer to it directly.
  4. **Check the plane.** Switch the other MFD to [Align Planes](#m-align). If the relative inclination is more than a fraction of a degree, burn normal/anti-normal at the AN or DN first (see [below](#workflow)).
  5. **Read the numbers.** Transfer shows Burn 1 and Burn 2 (m/s), the Total, the flight time, the _Needed phase_ (how far ahead of you the target must be when you burn) against the _Current phase_ , and _Next window in_. The diagram shows your orbit with you at the right, and the target as an orange dot at its current phase angle.
  6. **Wait for the window.** Time-warp (`T`) until "Next window in" is a minute or two; the Current phase approaches the Needed phase. Drop to 1x before the burn.
  7. **Read the diagram.** The body you orbit is at the centre. The green line is your actual orbit (a circle, or the transfer ellipse once Burn 1 has put you on it) with a white dot for you, the orange circle is the target's orbit with a dot for the target, and, while you are still in a near circular orbit, the yellow ellipse is the planned transfer. The hollow orange ring is where the target needs to be when you burn. When the two orbits differ greatly in size the radii are compressed, so the picture is not to scale. OrbitComp's PLN page draws this diagram for Hohmann plans; for eccentric or inclined same-frame orbits it instead gives a Lambert burn vector and flight time.
  8. **Burn 1 on autopilot (optional).** Press **AUTO** (button 3). The ship holds prograde, slows time warp as the window nears, ignites so the burn is centred on the window and stops when your apoapsis reaches the target's orbit. The display shows _ignition in_ , then _BURNING dV LEFT_ with a progress bar. **ABORT** (button 4) cancels and cuts the throttle. Real burns take time, so the autopilot finishes on the apoapsis rather than on the planned delta-v. It does not fly Burn 2, and it does not fix plane errors: expect to trim the approach yourself.
  9. **Burn 1 by hand.** Turn to _prograde_ (`[`) and burn the Burn 1 delta-v. Your apoapsis rises to the target's altitude.
  10. **Coast** for the flight time (warp is fine; you are on rails).
  11. **Burn 2.** At apoapsis turn to prograde and burn the Burn 2 delta-v to circularise. Use [Sync Orbit](#m-sync) and [Docking](#m-docking) for the final approach.



Accuracy depends on how circular your orbit and the target's are. Correct leftover error with a small burn when you arrive, using [OrbitComp](#m-orbitcomp) to see the closest approach.

<a id="orbitcomp-howto"></a>

## OrbitComp: step by step

OrbitComp is the orbital computer. It has four pages, picked with the left buttons: **ORB** (1), **MAN** (2), **PLN** (3) and **HEL** (6). Buttons 4 and 5 are **TGT+** and **TGT-** on every page.

### A. Reading your orbit (ORB)

![The ORB page: Pe and Ap markers, your position \(white dot\), the target's orbit and the closest approach.](img/mfd/orbitcomp-orb.png)The ORB page: Pe and Ap markers, your position (white dot), the target's orbit and the closest approach.

  1. Press **ORB**. The body is drawn in blue, your orbit in green with _Pe_ and _Ap_ markers, and your position as a white dot.
  2. With a target set, its orbit is drawn in orange, and a red marker shows the _closest approach_ between the two orbits. The text gives the time to it ("Cls distance in time").
  3. Text values: period _T_ , time to periapsis _Tpe_ and apoapsis _Tap_ , speed, altitude, relative inclination, PeA/ApA, eccentricity, inclination, LAN and argument of periapsis.
  4. Right buttons: **ZOOM+** , **ZOOM-** and **FIT** (reset zoom).



### B. Planning a transfer to another body (PLN)

![The PLN page with Mars as target: ejection delta-v, v∞, transfer time, phases and the window, with the ejection diagram below.](img/mfd/orbitcomp-pln.png)The PLN page with Mars as target: ejection delta-v, v∞, transfer time, phases and the window, with the ejection diagram below.

  1. Press **TGT+** until the destination is named. ("No target" means none is set.)
  2. Press **PLN**. 
     * If both orbits are near-circular and coplanar, PLN shows a **Hohmann plan** : Burn 1, Burn 2, Total, flight time, phase now against needed, and _Burn 1 in_. For eccentric or inclined orbits in the same frame, it instead computes a **Lambert intercept** from the live states, showing the prograde/radial/normal burn vector and flight time. Press **TO MAN** to load that vector; its AUTO button is disabled. The Lambert plan uses two-body propagation and does not include arrival capture.
     * If the target is another planet around the same star, PLN shows an **ejection plan** : ejection delta-v, hyperbolic excess speed v∞, C3, transfer time, phases, _Window in_ , _Ejection burn in_ , and an _ejection angle_ from prograde/retrograde. The diagram shows the planet's direction of travel upward and a yellow dot where you must burn.
     * For a return to the parent body, the _target periapsis_ is a design goal, not the predicted arrival. Load the plan into MAN and check the predicted encounter; correct the flight as needed.
     * "Needs a bound orbit" means you are on an escape trajectory already; "No transfer plan for this target" means the pair has no supported plan.
  3. **Fast transfer (ejections).** The default plan searches approximate Lambert transfers and falls back to a circular Hohmann estimate if the search fails. Button **FAST** (10) cycles **FLIGHT** , **DEPART** , **BEST** and back to the default plan. In FLIGHT, **+** / **-** (11, 12) change the flight time by 10 days; in DEPART they change the departure date by 1 day; in BEST, **FIND** (11) searches the next synodic period for the departure date and flight time with the lowest departure speed, for a flight no longer than the one set. PLN solves a Lambert arc about the star from the planet at departure to the target at arrival and shows the ejection dV, v∞, C3, the arrival v∞ and the arc. Press **TO MAN** and read _Close_ on the MAN page, which draws the Sun-centred arrival: the white departure path, the target's orbit and where you and the target are at closest approach. When the destination's sphere of influence is entered, the predicted position follows a different conic from the white path. Adjust Pro, Rad, Nrm or Time and watch the encounter change. See the [complete tutorial](#oc-tutorial).
  4. If the active stage cannot supply the plan's dV, PLN shows a red **NOT ENOUGH dV** line (need against stage) and AUTO will not arm: the burn would run dry and the transfer fall short. Outer-planet departures need several km/s.
  5. Press **TO MAN** (button 7). The plan is loaded into a manoeuvre node and the MAN page opens. For Hohmann and ejection plans, press **AUTO** (button 8) to have the burn autopilot fly the plan.



### Worked example: ejection from Earth orbit to Jupiter

Start in orbit about Earth, for example the Deltaglider undocked in the Orbital Stations scenario.

  1. **Show the displays.** Press `F5` until the MFDs appear (dashboard panels or heads-up). `F8` / `F9` toggle power on the left / right display.
  2. **Select OrbitComp.** Press `F6` (left) or `F7` (right) until the title reads OrbitComp, or press the display's **MNU** button and pick it from the list.
  3. **Open the PLN page.** The left buttons are ORB, MAN, PLN, TGT+, TGT-, HEL from the top. Press **PLN** ; the header reads `ORBITCOMP PLN`.
  4. **Target Jupiter.** Press **TGT+** (or TGT-) until the line reads `Target: Jupiter`. Planets come after the ships in the cycle, so with many vessels it takes a number of presses. A ship around another planet, such as `ISS (Jupiter)`, is not a valid ejection target: PLN says "No transfer plan for this target", so keep pressing past it to the planet itself.
  5. **Read the plan.** PLN shows "Ejection to an outer planet", the ejection delta-v, v∞ and C3, transfer time, the phase now against the phase needed, _Window in_ , _Ejection burn in_ and the ejection angle from prograde. The estimated delta-v and wait depend on the date and orbit.
  6. **Optional: TO MAN.** Press the top right button (**TO MAN**) to load the burn into a manoeuvre node and open MAN, where you can fine-tune it.
  7. **Let it fly the burn (optional).** Press **AUTO** (button 8) on the PLN page. The burn autopilot holds prograde, waits for the ignition, burns, and stops at the planned speed; the page shows `AUTO armed` and then `AUTO burning`. **ABORT** (button 9) disarms it and cuts the throttle. Keep PLN showing while it waits, so the start time follows the live plan.
  8. **Watch the burn left.** Once you leave a bound orbit PLN switches to an escape readout: target v∞, your v∞ now, dV left and burn left, then "Escape speed reached - cut the engines".
  9. **Skip to the burn.** With OrbitComp still showing PLN or MAN, choose _File → Set time to T-5:00 before burn_. The clock steps forward while re-planning; check the resulting countdown and predicted encounter before flying the burn.



### C. Building and checking a burn (MAN)

![The MAN page with 250 m/s prograde at T+10 min: the node, the orbit after the burn and the close-approach distance.](img/mfd/orbitcomp-man.png)The MAN page with 250 m/s prograde at T+10 min: the node, the orbit after the burn and the close-approach distance.

A manoeuvre node is a planned burn at a chosen time, described by three delta-v components (m/s): _Pro_ (prograde), _Rad_ (radial) and _Nrm_ (normal), plus a time. The page predicts what your orbit will be afterwards. If it escapes the current body's sphere of influence and the target is its parent or a sibling body, MAN patches the trajectory into the parent's frame; from 0.50 it also patches into a sibling destination's sphere of influence. This is a two-body patched-conic estimate, not a full n-body or finite-duration burn simulation. It does not predict the effect of optional nonspherical gravity, so monitor the actual flight and correct as needed.

  1. Press **MAN**.
  2. Choose _when_ with **NODE** (button 11), which cycles: **T+** (a fixed time from now), **Pe** (next periapsis), **Ap** (next apoapsis), **AN** / **DN** (next ascending / descending node against the target; these need a target).
  3. Press **FIELD** (button 7) to pick what to edit: Pro, Rad, Nrm, or Time. The active field is marked `>` and shown in yellow. (Time only matters in T+ mode; editing it switches to T+.)
  4. Press **STEP** (button 10) to choose the increment: 0.1, 1, 10, 100 or 1000 m/s, or 1, 10, 60, 600 or 3600 s when the Time field is active.
  5. Press **+** / **-** to change the field by one step. **ZERO** clears the node (and resets the time to 600 s).
  6. Read the results: total dV, _Burn time_ , _Ignite T-_ (the burn is centred on the node, so ignition is half the burn time before it), the orbit _After burn_ (PeA, ApA, Ecc, Inc, and v∞ if it escapes) and _Close dist in time_ , the closest approach to the target after the burn. The plot shows your current orbit dim, the post-burn orbit white, the target orange and the node as a marker.
  7. Tune until the after-burn orbit is what you want. For a rendezvous, adjust until the close distance is small.



### D. Flying the burn

  1. Time-warp (`T`) until _Ignite T-_ is under a minute, then drop to 1x. (You cannot burn on rails.)
  2. Point the ship along the burn: with a pure prograde burn, press `[`; for normal use `;`; use RCS Attitude for radial.
  3. At _Ignite T-0_ apply full thrust and cut the engine when the dV remaining reaches zero (the Burn time on screen is your guide). Because the node is re-evaluated against your current orbit, the numbers shrink as you burn.
  4. Press **ORB** to confirm the new orbit and the closest approach, then plan a correction if needed.



### E. Escaping to the Sun (HEL)

The HEL page shows the _heliocentric_ orbit you would follow after leaving the planet's sphere of influence. It needs an unbound (escape) orbit, for example after an ejection burn; otherwise it shows "Bound to X - no escape orbit". It draws the orbit around the Sun and gives its periapsis and apoapsis in AU and v∞. Use **ZOOM+** , **ZOOM-** and **FIT** to frame it. Preview an ejection by building the node on MAN and checking HEL before you commit.

<a id="oc-tutorial"></a>

## OrbitComp: a complete tutorial for beginners

This assumes you have never planned a flight between planets. It takes you from an empty orbit around Earth to a transfer that reaches Jupiter. Read the short glossary first; every term is used on the screen.

### What the words mean

  * **Delta-v (dV)** : how much your speed has to change, in metres per second. It is the price of a manoeuvre. Your ship has a limited amount (its _stage dV_); if the plan needs more, you cannot fly it. OrbitComp warns you in red: `NOT ENOUGH dV`.
  * **Prograde / retrograde, radial, normal** : the three directions a burn can point. Prograde is the way you are travelling, and burning that way raises the opposite side of your orbit. Radial points away from the planet, normal is sideways out of your orbit plane.
  * **Transfer window** : a period when the planets are suitably placed for a transfer. _Window in_ is the planner's estimate of a departure time, not a guarantee of an intercept.
  * **Sphere of influence (SOI)** : the region where one body's gravity rules. Leaving Earth's SOI, you then orbit the Sun.
  * **v∞ (v infinity)** : the speed you still have, relative to the planet, after escaping it. It decides how much energy the transfer needs. **C3** is v∞ squared (km²/s²).
  * **Closest approach** : the smallest distance between you and the target. _Close_ on MAN is the one that matters: it is how far you would miss by.



### The big picture

  1. PLN works out _what_ burn to make and _when_.
  2. MAN lets you check where that burn takes you, and adjust it.
  3. You skip time to the burn and fly it (or let AUTO).
  4. After the burn, ORB and HEL show your new path, and you correct it on the way.



### Part 1: set up

  1. Start a ship in orbit, for example `--ship "CVEL Deltaglider" --orbit` or the Orbital Stations scenario. For Jupiter you need a ship with a lot of dV (a nuclear Shuttle-A, not the Deltaglider).
  2. Press `F5` until the MFDs show. Press `F6` (left) until the title is `OrbitComp`.
  3. Press **TGT+** (button 4) until `Target: Jupiter` appears. Ships come first, then the planets.
  4. Press **PLN** (button 3). The page title now reads `ORBITCOMP PLN Earth`.



### Part 2: the default ejection plan

  1. PLN searches approximate Lambert transfers over propagated planetary orbits and shows _Ejection to Jupiter_. Read _Ejection dV_ , _Flight_ and _Window in_. If the search fails, the page falls back to a circular Hohmann estimate. A red NOT ENOUGH dV line means your stage cannot fly the plan.
  2. Read the ejection burn countdown and the plane offset. Use **TO MAN** to inspect the predicted encounter; expect to refine the node or correct the trajectory.
  3. If necessary, choose _File → Set time to T-5:00 before burn_ to approach the planned burn. Verify the countdown afterwards; the planner and time skip are approximate.
  4. Press **AUTO** (button 8) with PLN showing, or press **TO MAN** and fly the node by hand (Part 5). Check the direction and predicted encounter rather than assuming a pure prograde burn.



### Part 3: a faster transfer with FAST

Real probes such as Voyager 2 did not take the slow plan; they left earlier and flew faster, at a price in dV. FAST lets you choose.

  1. On PLN, press **FAST** (button 10). The label becomes **FLIGHT** and the page now reads _Fast ejection to Jupiter_ , with a white arc about the Sun between Earth (blue dot) and Jupiter (orange dot).
  2. Press **+** or **-** (buttons 11 and 12) to change _Flight_ in 10-day steps. A shorter flight raises the dV and v∞; the arc gets straighter.
  3. Press **FAST** again: the label becomes **DEPART**. Now **+** / **-** move _Depart in_ a day at a time.
  4. Press **FAST** a third time: **BEST**. Set the longest flight you will accept first (in FLIGHT), then in BEST press **FIND** (button 11). OrbitComp searches the coming window and sets _Depart in_ and _Flight_ to the cheapest departure it finds. Read the new dV, C3 and arrival v∞.
  5. Press **FAST** once more to return to the default ejection plan.



If the dV is too high, lengthen the flight and press FIND again.

### Part 4: checking where you will arrive (MAN)

  1. With the fast plan on screen, press **TO MAN** (button 7). The MAN page opens with the burn loaded as a node.
  2. Read _Close … in … (SOI patched)_. That is the distance by which you would miss Jupiter and when. Millions of kilometres is normal at this stage.
  3. The plot is now the Sun's view: the blue and orange loops are Earth's and Jupiter's orbits, the white curve is your path, and the line joins your position and the target's at closest approach.
  4. Press **FIELD** (button 7) to pick _Pro_ , _Rad_ , _Nrm_ or _Time_ ; press **STEP** (10) to pick the size; press **+** / **-**. Each press moves your path, and the miss distance changes. Keep nudging in the direction that makes _Close_ smaller. _Time_ moves the burn earlier or later; _Nrm_ tilts the path out of the plane to fix a sideways miss.
  5. Also watch the _dV_ and _Burn_ lines. A plan whose dV exceeds your stage will not work.



### Part 5: flying the burn

  1. Choose _File → Set time to T-5:00 before burn_. If the lead time is shorter than five minutes, it is raised to five; you can lengthen it in _File → Transfer windows_.
  2. Point at the burn direction (prograde: `[`). Use **AUTO** if you want the computer to hold attitude, ignite and cut off; it needs PLN on screen. **ABORT** (button 9) stops it.
  3. Burn at full throttle until _Burn left_ reaches zero. Leave time warp at 1x.
  4. After the cut-off, press **HEL** (6): the Sun-centred orbit you now follow. Press **ORB** to see the closest approach to the target.



### Part 6: trimming on the way

Your first burn is never perfect. Days later, open MAN, set _NODE_ to T+ with a short time, and build a small correction while watching _Close_ fall. Small burns early are far cheaper than big ones late.

### If something looks wrong

  * _No target - TGT+ / TGT-_ : press TGT+ until a body is named.
  * _No transfer plan for this target_ : the target is a ship around another planet; keep pressing TGT+ to the planet itself.
  * _Needs a bound orbit_ : you are already escaping; use the escape readout and HEL.
  * _NOT ENOUGH dV_ : lengthen the flight, choose a nearer target, or use a ship with more fuel. AUTO refuses to arm.
  * _No flight found; raise FLIGHT_ : FIND could not make a flight in that time; increase it in FLIGHT.
  * _Orbit plane is N deg off_ : your orbit is tilted from the departure line; the arrival will miss by more, so trim with _Nrm_.



<a id="workflow"></a>

## Plane change, phasing, transfer

A complete rendezvous with a station in another plane uses these displays in order:

  1. [Align Planes](#m-align): read the relative inclination and the time to AN and DN. Burn normal or anti-normal at the node (OrbitComp: NODE → AN or DN, set Nrm) to bring it near zero.
  2. [Sync Orbit](#m-sync) (or Transfer's phase readout): learn whether you are ahead or behind, and the period ratio. A lower orbit is faster and catches up.
  3. [Transfer](#transfer-howto) or OrbitComp PLN: plan and fly the two Hohmann burns.
  4. OrbitComp ORB: check the closest approach; use MAN to fine-tune.
  5. Sync Orbit **APPR** : monitor relative speed, sideways drift and the short-range linear closest approach, then reduce relative speed.
  6. [Docking](#m-docking): close the last kilometres and dock.



## Mode reference

<a id="m-orbit"></a>

### Orbit

Draws your orbit in its own plane with periapsis (Pe) and apoapsis (Ap) markers, and lists period, time to Pe/Ap, orbital speed relative to the central body, altitude and orbital elements. The title shows the body whose sphere of influence you are in. No buttons.

![The Orbit display in a 400 km Earth orbit: Pe and Ap markers, period and altitude.](img/mfd/orbit.png)The Orbit display in a 400 km Earth orbit: Pe and Ap markers, period and altitude.

<a id="m-surface"></a>

### Surface

A surface-relative view: artificial horizon with pitch ladder, heading tape, altitude, speed, vertical speed, pitch, latitude and longitude. No buttons.

![The Surface display on the ground: horizon, heading tape, altitude and speed.](img/mfd/surface-air.png)The Surface display on the ground: horizon, heading tape, altitude and speed.

<a id="m-radalt"></a>

### Radar Altimeter

Shows estimated local ground clearance (AGL), vertical speed and horizontal ground speed. Clearance uses terrain height beneath the vessel and a nominal vessel-bottom offset; it is not a ray-cast radar. Without terrain data it uses the spherical surface. During descent, it warns if the current vertical speed would reach this clearance within 30 or 10 seconds. From 0.50, with terrain data it also samples a constant-speed ground track every two seconds for 30 seconds. Neither warning predicts stopping distance; terrain between samples, buildings, obstacles and changes in motion can be missed. It reports when terrain-ahead data is unavailable. No buttons.

<a id="m-map"></a>

### Map

A ground-track map of the body you are over, with your position, the orbit's predicted ground track, the predicted _impact_ point and time if your path meets the surface, and a selected base with distance and bearing.

![The Map display: ground track over the planet imagery, with the orbit and its numbers below.](img/mfd/map.png)The Map display: ground track over the planet imagery, with the orbit and its numbers below.

  * **BASE+** / **BASE-** (1, 2): cycle the selected base. **NONE** (3): clear it.
  * **TEXTURE** (7): toggle the planet imagery. **GRID** (8): toggle the lat/long grid.



<a id="m-attitude"></a>

### RCS Attitude

Shows the current SAS hold, pitch, heading, a compass rose and the throttle. The buttons select an attitude hold (and turn off the autopilot): **STAB** (1, kill rotation), **PRO** (2), **RETRO** (3), **RAD+** (4, away from the body), **RAD-** (5), **NRM+** (7), **NRM-** (8) and **ATT** (9, hold the target attitude). Prograde and retrograde use orbital velocity relative to the central body, not the rotating ground.

![The RCS Attitude display: SAS hold buttons on the left, compass rose and throttle.](img/mfd/attitude.png)The RCS Attitude display: SAS hold buttons on the left, compass rose and throttle.

<a id="m-ascent"></a>

### Ascent Profile

Plots altitude and speed against time during a launch so you can judge your gravity turn. **RESET** (12) clears the plot.

![The Ascent Profile display forty seconds after liftoff: altitude and speed against time.](img/mfd/ascent.png)The Ascent Profile display forty seconds after liftoff: altitude and speed against time.

<a id="m-align"></a>

### Align Planes

Needs a target in the same orbit frame. Draws your orbit with the target plane's ascending (AN, green) and descending (DN, orange) nodes and your position, and lists the _relative inclination_ and the _time to AN_ and _DN_. A plane-change burn is cheapest at a node. Buttons: **TGT+** , **TGT-**.

![Align Planes with the Moon as target: relative inclination and the times to the AN and DN.](img/mfd/align.png)Align Planes with the Moon as target: relative inclination and the times to the AN and DN.

<a id="m-sync"></a>

### Sync Orbit

A read-out for rendezvous and phasing. It shows where the target is relative to you and works out the burns to meet it; it does not fly the ship. It needs a target orbiting the same body as you, and you must be in a bound orbit (not on the ground or on an escape trajectory). Otherwise it shows "No target" or "Target not in this orbit's frame".

**Choosing a target.** Press **TGT+** or **TGT-** (buttons 1 and 2) to step through the other ships and the bodies. You can also use `--target NAME` or the `/sim/target` property. The target name is shown at the top.

**The readings.**

  * _Range_ , _Rel. speed_ and _Closing_ : straight-line distance, relative velocity and the rate the distance is shrinking (negative means you are drawing apart).
  * _Diagram_ : seen from above your orbital plane, with the body you orbit as the blue dot in the middle. The green line is your actual orbit (a circle, or an ellipse once you are on a transfer or phasing orbit) and the orange circle is the target's orbit. The white dot is you (labelled, and always drawn on the right); the orange dot is the target. The angle between the two lines is the phase angle. When the orbits differ a lot in size the radii are compressed so both fit, so it is not to scale.
  * _Phase_ : how far the target leads you along your orbit, in degrees. Positive means it is ahead, negative means it is behind. Zero means you are side by side.
  * _Tship/Ttgt_ : your orbital period divided by the target's. Below 1 you are the faster one and gain on it; above 1 you fall behind.
  * _Rel. inc_ : the angle between the two orbital planes. Near zero is needed before a rendezvous; fix it first with [Align Planes](#m-align).



**The burn plan.** Under the range readings the display works out a plan, assuming both orbits are near circular and in the same plane:

  * _Same orbit_ (heights within 2%): a _phasing_ plan. Burn 1 is **now** , prograde or retrograde by the dV shown, which puts you in a slightly higher or lower orbit with a different period. After the time shown ("after burn 1") you are back at the same spot beside the target; burn the same dV the opposite way (Burn 2) to return to the target's orbit. The display shows the total dV. **ORB+** / **ORB-** (3, 4) choose how many orbits to take: more orbits cost less dV but take longer. Orbit counts that would dip below the surface are skipped automatically.
  * _Different orbit height_ : a Hohmann transfer. "Burn 1 in" counts down to the moment the phase is right, then shows both dVs and the flight time, like the Transfer display.
  * _Once you are in an eccentric phasing orbit_ : it shows "Match orbit at apoapsis/periapsis", a countdown to that point and the dV to circularise at the target's height. Burn there, then check _Tship/Ttgt_ returns to 1.000 and the phase is near zero.



Use time warp for the countdowns, and turn the ship to prograde/retrograde (see the Attitude display) before the burn. These are first estimates for circular orbits; finish with small corrections and the Docking display.

**Step by step: catching a station in the same orbit.**

  1. Select the station with TGT+. Check _Rel. inc_ is under about 0.5°; if not, use [Align Planes](#m-align) and burn at the node.
  2. Read the plan. Turn to the burn direction and do Burn 1 for the dV shown (use the Attitude display to hold prograde or retrograde).
  3. Coast, warping if you like, and watch _Phase_ shrink towards zero.
  4. When the countdown ends, do Burn 2 so _Tship/Ttgt_ returns to 1.000.
  5. Switch to [Docking](#m-docking) once the range is under a few kilometres.



**Short-range approach (0.50 and later).** Press **APPR** (5) for range, relative speed, closing speed and sideways speed to a target ship. When closing, it estimates the closest distance and time by extending the current relative velocity in a straight line. The estimate is available only in the same sphere of influence, within 100 km, and within five minutes; it is not an orbital prediction or a collision guarantee, and gravity or any burn can change it. Negative closing speed means the ships are separating. **RVEL** (7) selects target-relative retrograde attitude hold, pointing opposite your velocity relative to the target. It does not fire the engines: burn gently and cut thrust before the relative speed reaches zero. The hold needs a target ship in the same sphere of influence, at least 0.1 m/s relative speed and RCS enabled, and cannot be selected while an autopilot is engaged. If the target becomes invalid or too slow, it falls back to damping without cutting thrust. Switch to Docking for port alignment; press APPR again to return to the orbit page.

Buttons: **TGT+** , **TGT-** , **ORB+** , **ORB-** , **APPR** , **RVEL**.

<a id="m-transfer"></a>

### Transfer

Hohmann transfer calculator. See [step by step](#transfer-howto). Buttons: **TGT+** , **TGT-**. While it shows a plan, _File → Set time to T-5:00 before burn_ skips the clock to just before the burn it displays.

![Transfer with the Moon as target: the two burns, flight time, phase angles and the next window.](img/mfd/transfer.png)Transfer with the Moon as target: the two burns, flight time, phase angles and the next window.

<a id="m-orbitcomp"></a>

### OrbitComp

The orbital computer. See [step by step](#orbitcomp-howto). _File → Set time to T-5:00 before burn_ skips to the burn shown on MAN (when it has a burn time) or PLN. Buttons: 1 **ORB** , 2 **MAN** , 3 **PLN** , 4 **TGT+** , 5 **TGT-** , 6 **HEL** ; on the right ORB/HEL: **ZOOM+** , **ZOOM-** , **FIT** ; MAN: **FIELD** , **+** , **-** , **STEP** , **NODE** , **ZERO** ; PLN: **TO MAN** , **AUTO** , **ABORT** and, for ejections, **FAST** (FLIGHT, DEPART, BEST) with **+** / **-** or **FIND**. A red **NOT ENOUGH dV** line means the active stage cannot fly the plan. Scripts and `--set` can drive it: `/orbitcomp/fast` (1 FLIGHT, 2 DEPART, 3 BEST), `/orbitcomp/flight-days`, `/orbitcomp/depart-days` and `/orbitcomp/find` (1 runs the search).

![OrbitComp on its ORB page with a target set: your orbit in green, the target's in blue, the closest approach marked in red.](img/mfd/orbitcomp-orb.png)OrbitComp on its ORB page with a target set: your orbit in green, the target's in blue, the closest approach marked in red.

<a id="m-docking"></a>

### Docking

A docking-port camera display for ships with a docking port.

  1. Press **RCV** (1) to choose which NAV receiver listens (the top-left shows `NAV1`/`NAV2` and its frequency).
  2. Press **PRT** (2) to pick which of your ports to use; the port name is at the top right ("in use" if occupied).
  3. Press **TUNE** (3) to tune the receiver to the nearest docking beacon (IDS). "No IDS signal" means none is in range.
  4. Fly in using the display: the crosshair is the target port. Keep the offset and lateral velocity near zero; align yaw, pitch and roll (values turn green when within limits: offset under 0.5 m, angles under 5°, roll under 10°). Watch _Range_ and _Closing_ speed and keep the closing speed slow.
  5. Contact docks the ships. The display then shows "DOCKED" and the partner's name. **UNDK** (4) releases them.



<a id="m-comnav"></a>

### COM/NAV

Lists the transmitters in range (ident, frequency, distance in km) and the two NAV receivers' frequencies. **NAV1** / **NAV2** (1, 2) choose the receiver to tune; **F+** / **F-** (3, 4) step 0.05 MHz; **F++** / **F--** (5, 6) step 1 MHz. The numbered buttons 7–12 tune to the corresponding entry of the list. Frequencies below 1 are NDB beacons shown in kHz.

![COM/NAV listing the transmitters in range.](img/mfd/comnav.png)COM/NAV listing the transmitters in range.

<a id="m-vor"></a>

### VOR/ILS

Two course-deviation indicators, one per NAV receiver, showing the beacon ident, distance, bearing, radial, closing speed and a TO/FROM or ILS localiser/glideslope needle. **OBS1+** / **OBS1-** and **OBS2+** / **OBS2-** turn each receiver's omni-bearing selector by 1°.

![VOR/ILS: one course-deviation indicator per NAV receiver.](img/mfd/vor.png)VOR/ILS: one course-deviation indicator per NAV receiver.

<a id="m-hsi"></a>

### HSI

Horizontal situation indicator: compass card with course pointer and deviation bar for the chosen receiver. **RCV** (1) switches NAV1/NAV2, **OBS+** / **OBS-** (2, 3) change the course by 1°, **+10** / **-10** (10, 11) by 10°. Tune with COM/NAV. Shows "No signal" if the receiver has nothing.

![The HSI before a beacon is tuned.](img/mfd/hsi.png)The HSI before a beacon is tuned.

<a id="m-vtol"></a>

### VTOL

A top-down landing aid for VTOL beacons: your position relative to the pad, height, vertical speed, distance to the pad and ground speed. **RCV** (1) selects the receiver, **ZOOM+** / **ZOOM-** (2, 3) change the scale. Tune a VTOL beacon frequency in COM/NAV first.

![The VTOL display, waiting for a VTOL beacon.](img/mfd/vtol.png)The VTOL display, waiting for a VTOL beacon.

<a id="m-adf"></a>

### ADF

An automatic direction finder: the needle points at the NDB tuned on each receiver, with the ident, distance and bearing. NDB frequencies are 190–535 kHz. Buttons: **NAV1 +1** , **-1** , **+10** , **-10** (1–4) and the same for NAV2 (5–8), in kHz.

![ADF: two needles, one per ADF receiver.](img/mfd/adf.png)ADF: two needles, one per ADF receiver.

<a id="m-systems"></a>

### Systems

Electrical bus (powered or brown-out, supply and demand watts) and each pressure vessel's name, hazard state, pressure, O2 and CO2 partial pressures, health and leak rate, with alarms at the bottom. **LEAK5** (1) opens a 5 cm² breach in the first vessel, for drills. **LOADS** (button 2) opens a page listing every switchable load (instruments, heaters, computers) with its power draw and state (ON, OFF, or SHED when the bus cannot feed it). **UP** and **DOWN** move the cursor, **TOGGLE** switches the selected load on or off, **SHED** switches off every load except the essential ones (priority 0), and **ALL ON** restores them all; **SYS** goes back. Both pages show how long the battery lasts at the present drain, so a battery-powered probe can shed instruments before a long coast. Loads can also be switched in the Ship systems window (F1).

![Systems on a space station: electrical bus, battery, gas stores and the habitat's pressure and air quality.](img/mfd/systems.png)Systems on a space station: electrical bus, battery, gas stores and the habitat's pressure and air quality.

<a id="m-fuel"></a>

### Fuel

Propellant of every stage still on the ship (the active one highlighted), RCS propellant, total, burn rate and endurance. When docked it shows what the partners hold, and the side buttons set fuel transfer: **AUTO** (tank pods top up what they are docked to), **OFF** , **GIVE** (flown ship to its partners) or **TAKE**. Same as Fuel transfer in the Edit menu.

![Fuel: stage tanks, totals and endurance.](img/mfd/fuel.png)Fuel on a Delta IV Heavy climbing out.

<a id="m-sixpack"></a>

### Aircraft instruments

Attitude Indicator, Airspeed, Vertical Speed, Heading, Turn Coordinator, Altimeter and **Engine** (fuel flow, endurance, thrust and TWR, mass and electrical bus). These are the classic six-pack and have no buttons. They are filtered to atmospheric flight; ships can hide them with `mfd_hide`.

![The Attitude Indicator.](img/mfd/adi.png)The Attitude Indicator. ![The Airspeed indicator.](img/mfd/asi.png)The Airspeed indicator. ![The Altimeter.](img/mfd/altimeter.png)The Altimeter. ![The Vertical Speed indicator.](img/mfd/vsi.png)The Vertical Speed indicator. ![The Heading indicator.](img/mfd/dg.png)The Heading indicator. ![The Turn Coordinator.](img/mfd/turn.png)The Turn Coordinator. ![The Engine display.](img/mfd/engine.png)The Engine display.

<a id="m-ship"></a>

### Ship-specific modes

  * **Ares Ascent** , **Deltaglider Ascent** and **Shuttle-A Ascent** : experimental ascent controllers for the Ares on Mars and Deltaglider and Shuttle-A variants on Earth. Set target and heading with **ALT+/-** and **HDG+/-** , then **START** from rest; **STOP** cancels. Monitor the flight and take over if needed. See the [flight manual](manual.md#autopilot) for restrictions and cancellation controls.
  * **DG Angle of Attack** , **DG Slip Angle** and **DG Slope** (Deltaglider): angle of attack, sideslip and the local ground slope. No buttons. They are not selectable in the Deltaglider's virtual cockpit.
  * **Engine levels** (Shuttle-A): four gauges for the main-left, main-right, hover-forward and hover-aft engines.
  * **ICBM Targeting** (CVEL ICBM): **LAT+/-** , **LON+/-** (1–4) set the target; **STEP** (5) its size; **BURST** (6) toggles air or ground burst; **YIELD+/-** (7, 8); **HGT+/-** (9, 10) the burst height; **ARM** (11) and **LAUNCH** (12). It shows range, whether the target is in range and the status.



<a id="scripting"></a>

## Scripting

Select modes with `--mfd-left ID` / `--mfd-right ID`, or drive OrbitComp from a script via the property tree: `/orbitcomp/page` (1–4), `/orbitcomp/node-mode`, `/orbitcomp/time`, `/orbitcomp/prograde`, `/orbitcomp/radial`, `/orbitcomp/normal`. Each is applied once; `page` then resets to 0. New modes are Lua addons; see the [Modders manual](modding.md).
