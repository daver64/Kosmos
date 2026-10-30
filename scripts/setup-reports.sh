#!/bin/sh
# Installs the run-log report pages (report/ in this repo) into the web site and sets up the report store.
#
# Usage: setup-reports.sh                this machine (asks for sudo for the store and config)
#        setup-reports.sh HOST           another machine over ssh, e.g. dave@gemini (needs its sudo password)
#
# It copies the pages to <web root>/kosmos/report/, creates /var/lib/kosmos-reports (owned by www-data, mode 700; reports
# are kept there, outside the web root, so rsync --delete from kosmos-turnkey never removes them) and writes
# /etc/kosmos-reports.conf.php holding the admin password hash. If that file exists it is kept; delete it to set a new password.
# The pages are also copied to the web root by kosmos-turnkey, so after the first setup only the store and password are one-off.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
WEB_ROOT=${WEB_ROOT:-/var/www/html}

if [ -z "$1" ]; then
    mkdir -p "$WEB_ROOT/kosmos/report"
    cp "$SRC"/report/*.php "$SRC"/report/index.html "$SRC"/report/.htaccess "$WEB_ROOT/kosmos/report/"
    sudo sh "$SRC/scripts/setup-reports-store.sh"
else
    HOST=$1
    ssh "$HOST" "mkdir -p $WEB_ROOT/kosmos/report"
    rsync -a "$SRC"/report/*.php "$SRC"/report/index.html "$SRC"/report/.htaccess "$HOST:$WEB_ROOT/kosmos/report/"
    scp -q "$SRC/scripts/setup-reports-store.sh" "$HOST:kosmos-reports-store.sh"
    ssh -t "$HOST" "sudo sh \$HOME/kosmos-reports-store.sh; rm -f \$HOME/kosmos-reports-store.sh"
fi
echo "Report pages installed: <site>/kosmos/report/ (public form) and <site>/kosmos/report/admin.php"
