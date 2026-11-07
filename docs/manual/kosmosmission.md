# KosmosMission manual

KosmosMission is a mission planner (Linux and Windows). You choose where the mission starts, which ship flies it and what it carries, then add the legs one after another: ascent, orbit change, transfer to another body, capture. It keeps a running delta-v ledger against what the ship's stages can deliver and shows you the trajectory. The same transfer search is what OrbitComp uses in the game, so the numbers agree. A finished plan is saved as an ordinary scenario, exported as a text or CSV table, and any leg can be sent to OrbitComp's manoeuvre node in the running game.

![KosmosMission with a three-leg plan from Alcantara to Mars orbit: left, the start, ship and add-leg panels; right, the transfer drawn over Earth's and Mars's orbits and the ledger.](img/mission/mars-plan.png)A launch from Alcantara to a 300 km orbit of Mars with a Saturn V-23L: the plan on the left, the transfer (yellow) between Earth's (green) and Mars's (red) orbits on the right, and the ledger underneath.

<a id="start"></a>

## Starting
    
    
    addons/kosmosmission/bin/kosmosmission               # Linux
    addons\kosmosmission\bin\kosmosmission.exe            # Windows
    kosmosmission --demo                                  # a ready-made launch to Mars
    kosmosmission --demo --shot plan.png                  # draw the window to a PNG, then exit

It also starts from _View > Native addons_ in the game (tick **KosmosMission**). It needs the Kosmos data folder (`data/sol.json` for the bodies and the ship folders for the stage data) and finds it from its own location, so run it from the Kosmos folder or the addon folder. Build it on Linux with `cmake --build build --target kosmosmission`; the Windows build is made by `scripts/build-win-tools.sh`. The views are OpenGL 3.2.

<a id="window"></a>

## The window

  * **Left, top: Mission** is the name. It is also the name given to a Custom burn leg.
  * **Start** : the _Body_ (Earth, Moon, Mars and so on), a _Base_ on it or none (a circular orbit), the _Orbit alt_ (km) of the orbit the ascent aims for or the mission starts in, and its _Inclination_.
  * **Payload ships** : choose a ship, how many, and optionally which payload volume of the launcher it goes in (otherwise the first that takes it), then _Add payload_. A Dragonfly and a dozen probes are two entries. Each ship counts at its full wet mass in the budget, and the coloured line under the list says whether everything fits. Launchers declare their volumes (`payload_bays`, cylinders or boxes) and ships their size (`payload_size`) in `ship.json`; the check puts the items in layers along the bay and tries each one every way round, and is deliberately a little conservative. A launcher with no declared volume gets the mass but no fit check. **Stacks:** use _Add to stack_ for each stage from the bottom up (for example Centaur G, Fregat, a probe), then _Add stack_ ; it is one payload with summed mass and size, and _Count its own stages' delta-v_ adds its delta-v to the budget. Saving the mission or starting the game docks the first unit in each bay to the launcher (the rest are listed as not placed in the scenario description).
  * **Ship** : any ship found in the addons, an _Extra mass_ in kg, and the _Reserve_ in percent. The line below summarises the stages: the delta-v the ship has, and what the mission needs including the reserve.
  * **Add a leg** : the _Kind_ , and the fields that kind uses (_Destination_ , _Altitude_ , _Angle_ , _Burn dV_). **Add leg** appends it, **Remove** takes off the selected leg (the last if none is selected), **Clear** empties the list.
  * **Save, Open, Export, To game** : see [below](#files).
  * **Right: the view** draws the orbits and the transfer. Tick _Porkchop_ to swap it for the departure/flight-time grid. A line under it gives the departure day, the flight time and the hyperbolic excess speeds.
  * **The ledger** lists each leg with its type, its delta-v, the running total and what is left of the budget. A leg that runs past the budget turns red.



<a id="howto"></a>

## How to set up a mission

  1. **Choose the start.** Pick the body. To launch from the ground pick a _Base_ ; to start already in orbit leave the base empty and give the orbit's altitude and inclination.
  2. **Choose the ship and the payload.** The ship's stages (from its `ship.json`: dry mass, propellant, engine thrust and Isp) give its delta-v by the rocket equation, burning the stages in order. Enter the payload mass in kg: it is carried by every stage, so a heavy payload lowers the budget. The reserve (default 5 %) is held back, so the ledger shows what you have left _after_ it.
  3. **Add the legs in order.** Pick a kind, fill in its fields and press **Add leg**. Each leg starts from where the last one ended, so you can see the whole mission take shape in the view and the ledger.
  4. **Read the budget.** If the last line of the ledger shows a negative remainder the ship cannot fly it. Change the payload, the ship, the orbit altitude or the target, or add stages, until it fits.
  5. **For a transfer, tune the date.** Tick _Porkchop_ and see how the cost changes with the departure and the flight time. The planner takes the cheapest point of the grid (the cross-hair); the departure and flight times are in the line under the view.
  6. **Save it** as a scenario, or **Export** a table, or send a burn to the game with **To game**.



<a id="legs"></a>

## The kinds of leg

Kind| Fields used| What it adds  
---|---|---  
Ascent| Start base and orbit alt| From the base to a circular orbit at that altitude. The cost is the orbital speed minus the ground's own speed (a launch eastwards from the equator gets the most help) plus a gravity and drag loss. The loss is a rule of thumb, about 190 s of the surface gravity on a body with an atmosphere and 100 s without. Measured values you add to `data/mission/ascent.json` replace it.  
Hohmann to altitude| Altitude| The two-burn Hohmann transfer to a circular orbit of that altitude around the same body.  
Plane change| Angle| A pure inclination change, at the orbit's speed.  
Transfer to body| Destination, Altitude| Eject from the current orbit, fly to another planet, or to a moon of the body you are at, and brake into a circular orbit at the given altitude. This adds two ledger lines: the ejection and the capture. Going to a sibling planet is solved with a Lambert search over the departure and flight time; going to a moon uses OrbitComp's burn-delay search. Going back to the parent body from a moon is also supported.  
Custom burn| Burn dV, and the Mission name| Any delta-v you want to account for: a docking budget, a landing, a station-keeping allowance.  
  
<a id="budget"></a>

## The delta-v budget

The ship's delta-v is the sum over its stages of `Isp · g0 · ln(m0 / m1)`, where each stage's mass includes everything above it and the payload. Each stage uses the thrust-weighted vacuum Isp of its engines. The ledger compares the running total of the legs, plus the reserve, with that figure. It does not model staging losses, thrust to weight limits or the time a burn takes: it is a budget, not a flight simulation. Fly the mission in the game to see how it really goes.

<a id="porkchop"></a>

## The porkchop plot

![The porkchop plot for an Earth to Mars transfer: two green islands where the total delta-v is lowest.](img/mission/mars-porkchop.png)The porkchop for the Earth to Mars leg: departure day across, flight time up, green is cheap and red is expensive. The cross-hair marks the cheapest point the planner chose.

For a transfer to another planet the cost depends on when you leave and how long you take. The plot is a grid of those two with the total delta-v as colour: green islands are the cheap windows, dark red is where no useful transfer exists. The best point is refined after the grid search, and the plan uses it. The windows come round again every synodic period (about 26 months for Mars), so another green island on the departure axis is a later window, not a mistake.

<a id="files"></a>

## Saving, exporting, To game

  * **Save** writes the mission as a normal Kosmos scenario in the `scenarios` folder: the ship at the start point, and an extra `"mission"` block holding the legs. The game loads it like any scenario and ignores the block. **Open** reads one back, including its legs.
  * **Export** writes the leg list as text (`.txt`) or a table (`.csv`): leg, type, delta-v, running total, remaining.
  * **Start Kosmos with this mission** launches the game (with `--link`) on the mission as a scenario. **To game** does the same if no game is running, then sends the leg when the game answers. The launcher also has a KosmosMission button on its Play page.
  * **To game** sends the selected leg's delta-v to OrbitComp's manoeuvre node in the running game, over KosmosLink. KosmosMission is a bundled tool, so the game allows it without asking; it sets OrbitComp to the manoeuvre page with a prograde burn of that size about 600 s ahead. Then adjust it there as usual.



<a id="example"></a>

## Worked example: Earth to Mars

The mission: launch a Saturn V-23L from Alcantara, reach a 200 km parking orbit, leave for Mars and be captured into a 300 km orbit of Mars. The numbers below are what KosmosMission gives; `kosmosmission --demo` builds the same plan.

  1. **Start.** Body: _Earth_. Base: _Alcantara Launch Centre_. Orbit alt: `200`. Inclination: `0` (Alcantara is almost on the equator, so an equatorial orbit costs nothing extra).
  2. **Ship.** Choose _CVEL Saturn V-23L_. Payload `0` for now, reserve `5`. The summary line reads _18649 m/s from 6 stage(s)_ : that is what the ship has.
  3. **Leg 1, the ascent.** Kind: _Ascent_ , then **Add leg**. The ledger gets _Ascent to 200 km, 9190 m/s_. That is the speed of a 200 km circular orbit (7.8 km/s) minus what the launch site's rotation gives, plus the loss.
  4. **Leg 2 and 3, the transfer.** Kind: _Transfer to body_ , Destination: _Mars_ , Altitude: `300`, **Add leg**. Two lines appear: _Eject Earth to Mars, 3683 m/s_ and _Capture at Mars, 2545 m/s_. The info line gives _Depart day 458, flight 200 d, v-inf 3.21 / 3.49 km/s_ : leave on day 458 of the planner's calendar (counted from its epoch, day 0), arrive 200 days later. The view shows the yellow transfer ellipse between the two orbits.
  5. **Check the budget.** The running total after the capture is 15418 m/s against 18649 m/s available, so 2298 m/s is left after the reserve (the ledger's last column). The mission fits.
  6. **See the window.** Tick _Porkchop_. The cross-hair sits in the lower green island. Try a payload in the Extra mass box: it lowers the ship's delta-v, and once the remainder goes negative the ledger lines turn red. Then use a bigger ship or ask for less (a higher capture orbit).
  7. **Save** it as `earth-mars`. It appears in the scenario list in the game as a ship on the ground at Alcantara. **Export** a `.csv` if you want the table in a spreadsheet.
  8. **Fly the first burn.** Start the game on that scenario, launch to your 200 km orbit, then select leg 2 in KosmosMission and press **To game**. OrbitComp shows a 3683 m/s prograde node; use OrbitComp's own planner at the departure window to place it.



<a id="full"></a>

## Complete mission: a Centaur, Fregat and probe stack to Mars

This follows one mission from the empty window to a probe in Mars orbit. Ship sizes in the game data are estimates, so your numbers may differ a little.

  1. **Start.** Body _Earth_ , Base _Alcantara Launch Centre_ , Orbit alt `200`, Inclination `0`.
  2. **Launcher.** Ship: _CVEL Titan IVA_. Leave _Extra mass_ at 0.
  3. **Assemble the stack, bottom first.** In _Carry_ choose _CVEL Centaur G_ and press **Add to stack**. Choose _CVEL Fregat_ , **Add to stack**. Choose _Luna E3_ (the probe), **Add to stack**. The text under the buttons shows _Centaur G > Fregat > Luna E3_. Press **Add stack**. The payload list now has one line, with the combined mass and a _dv_ column showing what the stack's own stages add.
  4. **Check the fit.** The green line reads _Everything fits_ and says how much of the fairing is used. Red means it does not fit: pick a launcher with a bigger bay. Pressing _Add payload_ three times instead would make three separate payloads, not a stack, and only the first would be docked in the scenario.
  5. **Check attachment ports.** A launcher needs a declared payload bay with a port, and each stacked ship needs a docking port. A member without an attachment port is left out and reported in the scenario description. Use the Saturn V _Payload_ variants for cargo; the Apollo variants carry the CSM instead.
  6. **Leg 1: ascent.** Kind _Ascent (from the base)_ , **Add leg**. This is the launcher's job: the budget line shows the launcher's own delta-v, and the stack's delta-v is added for the legs after it.
  7. **Leg 2: the Mars transfer (Lambert).** Kind _Transfer to body_ , Destination _Mars_ , Altitude `300`, **Add leg**. You get two lines, the ejection and the capture. Tick _Porkchop_ to see the launch window; the info line gives the departure day and the flight time.
  8. **Hohmann instead.** For a change of orbit around one body (say 200 km to a 35 786 km geostationary orbit) use kind _Hohmann to altitude_ with the target altitude. Plane changes and custom burns are added the same way. Use Lambert (_Transfer to body_) when the destination is another body.
  9. **Read the ledger.** The last column is what is left after the reserve. If it goes red the mission does not fit: change the launcher, the stack or the target.
  10. **Save** (for example as `mars-probe`) and press **Start Kosmos with this mission**. In the game the launcher stands on the pad with the Centaur docked to it, the Fregat to the Centaur and the probe on top.
  11. **Fly the ascent.** Launch with the launcher's own autopilot or by hand and reach the 200 km orbit. Jettison the launcher stages as they burn out.
  12. **Fly the transfer.** In KosmosMission select leg 2 and press **To game**. OrbitComp shows a prograde node of that size. In OrbitComp use its transfer planner to place the burn at the departure window and fly it with the Centaur. Later, after the Centaur is spent, drop it and use the Fregat for corrections.
  13. **Capture.** Near Mars select the capture leg, **To game** , and burn retrograde at periapsis with the Fregat to enter the 300 km orbit. Detach the probe when you are in orbit.



The same steps are in the app: press **Guide: a complete mission** in the left column. KosmosMission plans the legs and the budget; OrbitComp in the game places the burns and flies them. Only the legs you add are planned, so add the ascent and the transfer both.

<a id="limits"></a>

## Limits

  * Moon-to-planet transfers are not planned directly. Go from the moon to its parent first (a Transfer to body leg with the parent as destination) and then on to the planet.
  * A transfer to a sibling planet starts from a circular parking orbit in a free plane; a transfer to a moon assumes the parking orbit is in the moon's orbital plane.
  * The ascent loss is an estimate. The budget ignores gravity losses on other burns, finite burn time and steering.
  * The bodies come from `data/sol.json`, so missions are planned in the Sol system.
  * **To game** only sets a node in OrbitComp; it does not fly it.



<a id="trouble"></a>

## Troubleshooting

  * **The ship list is empty or a ship is missing.** The planner reads the ship folders under `addons`. A ship needs a `ship.json` with a `stages` list.
  * **Every leg is red.** The ship has less delta-v than the mission needs. Look at the summary line under the Ship panel and compare.
  * **The window is blank or the view is black.** The view needs OpenGL 3.2; check the graphics driver.
  * **To game does nothing.** The game must be running with _Native addons_ on, and KosmosMission allowed to write. The message line shows what was sent or why not.


