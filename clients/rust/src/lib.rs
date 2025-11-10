use std::{collections::HashMap, sync::Arc, time::Duration};

use once_cell::sync::Lazy;
use reqwest::{Client, Method, Response};
use serde::{de::DeserializeOwned, Deserialize, Serialize};
use serde_json::{json, Value};
use thiserror::Error;
use tokio::sync::RwLock;
use tokio::time::sleep;
use url::Url;

static JSON_CONTENT_TYPE: Lazy<&'static str> = Lazy::new(|| "application/json");

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ThemisClientConfig {
    pub endpoints: Vec<String>,
    #[serde(default = "default_namespace")]
    pub namespace: String,
    #[serde(default = "default_timeout_ms")]
    pub timeout_ms: u64,
    pub metadata_endpoint: Option<String>,
    #[serde(default = "default_max_retries")]
    pub max_retries: usize,
}

fn default_namespace() -> String {
    "default".to_string()
}

fn default_timeout_ms() -> u64 {
    30_000
}

fn default_max_retries() -> usize {
    3
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BatchGetResult<T> {
    pub found: HashMap<String, T>,
    pub missing: Vec<String>,
    pub errors: HashMap<String, String>,
}

impl<T> Default for BatchGetResult<T> {
    fn default() -> Self {
        Self {
            found: HashMap::new(),
            missing: Vec::new(),
            errors: HashMap::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QueryOptions {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub params: Option<HashMap<String, Value>>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub use_cursor: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cursor: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub batch_size: Option<u32>,
}

impl Default for QueryOptions {
    fn default() -> Self {
        Self {
            params: None,
            use_cursor: false,
            cursor: None,
            batch_size: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QueryResult<T> {
    pub items: Vec<T>,
    pub has_more: bool,
    pub next_cursor: Option<String>,
    pub raw: Value,
}

impl<T> Default for QueryResult<T> {
    fn default() -> Self {
        Self {
            items: Vec::new(),
            has_more: false,
            next_cursor: None,
            raw: Value::Null,
        }
    }
}

#[derive(Debug, Error)]
pub enum ThemisError {
    #[error("invalid configuration: {0}")]
    InvalidConfig(String),
    #[error("topology error: {0}")]
    Topology(String),
    #[error("http error: {status} {body}")]
    Http { status: u16, body: String },
    #[error(transparent)]
    Transport(#[from] reqwest::Error),
}

pub type Result<T> = std::result::Result<T, ThemisError>;

#[derive(Clone)]
pub struct ThemisClient {
    http: Client,
    config: ThemisClientConfig,
    topology: Arc<RwLock<Option<Vec<String>>>>,
}

impl ThemisClient {
    pub fn new(config: ThemisClientConfig) -> Result<Self> {
        if config.endpoints.is_empty() {
            return Err(ThemisError::InvalidConfig("endpoints must not be empty".into()));
        }
        let client = Client::builder()
            .timeout(Duration::from_millis(config.timeout_ms))
            .build()?;
        Ok(Self {
            http: client,
            config,
            topology: Arc::new(RwLock::new(None)),
        })
    }

    pub async fn health(&self) -> Result<Value> {
        let endpoint = self.config.endpoints.first().unwrap().clone();
        let url = format!("{endpoint}/health");
        let response = self.request(Method::GET, url, None).await?;
        Ok(response.json().await?)
    }

    pub async fn get<T>(&self, model: &str, collection: &str, uuid: &str) -> Result<Option<T>>
    where
        T: DeserializeOwned,
    {
        let endpoint = self.resolve_endpoint(model, collection, uuid).await?;
        let key = build_entity_key(model, &self.config.namespace, collection, uuid);
        let url = format!("{endpoint}/entities/{key}");
        let response = self.request(Method::GET, url, None).await?;
        if response.status() == 404 {
            return Ok(None);
        }
        ensure_success(response).await.map(|resp| resp.json().await.map(decode_entity))?
    }

    pub async fn put<T>(&self, model: &str, collection: &str, uuid: &str, data: &T) -> Result<()>
    where
        T: Serialize,
    {
        let endpoint = self.resolve_endpoint(model, collection, uuid).await?;
        let key = build_entity_key(model, &self.config.namespace, collection, uuid);
        let url = format!("{endpoint}/entities/{key}");
        let body = json!({ "blob": encode_entity(data)? });
        let response = self
            .request(
                Method::PUT,
                url,
                Some(RequestBody {
                    body: Some(body.to_string()),
                    content_type: Some(JSON_CONTENT_TYPE.to_string()),
                }),
            )
            .await?;
        ensure_success(response).await.map(|_| ())
    }

    pub async fn delete(&self, model: &str, collection: &str, uuid: &str) -> Result<bool> {
        let endpoint = self.resolve_endpoint(model, collection, uuid).await?;
        let key = build_entity_key(model, &self.config.namespace, collection, uuid);
        let url = format!("{endpoint}/entities/{key}");
        let response = self.request(Method::DELETE, url, None).await?;
        if response.status() == 404 {
            return Ok(false);
        }
        ensure_success(response).await.map(|_| true)
    }

    pub async fn batch_get<T>(&self, model: &str, collection: &str, uuids: &[String]) -> Result<BatchGetResult<T>>
    where
        T: DeserializeOwned,
    {
        let mut result = BatchGetResult::default();
        for uuid in uuids {
            match self.get::<T>(model, collection, uuid).await {
                Ok(Some(value)) => {
                    result.found.insert(uuid.clone(), value);
                }
                Ok(None) => result.missing.push(uuid.clone()),
                Err(err) => {
                    result
                        .errors
                        .insert(uuid.clone(), format!("{err}"));
                }
            }
        }
        Ok(result)
    }

    pub async fn query<T>(&self, aql: &str, options: QueryOptions) -> Result<QueryResult<T>>
    where
        T: DeserializeOwned,
    {
        let mut payload = serde_json::Map::new();
        payload.insert("query".into(), Value::String(aql.to_string()));
        if let Some(params) = options.params {
            payload.insert("params".into(), Value::Object(params.into_iter().collect()));
        }
        if options.use_cursor {
            payload.insert("use_cursor".into(), Value::Bool(true));
        }
        if let Some(cursor) = options.cursor {
            payload.insert("cursor".into(), Value::String(cursor));
        }
        if let Some(batch_size) = options.batch_size {
            payload.insert("batch_size".into(), Value::Number(batch_size.into()));
        }

        let endpoints = if is_single_shard_query(aql) {
            vec![self.resolve_query_endpoint(aql).await?]
        } else {
            self.current_endpoints().await?
        };

        let mut partials = Vec::new();
        for endpoint in endpoints {
            let url = format!("{endpoint}/query/aql");
            let response = self
                .request(
                    Method::POST,
                    url,
                    Some(RequestBody {
                        body: Some(Value::Object(payload.clone()).to_string()),
                        content_type: Some(JSON_CONTENT_TYPE.to_string()),
                    }),
                )
                .await?;
            let payload = ensure_success(response).await?.json::<Value>().await?;
            partials.push(parse_query_result::<T>(payload)?);
        }

        if partials.is_empty() {
            return Ok(QueryResult::default());
        }
        if partials.len() == 1 {
            return Ok(partials.into_iter().next().unwrap());
        }
        let mut items = Vec::new();
        let mut has_more = false;
        for part in &partials {
            has_more |= part.has_more;
            items.extend(part.items.clone());
        }
        Ok(QueryResult {
            items,
            has_more,
            next_cursor: None,
            raw: Value::Array(partials.into_iter().map(|p| p.raw).collect()),
        })
    }

    pub async fn vector_search(
        &self,
        embedding: &[f32],
        filter: Option<Value>,
        top_k: Option<u32>,
    ) -> Result<Value> {
        let mut payload = serde_json::Map::new();
        payload.insert(
            "vector".into(),
            Value::Array(embedding.iter().map(|v| Value::from(*v)).collect()),
        );
        if let Some(filter) = filter {
            payload.insert("filter".into(), filter);
        }
        if let Some(top_k) = top_k {
            payload.insert("k".into(), Value::from(top_k));
        }

        let endpoints = self.current_endpoints().await?;
        let mut results = Vec::new();
        let mut raw = Vec::new();
        for endpoint in endpoints {
            let url = format!("{endpoint}/vector/search");
            let response = self
                .request(
                    Method::POST,
                    url,
                    Some(RequestBody {
                        body: Some(Value::Object(payload.clone()).to_string()),
                        content_type: Some(JSON_CONTENT_TYPE.to_string()),
                    }),
                )
                .await?;
            let value = ensure_success(response).await?.json::<Value>().await?;
            if let Some(items) = value.get("results").and_then(|v| v.as_array()) {
                results.extend(items.clone());
            }
            raw.push(value);
        }
        Ok(json!({ "results": results, "partials": raw }))
    }

    async fn current_endpoints(&self) -> Result<Vec<String>> {
        self.ensure_topology().await?;
        let topo = self.topology.read().await;
        Ok(topo
            .clone()
            .unwrap_or_else(|| self.config.endpoints.clone()))
    }

    async fn resolve_endpoint(&self, model: &str, collection: &str, uuid: &str) -> Result<String> {
        let endpoints = self.current_endpoints().await?;
        if endpoints.is_empty() {
            return Err(ThemisError::Topology("no endpoints available".into()));
        }
        let index = stable_hash(&build_urn(model, &self.config.namespace, collection, uuid)) % endpoints.len();
        Ok(endpoints[index].clone())
    }

    async fn resolve_query_endpoint(&self, aql: &str) -> Result<String> {
        let endpoints = self.current_endpoints().await?;
        if endpoints.is_empty() {
            return Err(ThemisError::Topology("no endpoints available".into()));
        }
        let index = stable_hash(aql) % endpoints.len();
        Ok(endpoints[index].clone())
    }

    async fn ensure_topology(&self) -> Result<()> {
        {
            let topo = self.topology.read().await;
            if topo.is_some() {
                return Ok(());
            }
        }
        let mut topo = self.topology.write().await;
        if topo.is_some() {
            return Ok(());
        }
        match self.fetch_topology().await {
            Ok(shards) if !shards.is_empty() => {
                *topo = Some(shards);
            }
            Ok(_) => {
                return Err(ThemisError::Topology("topology response missing shards".into()));
            }
            Err(err) => {
                *topo = Some(self.config.endpoints.clone());
                return Err(err);
            }
        }
        Ok(())
    }

    async fn fetch_topology(&self) -> Result<Vec<String>> {
        let url = self.metadata_url()?;
        let response = self.request(Method::GET, url, None).await?;
        let payload = ensure_success(response).await?.json::<Value>().await?;
        Ok(extract_endpoints(&payload))
    }

    fn metadata_url(&self) -> Result<String> {
        if let Some(endpoint) = &self.config.metadata_endpoint {
            if endpoint.starts_with("http") {
                return Ok(endpoint.clone());
            }
            let base = self.config.endpoints.first().unwrap();
            return Ok(format!("{base}{endpoint}"));
        }
        let base = self.config.endpoints.first().unwrap();
        Ok(format!("{base}/_admin/cluster/topology"))
    }

    async fn request(&self, method: Method, url: String, body: Option<RequestBody>) -> Result<Response> {
        let mut attempt = 0usize;
        let max_attempts = self.config.max_retries.max(1);
        loop {
            let mut builder = self.http.request(method.clone(), &url);
            if let Some(body) = &body {
                if let Some(content_type) = &body.content_type {
                    builder = builder.header("Content-Type", content_type);
                }
                if let Some(payload) = &body.body {
                    builder = builder.body(payload.clone());
                }
            }
            let result = builder.send().await;
            match result {
                Ok(response) => {
                    if response.status().is_server_error() && attempt + 1 < max_attempts {
                        attempt += 1;
                        sleep(backoff(attempt)).await;
                        continue;
                    }
                    return Ok(response);
                }
                Err(err) => {
                    if attempt + 1 >= max_attempts || !should_retry(&err) {
                        return Err(ThemisError::Transport(err));
                    }
                    attempt += 1;
                    sleep(backoff(attempt)).await;
                }
            }
        }
    }
}

struct RequestBody {
    body: Option<String>,
    content_type: Option<String>,
}

async fn ensure_success(response: Response) -> Result<Response> {
    if response.status().is_success() {
        return Ok(response);
    }
    let status = response.status().as_u16();
    let body = response.text().await.unwrap_or_default();
    Err(ThemisError::Http { status, body })
}

fn extract_endpoints(payload: &Value) -> Vec<String> {
    let mut endpoints = Vec::new();
    if let Some(shards) = payload.get("shards").and_then(|v| v.as_array()) {
        for shard in shards {
            match shard {
                Value::String(endpoint) => endpoints.push(normalize(endpoint)),
                Value::Object(map) => {
                    if let Some(Value::String(endpoint)) = map.get("endpoint") {
                        endpoints.push(normalize(endpoint));
                    }
                    if let Some(Value::String(endpoint)) = map.get("http_endpoint") {
                        endpoints.push(normalize(endpoint));
                    }
                    if let Some(Value::Array(values)) = map.get("endpoints") {
                        for endpoint in values {
                            if let Value::String(endpoint) = endpoint {
                                endpoints.push(normalize(endpoint));
                            }
                        }
                    }
                }
                _ => {}
            }
        }
    }
    endpoints
}

fn normalize(endpoint: &str) -> String {
    endpoint.trim_end_matches('/').to_string()
}

fn build_urn(model: &str, namespace: &str, collection: &str, uuid: &str) -> String {
    format!("urn:themis:{model}:{namespace}:{collection}:{uuid}")
}

fn build_entity_key(model: &str, namespace: &str, collection: &str, uuid: &str) -> String {
    format!("{model}.{namespace}.{collection}:{uuid}")
}

fn encode_entity<T: Serialize>(value: &T) -> Result<String> {
    if let Some(raw) = value_as_string(value) {
        Ok(raw)
    } else {
        serde_json::to_string(value).map_err(|err| ThemisError::InvalidConfig(err.to_string()))
    }
}

fn value_as_string<T: Serialize>(value: &T) -> Option<String> {
    if let Ok(Value::String(inner)) = serde_json::to_value(value) {
        Some(inner)
    } else {
        None
    }
}

fn decode_entity<T: DeserializeOwned>(payload: Value) -> T {
    if let Some(entity) = payload.get("entity") {
        serde_json::from_value(entity.clone()).unwrap_or_else(|_| panic!("failed to decode entity"))
    } else if let Some(blob) = payload.get("blob").and_then(|v| v.as_str()) {
        serde_json::from_str(blob).unwrap_or_else(|_| panic!("failed to decode blob"))
    } else {
        serde_json::from_value(payload).unwrap_or_else(|_| panic!("failed to decode payload"))
    }
}

fn parse_query_result<T: DeserializeOwned>(payload: Value) -> Result<QueryResult<T>> {
    if let Some(entities) = payload.get("entities").and_then(|v| v.as_array()) {
        let items = decode_entities::<T>(entities)?;
        return Ok(QueryResult {
            items,
            has_more: false,
            next_cursor: None,
            raw: payload,
        });
    }
    let items = payload
        .get("items")
        .and_then(|v| v.as_array())
        .map(|values| decode_entities::<T>(values))
        .transpose()?;
    let has_more = payload.get("has_more").and_then(|v| v.as_bool()).unwrap_or(false);
    let next_cursor = payload
        .get("next_cursor")
        .and_then(|v| v.as_str())
        .map(|s| s.to_string());
    Ok(QueryResult {
        items: items.unwrap_or_default(),
        has_more,
        next_cursor,
        raw: payload,
    })
}

fn decode_entities<T: DeserializeOwned>(values: &[Value]) -> Result<Vec<T>> {
    values
        .iter()
        .map(|value| {
            if let Some(raw) = value.as_str() {
                serde_json::from_str(raw).map_err(|err| ThemisError::InvalidConfig(err.to_string()))
            } else {
                serde_json::from_value(value.clone()).map_err(|err| ThemisError::InvalidConfig(err.to_string()))
            }
        })
        .collect()
}

fn is_single_shard_query(aql: &str) -> bool {
    aql.to_lowercase().contains("urn:themis:")
}

fn stable_hash(input: &str) -> usize {
    let mut hash: u64 = 2166136261;
    for byte in input.as_bytes() {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(16777619);
    }
    (hash & 0x7FFF_FFFF) as usize
}

fn should_retry(err: &reqwest::Error) -> bool {
    err.is_timeout() || err.is_connect() || err.is_request()
}

fn backoff(attempt: usize) -> Duration {
    let millis = 50 * 2u64.saturating_pow(attempt as u32);
    Duration::from_millis(millis.min(1_000));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn stable_hash_is_deterministic() {
        let value = stable_hash("urn:themis:relational:default:users:1");
        assert_eq!(value, stable_hash("urn:themis:relational:default:users:1"));
        assert_ne!(value, stable_hash("urn:themis:relational:default:users:2"));
    }

    #[test]
    fn normalize_trims_slash() {
        assert_eq!(normalize("http://example.com/"), "http://example.com");
    }
}
