#pragma once

#include "camera_dynamics.h"
#include "interior.h"
#include "lights.h"
#include "reentry.h"
#include "systems.h"
#include "universe.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kosmos
{
    /**
     * What kind of drive an engine is. Jets and scramjets breathe air (and so stop working in a vacuum), rockets carry
     * their own propellant (liquid or solid, see EngineSpec::fuel), a nuclear rocket heats hydrogen in a reactor, and a
     * hyperjump drive makes no thrust or plume: it is the interstellar jump.
     */
    enum class EngineType { jet, scramjet, rocket, solid, nuclear, hyperjump };

    const char *engine_type_name(EngineType type);
    EngineType engine_type_from_name(const std::string &name, EngineType fallback);

    struct EngineSpec
    {
        sl::dvec3 position{0.0, 0.0, 0.0};
        /** Direction the engine pushes the ship, in the ship frame (nose is -Z). */
        sl::dvec3 direction{0.0, 0.0, -1.0};
        double thrust = 0.0;
        /** Vacuum Isp in seconds. With `isp_sl` set, thrust is the vacuum thrust and falls with air pressure. */
        double isp = 300.0;
        /** Sea-level Isp in seconds; 0 keeps the Isp the same at every pressure. */
        double isp_sl = 0.0;
        /** Visual plume length in metres. */
        double plume = 10.0;
        /** Scales the plume's width and brightness; 1 is the default for its fuel. */
        double plume_width = 1.0;
        double plume_glow = 1.0;
        /** Scales how many particles the plume spawns; raise it for a big engine that looks sparse. */
        double plume_density = 1.0;
        double plume_billow = 0.0; // 0..1: how much a hydrogen plume stays full and soft in vacuum instead of going tight and clean
        /** Thrust fades with air density (propellers, jets); such an engine does not work in a vacuum. */
        bool air_breathing = false;
        EngineType type = EngineType::rocket;
        /** Propellant of a rocket, which picks the plume: "kerosene", "hydrogen", "methane", "hypergolic" or "solid". */
        std::string fuel = "kerosene";
        /** A jet with an afterburner shows a plume at high throttle; without one it is invisible. */
        bool afterburner = false;
        /** Engines with `"group": "hover"` follow /controls/hover (keypad + and -) instead of the main throttle. */
        bool hover = false;
        /**
         * `"group": "pod"`: a swivelling engine pod, like the Shuttle-A's. Its thrust follows /controls/pod-left or pod-right (by
         * which side of the ship it is on) and its direction swings in the YZ plane from aft (0) through up to forward, chasing
         * /controls/pod-tilt-left or pod-tilt-right (0 to 1) at `pod_turn_rate`.
         */
        bool pod = false;
        /** Jump range of a hyperjump drive, in light years. */
        double range_ly = 50.0;
        /**
         * `"group": "rcs"`: a reaction-control thruster fired by the control groups it lists in `rcs_groups`: "left", "right", "up",
         * "down", "forward", "back" (translation) and "pitch-up", "pitch-down", "yaw-left", "yaw-right", "bank-left", "bank-right".
         */
        std::vector<std::string> rcs_groups;
    };

    /**
     * A wing, tail or control surface, in the ship frame (nose is -Z, roof +Y, right +X, origin at the centre of mass).
     * `control` names the input it responds to: "elevator" (pitch), "aileron" (roll) or "rudder" (yaw), or empty for a
     * fixed surface. `gain` is the change in angle of attack in radians for a full command (pitch up, roll left, yaw
     * left), so an elevator that raises the nose has a negative gain, and the two ailerons have opposite signs.
     */
    struct SurfaceSpec
    {
        std::string control;
        sl::dvec3 position{0.0};
        sl::dvec3 forward{0.0, 0.0, -1.0};
        sl::dvec3 up{0.0, 1.0, 0.0};
        double area = 1.0, cl0 = 0.0, cl_alpha = 5.0, cd0 = 0.02, induced_drag = 0.05;
        double stall = 0.28, gain = 0.0;
    };

    /** A wheel of an aircraft or rover. `steer` is the largest steering angle in radians (0 for fixed). */
    struct WheelSpec
    {
        sl::dvec3 position{0.0};
        double steer = 0.0;
        double rolling_resistance = 0.02;
        /** A driven wheel shares the vehicle's drive force (ground vehicles only). */
        bool drive = false;
    };

    /** A solid of revolution about the Z axis, from (z, radius) points, drawn in a flat colour. */
    struct PartSpec
    {
        /**
         * When set, the part comes from the props library instead of a profile: "stage", "adapter", "engines", "fins",
         * "fairing" or "csm". Props stack along their own +Y from y = 0 to y = length; they are placed with y = 0 at ship
         * z = `z` and +Y pointing to the nose (-Z).
         */
        std::string prop;
        /** An OBJ/glTF model path relative to the addon folder, already in the ship frame (nose is -Z). Takes precedence over prop and profile. */
        std::string model;
        float radius = 1.0f, top_radius = 1.0f, length = 1.0f, span = 1.0f;
        int count = 1;
        float z = 0.0f;
        float accent[3] = {0.11f, 0.11f, 0.125f};
        std::vector<std::pair<float, float>> profile;
        float colour[3] = {0.8f, 0.8f, 0.8f};
        float emissive = 0.0f;
        sl::dvec3 offset{0.0};
        /** Model parts only: a scale, then a rotation in degrees about X, Y and Z, applied before `offset`. */
        sl::dvec3 scale{1.0};
        sl::dvec3 rotate{0.0};
        /** Model parts only: a simlib animation JSON (relative to the addon folder) loaded onto the model. */
        std::string animations;
    };

    /** One separable section of a ship. Stages are listed from the bottom of the stack, which is jettisoned first. */
    struct StageSpec
    {
        std::string name;
        double dry_mass = 1000.0;
        double propellant = 0.0;
        /** Fraction of the propellant loaded at the start (0 to 1). */
        double fuel_start = 1.0;
        /** Centre, length and radius for the inertia estimate and for debris. */
        double z = 0.0, length = 1.0, radius = 1.0;
        std::vector<EngineSpec> engines;
        std::vector<PartSpec> parts;
        /** Landing gear contact points in the ship frame; they leave with the stage. */
        std::vector<sl::dvec3> gear;
        double separation_speed = 1.0;
        bool with_next = false; // also drop the next stage on the same press; chains are allowed
        /** Two equally massive parts at opposite offsets separate radially as independent debris. */
        bool radial_pair = false;
        /** Ship-frame direction the stage is pushed when dropped; the default is straight back along +z. */
        sl::dvec3 separation_dir{0.0, 0.0, 1.0};
    };

    /** A docking port: where it sits in the ship frame, which way it faces (outward) and the "up" that fixes roll alignment. */
    struct PortSpec
    {
        std::string name = "Port";
        sl::dvec3 position{0.0, 0.0, 0.0};
        sl::dvec3 direction{0.0, 0.0, -1.0};
        sl::dvec3 up{0.0, 1.0, 0.0};
        /** IDS beacon frequency in MHz; 0 assigns one when the ship is created. */
        double frequency = 0.0;
        /** Stage the port sits on (-1 assigns the nearest one when the ship loads); it is gone once that stage is jettisoned. */
        int stage = -1;
    };

    /** One readout on a cockpit panel: any property-tree path, so addons can show whatever they publish. */
    struct Instrument
    {
        std::string label, property;
        double scale = 1.0;
        std::string format = "%.1f";
    };

    /** The view from inside a ship: an eye point in the ship frame and a panel of property readouts. */
    /** A virtual-cockpit part (matched by name substring) that rotates about a pivot as a value in 0..1 moves; used for levers and throttles. */
    struct VcAnimation
    {
        std::vector<std::string> parts;
        /** A property path, "gear" (gear position) or "anim:<property>" (an animation control's position). */
        std::string source;
        double pivot[3] = {0, 0, 0}, axis[3] = {1, 0, 0};
        double angle_deg = 0.0;
        /** The lowest value of the source, below zero for a reversing throttle. */
        double min = 0.0;
        /** The fraction of the full angle held when the value is 0 (the rest is swept as it goes to 1). */
        double initial = 0.0;
        /** A sliding part: the ship-frame shift at value 1 (instead of a rotation), scaled by (value - initial). */
        double move[3] = {0, 0, 0};
        bool slide = false;
    };

    struct Cockpit
    {
        bool present = false;
        sl::dvec3 eye{0.0, 0.0, 0.0};
        /** True when the ship file gave an eye; otherwise it defaults to the front centre of the hull. */
        bool eye_set = false;
        /** Degrees from straight ahead, and the field of view (0 keeps the default). */
        double yaw = 0.0, pitch = 0.0, fov = 0.0;
        std::vector<Instrument> panel;
        /** Draw the dashboard with two MFDs (default modes orbit and surface) below the instrument row. */
        bool mfds = true;
        std::string mfd_left = "orbit", mfd_right = "surface";
        /** A virtual cockpit mesh (OBJ, relative to the ship file) drawn around the eye in the 3D cockpit, in the hull's frame. */
        std::string vc_model;
        /** The MFD screen parts of the vc_model have their texture coordinates the other way up from the Deltaglider's. */
        bool vc_mfd_flip = false;
        std::vector<VcAnimation> vc_animations;
        /** VC parts (name substrings) that are never drawn, such as the alternate of two overlapping panel variants. */
        std::vector<std::string> vc_hide;
        /** VC parts (name substring) that show a panel instrument: `unit` indexes the cockpit's instruments list. */
        struct VcInstrument
        {
            std::string part;
            int unit = 0;
        };
        std::vector<VcInstrument> vc_instruments;
        /** Which MFD modes the ship offers: "auto" (aircraft get no orbital modes, everything else no airspeed-style flight gauges), "atmosphere", "space", or "none" to offer all. */
        std::string mfd_filter = "auto";
        /** MFD mode ids this ship never offers, whatever the filter says. */
        std::vector<std::string> mfd_hide;
        /** HUD modes the H key cycles ("orbit", "surface", "docking"); empty means the class default (see ShipSpec::hud_modes). */
        std::vector<std::string> hud_modes;
        /** How the Dynamic Camera moves the eye on its springs. */
        HeadMotion head_motion;

        /** One instrument on the cockpit panel: an MFD mode drawn in a square at (x, y) in panel units, with its buttons if `frame`. */
        struct PanelInstrument
        {
            std::string mode;
            float x = 0, y = 0, size = 200;
            bool frame = false;
            /** Shown only on a virtual cockpit part (`vc_instruments`), not on the 2D panel. */
            bool vc_only = false;
        };
        /**
         * An Orbiter-style instrument panel, used for both the 2D panel and the 3D cockpit. When `instruments` is not empty it replaces
         * the left and right MFD pair. The panel is `panel_width` x `panel_height` units, optionally over a background bitmap; in the 3D
         * cockpit it is a flat board `panel_size` metres wide, centred `panel_pos` from the eye and tilted back by `panel_tilt` degrees.
         * The first two instruments are the ones the F6 to F9 keys drive.
         */
        std::vector<PanelInstrument> instruments;
        /**
         * A live control over the panel bitmap, in panel units. `type` is "button" (sets `property` to `value`, lit while equal),
         * "toggle" (flips `property` between 0 and 1), "slider" (drags `property` from 0 to 1, bottom to top), "mfd" (presses
         * button `button` of MFD unit `unit`) or "hud" (selects HUD mode `value`). Panels without artwork can also use
         * "label" (fixed text), "readout" (`label` and the property printed with `format`, times `scale`), "gauge" (a bar
         * filling as the property goes from 0 to `max`) and "lamp" (lit while the property is above 0.5, or equals `value`;
         * `alarm` lights it red). Any control draws its `label` when it has one.
         */
        struct PanelControl
        {
            std::string type, property, value, label, format = "%.1f";
            float scale = 1.0f, max = 1.0f;
            bool alarm = false;
            float x = 0, y = 0, w = 20, h = 20;
            int unit = 0, button = 0;
            /** Drawn over the artwork: "knob" (a dial pointing at `angle` when the button is selected) or "lever" (a switch segment); for "needle" and "knob", the pivot and length. */
            std::string style;
            float cx = 0, cy = 0, r = 0, angle = 0;
            /** Needle sweep in degrees clockwise from straight up, for 0 and for full scale (`max`). */
            float a0 = -135.0f, a1 = 135.0f;
            /** "bar" runs from `min` to `max`; "number" is value * scale + `offset` limited to `lo`..`hi`. */
            float min = 0.0f, offset = 0.0f, lo = -1e30f, hi = 1e30f;
            /** Drawn only while this property lies between gate_min and gate_max (any control type). */
            std::string gate_property;
            float gate_min = -1e30f, gate_max = 1e30f;
        };
        std::vector<PanelControl> controls;
        /** VC parts (name substring) whose texture `image` (relative to the ship folder) is redrawn each frame with needles and numbers; control coordinates are in image pixels. */
        struct VcGauge
        {
            std::string part, image;
            std::vector<PanelControl> controls;
        };
        std::vector<VcGauge> vc_gauges;
        /** A clickable quad in the virtual cockpit (ship frame; corners top-left, top-right, bottom-left, bottom-right) that presses a panel control. */
        struct VcHotspot
        {
            double quad[4][3] = {};
            PanelControl control;
            /** A part name of a vc_animations entry: the quad moves with that animated part. */
            std::string follow;
            /** A held-drag slider: dragging along the quad's second edge changes this property by `drag_gain` per quad length. */
            std::string drag;
            double drag_gain = 1.0, drag_min = 0.0, drag_max = 1.0;
            /** Outlined in red on the panel while the control is selected (the artwork carries a green surround). */
            bool outline_active = false;
        };
        std::vector<VcHotspot> vc_hotspots;
        float panel_width = 1400.0f, panel_height = 500.0f;
        std::string panel_image;
        /** A name for the panel above in the 2D Panels menu. */
        std::string panel_name = "Instrument panel";
        /** Further 2D panels (the "panels" list in the ship file), each its own set of instruments. They show on screen only, not in the 3D cockpit. */
        struct Panel2D
        {
            std::string name;
            float width = 1400.0f, height = 500.0f;
            std::string image;
            std::vector<PanelInstrument> instruments;
            std::vector<PanelControl> controls;
        };
        std::vector<Panel2D> extra_panels;
        /** Every 2D panel the ship has: the main instrument panel first, then the extra ones. */
        std::vector<Panel2D> panels_2d() const
        {
            std::vector<Panel2D> all;
            if (!instruments.empty() || !controls.empty())
                all.push_back({panel_name, panel_width, panel_height, panel_image, instruments, controls});
            all.insert(all.end(), extra_panels.begin(), extra_panels.end());
            return all;
        }
        sl::dvec3 panel_pos{0.0, -0.2, -0.7};
        double panel_size = 0.9, panel_tilt = 25.0;
    };

    /** An extra viewpoint inside or around a ship, in the ship frame (nose is -Z). Angles are degrees from straight ahead. */
    struct CameraSpec
    {
        std::string name;
        sl::dvec3 position{0.0};
        double yaw = 0.0, pitch = 0.0, fov = 60.0;
    };

    struct ShipSpec
    {
        std::string name;
        std::string kind = "ship";
        std::string directory;
        /** The ship.json this was read from, so the part editor can write a converted part back. */
        std::string source_file;
        std::string script;
        /** A looping engine sound (path inside the ship folder) that replaces the generic engine voice while this ship is flown. */
        std::string engine_sound;
        Cockpit cockpit;
        /** The native addon that flies this ship and the most force (N) and torque (N m) it may apply; 0 uses a default scaled to the ship's mass. */
        std::string module_addon;
        double module_max_force = 0.0, module_max_torque = 0.0;
        std::vector<PortSpec> ports;
        /** Transponder frequency in MHz; 0 assigns one when the ship is created. */
        double transponder = 0.0;
        /** Attitude control authority in rad/s^2 and the fastest turn rate in rad/s. */
        double torque_accel = 0.3;
        double max_rate = 0.5;
        /** Translation thrust in newtons for the RCS (0 for none). */
        double rcs_thrust = 0.0;
        /** Cd * A for the whole stack in m^2. */
        double drag_area = 10.0;
        /** Re-entry heating, hypersonic aerodynamics and parachutes; all off unless the ship file asks for them. */
        ThermalSpec thermal;
        HypersonicSpec hypersonic;
        std::vector<ParachuteSpec> parachutes;
        bool hit_sphere = false; // weapons hit a sphere of the stage radius instead of the bounding box
        double portal_radius = 0.0; // > 0: a quantum portal whose open disc has this radius (m); the ship nose axis (Z) is the disc normal
        /** Camera distance that frames the ship. */
        double camera_distance = 60.0;
        /** Point in the ship frame the external camera orbits and looks at; unset means the centre of mass. */
        bool has_camera_target = false;
        /** A rocket-class ship that rests level on its gear (roof up, nose along the heading) instead of standing on its tail. */
        bool horizontal = false;
        sl::dvec3 camera_target{0.0};
        /** Landing gear that folds away: it takes `gear_time` seconds to move and only carries load while fully down. */
        bool retractable_gear = false;
        /** An RCS mode switch (/controls/rcs-mode: "rot", "lin" or "off"), as on the Shuttle-A. */
        bool rcs_modes = false;
        /** The main throttle can go below zero: reverse thrust, made by the linear RCS (needs rcs_thrust). */
        bool reverse_throttle = false;
        /** Draw the generic cold-gas puffs at the ends of the stage envelope when the RCS fires (ships without RCS thrusters of their own). */
        bool rcs_effects = true;
        /** Kilograms of RCS propellant (0 for an unlimited supply). The generic RCS stops when it runs out. */
        double rcs_propellant = 0.0;
        /** Specific impulse of the RCS in seconds, which sets how fast it uses propellant. */
        double rcs_isp = 280.0;
        /** An aircraft that flies as a spaceship (RCS, SAS, orbital autopilots) wherever the air is thinner than a tenth of Earth's at sea level, as the Deltaglider does. */
        bool hybrid = false;
        /** How fast an engine pod swings, in radians per second. */
        double pod_turn_rate = 0.2;
        /**
         * A named animation the player can trigger (the "animation_controls" list), such as a nose hatch. It runs a model
         * animation property from 0 to 1 over `duration` seconds, chasing /controls/anim/<property> (0 or 1).
         */
        struct AnimationControl
        {
            std::string name, property;
            double duration = 10.0;
            double initial = 0.0;
        };
        std::vector<AnimationControl> animation_controls;
        /**
         * A place on the ship that carries releasable equipment (the "hardpoints" list), such as a bomb rack: `position` in the ship frame,
         * `equipment` an equipment id and `count` how many are loaded. Released items fall away from the ship.
         */
        struct Hardpoint
        {
            std::string name, equipment;
            sl::dvec3 position{0.0};
            int count = 1;
            /** Ship-frame direction and speed (m/s) a released item leaves in; the default drops it. */
            sl::dvec3 eject{0.0, -1.0, 0.0};
            double eject_speed = 1.5;
        };
        std::vector<Hardpoint> hardpoints;
        /**
         * A science instrument (the "instruments" list): a camera (`visual`, `ir`, `uv`), `radar` or `spectrometer` driven from the Instrument MFD.
         * `position` and `direction` are in the ship frame; `load` names the systems load that powers it (empty means always powered), `fov` is the
         * field of view in degrees, `resolution` the picture width in pixels, `slew` the pointing rate in degrees per second, `max_slew` how far
         * from `direction` it can point, `sun_limit` the angle in degrees to the Sun inside which the shutter closes, and `frames` the film or storage.
         */
        struct Instrument
        {
            std::string id, type = "visual", name, load;
            sl::dvec3 position{0.0}, direction{0.0, 0.0, 1.0};
            double fov = 10.0, slew = 2.0, max_slew = 30.0, sun_limit = 20.0;
            int resolution = 400, frames = 64;
        };
        std::vector<Instrument> instruments;
        /**
         * A gun on the ship (the "weapons" list): `weapon` is a weapon id from an addon's weapons folder, `position` and `direction` are in the ship
         * frame, and `group` is the fire control that triggers it (`/controls/weapons/<group>/fire` for the flown ship).
         */
        struct WeaponMount
        {
            std::string name, weapon, group = "primary";
            sl::dvec3 position{0.0}, direction{0.0, 0.0, -1.0};
        };
        std::vector<WeaponMount> weapons;
        /** Energy shield: absorbs weapon hits (in fractions of the ship's health) before the hull is touched, then recharges after `delay` seconds. */
        struct Shield
        {
            double capacity = 0.0, recharge = 0.05, delay = 4.0;
            int colour[3] = {120, 190, 255};
        };
        Shield shield;
        /** Equipment ids a person leaving this ship or building takes along (the "locker" list). */
        std::vector<std::string> locker;
        /** A tank pod: when docked it can feed its propellant to the ship it is attached to (see /dock/transfer). */
        bool fuel_pod = false;
        /** A supply pod: when docked it tops up the host ship's oxygen, nitrogen and water tanks and CO2 sorbent from its own. */
        bool life_pod = false;
        double gear_time = 6.0;
        std::vector<StageSpec> stages;
        /** Aerodynamic surfaces and wheels, for aircraft and anything else that flies in an atmosphere. */
        std::vector<CameraSpec> cameras;
        std::vector<SurfaceSpec> surfaces;
        /** Main-engine gimbal swing at full deflection, in degrees. */
        double gimbal_range = 5.0;
        std::vector<WheelSpec> wheels;
        /** Running lights, strobes and floodlamps, in the ship frame. */
        std::vector<LightSpec> lights;
        /** Power, consumables and pressure vessels (empty for ships without any). */
        SystemsSpec systems;
        /** Moments of inertia about the ship X, Y and Z axes in kg m^2; zero estimates them from the stages. */
        sl::dvec3 inertia{0.0};
        /** Rate damping gains for pitch, yaw and roll while the stability hold is on (fly-by-wire). */
        sl::dvec3 damping{0.6, 0.6, 0.6};
        /** Built for orbit, not for the ground: touching down faster than `crash_speed` m/s destroys it. Tugs, transfer stages and stations are by default. */
        bool orbital_only = false;
        double pad_turn = 0.0; // degrees added to the pad heading when standing on a launch pad, to keep strap-ons clear of the tower
        double crash_speed = 5.0;
        /** A fixed building (class "building"): no engines or flight, held to the ground, entered on foot at `door`. */
        bool building() const { return kind == "building"; }
        /** Where the door is, in the ship frame, and how close (metres) a person must stand to go in. */
        sl::dvec3 door{0.0, 0.0, 0.0};
        double door_radius = 4.0;
        /** A walkable inside, if the building has one (the "interior" block of its ship.json). */
        std::shared_ptr<const Interior> interior;
        /** Buildings with the same grid name at a base share one power bus. */
        std::string grid;
        bool aircraft() const { return kind == "aircraft"; }
        /** A wheeled vehicle: it drives on its wheels with throttle from -1 (reverse) to 1, and has no SAS or orbital instruments. */
        bool ground() const { return kind == "ground"; }
        /** Ground vehicles: total drive force in N over the driven wheels, and the speed in m/s where it fades to nothing. */
        double ground_force = 0.0, ground_max_speed = 15.0;
        bool has_pods() const
        {
            for (const StageSpec &st : stages)
                for (const EngineSpec &e : st.engines)
                    if (e.pod)
                        return true;
            return false;
        }
        bool has_hover() const
        {
            for (const StageSpec &st : stages)
                for (const EngineSpec &e : st.engines)
                    if (e.hover)
                        return true;
            return false;
        }
        /** HUD modes this ship offers: its cockpit.hud_modes, else surface only for aircraft and all three for other ships. */
        std::vector<std::string> hud_modes() const
        {
            if (!cockpit.hud_modes.empty())
                return cockpit.hud_modes;
            if (aircraft() || ground())
                return {"surface"};
            return {"orbit", "surface", "docking"};
        }
        /** The MfdSystem domain this ship flies in, from cockpit.mfd_filter. */
        std::string mfd_domain() const
        {
            const std::string &f = cockpit.mfd_filter;
            if (f == "atmosphere" || f == "space")
                return f;
            return f == "none" ? "any" : aircraft() || ground() ? "atmosphere" : "space";
        }
    };

    bool load_ship_spec(const std::string &file, ShipSpec &spec, std::string &error);

    /** The folder holding a ship's skins: "Skins" beside the folder of its first model, which a variant ship shares with the ship it borrows the model from. Empty when it has no model. */
    std::string ship_skins_directory(const ShipSpec &spec);
    /** The names of the skins available for a ship, each a sub-folder of its skins directory. */
    std::vector<std::string> ship_skins(const ShipSpec &spec);

    /** Control inputs applied to the active ship each frame. */
    struct Controls
    {
        double throttle = 0.0;
        /** Level of the hover engine group, 0 to 1. */
        double hover = 0.0;
        double pitch = 0.0, yaw = 0.0, roll = 0.0;
        double rcs_x = 0.0, rcs_y = 0.0, rcs_z = 0.0;
        /** Thrust of the left and right engine pods, and where each is asked to point (0 aft/retro, 0.5 up/hover, 1 forward). */
        double pod_left = 0.0, pod_right = 0.0, pod_tilt_left = 0.0, pod_tilt_right = 0.0;
        /** With an RCS mode switch: "rot" (keys rotate), "lin" (the rotation keys translate) or "off". */
        std::string rcs_mode = "rot";
        /** Targets of the ship's animation controls, in the order of `ShipSpec::animation_controls`. */
        std::vector<double> anim;
        /** off, stability, prograde, retrograde, target-retrograde, radial-out, radial-in, normal, antinormal or attitude. */
        std::string sas = "stability";
        sl::dvec3 target_relative_velocity{0.0};
        bool target_velocity_valid = false;
        double target_pitch = 90.0, target_heading = 90.0;
        bool ascent_attitude = false;
        bool stage = false;
        /** 1 deploys the next parachute, -1 cuts every open one. */
        int parachute = 0;
        /** Wheel brakes, 0 to 1. */
        double brake = 0.0;
        /** Flaps, 0 (up) to 1 (full). */
        double flaps = 0.0;
        /** Landing gear, 0 (up) or 1 (down); only ships with retractable gear react. */
        double gear = 1.0;
        /** Control-surface mode: "on" (all axes), "pitch" (elevators only) or "off". */
        std::string af_mode = "on";
        /** Elevator trim, -1 to 1, added to the pitch command. */
        double trim = 0.0;
        /** Main-engine gimbal offsets, -1 to 1 (full scale is `ShipSpec::gimbal_range` degrees). */
        double gimbal_pitch = 0.0, gimbal_yaw = 0.0;
    };

    /** Values derived from the ship's state once per frame; published to the property tree. */
    struct Telemetry
    {
        int soi = 0;
        /** Clearance above the local collision terrain, measured from the nominal bottom of the vessel. */
        double altitude = 0.0;
        double surface_speed = 0.0;
        double orbital_speed = 0.0;
        double vertical_speed = 0.0;
        double apoapsis = 0.0, periapsis = 0.0;
        double eccentricity = 0.0, inclination = 0.0, period = 0.0;
        double time_to_apoapsis = 0.0, time_to_periapsis = 0.0;
        bool bound = false;
        double pitch = 0.0, heading = 0.0;
        /** Bank angle in degrees, positive with the right wing down; 180 is inverted. */
        double roll = 0.0;
        double mass = 0.0, thrust = 0.0, max_thrust = 0.0, twr = 0.0, acceleration_g = 0.0;
        double stage_fuel = 0.0, total_fuel = 0.0;
        /** Aerodynamics: angle of attack and sideslip in degrees, dynamic pressure in pascals and the climb angle in degrees. */
        double alpha = 0.0, sideslip = 0.0, dynamic_pressure = 0.0, flight_path = 0.0;
        double latitude = 0.0, longitude = 0.0;
        double semi_major_axis = 0.0, raan = 0.0, arg_periapsis = 0.0, true_anomaly = 0.0;
        double body_radius = 0.0;
        bool landed = false;
    };

    struct ShipCarryState
    {
        int active_stage = 0;
        std::vector<double> stage_propellant;
        double rcs_propellant = 0.0;
        double gear_position = 1.0;
        std::vector<double> animation_positions;
        double health = 1.0, shield = 0.0;
        double mission_time = 0.0;
        Systems::State systems;
        bool has_systems = false;
    };

    struct Debris
    {
        int body = -1;
        int stage = 0;
        double age = 0.0;
        sl::dvec3 centre{0.0};
        int part = -1;
    };

    /**
     * A vessel: a FlightWorld body built from a ShipSpec, with staging, attitude control, telemetry and rails handling.
     * Ships are not movable once published, because the property tree holds callbacks into them.
     */
    class Ship
    {
    public:
        Ship(Universe &universe, ShipSpec spec);
        ~Ship();
        Ship(const Ship &) = delete;
        Ship &operator=(const Ship &) = delete;

        const ShipSpec &spec() const { return spec_; }
        /** For the part editor, which swaps a procedural part for an exported mesh while the ship flies. */
        ShipSpec &editable_spec() { return spec_; }
        std::string name;
        /** The skin in use, a folder name from ship_skins(); empty for the model's own textures. */
        std::string skin;
        /** Set for ships a KosmosLink addon created, which are the only ones an addon may remove. */
        bool from_link = false;
        int body_id() const { return body_; }
        /**
         * The body's position is its centre of mass, which leaves the ship-frame origin once the lower stages have gone. This is
         * the world position of the ship-frame origin, where the model is drawn from, and `com_offset` is the mass centre in that frame.
         */
        sl::dvec3 origin() const { return body().position - body().direction_to_world(com_); }
        const sl::dvec3 &com_offset() const { return com_; }
        int active_stage() const { return active_stage_; }
        ShipCarryState carry_state() const;
        void restore_carry_state(const ShipCarryState &state);
        bool attitude_control_available() const { return !rcs_dry() && (explicit_rcs_ || spec_.torque_accel > 0.0); }
        /** False once the stage carrying docking port `index` has been jettisoned. */
        bool port_attached(int index) const { return spec_.ports[static_cast<std::size_t>(index)].stage >= active_stage_; }
        /** The camera's focus in the ship frame: `camera_target` moved along the axis by however far the middle of the stages still attached has shifted from the middle of the full stack. */
        sl::dvec3 camera_focus() const;
        /** Landing gear extension: 0 retracted to 1 down and locked (always 1 without retractable gear). */
        double gear_position() const { return gear_position_; }
        void set_gear_position(double p) { gear_position_ = std::clamp(p, 0.0, 1.0); }
        /** Where an engine pod points, as a fraction: 0 aft (retro), 0.5 up (hover), 1 forward. `right` picks the starboard pod. */
        /** Progress (0 to 1) of the ship's `index`th animation control. */
        double animation_position(std::size_t index) const { return index < anim_pos_.size() ? anim_pos_[index] : index < spec_.animation_controls.size() ? spec_.animation_controls[index].initial : 0.0; }
        double pod_angle(bool right) const { return pod_angle_[right ? 1 : 0] / 3.14159265358979; }
        int stage_count() const { return static_cast<int>(spec_.stages.size()); }
        bool crashed() const { return crashed_; }
        /** 1 undamaged down to 0 destroyed. */
        double health() const { return health_; }
        /** Take damage (0 to 1 of the whole ship); at 0 the ship is destroyed as if it had crashed. */
        void damage(double amount);
        /** The shield's charge, 0 up to the spec's capacity. */
        double shield_level() const { return shield_; }
        /** A weapon hit of `amount`: the shield takes what it can and the rest is returned for the hull. */
        double absorb(double amount);
        /** Recharge the shield after its quiet period. */
        void shield_tick(double dt);
        /** Mass of released-in-flight equipment still aboard (bombs on racks); included in the ship's mass. */
        void set_payload_mass(double kilograms);
        const Telemetry &telemetry() const { return telemetry_; }
        void refresh_telemetry() { update_telemetry(); }
        /** Maximum available thrust controlled by the main throttle, excluding hover, pod and RCS engines. */
        double main_max_thrust() const;
        /** Main-engine pitch gimbal setting that minimises pitch torque about the current centre of mass. */
        double main_pitch_trim(double yaw) const;
        /** What the reaction-control jets did on the last update, in the ship frame: torque command (pitch, yaw, roll) and translation push, each -1..1. */
        const sl::dvec3 &rcs_torque() const { return rcs_torque_; }
        const sl::dvec3 &rcs_push() const { return rcs_push_; }
        const std::vector<Debris> &debris() const { return debris_; }
        sl::FlightBody &body();
        const sl::FlightBody &body() const;
        /** Power, life-support and pressure-vessel state; null for ships that declare none. */
        /** Doors and airlock of a building with an interior. */
        InteriorState interior_state;
        /** Runtime state of one `ShipSpec::Instrument`. Pointing is an offset from the mounting direction, in radians. */
        struct InstrumentState
        {
            double az = 0.0, el = 0.0, target_az = 0.0, target_el = 0.0;
            double sun_angle = 180.0;
            int frames = 0, shots = 0, pending = 0;
            bool powered = false, shutter = false;
        };
        std::vector<InstrumentState> &instruments() { return instrument_state_; }
        const std::vector<InstrumentState> &instruments() const { return instrument_state_; }
        /** The camera direction of an instrument in the world frame, or the ship's mounting direction when out of range. */
        sl::dvec3 instrument_direction(std::size_t i) const;
        sl::dvec3 instrument_position(std::size_t i) const;
        /** Asks for a shot: it is taken (and film used) by the renderer on its next frame. */
        void instrument_shoot(std::size_t i);
        Systems *systems() { return systems_.get(); }
        const Systems *systems() const { return systems_.get(); }
        /** Whether failing life support only warns, can kill the crew (ending the flight), or is ignored. */
        void set_life_support(LifeSupportMode mode);

        /** Stand on a body's surface at the given direction from its centre, nose up, facing a compass heading. */
        void place_on_surface(int body, const sl::dvec3 &direction, double heading_degrees, double ground_height);
        /** Place into a circular equatorial-ish orbit of the given altitude. */
        /** Fly level at `altitude` metres above `ground_height`, heading in degrees, at `speed` m/s through the air. */
        void place_airborne(int body, const sl::dvec3 &direction, double heading_degrees, double ground_height, double altitude, double speed);
        /** Put the ship at a position and velocity relative to the centre of body `body` (world axes), e.g. beside another player. */
        void place_relative(int body, const sl::dvec3 &position, const sl::dvec3 &velocity, const sl::dquat &orientation);
                void place_in_orbit(int body, double altitude, double inclination_degrees, double phase_degrees = 0.0);
        /**
         * Place on an ellipse with the given periapsis and apoapsis altitudes at a true anomaly. With `toward` >= 0 the orbit
         * lies in that body's orbital plane and its apoapsis points at where the body will be when the ship gets there.
         */
        void place_on_ellipse(int body, double periapsis, double apoapsis, double true_anomaly_degrees, int toward);
        /**
         * Places the ship on an escape hyperbola about `body`, with the transfer excess velocity for a Hohmann transfer to `toward` (or,
         * with `flight_days`, a Lambert transfer arriving after that many days): the state just after the departure burn.
         * `true_anomaly_degrees` is the angle past periapsis.
         */
        void place_on_escape(int body, double periapsis, double true_anomaly_degrees, int toward, double flight_days = 0.0);

        /** Docking port `index` in the world frame: position, outward direction and up. */
        void port_world(int index, sl::dvec3 &position, sl::dvec3 &direction, sl::dvec3 &up) const;
        /** The farthest any docking port lies from the body origin, in metres. */
        double port_reach() const;
        /** Velocity of a world-space point rigidly attached to the ship. */
        sl::dvec3 point_velocity(const sl::dvec3 &world_point) const;
        /** Mass of ships docked to this one that it carries along; included in its mass and inertia. */
        void set_attached_mass(double kilograms);
        /** Scales every gear contact's sideways and brake friction (1 is the ship's own grip), for wet or icy ground. */
        void set_surface_grip(double factor);

        void apply_controls(const Controls &controls, double dt);
        /** Refresh telemetry and handle impacts and debris after the world has stepped. */
        void after_step(double dt);
        /** Re-entry heat of the vehicle (see ThermalState); the spec's `thermal` block must be on. */
        const ThermalState &thermal() const { return thermal_; }
        const std::vector<ChuteRuntime> &chutes() const { return chutes_; }
        /** Deploy the next parachute that is still stowed. Returns false when there is none. */
        bool deploy_parachute();
        void cut_parachutes();

        /** Free the ship from rails and back to full dynamics. */
        void go_dynamic();
        /** Put a coasting ship on Kepler rails around the body whose sphere of influence holds it. */
        bool go_rails();
        bool on_rails() const;
        /** True while the ship is flying on its wings: an aircraft, except a hybrid one outside thick air. */
        bool flies_as_aircraft() const;
        /** True when the ship may be put on rails: coasting above any atmosphere and not landed. */
        bool can_rails(const Controls &controls) const;
        /** Why the ship cannot go on rails, or an empty string when it can. */
        std::string rails_blocker(const Controls &controls) const;
        /** Metres of height above the point where the warp must drop to 4x (the top of the atmosphere plus a margin); large when there is no air. */
        double rails_margin() const;

        bool stage_once();
        /** Direction of travel relative to the sphere-of-influence body (including its spin) in world axes. */
        sl::dvec3 relative_velocity() const;
        /** Local up, east and north for the body the ship is closest to in influence. */
        void local_frame(sl::dvec3 &up, sl::dvec3 &east, sl::dvec3 &north) const;
        sl::dvec3 sas_direction(const Controls &controls, bool &valid) const { return target_direction(controls, valid); }

        /** Publish values under base (such as /ships/ship[0]). */
        void publish(sl::PropertyTree &tree, const std::string &base);
        void unpublish();

    private:
        void build_body();
        void refresh_mass_properties();
        void update_telemetry();
        /** Re-predicts the ground track and impact point by integrating the orbit about the sphere-of-influence body. */
        void update_ground_track();
        sl::dvec3 target_direction(const Controls &controls, bool &valid) const;
        void apply_attitude(const Controls &controls, double dt);
        void apply_flight_controls(const Controls &controls);
        void apply_ground_controls(const Controls &controls);
        void drop_stage(int index);
    public:
        /** Pins a motionless landed ship to the ground (on) or lets it go again (off); used by very high ground warp. */
        void hold_for_warp(bool on);
    private:
        /** Moves the body's centre of mass to that of the stages left, keeping every point of the ship where it was. */
        void recentre();

        Universe &universe_;
        ShipSpec spec_;
        int body_ = -1;
        int active_stage_ = 0;
        sl::dvec3 com_{0.0};
        double stage_time_ = -1.0e9;
        bool crashed_ = false;
        double health_ = 1.0;
        double rcs_fuel_ = 0.0, rcs_flow_ = 0.0, fuel_flow_ = 0.0;
    public:
        static constexpr int track_points = 160;
    private:
        struct GroundTrack
        {
            double stamp = -1e30;
            int soi = -1;
            int count = 0;
            double lat[track_points + 1] = {}, lon[track_points + 1] = {};
            bool impact = false;
            double impact_lat = 0.0, impact_lon = 0.0, impact_time = 0.0;
        } track_;
        double shield_ = 0.0, shield_quiet_ = 0.0;
        double payload_mass_ = 0.0;
        // Wrecks and resting orbital ships are fixed to the ground: position and attitude in the planet's frame, re-applied each frame.
        bool pinned_ = false;
        bool warp_pinned_ = false;
        int pin_body_ = -1;
        sl::dvec3 pin_position_{0.0};
        sl::dquat pin_attitude_{1.0, 0.0, 0.0, 0.0};
        void pin_to_ground();
        void hold_pinned();
        void step_systems(double dt);
        void step_instruments(double dt, const sl::dvec3 &sun_local, bool sunlit);
        void step_reentry(double dt);
        void step_parachutes(double dt, const sl::dvec3 &air_velocity, double rho);
        ThermalState thermal_;
        std::vector<ChuteRuntime> chutes_;
        bool chutes_armed_ = false; // set once the ship has climbed above a chute's arm_altitude
        double base_drag_area_ = 0.0;
        std::vector<double> base_surface_area_;
        bool aero_scaled_ = false;
        void apply_rcs_mode(Controls &c) const;
        void animate_mechanisms(const Controls &c, double dt);
        void set_thruster_levels(const Controls &c);
        void hold_hover_attitude(const Controls &c);
        void apply_rcs(const Controls &c);
        bool rcs_dry() const;
        void use_rcs_propellant(double dt);
        /** Thrust (newtons), level (0 to 1) or propellant flow (kg/s) of the engines of the active stage in one role: "main-left", "main-right", "hover-front", "hover-aft", "pod-left" or "pod-right". */
        double engine_value(const std::string &role, const std::string &what) const;
        void resolve_landing();
        void expire_debris(double dt);
        bool resting_ = false; // an orbital-only ship that has touched down gently and sits on the surface
        double approach_speed_ = 0.0; // surface speed the frame before touchdown
        double gear_k_ = 0.0;
        double gear_position_ = 1.0;
        std::vector<double> animation_previous_;
        double animation_elapsed_ = 0.0;
        double pod_angle_[2] = {0.0, 0.0};
        std::vector<double> anim_pos_;
        std::vector<int> thruster_pod_;
        /** Bit mask of the RCS control groups driving each thruster; 0 for a main or hover engine. */
        std::vector<unsigned> thruster_rcs_;
        bool explicit_rcs_ = false;
        double attached_mass_ = 0.0;
        std::vector<std::pair<double, double>> base_grip_;
        Telemetry telemetry_;
        std::vector<Debris> debris_;
        sl::PropertyTree *tree_ = nullptr;
        std::string base_;
        std::vector<int> stage_first_thruster_;
        std::vector<bool> thruster_hover_;
        sl::dvec3 rcs_torque_{0.0}, rcs_push_{0.0};
        std::unique_ptr<Systems> systems_;
        std::vector<InstrumentState> instrument_state_;
        LifeSupportMode life_support_ = LifeSupportMode::warn;
        double mission_time_ = 0.0;
    };
} // namespace kosmos
