#!/bin/sh
# Installs or upgrades KosmosAV on this machine. Run as root, from the folder holding kosmosavd, registry.txt, kosmosavd.service, apache-kav.conf and index.html
# (scripts/deploy-kav.sh copies them over and runs this). Safe to repeat: the database, the uploads and /etc/default/kosmosav are kept.
#   install.sh [--secure]     --secure is written to /etc/default/kosmosav on a first install, for a machine that serves HTTPS
set -e
cd "$(dirname "$0")"
OPTS=""
[ "$1" = "--secure" ] && OPTS="--secure"

id kav >/dev/null 2>&1 || useradd -r -s /usr/sbin/nologin kav
mkdir -p /opt/kosmosav /var/lib/kosmosav
install -m 755 kosmosavd /opt/kosmosav/kosmosavd
install -m 644 registry.txt /opt/kosmosav/registry.txt
chown kav /var/lib/kosmosav
[ -f /etc/default/kosmosav ] || echo "KAV_OPTS=\"$OPTS\"" > /etc/default/kosmosav
install -m 644 kosmosavd.service /etc/systemd/system/kosmosavd.service
systemctl daemon-reload
systemctl enable kosmosavd >/dev/null 2>&1
systemctl restart kosmosavd

install -m 644 apache-kav.conf /etc/apache2/conf-available/kosmosav.conf
a2enmod proxy proxy_http headers >/dev/null
a2enconf kosmosav >/dev/null
apache2ctl configtest
systemctl reload apache2

if [ -f index.html ]; then
    mkdir -p /var/www/html/kosmos/kav
    install -m 644 index.html /var/www/html/kosmos/kav/index.html
fi

if [ -z "$(runuser -u kav -- /opt/kosmosav/kosmosavd --db /var/lib/kosmosav/kav.db --list-admins 2>/dev/null)" ]; then
    echo "No administrator yet. Create one with:"
    echo "  sudo -u kav /opt/kosmosav/kosmosavd --db /var/lib/kosmosav/kav.db --make-admin CALLSIGN 'PASSWORD'"
fi
systemctl --no-pager --lines=0 status kosmosavd | head -4
