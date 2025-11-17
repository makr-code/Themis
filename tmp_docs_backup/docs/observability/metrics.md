# Metrics & Histograms

Prometheus histograms exported by Themis follow the Prometheus cumulative-bucket semantics.

Important notes:

- Each histogram bucket in Prometheus uses a cumulative count: the bucket with `le=X` contains the number of observations less-or-equal X.
- Internally Themis records bucket counters; historically some implementations used per-bucket counters (only the range). The exporter is robust and will produce cumulative buckets regardless of whether the internal counters are stored as per-bucket or cumulative counts.

How the exporter behaves:

1. It reads the raw bucket counters from memory.
2. If the raw counters are non-decreasing (each subsequent bucket >= previous), the exporter treats them as already cumulative and exports them as-is.
3. If the raw counters are not non-decreasing, the exporter treats the raw values as per-bucket counts and computes a running sum to produce cumulative values.

Adding or changing buckets:

- Locate the recording site (e.g. `HttpServer::recordLatency` or `HttpServer::recordPageFetch`) and add the new bucket threshold in both the recorder and the exporter.
- Ensure that either you increment all buckets `<= threshold` (cumulative recording) or increment only the exact per-bucket counter; the exporter will handle either case.

Testing:

- The repository includes `tests/test_metrics_api.cpp` which verifies that the `/metrics` endpoint exposes buckets, sum and count and that buckets are cumulative. Add tests that exercise new buckets when you change them.

Examples:

- Latency histogram buckets (microseconds): 100, 500, 1000, 5000, 10000, 50000, 100000, 500000, 1000000, 5000000, +Inf
- Page fetch histogram buckets (ms): 1, 5, 10, 25, 50, 100, 250, 500, 1000, 5000, +Inf

If you change bucket thresholds, update this document and the tests accordingly.
