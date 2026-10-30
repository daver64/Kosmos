#!/bin/sh
# Run as root by setup-reports.sh: creates the report store and the admin password file.
set -e
STORE=/var/lib/kosmos-reports
CONF=/etc/kosmos-reports.conf.php
USER_WWW=$(id -u www-data >/dev/null 2>&1 && echo www-data || echo root)

install -d -m 700 -o "$USER_WWW" -g "$USER_WWW" "$STORE"
if [ -f "$CONF" ]; then
    echo "$CONF exists; keeping the admin password."
else
    printf 'Admin password for the report page: '
    stty -echo 2>/dev/null || true
    read -r PW
    stty echo 2>/dev/null || true
    echo
    [ -n "$PW" ] || { echo "empty password" >&2; exit 1; }
    HASH=$(KOSMOS_PW="$PW" php -r 'echo password_hash(getenv("KOSMOS_PW"), PASSWORD_DEFAULT);')
    umask 077
    printf "<?php\nreturn ['store' => '%s', 'admin_hash' => '%s'];\n" "$STORE" "$HASH" > "$CONF"
    chown "$USER_WWW" "$CONF"
    chmod 600 "$CONF"
    echo "Wrote $CONF"
fi
