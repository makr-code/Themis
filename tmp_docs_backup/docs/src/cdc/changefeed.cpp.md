# changefeed.cpp

Path: `src/cdc/changefeed.cpp`

Purpose: Changefeed implementation (writer/reader). If incomplete, document intended behavior: append events on DB writes, provide sequence numbers and replay support.

Public functions / symbols:
- `switch (type) {`
- `if (!db_) {`
- ``
- `if (event.timestamp_ms == 0) {`
- `if (options.long_poll_ms > 0) {`
- `if (latest <= options.from_sequence) {`
- `if (latest > from_sequence) {`
- `if (elapsed >= timeout) {`
- `if (cf_) {`
- `waitForEvents(options.from_sequence, options.long_poll_ms);`

Notes / TODOs:
- Verify whether writer hooks are invoked on every put/update/delete and where events are persisted (CF name).
