/*
 * KosmosLink client library, protocol version 1.
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

#define KL_PROTOCOL_VERSION 1
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
    KL_CAP_ALL = 63
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
/* Called for events: "ship", "mfd" (draw the page named in arg), "granted", "shutdown". */
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

/* ---- Misc */
int kl_log(kl_client *client, const char *text); /* shows in the Lua console window */
int kl_ping(kl_client *client);                  /* round trip to Kosmos */

#ifdef __cplusplus
}
#endif
#endif
