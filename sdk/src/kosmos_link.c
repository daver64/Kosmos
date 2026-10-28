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
    MSG_WELCOME = 101, MSG_ERROR = 102, MSG_VALUE = 103, MSG_EVENT = 104, MSG_OK = 105, MSG_PONG = 106
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
