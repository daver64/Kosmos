/*
 * KosmosLink client library, protocol version 2.
 *
 * Talks to a running Kosmos over a loopback socket so that a native program (an autopilot, a hardware bridge, a custom
 * instrument) can read and write the simulator's properties, define keys, spawn things and draw on an MFD page.
 * Plain C99, no dependencies; builds with MSVC, gcc and clang. See docs/MANUAL.md and docs/PROTOCOL.md.
 *
 * Every function that talks to Kosmos returns 0 on success and a negative KL_ERR_* value on failure; kl_error()
 * then describes it. Functions are not thread safe: use one kl_client from one thread.
 */
#ifndef KOSMOS_LINK_H
#define KOSMOS_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define KL_PROTOCOL_VERSION 3
#define KL_DEFAULT_PORT 9420

/* What an addon may ask for; the player decides what is granted. */
enum
{
    KL_CAP_READ = 1,     /* get and subscribe to properties */
    KL_CAP_CONTROLS = 2, /* set /controls/ and /addons/<your id>/ */
    KL_CAP_WRITE = 4,    /* set any property */
    KL_CAP_SPAWN = 8,    /* spawn ships, release ordnance, give equipment */
    KL_CAP_DRAW = 16,    /* add MFD pages and draw on them */
    KL_CAP_KEYS = 32,    /* define key actions */
    KL_CAP_WORLD = 64,   /* change the weather, place and remove ships */
    KL_CAP_UI = 128,     /* notices and small windows */
    KL_CAP_VESSEL = 256, /* own the behaviour of a ship */
    KL_CAP_DRAW3D = 512, /* lines, markers and labels in the 3D view */
    KL_CAP_ALL = 1023
};

enum
{
    KL_OK = 0,
    KL_ERR_IO = -1,         /* the connection failed or closed */
    KL_ERR_TIMEOUT = -2,    /* Kosmos did not answer in time */
    KL_ERR_BAD_REQUEST = -3,
    KL_ERR_UNKNOWN = -4,
    KL_ERR_DENIED = -5,     /* the player has not allowed this addon to do that */
    KL_ERR_FAILED = -6,     /* Kosmos refused: see kl_error() */
    KL_ERR_VERSION = -7,
    KL_ERR_ARGUMENT = -8
};

typedef enum
{
    KL_NONE = 0,
    KL_BOOL = 1,
    KL_INT = 2,
    KL_DOUBLE = 3,
    KL_STRING = 4
} kl_type;

/* A property value. Strings are cut to 255 bytes. */
typedef struct
{
    kl_type type;
    int boolean;
    int integer;
    double real;
    char string[256];
} kl_value;

typedef struct kl_client kl_client;

typedef struct
{
    const char *host; /* default "127.0.0.1" */
    int port;         /* default KL_DEFAULT_PORT */
    const char *addon_id; /* letters, digits, '-' and '_'; must match the addon folder's addon.json id */
    const char *name;
    const char *version;
    const char *token;    /* from KOSMOS_LINK_TOKEN; NULL when started by hand */
    unsigned capabilities; /* KL_CAP_* flags you want */
} kl_options;

/* Called when a subscribed property changes (path and value are valid only during the call). */
typedef void (*kl_value_fn)(const char *path, const kl_value *value, void *user);
/* Called for events: "ship", "mfd" (draw the page named in arg), "granted", "shutdown", and the ones you ask for with
   kl_subscribe_event: "pause", "warp", "scenario", "soi", "crash", "touchdown", "weather", "ships", "tick". */
typedef void (*kl_event_fn)(const char *name, const char *arg, void *user);
/* Called when Kosmos rejects a request that had no reply wanted (kl_set, drawing). */
typedef void (*kl_error_fn)(int code, const char *message, void *user);

/* Fills host, port and token from the KOSMOS_LINK ("127.0.0.1:9420") and KOSMOS_LINK_TOKEN environment variables that
   Kosmos sets when it starts your program. Other fields are left as they are. */
void kl_options_from_env(kl_options *options);

/* Connects and says hello. Returns NULL on failure and writes the reason to err. */
kl_client *kl_connect(const kl_options *options, char *err, size_t err_size);
void kl_close(kl_client *client);

/* The capabilities the player has allowed so far (this changes when the player decides; see the "granted" event). */
unsigned kl_granted(const kl_client *client);
const char *kl_sim_version(const kl_client *client);
/* The text of the most recent failure. */
const char *kl_error(const kl_client *client);

void kl_set_value_handler(kl_client *client, kl_value_fn fn, void *user);
void kl_set_event_handler(kl_client *client, kl_event_fn fn, void *user);
void kl_set_error_handler(kl_client *client, kl_error_fn fn, void *user);

/* Waits up to timeout_ms for messages and runs the handlers. Returns the number handled, or KL_ERR_IO once disconnected. */
int kl_poll(kl_client *client, int timeout_ms);

/* ---- Properties */
/* hz is the most updates per second; 0 means every frame in which the value changed. The current value arrives at once. */
int kl_subscribe(kl_client *client, const char *path, int hz);
int kl_unsubscribe(kl_client *client, const char *path);
/* Reads one property now (blocks for the answer). The type is KL_NONE when it does not exist. */
int kl_get(kl_client *client, const char *path, kl_value *out);
/* Writes a property without waiting. A refusal arrives at the error handler. */
int kl_set(kl_client *client, const char *path, const kl_value *value);
int kl_set_bool(kl_client *client, const char *path, int value);
int kl_set_int(kl_client *client, const char *path, int value);
int kl_set_double(kl_client *client, const char *path, double value);
int kl_set_string(kl_client *client, const char *path, const char *value);

/* ---- Keys */
/* Adds a key action the player can rebind. The key uses simlib key names such as "letter_j" or "digit_5" (see the modders manual, Key actions); "" leaves it unbound. While toggled (or held when
   hold is non-zero) the bool property /addons/<your id>/keys/<id> is true. Subscribe to it to react. */
int kl_define_key(kl_client *client, const char *id, const char *label, const char *group, const char *key, int shift, int hold);

/* ---- The world */
/* Spawns a ship (by its name in ship.json) beside the flown one, offset in the flown ship's axes in metres. */
int kl_spawn_ship(kl_client *client, const char *ship_name, double x, double y, double z, unsigned *handle);
/* Releases ordnance from a hardpoint of the flown ship (-1: the first loaded one). */
int kl_release(kl_client *client, int hardpoint);
/* Adds equipment (an id from data/equipment) to the person's pack. */
int kl_give(kl_client *client, const char *equipment_id, int count);

/* ---- Weather (protocol 2) */
typedef struct
{
    double wind_east, wind_north; /* m/s, the way the air moves toward */
    double wind_speed;            /* m/s */
    double wind_from;             /* degrees true, the way the wind blows from */
    double gust;                  /* extra m/s the wind can briefly add */
    double rain, thunder;         /* 0 to 1 */
    double cloud_cover;           /* 0 clear to 1 overcast */
    double cloud_base, cloud_top; /* metres above the ground */
    double visibility;            /* metres */
    double temperature;           /* Celsius */
    double pressure;              /* hPa at the surface */
    char station[32];             /* the reporting station and METAR, filled in only for the flown ship's own body */
    char metar[256];
} kl_weather;

/* Weather at a place. body "" means the flown ship's body; altitude is metres above the ground. Needs KL_CAP_READ. */
int kl_weather_at(kl_client *client, const char *body, double latitude, double longitude, double altitude, kl_weather *out);
/* Overrides the weather until the player changes it. mode is "metar" (arg is a METAR line), "preset" (arg is a preset id such as "clear", "storm") or "auto" (back to simulated weather). Needs KL_CAP_WORLD. */
int kl_set_weather(kl_client *client, const char *mode, const char *arg);

/* ---- Events (protocol 2) */
/* Starts or stops delivery of an opt-in event to the event handler. Needs KL_CAP_READ. */
int kl_subscribe_event(kl_client *client, const char *name);
int kl_unsubscribe_event(kl_client *client, const char *name);

/* ---- Ships (protocol 2) */
enum
{
    KL_SHIP_FLOWN = 1,
    KL_SHIP_LANDED = 2,
    KL_SHIP_CRASHED = 4,
    KL_SHIP_FROM_LINK = 8 /* spawned by a link addon; only these can be removed */
};

typedef struct
{
    unsigned index; /* a ship's place in the list; it changes when an earlier ship is removed, so list again after a removal */
    char name[64], body[32];
    double latitude, longitude, altitude, heading, speed, vertical_speed;
    double distance; /* metres from the flown ship */
    unsigned flags;  /* KL_SHIP_* */
} kl_ship_info;

/* Lists ships within radius metres of the flown one (0: all). Fills up to max entries and sets *count to the number found. Needs KL_CAP_READ. */
int kl_list_ships(kl_client *client, double radius, int include_buildings, kl_ship_info *out, int max, int *count);
/* Deletes a ship that an addon spawned (never the flown one). Needs KL_CAP_WORLD. */
int kl_remove_ship(kl_client *client, unsigned index);
/* Moves a ship (any in the list, including the flown one) to a place: altitude 0 puts it on the ground, otherwise it flies level at that height (m) and speed (m/s). body "" is the flown ship's body. Needs KL_CAP_WORLD. */
int kl_place_ship(kl_client *client, unsigned index, const char *body, double latitude, double longitude, double altitude, double heading, double speed);

/* ---- Facilities (protocol 2) */
#define KL_MAX_RUNWAYS 8
#define KL_MAX_NAVAIDS 16

typedef struct
{
    char name[24];
    double latitude, longitude, heading, length, width; /* the runway's centre, its direction (degrees true), metres */
    int ils_count;
    double ils[4]; /* ILS frequencies in MHz */
} kl_runway;

typedef struct
{
    char type[16], id[24], name[48];
    double latitude, longitude, frequency, range;
} kl_navaid;

typedef struct
{
    char id[48], name[64], icao[8], body[32];
    double latitude, longitude, elevation, heading;
    double distance; /* metres from the point asked about */
    int runway_count, navaid_count; /* the number stored below (more may exist) */
    kl_runway runways[KL_MAX_RUNWAYS];
    kl_navaid navaids[KL_MAX_NAVAIDS];
} kl_facility;

/* Lists bases near a point, nearest first. kind is "airport" (has runways), "base" (no runways) or "" for both. body "" is the flown ship's body, radius_km 0 means any distance. Fills up to max entries (at most 200 are sent) and sets *count. Needs KL_CAP_READ. */
int kl_list_facilities(kl_client *client, const char *kind, const char *body, double latitude, double longitude, double radius_km, kl_facility *out, int max, int *count);

/* ---- MFD pages */
/* Adds a page the player can pick on any MFD. Draw it when you receive the "mfd" event with this id. */
int kl_define_mfd(kl_client *client, const char *id, const char *title);
/* Draw commands are collected, then sent as one page. The screen is 400 x 400 units, origin top left. */
void kl_draw_begin(kl_client *client);
void kl_draw_colour(kl_client *client, int r, int g, int b);
void kl_draw_line(kl_client *client, float x1, float y1, float x2, float y2);
void kl_draw_rect(kl_client *client, float x1, float y1, float x2, float y2);
void kl_draw_fillrect(kl_client *client, float x1, float y1, float x2, float y2);
void kl_draw_circle(kl_client *client, float x, float y, float radius);
void kl_draw_disc(kl_client *client, float x, float y, float radius);
void kl_draw_tri(kl_client *client, float x1, float y1, float x2, float y2, float x3, float y3);
/* align: 0 left, 1 centre, 2 right */
void kl_draw_text(kl_client *client, float x, float y, const char *text, int align);
/* A bitmap from your addon folder, centred at x,y, drawn w by h, rotated by angle degrees. */
void kl_draw_image(kl_client *client, const char *file, float cx, float cy, float w, float h, float angle);
int kl_draw_end(kl_client *client, const char *mfd_id);

/* ---- Windows and notices (KL_CAP_UI) */
/* Widgets are collected, then sent as one window that replaces the previous contents. Widget ids identify what changed in the "ui" event. */
void kl_ui_begin(kl_client *client);
void kl_ui_label(kl_client *client, const char *text);
void kl_ui_button(kl_client *client, const char *id, const char *label);
void kl_ui_checkbox(kl_client *client, const char *id, const char *label, int value);
void kl_ui_slider(kl_client *client, const char *id, const char *label, double value, double low, double high);
void kl_ui_text(kl_client *client, const char *id, const char *label, const char *value);
void kl_ui_separator(kl_client *client);
/* Shows (or updates) the window. At most 8 windows of 64 widgets each. The player can close it; you then get a "ui" event with an empty widget and value "closed". */
int kl_ui_show(kl_client *client, const char *window_id, const char *title);
int kl_ui_close(kl_client *client, const char *window_id);
/* A short message on the HUD. */
int kl_ui_notify(kl_client *client, const char *text, double seconds);
/* Splits the argument of a "ui" event ("window widget value") into its parts. Returns 0 on success. */
int kl_parse_ui_event(const char *arg, char *window, size_t window_size, char *widget, size_t widget_size, char *value, size_t value_size);

/* ---- Lockstep (KL_CAP_CONTROLS) */
/* While on, Kosmos sends a "step" event before every simulation step and waits (at most max_wait_ms, default 100) for kl_step_done. Writes made before that apply to the step. Only one program may hold lockstep; time warp is held at 1x. fixed_dt (0.001 to 0.1 s) makes every step that long; 0 uses the frame time. */
int kl_lockstep(kl_client *client, int enable, int max_wait_ms, double fixed_dt);
int kl_step_done(kl_client *client, unsigned frame);
/* Splits the argument of a "step" event. Returns 0 on success. */
int kl_parse_step(const char *arg, unsigned *frame, double *dt, double *sim_time);

/* ---- Vessel modules (KL_CAP_VESSEL) */
/* Takes over a ship (an index from kl_list_ships): your controls replace the player's (or add to them with passthrough). The flown ship needs the player's consent in the Native addons window. Released when you disconnect. */
int kl_claim_ship(kl_client *client, unsigned index, int passthrough);
int kl_release_ship(kl_client *client, unsigned index);
/* Publishes a number at /ships/ship[index]/module/<your id>/<name> for MFDs, scripts and panels. Names starting "controls/" are the ship's controls: controls/throttle, controls/pitch, controls/yaw, controls/roll, controls/gear, controls/sas and so on, as under /controls/. */
int kl_ship_value(kl_client *client, unsigned index, const char *name, double value);
/* Force (N) and torque (N m) in the ship's own axes, held until replaced and limited by the ship's "module" block (or 20 N and 20 N m per kg). Send zeros to stop. */
int kl_ship_force(kl_client *client, unsigned index, const double force[3], const double torque[3]);

/* ---- 3D drawing (KL_CAP_DRAW3D) */
/* Primitives are collected, then sent as one layer that replaces the previous one (an empty layer removes it); at most 2000 per layer and 8 layers. Frames: KL_FRAME_LLA is latitude, longitude (degrees) and altitude (m) above the mean radius of the body the flown ship is at; KL_FRAME_BODY is metres in the body's fixed axes (x through latitude 0 longitude 0, y the north pole); KL_FRAME_SHIP is metres from the ship's centre of mass in its own axes (ship is the kl_list_ships index). Depth hides what the planet itself covers. */
enum { KL_FRAME_LLA = 0, KL_FRAME_BODY = 1, KL_FRAME_SHIP = 2 };
void kl_3d_begin(kl_client *client);
void kl_3d_colour(kl_client *client, int r, int g, int b, int a);
void kl_3d_depth(kl_client *client, int hidden_by_planet);
void kl_3d_line(kl_client *client, int frame, unsigned ship, double ax, double ay, double az, double bx, double by, double bz);
/* radius_px is the size on screen */
void kl_3d_marker(kl_client *client, int frame, unsigned ship, double x, double y, double z, double radius_px);
void kl_3d_label(kl_client *client, int frame, unsigned ship, double x, double y, double z, const char *text);
/* A flat circle about the point, level with the ground there (or the ship's up), radius in metres. */
void kl_3d_ring(kl_client *client, int frame, unsigned ship, double x, double y, double z, double radius_m);
/* Three circles that make a sphere of radius_m. */
void kl_3d_sphere(kl_client *client, int frame, unsigned ship, double x, double y, double z, double radius_m);
int kl_3d_end(kl_client *client, const char *layer_id);

/* ---- Misc */
int kl_log(kl_client *client, const char *text); /* shows in the Lua console window */
int kl_ping(kl_client *client);                  /* round trip to Kosmos */

#ifdef __cplusplus
}
#endif
#endif
