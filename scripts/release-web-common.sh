#!/bin/sh

prune_release_archives() {
    download=$1
    current=$2
    case $current in
        kosmos-beta-*-Linux.tar.gz) suffix=Linux.tar.gz ;;
        kosmos-beta-*-Windows.zip) suffix=Windows.zip ;;
        *) echo "publish-web: unrecognized game archive: $current" >&2; return 1 ;;
    esac

    for old in "$download"/kosmos-beta-*-"$suffix" "$download/kosmos-beta-$suffix"; do
        if [ "$old" != "$download/$current" ] && { [ -e "$old" ] || [ -L "$old" ]; }; then
            rm -f "$old"
        fi
    done
    awk -v suffix="$suffix" -v current="$current" '
        $2 == current || $2 !~ /^kosmos-beta-/ || substr($2, length($2) - length(suffix) + 1) != suffix
    ' "$download/SHA256SUMS.txt" > "$download/SHA256SUMS.txt.new"
    mv "$download/SHA256SUMS.txt.new" "$download/SHA256SUMS.txt"
}

update_download_size() {
    page=$1
    archive=$2
    name=$(basename "$archive")
    size=$(stat -c %s "$archive")
    if [ "$size" -lt 1048576 ]; then
        label="$(( (size + 1023) / 1024 )) KB"
    else
        label="$(( (size + 524288) / 1048576 )) MB"
    fi
    sed -i -E "s#(>$name</a></td><td>)[0-9.]+ [KMG]B#\1$label#" "$page"
}
