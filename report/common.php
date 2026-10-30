<?php
// Shared helpers for the Kosmos run-log report pages (submit.php, admin.php).
// Settings live outside the web root: KOSMOS_REPORT_CONFIG or /etc/kosmos-reports.conf.php, which returns
// ['store' => '/var/lib/kosmos-reports', 'admin_hash' => password_hash(...)]. See scripts/setup-reports.sh.

const MAX_REQUEST_BYTES = 1048576; // the whole POST
const MAX_LOG_BYTES = 512000;      // the tail of a log that is kept
const MAX_FIELD_BYTES = 4000;      // note, contact, info, options
const RATE_LIMIT = 10;             // reports per address per hour
const STORE_LIMIT_BYTES = 2147483648; // refuse new reports when the store is this full

function config(): array
{
    static $config = null;
    if ($config === null) {
        $path = getenv('KOSMOS_REPORT_CONFIG') ?: '/etc/kosmos-reports.conf.php';
        $config = is_file($path) ? (require $path) : [];
        $config += ['store' => '/var/lib/kosmos-reports', 'admin_hash' => ''];
    }
    return $config;
}

function store_dir(): string
{
    return rtrim(config()['store'], '/');
}

function valid_id(string $id): bool
{
    return preg_match('/^\d{8}-\d{6}-[0-9a-f]{8}$/', $id) === 1;
}

// Keeps readable text only: valid UTF-8, no control characters other than tab and newline.
function clean_text(string $text, int $limit): string
{
    $text = @iconv('UTF-8', 'UTF-8//IGNORE', $text);
    if ($text === false) {
        $text = '';
    }
    $text = str_replace("\r\n", "\n", $text);
    $text = preg_replace('/[^\P{C}\t\n]/u', '', $text) ?? '';
    return strlen($text) > $limit ? substr($text, 0, $limit) : $text;
}

function clean_tail(string $text, int $limit): string
{
    $text = @iconv('UTF-8', 'UTF-8//IGNORE', $text);
    if ($text === false) {
        $text = '';
    }
    if (strlen($text) > $limit) {
        $text = "[earlier output cut]\n" . substr($text, -$limit);
        $text = @iconv('UTF-8', 'UTF-8//IGNORE', $text) ?: '';
    }
    $text = str_replace("\r\n", "\n", $text);
    return preg_replace('/[^\P{C}\t\n]/u', '', $text) ?? '';
}

function client_address(): string
{
    return $_SERVER['REMOTE_ADDR'] ?? 'unknown';
}

function h(string $text): string
{
    return htmlspecialchars($text, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function store_ids(): array
{
    $ids = [];
    foreach (glob(store_dir() . '/*.json') ?: [] as $file) {
        $id = basename($file, '.json');
        if (valid_id($id)) {
            $ids[] = $id;
        }
    }
    rsort($ids);
    return $ids;
}

function read_meta(string $id): ?array
{
    if (!valid_id($id)) {
        return null;
    }
    $text = @file_get_contents(store_dir() . "/$id.json");
    $meta = $text === false ? null : json_decode($text, true);
    return is_array($meta) ? $meta : null;
}

function store_bytes(): int
{
    $total = 0;
    foreach (glob(store_dir() . '/*') ?: [] as $file) {
        $total += (int) @filesize($file);
    }
    return $total;
}
