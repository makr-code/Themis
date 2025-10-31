# Time-Series Engine (MVP)

## Overview

The Time-Series subsystem provides:
- Raw time-series storage over RocksDB (TSStore)
- Range queries and basic aggregations (min/max/avg/sum/count)
- Retention policies (per-metric) via RetentionManager
- Continuous aggregates (windowed) via ContinuousAggregateManager
- Gorilla-style compression utilities (codec) for future block storage

Status: MVP ready with unit tests. HTTP endpoints can be added later.

## Components

- TSStore: JSON-based storage for time-series points with tags and metadata
- TimeSeriesStore: Minimal variant for metric/entity simple values
- Gorilla codec: Streaming encoder/decoder for (timestamp,double)
- RetentionManager: Applies per-metric retention cutoffs
- ContinuousAggregateManager: Computes windowed aggregates into derived metrics

## TSStore API

Key format: `ts:{metric}:{entity}:{timestamp_ms_padded}`

Data model:
```json
{
  "metric": "cpu",
  "entity": "server01",
  "timestamp_ms": 1700000000000,
  "value": 0.73,
  "tags": { "env": "prod" },
  "metadata": { }
}
```

Queries:
- Range by [from_ms, to_ms]
- Optional entity filter
- Optional tag filter (exact match)
- Limit

Aggregations (on-the-fly): min/max/avg/sum/count on query result.

Retention:
- `deleteOldData(cutoff_ms)` global deletion (all metrics)
- `deleteOldDataForMetric(metric, cutoff_ms)` targeted deletion

## RetentionManager

```cpp
RetentionPolicy pol;
pol.per_metric["cpu"] = std::chrono::minutes(30);
pol.per_metric["mem"] = std::chrono::hours(2);
RetentionManager rm(&tsstore, pol);
rm.apply();
```

Implementation uses `deleteOldDataForMetric()` internally.

## Continuous Aggregates

Creates derived metrics with windowed aggregates.

- Derived metric name: `{metric}__agg_{window_ms}ms`
- Stores one point per window at window end
- Uses `value = avg`, with metadata carrying `{min,max,sum,count,from_ms,to_ms}`

```cpp
ContinuousAggregateManager mgr(&tsstore);
AggConfig cfg{ .metric = "temp", .entity = std::string("sensorA"), .window = {std::chrono::minutes(1)} };
mgr.refresh(cfg, from_ms, to_ms);
```

## Gorilla Codec

Minimal Gorilla-style codec:
- Timestamps: delta-of-delta with ZigZag + varint
- Values: XOR of IEEE-754 with leading/trailing zero bit packing

Use:
```cpp
GorillaEncoder enc;
enc.add(ts, value);
auto bytes = enc.finish();
GorillaDecoder dec(bytes);
while (auto p = dec.next()) { /* ... */ }
```

## Tests

- `tests/test_tsstore.cpp` – CRUD, queries, aggregations, stats, perf
- `tests/test_timeseries_retention.cpp` – retention per metric
- `tests/test_continuous_agg.cpp` – windowed aggregates
- `tests/test_gorilla.cpp` – codec roundtrip and compression property

## Next Steps

- HTTP endpoints for ingest/query/aggregate/retention
- Blocked compressed storage using Gorilla codec (L1 cache + L2 blocks)
- Downsampling tiers (1m → 1h → 1d)
- Background schedulers for continuous aggregates/retention
