/* Reads the flown ship's altitude and speed twice a second and logs them in Kosmos. */
#include "kosmos_link.h"

#include <stdio.h>

static void on_value(const char *path, const kl_value *v, void *user)
{
    (void)user;
    printf("%s = %.1f\n", path, v->type == KL_DOUBLE ? v->real : (double)v->integer);
}

int main(void)
{
    kl_options o;
    char err[256];
    kl_options_from_env(&o);
    o.addon_id = "hello-props";
    o.name = "Hello props";
    o.version = "1.0";
    o.capabilities = KL_CAP_READ;
    kl_client *c = kl_connect(&o, err, sizeof err);
    if (!c)
    {
        fprintf(stderr, "cannot connect: %s\n", err);
        return 1;
    }
    kl_set_value_handler(c, on_value, NULL);
    kl_subscribe(c, "/ships/ship[0]/altitude", 2);
    kl_subscribe(c, "/ships/ship[0]/speed", 2);
    while (kl_poll(c, 500) >= 0)
    {
    }
    kl_close(c);
    return 0;
}
