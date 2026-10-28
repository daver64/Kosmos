#include "kosmos_link.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET kl_socket;
#define KL_BAD_SOCKET INVALID_SOCKET
#define kl_close_socket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int kl_socket;
#define KL_BAD_SOCKET (-1)
#define kl_close_socket close
#endif

enum
{
    MSG_HELLO = 1, MSG_SUBSCRIBE = 2, MSG_UNSUBSCRIBE = 3, MSG_GET = 4, MSG_SET = 5, MSG_DEFINE_KEY = 6, MSG_SPAWN_SHIP = 7,
    MSG_RELEASE = 8, MSG_GIVE = 9, MSG_DEFINE_MFD = 10, MSG_DRAW = 11, MSG_LOG = 12, MSG_PING = 13,
    MSG_WEATHER_AT = 14, MSG_SET_WEATHER = 15, MSG_SUBSCRIBE_EVENT = 16, MSG_UNSUBSCRIBE_EVENT = 17, MSG_LIST_SHIPS = 18,
    MSG_REMOVE_SHIP = 19, MSG_PLACE_SHIP = 20, MSG_LIST_FACILITIES = 21,
    MSG_UI_WINDOW = 22, MSG_UI_CLOSE = 23, MSG_UI_NOTIFY = 24, MSG_LOCKSTEP = 25, MSG_STEP_DONE = 26, MSG_CLAIM_SHIP = 27,
    MSG_RELEASE_SHIP = 28, MSG_SHIP_VALUE = 29, MSG_SHIP_FORCE = 30, MSG_DRAW3D = 31,
    MSG_WELCOME = 101, MSG_ERROR = 102, MSG_VALUE = 103, MSG_EVENT = 104, MSG_OK = 105, MSG_PONG = 106, MSG_WEATHER = 107, MSG_SHIP_LIST = 108, MSG_FACILITY_LIST = 109
};

#define MAX_FRAME (1u << 20)
#define REPLY_TIMEOUT_MS 5000
#define MAX_DRAW 2000

/* A growable byte buffer. */
typedef struct
{
    unsigned char *data;
    size_t size, cap;
} buf;

static void buf_reserve(buf *b, size_t extra)
{
    if (b->size + extra <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->size + extra)
        cap *= 2;
    unsigned char *p = (unsigned char *)realloc(b->data, cap);
    if (!p)
        abort();
    b->data = p;
    b->cap = cap;
}

static void put_u8(buf *b, unsigned v)
{
    buf_reserve(b, 1);
    b->data[b->size++] = (unsigned char)v;
}

static void put_u16(buf *b, unsigned v)
{
    put_u8(b, v & 0xff);
    put_u8(b, (v >> 8) & 0xff);
}

static void put_u32(buf *b, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        put_u8(b, (v >> (8 * i)) & 0xff);
}

static void put_f32(buf *b, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);
    put_u32(b, bits);
}

static void put_f64(buf *b, double v)
{
    uint64_t bits;
    memcpy(&bits, &v, 8);
    put_u32(b, (uint32_t)bits);
    put_u32(b, (uint32_t)(bits >> 32));
}

static void put_str(buf *b, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n > 65535)
        n = 65535;
    put_u16(b, (unsigned)n);
    buf_reserve(b, n);
    if (n)
        memcpy(b->data + b->size, s, n);
    b->size += n;
}

static void put_value(buf *b, const kl_value *v)
{
    put_u8(b, (unsigned)v->type);
    switch (v->type)
    {
    case KL_BOOL: put_u8(b, v->boolean ? 1 : 0); break;
    case KL_INT: put_u32(b, (uint32_t)(int32_t)v->integer); break;
    case KL_DOUBLE: put_f64(b, v->real); break;
    case KL_STRING: put_str(b, v->string); break;
    default: break;
    }
}

/* A cursor over a received payload. */
typedef struct
{
    const unsigned char *p;
    size_t n, pos;
    int ok;
} reader;

static int rd_take(reader *r, size_t k)
{
    if (!r->ok || r->pos + k > r->n)
    {
        r->ok = 0;
        return 0;
    }
    r->pos += k;
    return 1;
}

static unsigned rd_u8(reader *r) { return rd_take(r, 1) ? r->p[r->pos - 1] : 0; }
static unsigned rd_u16(reader *r) { unsigned a = rd_u8(r); unsigned b = rd_u8(r); return a | (b << 8); }
static uint32_t rd_u32(reader *r) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t)rd_u8(r) << (8 * i); return v; }
static double rd_f64(reader *r)
{
    uint64_t lo = rd_u32(r), hi = rd_u32(r), bits = lo | (hi << 32);
    double v;
    memcpy(&v, &bits, 8);
    return v;
}

static void rd_str(reader *r, char *out, size_t out_size)
{
    size_t len = rd_u16(r);
    if (!rd_take(r, len))
    {
        if (out_size)
            out[0] = 0;
        return;
    }
    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, r->p + r->pos - len, copy);
    out[copy] = 0;
}

static void rd_value(reader *r, kl_value *v)
{
    memset(v, 0, sizeof(*v));
    v->type = (kl_type)rd_u8(r);
    switch (v->type)
    {
    case KL_BOOL: v->boolean = rd_u8(r) != 0; break;
    case KL_INT: v->integer = (int32_t)rd_u32(r); break;
    case KL_DOUBLE: v->real = rd_f64(r); break;
    case KL_STRING: rd_str(r, v->string, sizeof(v->string)); break;
    case KL_NONE: break;
    default: r->ok = 0;
    }
}

struct kl_client
{
    kl_socket sock;
    buf in;      /* bytes received, not yet cut into frames */
    size_t in_start;
    buf draw;    /* the page being drawn */
    unsigned draw_count;
    int colour[3];
    buf ui;
    unsigned ui_count;
    buf d3;
    unsigned d3_count;
    int d3_colour[4];
    int d3_depth;
    uint32_t next_request;
    unsigned granted;
    char sim_version[64];
    char error[256];
    char addon_id[64];
    kl_value_fn on_value;
    void *value_user;
    kl_event_fn on_event;
    void *event_user;
    kl_error_fn on_error;
    void *error_user;
    /* The reply being waited for. */
    uint32_t waiting;
    int reply_type, reply_error;
    kl_value reply_value;
    uint32_t reply_handle;
    int reply_ready;
    buf reply_payload; /* the body of a weather, ship list or facility list reply */
};

static void set_error(kl_client *c, const char *text)
{
    snprintf(c->error, sizeof(c->error), "%s", text);
}

static int error_from_code(unsigned code)
{
    switch (code)
    {
    case 1: return KL_ERR_BAD_REQUEST;
    case 2: return KL_ERR_UNKNOWN;
    case 3: return KL_ERR_DENIED;
    case 5: return KL_ERR_VERSION;
    default: return KL_ERR_FAILED;
    }
}

static int send_all(kl_client *c, const unsigned char *data, size_t n)
{
    while (n)
    {
#ifdef MSG_NOSIGNAL
        int sent = (int)send(c->sock, (const char *)data, (int)n, MSG_NOSIGNAL);
#else
        int sent = (int)send(c->sock, (const char *)data, (int)n, 0);
#endif
        if (sent <= 0)
        {
            set_error(c, "connection lost");
            return KL_ERR_IO;
        }
        data += sent;
        n -= (size_t)sent;
    }
    return KL_OK;
}

/* Starts a frame; finish_frame fills in its length and sends it. */
static void begin_frame(buf *b, unsigned type, uint32_t request)
{
    b->size = 0;
    put_u32(b, 0);
    put_u16(b, type);
    put_u32(b, request);
}

static int finish_frame(kl_client *c, buf *b)
{
    uint32_t len = (uint32_t)(b->size - 4);
    for (int i = 0; i < 4; ++i)
        b->data[i] = (unsigned char)((len >> (8 * i)) & 0xff);
    return send_all(c, b->data, b->size);
}

static void dispatch(kl_client *c, unsigned type, uint32_t request, const unsigned char *payload, size_t size)
{
    reader r = {payload, size, 0, 1};
    if (type == MSG_VALUE)
    {
        char path[256];
        kl_value v;
        rd_str(&r, path, sizeof(path));
        rd_value(&r, &v);
        if (!r.ok)
            return;
        if (request && request == c->waiting)
        {
            c->reply_type = type;
            c->reply_value = v;
            c->reply_ready = 1;
        }
        else if (c->on_value)
            c->on_value(path, &v, c->value_user);
    }
    else if (type == MSG_EVENT)
    {
        char name[64], arg[256];
        rd_str(&r, name, sizeof(name));
        rd_str(&r, arg, sizeof(arg));
        if (!r.ok)
            return;
        if (strcmp(name, "granted") == 0)
            c->granted = (unsigned)strtoul(arg, NULL, 10);
        if (c->on_event)
            c->on_event(name, arg, c->event_user);
    }
    else if (type == MSG_ERROR)
    {
        unsigned code = rd_u16(&r);
        char message[256];
        rd_str(&r, message, sizeof(message));
        if (request && request == c->waiting)
        {
            c->reply_type = type;
            c->reply_error = error_from_code(code);
            set_error(c, message);
            c->reply_ready = 1;
        }
        else
        {
            set_error(c, message);
            if (c->on_error)
                c->on_error(error_from_code(code), message, c->error_user);
        }
    }
    else if (type == MSG_WEATHER || type == MSG_SHIP_LIST || type == MSG_FACILITY_LIST)
    {
        if (request && request == c->waiting)
        {
            c->reply_payload.size = 0;
            buf_reserve(&c->reply_payload, size);
            memcpy(c->reply_payload.data, payload, size);
            c->reply_payload.size = size;
            c->reply_type = type;
            c->reply_ready = 1;
        }
    }
    else if (type == MSG_PING)
    {
        /* Kosmos checks that the program still answers; any message will do. */
        buf f = {0};
        begin_frame(&f, MSG_PING, 0);
        finish_frame(c, &f);
        free(f.data);
    }
    else if (type == MSG_OK || type == MSG_PONG)
    {
        if (request && request == c->waiting)
        {
            c->reply_type = type;
            c->reply_handle = type == MSG_OK ? rd_u32(&r) : 0;
            c->reply_ready = 1;
        }
    }
    else if (type == MSG_WELCOME)
    {
        if (request && request == c->waiting)
        {
            unsigned version = rd_u16(&r);
            rd_str(&r, c->sim_version, sizeof(c->sim_version));
            c->granted = rd_u32(&r);
            (void)version;
            c->reply_type = type;
            c->reply_ready = 1;
        }
    }
}

/* Reads from the socket for up to timeout_ms and handles every whole frame. Returns the number handled, or KL_ERR_IO. */
static int pump(kl_client *c, int timeout_ms)
{
    int handled = 0;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(c->sock, &set);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ready = select((int)c->sock + 1, &set, NULL, NULL, &tv);
    if (ready > 0)
    {
        buf_reserve(&c->in, 4096);
        int got = (int)recv(c->sock, (char *)c->in.data + c->in.size, (int)(c->in.cap - c->in.size), 0);
        if (got <= 0)
        {
            set_error(c, "connection closed");
            return KL_ERR_IO;
        }
        c->in.size += (size_t)got;
    }
    else if (ready < 0)
    {
        set_error(c, "connection failed");
        return KL_ERR_IO;
    }
    for (;;)
    {
        size_t avail = c->in.size - c->in_start;
        if (avail < 4)
            break;
        const unsigned char *p = c->in.data + c->in_start;
        uint32_t len = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        if (len < 6 || len > MAX_FRAME)
        {
            set_error(c, "bad data from Kosmos");
            return KL_ERR_IO;
        }
        if (avail < 4 + (size_t)len)
            break;
        unsigned type = p[4] | (p[5] << 8);
        uint32_t request = (uint32_t)p[6] | ((uint32_t)p[7] << 8) | ((uint32_t)p[8] << 16) | ((uint32_t)p[9] << 24);
        dispatch(c, type, request, p + 10, len - 6);
        c->in_start += 4 + (size_t)len;
        ++handled;
    }
    if (c->in_start == c->in.size)
        c->in.size = c->in_start = 0;
    else if (c->in_start > 65536)
    {
        memmove(c->in.data, c->in.data + c->in_start, c->in.size - c->in_start);
        c->in.size -= c->in_start;
        c->in_start = 0;
    }
    return handled;
}

/* Sends a request frame (already begun in scratch with the request id) and waits for its reply. */
static int request(kl_client *c, buf *frame, uint32_t id)
{
    c->waiting = id;
    c->reply_ready = 0;
    c->reply_error = 0;
    c->reply_handle = 0;
    memset(&c->reply_value, 0, sizeof(c->reply_value));
    int r = finish_frame(c, frame);
    if (r)
        return r;
    int waited = 0;
    while (!c->reply_ready)
    {
        if (waited >= REPLY_TIMEOUT_MS)
        {
            set_error(c, "Kosmos did not answer");
            c->waiting = 0;
            return KL_ERR_TIMEOUT;
        }
        int got = pump(c, 50);
        if (got < 0)
        {
            c->waiting = 0;
            return got;
        }
        if (got == 0)
            waited += 50;
    }
    c->waiting = 0;
    return c->reply_type == MSG_ERROR ? c->reply_error : KL_OK;
}

static uint32_t new_request(kl_client *c)
{
    if (++c->next_request == 0)
        c->next_request = 1;
    return c->next_request;
}

void kl_options_from_env(kl_options *o)
{
    static char host[128];
    const char *link = getenv("KOSMOS_LINK");
    const char *token = getenv("KOSMOS_LINK_TOKEN");
    if (link && *link)
    {
        const char *colon = strrchr(link, ':');
        size_t n = colon ? (size_t)(colon - link) : strlen(link);
        if (n >= sizeof(host))
            n = sizeof(host) - 1;
        memcpy(host, link, n);
        host[n] = 0;
        o->host = host;
        if (colon)
            o->port = atoi(colon + 1);
    }
    if (token && *token)
        o->token = token;
}

kl_client *kl_connect(const kl_options *o, char *err, size_t err_size)
{
    char scratch[256];
    if (!err || !err_size)
    {
        err = scratch;
        err_size = sizeof(scratch);
    }
    if (!o || !o->addon_id || !*o->addon_id)
    {
        snprintf(err, err_size, "addon_id is required");
        return NULL;
    }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        snprintf(err, err_size, "WSAStartup failed");
        return NULL;
    }
#endif
    kl_socket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == KL_BAD_SOCKET)
    {
        snprintf(err, err_size, "cannot create a socket");
        return NULL;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)(o->port ? o->port : KL_DEFAULT_PORT));
    if (inet_pton(AF_INET, o->host && *o->host ? o->host : "127.0.0.1", &addr.sin_addr) != 1)
    {
        snprintf(err, err_size, "host must be an IPv4 address such as 127.0.0.1");
        kl_close_socket(s);
        return NULL;
    }
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        snprintf(err, err_size, "cannot connect to Kosmos (is it running, and is the link enabled?)");
        kl_close_socket(s);
        return NULL;
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));

    kl_client *c = (kl_client *)calloc(1, sizeof(*c));
    c->sock = s;
    c->d3_colour[0] = c->d3_colour[1] = c->d3_colour[2] = c->d3_colour[3] = 255;
    c->d3_depth = 1;
    c->colour[0] = 120;
    c->colour[1] = 255;
    c->colour[2] = 140;
    snprintf(c->addon_id, sizeof(c->addon_id), "%s", o->addon_id);

    buf frame = {0};
    uint32_t id = new_request(c);
    begin_frame(&frame, MSG_HELLO, id);
    put_u16(&frame, KL_PROTOCOL_VERSION);
    put_str(&frame, o->addon_id);
    put_str(&frame, o->name ? o->name : o->addon_id);
    put_str(&frame, o->version ? o->version : "");
    put_str(&frame, o->token ? o->token : "");
    put_u32(&frame, o->capabilities);
    int r = request(c, &frame, id);
    free(frame.data);
    if (r != KL_OK)
    {
        snprintf(err, err_size, "%s", c->error[0] ? c->error : "hello failed");
        kl_close(c);
        return NULL;
    }
    return c;
}

void kl_close(kl_client *c)
{
    if (!c)
        return;
    kl_close_socket(c->sock);
    free(c->in.data);
    free(c->draw.data);
    free(c->ui.data);
    free(c->d3.data);
    free(c->reply_payload.data);
    free(c);
}

unsigned kl_granted(const kl_client *c) { return c->granted; }
const char *kl_sim_version(const kl_client *c) { return c->sim_version; }
const char *kl_error(const kl_client *c) { return c->error; }

void kl_set_value_handler(kl_client *c, kl_value_fn fn, void *user) { c->on_value = fn; c->value_user = user; }
void kl_set_event_handler(kl_client *c, kl_event_fn fn, void *user) { c->on_event = fn; c->event_user = user; }
void kl_set_error_handler(kl_client *c, kl_error_fn fn, void *user) { c->on_error = fn; c->error_user = user; }

int kl_poll(kl_client *c, int timeout_ms) { return pump(c, timeout_ms < 0 ? 0 : timeout_ms); }

int kl_subscribe(kl_client *c, const char *path, int hz)
{
    if (!path || hz < 0)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_SUBSCRIBE, 0);
    put_str(&frame, path);
    put_u16(&frame, (unsigned)hz);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_unsubscribe(kl_client *c, const char *path)
{
    if (!path)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_UNSUBSCRIBE, 0);
    put_str(&frame, path);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_get(kl_client *c, const char *path, kl_value *out)
{
    if (!path || !out)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t id = new_request(c);
    begin_frame(&frame, MSG_GET, id);
    put_str(&frame, path);
    int r = request(c, &frame, id);
    free(frame.data);
    if (r == KL_OK)
        *out = c->reply_value;
    return r;
}

int kl_set(kl_client *c, const char *path, const kl_value *value)
{
    if (!path || !value)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_SET, 0);
    put_str(&frame, path);
    put_value(&frame, value);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_set_bool(kl_client *c, const char *path, int v) { kl_value x; memset(&x, 0, sizeof(x)); x.type = KL_BOOL; x.boolean = v != 0; return kl_set(c, path, &x); }
int kl_set_int(kl_client *c, const char *path, int v) { kl_value x; memset(&x, 0, sizeof(x)); x.type = KL_INT; x.integer = v; return kl_set(c, path, &x); }
int kl_set_double(kl_client *c, const char *path, double v) { kl_value x; memset(&x, 0, sizeof(x)); x.type = KL_DOUBLE; x.real = v; return kl_set(c, path, &x); }
int kl_set_string(kl_client *c, const char *path, const char *v)
{
    kl_value x;
    memset(&x, 0, sizeof(x));
    x.type = KL_STRING;
    snprintf(x.string, sizeof(x.string), "%s", v ? v : "");
    return kl_set(c, path, &x);
}

int kl_define_key(kl_client *c, const char *id, const char *label, const char *group, const char *key, int shift, int hold)
{
    if (!id || !key)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_DEFINE_KEY, rid);
    put_str(&frame, id);
    put_str(&frame, label ? label : id);
    put_str(&frame, group ? group : "");
    put_str(&frame, key);
    put_u8(&frame, shift ? 1 : 0);
    put_u8(&frame, hold ? 2 : 1);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_spawn_ship(kl_client *c, const char *name, double x, double y, double z, unsigned *handle)
{
    if (!name)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_SPAWN_SHIP, rid);
    put_str(&frame, name);
    put_f64(&frame, x);
    put_f64(&frame, y);
    put_f64(&frame, z);
    int r = request(c, &frame, rid);
    free(frame.data);
    if (r == KL_OK && handle)
        *handle = c->reply_handle;
    return r;
}

int kl_release(kl_client *c, int hardpoint)
{
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_RELEASE, rid);
    put_u32(&frame, (uint32_t)(int32_t)hardpoint);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_give(kl_client *c, const char *id, int count)
{
    if (!id)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_GIVE, rid);
    put_str(&frame, id);
    put_u32(&frame, (uint32_t)(int32_t)count);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

static reader payload_reader(kl_client *c)
{
    reader r = {c->reply_payload.data, c->reply_payload.size, 0, 1};
    return r;
}

int kl_weather_at(kl_client *c, const char *body, double lat, double lon, double alt, kl_weather *out)
{
    if (!out)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_WEATHER_AT, rid);
    put_str(&frame, body ? body : "");
    put_f64(&frame, lat);
    put_f64(&frame, lon);
    put_f64(&frame, alt);
    int res = request(c, &frame, rid);
    free(frame.data);
    if (res != KL_OK)
        return res;
    reader r = payload_reader(c);
    memset(out, 0, sizeof(*out));
    out->wind_east = rd_f64(&r);
    out->wind_north = rd_f64(&r);
    out->wind_speed = rd_f64(&r);
    out->wind_from = rd_f64(&r);
    out->gust = rd_f64(&r);
    out->rain = rd_f64(&r);
    out->thunder = rd_f64(&r);
    out->cloud_cover = rd_f64(&r);
    out->cloud_base = rd_f64(&r);
    out->cloud_top = rd_f64(&r);
    out->visibility = rd_f64(&r);
    out->temperature = rd_f64(&r);
    out->pressure = rd_f64(&r);
    rd_str(&r, out->station, sizeof(out->station));
    rd_str(&r, out->metar, sizeof(out->metar));
    if (!r.ok)
    {
        set_error(c, "bad weather reply");
        return KL_ERR_IO;
    }
    return KL_OK;
}

int kl_set_weather(kl_client *c, const char *mode, const char *arg)
{
    if (!mode)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_SET_WEATHER, rid);
    put_str(&frame, mode);
    put_str(&frame, arg ? arg : "");
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

static int event_request(kl_client *c, unsigned type, const char *name)
{
    if (!name)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, type, rid);
    put_str(&frame, name);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_subscribe_event(kl_client *c, const char *name) { return event_request(c, MSG_SUBSCRIBE_EVENT, name); }
int kl_unsubscribe_event(kl_client *c, const char *name) { return event_request(c, MSG_UNSUBSCRIBE_EVENT, name); }

int kl_list_ships(kl_client *c, double radius, int include_buildings, kl_ship_info *out, int max, int *count)
{
    if (!out || max < 0)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_LIST_SHIPS, rid);
    put_f64(&frame, radius);
    put_u8(&frame, include_buildings ? 1 : 0);
    int res = request(c, &frame, rid);
    free(frame.data);
    if (res != KL_OK)
        return res;
    reader r = payload_reader(c);
    uint32_t n = rd_u32(&r);
    int stored = 0;
    for (uint32_t i = 0; i < n && r.ok; ++i)
    {
        kl_ship_info s;
        memset(&s, 0, sizeof(s));
        s.index = rd_u32(&r);
        rd_str(&r, s.name, sizeof(s.name));
        rd_str(&r, s.body, sizeof(s.body));
        s.latitude = rd_f64(&r);
        s.longitude = rd_f64(&r);
        s.altitude = rd_f64(&r);
        s.heading = rd_f64(&r);
        s.speed = rd_f64(&r);
        s.vertical_speed = rd_f64(&r);
        s.distance = rd_f64(&r);
        s.flags = rd_u8(&r);
        if (r.ok && stored < max)
            out[stored++] = s;
    }
    if (count)
        *count = stored;
    return r.ok ? KL_OK : KL_ERR_IO;
}

int kl_remove_ship(kl_client *c, unsigned index)
{
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_REMOVE_SHIP, rid);
    put_u32(&frame, index);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_place_ship(kl_client *c, unsigned index, const char *body, double lat, double lon, double alt, double heading, double speed)
{
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_PLACE_SHIP, rid);
    put_u32(&frame, index);
    put_str(&frame, body ? body : "");
    put_f64(&frame, lat);
    put_f64(&frame, lon);
    put_f64(&frame, alt);
    put_f64(&frame, heading);
    put_f64(&frame, speed);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_list_facilities(kl_client *c, const char *kind, const char *body, double lat, double lon, double radius_km, kl_facility *out, int max, int *count)
{
    if (!out || max < 0)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_LIST_FACILITIES, rid);
    put_str(&frame, kind ? kind : "");
    put_str(&frame, body ? body : "");
    put_f64(&frame, lat);
    put_f64(&frame, lon);
    put_f64(&frame, radius_km);
    put_u16(&frame, max > 200 ? 200 : (unsigned)max);
    int res = request(c, &frame, rid);
    free(frame.data);
    if (res != KL_OK)
        return res;
    reader r = payload_reader(c);
    uint32_t n = rd_u32(&r);
    int stored = 0;
    for (uint32_t i = 0; i < n && r.ok; ++i)
    {
        kl_facility f;
        memset(&f, 0, sizeof(f));
        rd_str(&r, f.id, sizeof(f.id));
        rd_str(&r, f.name, sizeof(f.name));
        rd_str(&r, f.icao, sizeof(f.icao));
        rd_str(&r, f.body, sizeof(f.body));
        f.latitude = rd_f64(&r);
        f.longitude = rd_f64(&r);
        f.elevation = rd_f64(&r);
        f.heading = rd_f64(&r);
        f.distance = rd_f64(&r);
        unsigned nr = rd_u16(&r);
        for (unsigned k = 0; k < nr && r.ok; ++k)
        {
            kl_runway rw;
            memset(&rw, 0, sizeof(rw));
            rd_str(&r, rw.name, sizeof(rw.name));
            rw.latitude = rd_f64(&r);
            rw.longitude = rd_f64(&r);
            rw.heading = rd_f64(&r);
            rw.length = rd_f64(&r);
            rw.width = rd_f64(&r);
            unsigned ni = rd_u8(&r);
            for (unsigned q = 0; q < ni && r.ok; ++q)
            {
                double mhz = rd_f64(&r);
                if (rw.ils_count < 4)
                    rw.ils[rw.ils_count++] = mhz;
            }
            if (f.runway_count < KL_MAX_RUNWAYS)
                f.runways[f.runway_count++] = rw;
        }
        unsigned nn = rd_u16(&r);
        for (unsigned k = 0; k < nn && r.ok; ++k)
        {
            kl_navaid nv;
            memset(&nv, 0, sizeof(nv));
            rd_str(&r, nv.type, sizeof(nv.type));
            rd_str(&r, nv.id, sizeof(nv.id));
            rd_str(&r, nv.name, sizeof(nv.name));
            nv.latitude = rd_f64(&r);
            nv.longitude = rd_f64(&r);
            nv.frequency = rd_f64(&r);
            nv.range = rd_f64(&r);
            if (f.navaid_count < KL_MAX_NAVAIDS)
                f.navaids[f.navaid_count++] = nv;
        }
        if (r.ok && stored < max)
            out[stored++] = f;
    }
    if (count)
        *count = stored;
    return r.ok ? KL_OK : KL_ERR_IO;
}

int kl_define_mfd(kl_client *c, const char *id, const char *title)
{
    if (!id)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_DEFINE_MFD, rid);
    put_str(&frame, id);
    put_str(&frame, title ? title : id);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

void kl_draw_begin(kl_client *c)
{
    c->draw.size = 0;
    c->draw_count = 0;
}

static void draw_cmd(kl_client *c, unsigned kind, float a, float b, float cc, float d, float e, float f, int align, const char *text)
{
    if (c->draw_count >= MAX_DRAW)
        return;
    put_u8(&c->draw, kind);
    put_f32(&c->draw, a);
    put_f32(&c->draw, b);
    put_f32(&c->draw, cc);
    put_f32(&c->draw, d);
    put_f32(&c->draw, e);
    put_f32(&c->draw, f);
    put_u8(&c->draw, (unsigned)c->colour[0]);
    put_u8(&c->draw, (unsigned)c->colour[1]);
    put_u8(&c->draw, (unsigned)c->colour[2]);
    put_u8(&c->draw, (unsigned)align);
    put_str(&c->draw, text ? text : "");
    ++c->draw_count;
}

static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

void kl_draw_colour(kl_client *c, int r, int g, int b) { c->colour[0] = clamp8(r); c->colour[1] = clamp8(g); c->colour[2] = clamp8(b); }
void kl_draw_line(kl_client *c, float x1, float y1, float x2, float y2) { draw_cmd(c, 0, x1, y1, x2, y2, 0, 0, 0, NULL); }
void kl_draw_rect(kl_client *c, float x1, float y1, float x2, float y2) { draw_cmd(c, 1, x1, y1, x2, y2, 0, 0, 0, NULL); }
void kl_draw_fillrect(kl_client *c, float x1, float y1, float x2, float y2) { draw_cmd(c, 2, x1, y1, x2, y2, 0, 0, 0, NULL); }
void kl_draw_circle(kl_client *c, float x, float y, float r) { draw_cmd(c, 3, x, y, r, 0, 0, 0, 0, NULL); }
void kl_draw_disc(kl_client *c, float x, float y, float r) { draw_cmd(c, 4, x, y, r, 0, 0, 0, 0, NULL); }
void kl_draw_tri(kl_client *c, float x1, float y1, float x2, float y2, float x3, float y3) { draw_cmd(c, 5, x1, y1, x2, y2, x3, y3, 0, NULL); }
void kl_draw_text(kl_client *c, float x, float y, const char *text, int align) { draw_cmd(c, 6, x, y, 0, 0, 0, 0, align, text); }
void kl_draw_image(kl_client *c, const char *file, float cx, float cy, float w, float h, float angle) { draw_cmd(c, 7, cx, cy, w, h, angle, 0, 0, file); }

int kl_draw_end(kl_client *c, const char *mfd_id)
{
    if (!mfd_id)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_DRAW, 0);
    put_str(&frame, mfd_id);
    put_u16(&frame, c->draw_count);
    buf_reserve(&frame, c->draw.size);
    if (c->draw.size)
        memcpy(frame.data + frame.size, c->draw.data, c->draw.size);
    frame.size += c->draw.size;
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

#define MAX_UI 64

static void ui_widget(kl_client *c, unsigned kind, const char *id, const char *label, const char *text, double value, double low, double high)
{
    if (c->ui_count >= MAX_UI)
        return;
    put_u8(&c->ui, kind);
    put_str(&c->ui, id);
    put_str(&c->ui, label);
    put_str(&c->ui, text);
    put_f64(&c->ui, value);
    put_f64(&c->ui, low);
    put_f64(&c->ui, high);
    ++c->ui_count;
}

void kl_ui_begin(kl_client *c) { c->ui.size = 0; c->ui_count = 0; }
void kl_ui_label(kl_client *c, const char *text) { ui_widget(c, 0, "", text, "", 0, 0, 0); }
void kl_ui_button(kl_client *c, const char *id, const char *label) { ui_widget(c, 1, id, label, "", 0, 0, 0); }
void kl_ui_checkbox(kl_client *c, const char *id, const char *label, int value) { ui_widget(c, 2, id, label, "", value ? 1 : 0, 0, 0); }
void kl_ui_slider(kl_client *c, const char *id, const char *label, double value, double low, double high) { ui_widget(c, 3, id, label, "", value, low, high); }
void kl_ui_text(kl_client *c, const char *id, const char *label, const char *value) { ui_widget(c, 4, id, label, value, 0, 0, 0); }
void kl_ui_separator(kl_client *c) { ui_widget(c, 5, "", "", "", 0, 0, 0); }

static int finish_with_body(kl_client *c, buf *frame, const buf *body, unsigned count)
{
    put_u16(frame, count);
    buf_reserve(frame, body->size);
    if (body->size)
        memcpy(frame->data + frame->size, body->data, body->size);
    frame->size += body->size;
    int r = finish_frame(c, frame);
    free(frame->data);
    return r;
}

int kl_ui_show(kl_client *c, const char *window_id, const char *title)
{
    if (!window_id)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_UI_WINDOW, 0);
    put_str(&frame, window_id);
    put_str(&frame, title ? title : window_id);
    return finish_with_body(c, &frame, &c->ui, c->ui_count);
}

int kl_ui_close(kl_client *c, const char *window_id)
{
    if (!window_id)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_UI_CLOSE, 0);
    put_str(&frame, window_id);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_ui_notify(kl_client *c, const char *text, double seconds)
{
    if (!text)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_UI_NOTIFY, 0);
    put_str(&frame, text);
    put_f64(&frame, seconds);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_parse_ui_event(const char *arg, char *window, size_t window_size, char *widget, size_t widget_size, char *value, size_t value_size)
{
    if (!arg || !window || !widget || !value || !window_size || !widget_size || !value_size)
        return KL_ERR_ARGUMENT;
    /* "window widget value": the widget may be empty and the value may hold spaces */
    const char *sp1 = strchr(arg, ' ');
    if (!sp1)
        return KL_ERR_BAD_REQUEST;
    const char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2)
        return KL_ERR_BAD_REQUEST;
    snprintf(window, window_size, "%.*s", (int)(sp1 - arg), arg);
    snprintf(widget, widget_size, "%.*s", (int)(sp2 - sp1 - 1), sp1 + 1);
    snprintf(value, value_size, "%s", sp2 + 1);
    return KL_OK;
}

int kl_lockstep(kl_client *c, int enable, int max_wait_ms, double fixed_dt)
{
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_LOCKSTEP, rid);
    put_u8(&frame, enable ? 1 : 0);
    put_u32(&frame, (uint32_t)(max_wait_ms < 0 ? 0 : max_wait_ms));
    put_f64(&frame, fixed_dt);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_step_done(kl_client *c, unsigned frame_number)
{
    buf frame = {0};
    begin_frame(&frame, MSG_STEP_DONE, 0);
    put_u32(&frame, frame_number);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_parse_step(const char *arg, unsigned *frame, double *dt, double *sim_time)
{
    unsigned f = 0;
    double d = 0, t = 0;
    if (!arg || sscanf(arg, "%u %lf %lf", &f, &d, &t) != 3)
        return KL_ERR_BAD_REQUEST;
    if (frame) *frame = f;
    if (dt) *dt = d;
    if (sim_time) *sim_time = t;
    return KL_OK;
}

int kl_claim_ship(kl_client *c, unsigned index, int passthrough)
{
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_CLAIM_SHIP, rid);
    put_u32(&frame, index);
    put_u8(&frame, passthrough ? 1 : 0);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_release_ship(kl_client *c, unsigned index)
{
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_RELEASE_SHIP, rid);
    put_u32(&frame, index);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}

int kl_ship_value(kl_client *c, unsigned index, const char *name, double value)
{
    if (!name)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_SHIP_VALUE, 0);
    put_u32(&frame, index);
    put_str(&frame, name);
    put_f64(&frame, value);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_ship_force(kl_client *c, unsigned index, const double force[3], const double torque[3])
{
    if (!force || !torque)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_SHIP_FORCE, 0);
    put_u32(&frame, index);
    for (int i = 0; i < 3; ++i)
        put_f64(&frame, force[i]);
    for (int i = 0; i < 3; ++i)
        put_f64(&frame, torque[i]);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

static void prim3(kl_client *c, unsigned kind, int frame, unsigned ship, const double a[3], const double b[3], double size, const char *text)
{
    if (c->d3_count >= MAX_DRAW)
        return;
    put_u8(&c->d3, kind);
    put_u8(&c->d3, (unsigned)frame);
    put_u32(&c->d3, ship);
    for (int i = 0; i < 3; ++i)
        put_f64(&c->d3, a[i]);
    for (int i = 0; i < 3; ++i)
        put_f64(&c->d3, b[i]);
    put_f64(&c->d3, size);
    put_u8(&c->d3, (unsigned)c->d3_colour[0]);
    put_u8(&c->d3, (unsigned)c->d3_colour[1]);
    put_u8(&c->d3, (unsigned)c->d3_colour[2]);
    put_u8(&c->d3, (unsigned)c->d3_colour[3]);
    put_u8(&c->d3, (unsigned)c->d3_depth);
    put_str(&c->d3, text ? text : "");
    ++c->d3_count;
}

void kl_3d_begin(kl_client *c) { c->d3.size = 0; c->d3_count = 0; }
void kl_3d_colour(kl_client *c, int r, int g, int b, int a) { c->d3_colour[0] = clamp8(r); c->d3_colour[1] = clamp8(g); c->d3_colour[2] = clamp8(b); c->d3_colour[3] = clamp8(a); }
void kl_3d_depth(kl_client *c, int hidden_by_planet) { c->d3_depth = hidden_by_planet ? 1 : 0; }

void kl_3d_line(kl_client *c, int frame, unsigned ship, double ax, double ay, double az, double bx, double by, double bz)
{
    const double a[3] = {ax, ay, az}, b[3] = {bx, by, bz};
    prim3(c, 0, frame, ship, a, b, 0, NULL);
}

void kl_3d_marker(kl_client *c, int frame, unsigned ship, double x, double y, double z, double radius_px)
{
    const double a[3] = {x, y, z}, b[3] = {0, 0, 0};
    prim3(c, 1, frame, ship, a, b, radius_px, NULL);
}

void kl_3d_label(kl_client *c, int frame, unsigned ship, double x, double y, double z, const char *text)
{
    const double a[3] = {x, y, z}, b[3] = {0, 0, 0};
    prim3(c, 2, frame, ship, a, b, 0, text);
}

void kl_3d_ring(kl_client *c, int frame, unsigned ship, double x, double y, double z, double radius_m)
{
    const double a[3] = {x, y, z}, b[3] = {0, 0, 0};
    prim3(c, 3, frame, ship, a, b, radius_m, NULL);
}

void kl_3d_sphere(kl_client *c, int frame, unsigned ship, double x, double y, double z, double radius_m)
{
    const double a[3] = {x, y, z}, b[3] = {0, 0, 0};
    prim3(c, 4, frame, ship, a, b, radius_m, NULL);
}

int kl_3d_end(kl_client *c, const char *layer_id)
{
    if (!layer_id)
        return KL_ERR_ARGUMENT;
    buf frame = {0};
    begin_frame(&frame, MSG_DRAW3D, 0);
    put_str(&frame, layer_id);
    return finish_with_body(c, &frame, &c->d3, c->d3_count);
}

int kl_log(kl_client *c, const char *text)
{
    buf frame = {0};
    begin_frame(&frame, MSG_LOG, 0);
    put_str(&frame, text);
    int r = finish_frame(c, &frame);
    free(frame.data);
    return r;
}

int kl_ping(kl_client *c)
{
    buf frame = {0};
    uint32_t rid = new_request(c);
    begin_frame(&frame, MSG_PING, rid);
    int r = request(c, &frame, rid);
    free(frame.data);
    return r;
}
