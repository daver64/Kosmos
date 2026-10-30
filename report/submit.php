<?php
// Receives a Kosmos run-log report. Accepts a JSON body (the launcher) or a form post (the web page).
// Answers JSON when the caller asks for it (Accept: application/json), a small HTML page otherwise.
require __DIR__ . '/common.php';

function respond(int $status, bool $ok, string $message, string $id = ''): never
{
    http_response_code($status);
    header('Cache-Control: no-store');
    header('X-Content-Type-Options: nosniff');
    $wants_json = str_contains($_SERVER['HTTP_ACCEPT'] ?? '', 'application/json');
    if ($wants_json) {
        header('Content-Type: application/json');
        echo json_encode(['ok' => $ok, 'id' => $id, 'message' => $message]);
    } else {
        header('Content-Type: text/html; charset=utf-8');
        echo '<!DOCTYPE html><meta charset="utf-8"><title>Kosmos report</title>'
            . '<link rel="stylesheet" href="../css/site.css"><main style="max-width:640px;margin:40px auto;padding:0 24px">'
            . '<h1>' . ($ok ? 'Thank you' : 'Not sent') . '</h1><p>' . h($message) . '</p>'
            . ($ok ? '<p>Reference: <code>' . h($id) . '</code></p>' : '')
            . '<p><a href="./">Back</a></p></main>';
    }
    exit;
}

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    respond(405, false, 'Use POST.');
}
if ((int) ($_SERVER['CONTENT_LENGTH'] ?? 0) > MAX_REQUEST_BYTES) {
    respond(413, false, 'The report is too large (limit 1 MB).');
}

$fields = [];
$content_type = $_SERVER['CONTENT_TYPE'] ?? '';
$from_launcher = str_starts_with($content_type, 'application/json');
if ($from_launcher) {
    $body = file_get_contents('php://input', false, null, 0, MAX_REQUEST_BYTES + 1);
    $decoded = $body === false || strlen($body) > MAX_REQUEST_BYTES ? null : json_decode($body, true);
    if (!is_array($decoded)) {
        respond(400, false, 'The report could not be read.');
    }
    $fields = $decoded;
} else {
    $fields = $_POST;
    $upload = $_FILES['logfile'] ?? null;
    if ($upload && $upload['error'] === UPLOAD_ERR_OK && is_uploaded_file($upload['tmp_name'])) {
        $fields['log'] = (string) file_get_contents($upload['tmp_name'], false, null, 0, MAX_REQUEST_BYTES);
    }
}

// A hidden field that people never fill in; bots do.
if (!empty($fields['website'])) {
    respond(200, true, 'Received.', 'ignored');
}

$text = static fn(string $key, int $limit = MAX_FIELD_BYTES): string => clean_text(is_string($fields[$key] ?? null) ? $fields[$key] : '', $limit);
$log = clean_tail(is_string($fields['log'] ?? null) ? $fields['log'] : '', MAX_LOG_BYTES);
if (trim($log) === '') {
    respond(400, false, 'There is no log text to send.');
}

$store = store_dir();
if (!is_dir($store) || !is_writable($store)) {
    respond(503, false, 'The report store is not set up on the server.');
}
if (store_bytes() > STORE_LIMIT_BYTES) {
    respond(503, false, 'The report store is full; please tell the developer another way.');
}

// Rate limit per address, counted from the reports already stored.
$address = client_address();
$recent = 0;
foreach (store_ids() as $existing) {
    $meta = read_meta($existing);
    if ($meta && ($meta['ip'] ?? '') === $address && ($meta['time'] ?? 0) > time() - 3600) {
        ++$recent;
    }
}
if ($recent >= RATE_LIMIT) {
    respond(429, false, 'Too many reports from this address; please try again later.');
}

$id = gmdate('Ymd-His') . '-' . bin2hex(random_bytes(4));
$meta = [
    'id' => $id,
    'time' => time(),
    'ip' => $address,
    'kosmos' => $text('kosmos', 40),
    'launcher' => $text('launcher', 40),
    'exit_code' => $text('exit_code', 20),
    'scenario' => $text('scenario', 80),
    'note' => $text('note'),
    'contact' => $text('contact', 200),
    'info' => $text('info'),
    'options' => $text('options'),
    'source' => $from_launcher ? 'launcher' : 'web',
    'log_bytes' => strlen($log),
];
if (file_put_contents("$store/$id.log", $log) === false
    || file_put_contents("$store/$id.json", json_encode($meta, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES)) === false) {
    respond(500, false, 'The report could not be saved.');
}
respond(200, true, 'Your report was received.', $id);
