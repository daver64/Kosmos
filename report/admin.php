<?php
// Password-protected list of the received reports: view, download and delete.
require __DIR__ . '/common.php';

session_name('kosmos_report_admin');
session_set_cookie_params([
    'path' => dirname($_SERVER['SCRIPT_NAME']),
    'httponly' => true,
    'samesite' => 'Strict',
    'secure' => !empty($_SERVER['HTTPS']),
]);
session_start();
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
header('X-Frame-Options: DENY');
header("Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'");

function page(string $title, string $body): never
{
    header('Content-Type: text/html; charset=utf-8');
    echo '<!DOCTYPE html><html lang="en"><meta charset="utf-8"><title>' . h($title) . '</title>'
        . '<style>body{font:14px sans-serif;background:#101418;color:#dde;margin:0;padding:24px}a{color:#8cf}'
        . 'table{border-collapse:collapse;width:100%}td,th{border-bottom:1px solid #345;padding:4px 8px;text-align:left;vertical-align:top}'
        . 'pre{background:#0a0d10;padding:12px;overflow:auto;max-height:70vh;white-space:pre-wrap}'
        . 'input,button{font:inherit}</style><h1>' . h($title) . '</h1>' . $body;
    exit;
}

$hash = config()['admin_hash'];
if ($hash === '') {
    page('Reports', '<p>No admin password is set up on this server (run scripts/setup-reports.sh).</p>');
}

if (empty($_SESSION['csrf'])) {
    $_SESSION['csrf'] = bin2hex(random_bytes(16));
}
$self = h($_SERVER['SCRIPT_NAME']);

if (($_GET['action'] ?? '') === 'logout') {
    session_destroy();
    header('Location: ' . $_SERVER['SCRIPT_NAME']);
    exit;
}

if (empty($_SESSION['admin'])) {
    $message = '';
    if (($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
        usleep(700000); // slows guessing
        if (password_verify((string) ($_POST['password'] ?? ''), $hash)) {
            session_regenerate_id(true);
            $_SESSION['admin'] = true;
            header('Location: ' . $_SERVER['SCRIPT_NAME']);
            exit;
        }
        $message = '<p>Wrong password.</p>';
    }
    page('Kosmos reports', $message . '<form method="post"><input type="password" name="password" autofocus> <button>Log in</button></form>');
}

$id = (string) ($_GET['id'] ?? $_POST['id'] ?? '');

if (($_SERVER['REQUEST_METHOD'] ?? '') === 'POST' && ($_POST['action'] ?? '') === 'delete') {
    if (hash_equals($_SESSION['csrf'], (string) ($_POST['csrf'] ?? '')) && valid_id($id)) {
        @unlink(store_dir() . "/$id.json");
        @unlink(store_dir() . "/$id.log");
    }
    header('Location: ' . $_SERVER['SCRIPT_NAME']);
    exit;
}

if ($id !== '') {
    $meta = read_meta($id);
    if (!$meta) {
        http_response_code(404);
        page('Report not found', "<p><a href=\"$self\">Back</a></p>");
    }
    $log = (string) @file_get_contents(store_dir() . "/$id.log");
    if (($_GET['action'] ?? '') === 'download') {
        header('Content-Type: text/plain; charset=utf-8');
        header("Content-Disposition: attachment; filename=\"kosmos-report-$id.txt\"");
        echo "Kosmos {$meta['kosmos']}  launcher {$meta['launcher']}  exit {$meta['exit_code']}  scenario {$meta['scenario']}\n"
            . "Note: {$meta['note']}\nContact: {$meta['contact']}\n\n{$meta['info']}\n\n{$meta['options']}\n\n----- log -----\n$log";
        exit;
    }
    $rows = '';
    foreach (['time', 'source', 'ip', 'kosmos', 'launcher', 'exit_code', 'scenario', 'contact'] as $key) {
        $value = $key === 'time' ? gmdate('Y-m-d H:i:s', (int) $meta['time']) . ' UTC' : (string) ($meta[$key] ?? '');
        $rows .= '<tr><th>' . h($key) . '</th><td>' . h($value) . '</td></tr>';
    }
    page("Report $id",
        "<p><a href=\"$self\">All reports</a> | <a href=\"$self?id=" . h($id) . "&amp;action=download\">Download</a></p>"
        . "<table>$rows</table><h3>Note</h3><pre>" . h((string) $meta['note']) . '</pre>'
        . '<h3>System</h3><pre>' . h((string) $meta['info']) . '</pre><h3>Launch options</h3><pre>' . h((string) $meta['options']) . '</pre>'
        . '<h3>Log</h3><pre>' . h($log) . '</pre>'
        . '<form method="post" onsubmit="return confirm(\'Delete this report?\')"><input type="hidden" name="action" value="delete">'
        . '<input type="hidden" name="id" value="' . h($id) . '"><input type="hidden" name="csrf" value="' . h($_SESSION['csrf']) . '"><button>Delete</button></form>');
}

$rows = '';
foreach (store_ids() as $report) {
    $meta = read_meta($report);
    if (!$meta) {
        continue;
    }
    $rows .= '<tr><td><a href="' . $self . '?id=' . h($report) . '">' . h(gmdate('Y-m-d H:i', (int) $meta['time'])) . '</a></td>'
        . '<td>' . h((string) $meta['kosmos']) . '</td><td>' . h((string) $meta['source']) . '</td>'
        . '<td>' . h((string) $meta['exit_code']) . '</td><td>' . h((string) $meta['scenario']) . '</td>'
        . '<td>' . h(substr(strtok((string) $meta['info'], "\n") ?: '', 0, 80)) . '</td>'
        . '<td>' . h(substr((string) $meta['note'], 0, 120)) . '</td></tr>';
}
page('Kosmos reports', "<p><a href=\"$self?action=logout\">Log out</a></p><table><tr><th>When (UTC)</th><th>Kosmos</th><th>From</th><th>Exit</th><th>Scenario</th><th>System</th><th>Note</th></tr>$rows</table>"
    . ($rows === '' ? '<p>No reports yet.</p>' : ''));
