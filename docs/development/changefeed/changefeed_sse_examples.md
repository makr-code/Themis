````markdown
---
title: Changefeed SSE Client Examples
---

Zweck: Praktische Beispiele, wie man sich mit dem Changefeed SSE‑Endpoint verbindet, Replay‑Events verarbeitet und Heartbeats handhabt.

1) Curl (CLI)

Beispiel (replay ab seq=10):

```bash
curl -N "http://localhost:8080/changefeed/stream?from_seq=10"
```

Hinweis: `-N` (no-buffer) erlaubt das sofortige Anzeigen von Events. Curl zeigt rohe SSE‑Daten (z. B. `event: message\ndata: {...}\n\n`).

2) Node.js (EventSource)

```javascript
const EventSource = require('eventsource');
const url = 'http://localhost:8080/changefeed/stream?from_seq=10';
const es = new EventSource(url);
es.onmessage = (e) => {
  try {
    const data = JSON.parse(e.data);
    console.log('event', data.seq, data);
  } catch (err) {
    console.warn('non-json payload', e.data);
  }
};
es.onerror = (err) => console.error('SSE error', err);

// close after 60s for demo
setTimeout(() => es.close(), 60000);
```

3) C++ (libcurl easy) — simplified example

This shows how to connect and process incoming SSE lines; production code should parse events robustly and handle reconnect.

```cpp
#include <curl/curl.h>
#include <iostream>
#include <string>

static size_t sse_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  size_t len = size * nmemb;
  std::string chunk(ptr, len);
  // naive: print incoming chunk
  std::cout << chunk;
  return len;
}

int main() {
  CURL* curl = curl_easy_init();
  if (!curl) return 1;
  curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/changefeed/stream?from_seq=10");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sse_cb);
  curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  return 0;
}
```

4) Heartbeat & reconnect

- Server should emit periodic heartbeat events (e.g. `event: heartbeat\ndata: {"ts":...}`) so clients know the connection is alive.
- Client strategy: if no event/heartbeat for `X` seconds, attempt reconnect with exponential backoff and resume using last received `seq`.

5) Security note

- Use TLS (https) in production and authenticate the client (JWT/API key). Handle auth expiry and re‑auth gracefully.

````
