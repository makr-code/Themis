# ThemisDB Rust SDK Quickstart

_Stand: 10. November 2025_

Das Rust-SDK (`themisdb_sdk`) befindet sich im Alpha-Stadium. Die API kann sich kurzfristig ändern. Dieser Leitfaden beschreibt, wie der Client gegen eine laufende ThemisDB-Instanz verwendet wird.

## Voraussetzungen

- Rust-Toolchain (stable) mit `cargo`
- ThemisDB-Endpunkt (z. B. `http://127.0.0.1:8765`)
- Optional: Topologie-Endpunkt (`/_admin/cluster/topology` oder vollständige URL)

> Hinweis: Im Repository ist noch kein veröffentlichtes Crate vorhanden. Verwenden Sie die Pfad- oder Git-Abhängigkeit lokal.

```toml
# Cargo.toml eines eigenen Projekts
[dependencies]
themisdb_sdk = { path = "../ThemisDB/clients/rust" }
```

## Minimalbeispiel

```rust
use themisdb_sdk::{ThemisClient, ThemisClientConfig};

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let client = ThemisClient::new(ThemisClientConfig {
        endpoints: vec!["http://127.0.0.1:8765".into()],
        namespace: "default".into(),
        metadata_endpoint: Some("/_admin/cluster/topology".into()),
        ..Default::default()
    })?;

    let health = client.health().await?;
    println!("{:?}", health);

    Ok(())
}
```

## Konfiguration

| Feld | Beschreibung |
| --- | --- |
| `endpoints` | Bootstrap-Liste der HTTP-Basen des Clusters. |
| `metadata_endpoint` | Optionaler Pfad oder absolute URL für den Topologie-Fetch. |
| `namespace` | Namespace-Komponente für URNs (Default `default`). |
| `timeout_ms` | HTTP-Timeout pro Request (Default 30 000 ms). |
| `max_retries` | Retries für Serverfehler oder Transienten (Default 3). |

Der Client cached die Topologie beim ersten Zugriff. Scheitert der Fetch, wird auf die Bootstrap-Liste zurückgegriffen und eine `ThemisError::Topology` zurückgegeben.

## CRUD & Batch

```rust
let user_id = "550e8400-e29b-41d4-a716-446655440000";

client
    .put("relational", "users", user_id, &serde_json::json!({ "name": "Alice" }))
    .await?;

if let Some(user) = client.get::<serde_json::Value>("relational", "users", user_id).await? {
    println!("{}", user);
}

let removed = client.delete("relational", "users", user_id).await?;
println!("removed? {removed}");

let batch = client
    .batch_get::<serde_json::Value>("relational", "users", &["1".into(), "2".into(), "404".into()])
    .await?;
println!("found {} users", batch.found.len());
```

## AQL & Cursor

```rust
use themisdb_sdk::QueryOptions;

let page = client
    .query::<serde_json::Value>(
        "FOR u IN users RETURN u",
        QueryOptions {
            use_cursor: true,
            batch_size: Some(100),
            ..Default::default()
        },
    )
    .await?;

if page.has_more {
    if let Some(cursor) = page.next_cursor {
        let next = client
            .query::<serde_json::Value>(
                "FOR u IN users RETURN u",
                QueryOptions {
                    use_cursor: true,
                    cursor: Some(cursor),
                    ..Default::default()
                },
            )
            .await?;
        println!("{} weitere Ergebnisse", next.items.len());
    }
}
```

Der Client erkennt URN-basierte Queries (`urn:themis:`) und adressiert automatisch nur einen Shard. Bei anderen Queries wird Scatter-Gather über alle bekannten Endpunkte ausgeführt.

## Vektor-Suche

```rust
let result = client
    .vector_search(
        &[0.12, -0.04, 0.9],
        Some(serde_json::json!({ "namespace": "docs" })),
        Some(5),
    )
    .await?;
println!("{:?}", result);
```

Treffer aus mehreren Shards werden zusammengeführt und nach Score bzw. inverser Distanz sortiert. `top_k` begrenzt optional die Rückgabegröße.

## Fehlerbehandlung

- `ThemisError::InvalidConfig`: ungültige Eingaben (z. B. leere Endpunktliste).
- `ThemisError::Topology`: Topologie konnte nicht geladen werden.
- `ThemisError::Http`: HTTP-Status ≥ 400, inkl. Body-Snippet.
- `ThemisError::Transport`: Netzwerk-/TLS-Fehler von `reqwest`.
- `ThemisError::Serde`: (De-)Serialisierung fehlgeschlagen.

## Tooling & Tests

```bash
cd clients/rust
cargo fmt
cargo clippy
cargo test
```

> Der Container enthält ggf. kein `cargo`. Installieren Sie Rust mit `rustup` oder verwenden Sie einen lokalen Dev-Container mit Rust-SDK, um Builds/Tests auszuführen.

## Roadmap

- Integrationstests mit httpmock oder Wiremock
- Auth/Signer-Unterstützung
- Async-Iteratoren für Cursor-Ergebnisse (`impl Stream`)
- Veröffentlichung als Crate (crates.io)
- Beispielprojekt (CLI) im Repository
