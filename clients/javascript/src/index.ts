const DEFAULT_METADATA_PATH = "/_admin/cluster/topology";
const HEALTH_PATH = "/health";

export interface ThemisClientConfig {
  endpoints: string[];
  namespace?: string;
  timeoutMs?: number;
  metadataEndpoint?: string;
  maxRetries?: number;
}

export interface BatchGetResult<T = unknown> {
  found: Record<string, T>;
  missing: string[];
  errors: Record<string, string>;
}

export interface QueryOptions {
  params?: Record<string, unknown>;
  useCursor?: boolean;
  cursor?: string;
  batchSize?: number;
}

export interface QueryResult<T = unknown> {
  items: T[];
  hasMore: boolean;
  nextCursor?: string | null;
  raw: Record<string, unknown>;
}

export class TopologyError extends Error {}

export class ThemisClient {
  private readonly endpoints: string[];
  private readonly namespace: string;
  private readonly timeoutMs: number;
  private readonly metadataEndpoint?: string;
  private readonly maxRetries: number;
  private topologyCache: { shards: string[] } | null = null;

  constructor(config: ThemisClientConfig) {
    if (!config.endpoints || config.endpoints.length === 0) {
      throw new Error("endpoints must not be empty");
    }
    this.endpoints = config.endpoints.map((e) => e.replace(/\/$/, ""));
    this.namespace = config.namespace ?? "default";
    this.timeoutMs = config.timeoutMs ?? 30_000;
    this.metadataEndpoint = config.metadataEndpoint;
    this.maxRetries = Math.max(1, config.maxRetries ?? 3);
  }

  async health(endpoint?: string): Promise<unknown> {
    const target = normalizeEndpoint(endpoint ?? this.endpoints[0]);
    const response = await this.request("GET", `${target}${HEALTH_PATH}`);
    return response.json();
  }

  async get<T = unknown>(model: string, collection: string, uuid: string): Promise<T | null> {
    const urn = this.buildUrn(model, collection, uuid);
    const key = this.buildEntityKey(model, collection, uuid);
    const endpoint = await this.resolveEndpoint(urn);
    const response = await this.request("GET", `${endpoint}/entities/${key}`);
    if (response.status === 404) {
      return null;
    }
    if (!response.ok) {
      throw await toHttpError(response, "failed to load entity");
    }
    const payload = (await response.json()) as Record<string, unknown>;
    return decodeEntity<T>(payload);
  }

  async put(model: string, collection: string, uuid: string, data: unknown): Promise<boolean> {
    const urn = this.buildUrn(model, collection, uuid);
    const key = this.buildEntityKey(model, collection, uuid);
    const endpoint = await this.resolveEndpoint(urn);
    const response = await this.request("PUT", `${endpoint}/entities/${key}`, {
      body: JSON.stringify({ blob: encodeEntity(data) }),
      headers: { "Content-Type": "application/json" },
    });
    if (!response.ok) {
      throw await toHttpError(response, "failed to upsert entity");
    }
    return true;
  }

  async delete(model: string, collection: string, uuid: string): Promise<boolean> {
    const urn = this.buildUrn(model, collection, uuid);
    const key = this.buildEntityKey(model, collection, uuid);
    const endpoint = await this.resolveEndpoint(urn);
    const response = await this.request("DELETE", `${endpoint}/entities/${key}`);
    if (response.status === 404) {
      return false;
    }
    if (!response.ok) {
      throw await toHttpError(response, "failed to delete entity");
    }
    return true;
  }

  async batchGet<T = unknown>(model: string, collection: string, uuids: string[]): Promise<BatchGetResult<T>> {
    const result: BatchGetResult<T> = { found: {}, missing: [], errors: {} };
    for (const uuid of uuids) {
      try {
        const entity = await this.get<T>(model, collection, uuid);
        if (entity === null) {
          result.missing.push(uuid);
        } else {
          result.found[uuid] = entity;
        }
      } catch (error) {
        result.errors[uuid] = error instanceof Error ? error.message : String(error);
      }
    }
    return result;
  }

  async query<T = unknown>(aql: string, options: QueryOptions = {}): Promise<QueryResult<T>> {
    const payload: Record<string, unknown> = { query: aql };
    if (options.params) payload.params = options.params;
    if (options.useCursor) payload.use_cursor = true;
    if (options.cursor) payload.cursor = options.cursor;
    if (options.batchSize !== undefined) payload.batch_size = options.batchSize;

    const endpoints = await this.queryEndpoints(aql);
    const responses: QueryResult<T>[] = [];
    for (const endpoint of endpoints) {
      const resp = await this.request("POST", `${endpoint}/query/aql`, {
        body: JSON.stringify(payload),
        headers: { "Content-Type": "application/json" },
      });
      if (!resp.ok) {
        throw await toHttpError(resp, "query execution failed");
      }
    const data = (await resp.json()) as Record<string, unknown>;
    responses.push(parseQueryResult<T>(data));
    }

    if (responses.length === 0) {
      return { items: [], hasMore: false, nextCursor: null, raw: {} };
    }
    if (responses.length === 1) {
      return responses[0];
    }
    const items: T[] = [];
    let hasMore = false;
    for (const part of responses) {
      items.push(...part.items);
      hasMore = hasMore || part.hasMore;
    }
    return { items, hasMore, nextCursor: null, raw: { partials: responses.map((r) => r.raw) } };
  }

  async vectorSearch(
    embedding: number[],
    options: { topK?: number; filter?: Record<string, unknown>; cursor?: string; useCursor?: boolean } = {},
  ): Promise<Record<string, unknown>> {
    const body: Record<string, unknown> = {
      vector: embedding,
      k: options.topK ?? 10,
    };
    if (options.filter) body.filter = options.filter;
    if (options.cursor) body.cursor = options.cursor;
    if (options.useCursor) body.use_cursor = true;

    const endpoints = await this.currentEndpoints();
    const payloads: Record<string, unknown>[] = [];
    for (const endpoint of endpoints) {
      const resp = await this.request("POST", `${endpoint}/vector/search`, {
        body: JSON.stringify(body),
        headers: { "Content-Type": "application/json" },
      });
      if (resp.ok) {
        payloads.push((await resp.json()) as Record<string, unknown>);
      }
    }
    if (payloads.length === 0) {
      return { results: [] };
    }
    if (payloads.length === 1) {
      return payloads[0];
    }
    const hits: Record<string, unknown>[] = [];
    for (const payload of payloads) {
      const items = (payload.results ?? payload.items) as Record<string, unknown>[] | undefined;
      if (items) hits.push(...items);
    }
    hits.sort((a, b) => {
      const scoreA = typeof a.score === "number" ? a.score : typeof a.distance === "number" ? -a.distance : 0;
      const scoreB = typeof b.score === "number" ? b.score : typeof b.distance === "number" ? -b.distance : 0;
      return scoreB - scoreA;
    });
    return { results: hits.slice(0, options.topK ?? 10), partials: payloads };
  }

  private async currentEndpoints(): Promise<string[]> {
    await this.ensureTopology();
    return this.topologyCache?.shards ?? this.endpoints;
  }

  private async queryEndpoints(aql: string): Promise<string[]> {
    if (isSingleShardQuery(aql)) {
      const endpoint = await this.resolveQueryEndpoint(aql);
      return [endpoint];
    }
    return this.currentEndpoints();
  }

  private async resolveEndpoint(urn: string): Promise<string> {
    const endpoints = await this.currentEndpoints();
    if (endpoints.length === 0) {
      throw new TopologyError("no endpoints available");
    }
    const index = stableHash(urn) % endpoints.length;
    return endpoints[index];
  }

  private async resolveQueryEndpoint(aql: string): Promise<string> {
    const endpoints = await this.currentEndpoints();
    if (endpoints.length === 0) {
      throw new TopologyError("no endpoints available for query");
    }
    const index = stableHash(aql) % endpoints.length;
    return endpoints[index];
  }

  private async ensureTopology(): Promise<void> {
    if (this.topologyCache) {
      return;
    }
    try {
      this.topologyCache = await this.fetchTopology();
    } catch (error) {
      this.topologyCache = { shards: [...this.endpoints] };
      if (error instanceof Error) {
        throw new TopologyError(`failed to fetch topology: ${error.message}`);
      }
      throw new TopologyError("failed to fetch topology");
    }
  }

  private async fetchTopology(): Promise<{ shards: string[] }> {
    const url = this.metadataUrl();
    const response = await this.request("GET", url);
    if (!response.ok) {
      throw await toHttpError(response, "topology request failed");
    }
    const payload = (await response.json()) as Record<string, unknown>;
    const shards = extractEndpoints(payload);
    if (shards.length === 0) {
      throw new TopologyError("topology response missing shards");
    }
    return { shards };
  }

  private metadataUrl(): string {
    if (this.metadataEndpoint) {
      if (this.metadataEndpoint.startsWith("http")) {
        return this.metadataEndpoint;
      }
      return `${this.endpoints[0]}${this.metadataEndpoint}`;
    }
    return `${this.endpoints[0]}${DEFAULT_METADATA_PATH}`;
  }

  private buildUrn(model: string, collection: string, uuid: string): string {
    return `urn:themis:${model}:${this.namespace}:${collection}:${uuid}`;
  }

  private buildEntityKey(model: string, collection: string, uuid: string): string {
    return `${model}.${this.namespace}.${collection}:${uuid}`;
  }

  private async request(method: string, url: string, init: RequestInit = {}): Promise<Response> {
    let attempt = 0;
    let lastError: unknown;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      while (attempt < this.maxRetries) {
        try {
          const resp = await getFetch()(url, {
            ...init,
            method,
            signal: controller.signal,
          });
          if (resp.status >= 500 && attempt + 1 < this.maxRetries) {
            attempt += 1;
            await delay(2 ** attempt * 50);
            continue;
          }
          return resp;
        } catch (error) {
          lastError = error;
          if (!shouldRetry(error) || attempt + 1 >= this.maxRetries) {
            throw error;
          }
          attempt += 1;
          await delay(2 ** attempt * 50);
        }
      }
      throw lastError ?? new Error("request failed");
    } finally {
      clearTimeout(timeout);
    }
  }
}

export const version = "0.0.0-alpha.0";

function decodeEntity<T>(payload: Record<string, unknown>): T {
  if ("entity" in payload) {
    return payload.entity as T;
  }
  if (typeof payload.blob === "string") {
    try {
      return JSON.parse(payload.blob) as T;
    } catch {
      return payload.blob as unknown as T;
    }
  }
  return payload as unknown as T;
}

function encodeEntity(data: unknown): string {
  if (typeof data === "string") {
    return data;
  }
  return JSON.stringify(data);
}

function extractEndpoints(payload: Record<string, unknown>): string[] {
  const shards = payload.shards;
  if (!Array.isArray(shards)) {
    return [];
  }
  const endpoints = new Set<string>();
  for (const shard of shards) {
    if (typeof shard === "string") {
      endpoints.add(normalizeEndpoint(shard));
      continue;
    }
    if (typeof shard === "object" && shard !== null) {
      const maybeEndpoint = (shard as { endpoint?: string; http_endpoint?: string; endpoints?: string[] }).endpoint;
      const httpEndpoint = (shard as { endpoint?: string; http_endpoint?: string; endpoints?: string[] }).http_endpoint;
      if (typeof maybeEndpoint === "string") endpoints.add(normalizeEndpoint(maybeEndpoint));
      if (typeof httpEndpoint === "string") endpoints.add(normalizeEndpoint(httpEndpoint));
      if (Array.isArray((shard as { endpoints?: string[] }).endpoints)) {
        for (const value of (shard as { endpoints?: string[] }).endpoints ?? []) {
          if (typeof value === "string") endpoints.add(normalizeEndpoint(value));
        }
      }
    }
  }
  return Array.from(endpoints);
}

function parseQueryResult<T>(payload: Record<string, unknown>): QueryResult<T> {
  if (Array.isArray(payload.entities)) {
    return {
      items: decodeEntities<T>(payload.entities as unknown[]),
      hasMore: false,
      nextCursor: null,
      raw: payload,
    };
  }
  if (Array.isArray(payload.items)) {
    return {
      items: decodeEntities<T>(payload.items as unknown[]),
      hasMore: Boolean(payload.has_more),
      nextCursor: (payload.next_cursor as string) ?? null,
      raw: payload,
    };
  }
  return { items: [], hasMore: false, nextCursor: null, raw: payload };
}

function decodeEntities<T>(values: unknown[]): T[] {
  return values.map((value) => {
    if (typeof value === "string") {
      try {
        return JSON.parse(value) as T;
      } catch {
        return value as unknown as T;
      }
    }
    return value as T;
  });
}

function normalizeEndpoint(endpoint: string): string {
  return endpoint.replace(/\/$/, "");
}

function stableHash(value: string): number {
  let hash = 2166136261;
  for (let i = 0; i < value.length; i += 1) {
    hash ^= value.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return Math.abs(hash);
}

function isSingleShardQuery(aql: string): boolean {
  return aql.toLowerCase().includes("urn:themis:");
}

async function toHttpError(response: Response, message: string): Promise<Error> {
  let body: unknown;
  try {
    body = await response.text();
  } catch {
    body = undefined;
  }
  return new Error(`${message}: ${response.status} ${response.statusText} ${(body as string) || ""}`.trim());
}

function shouldRetry(error: unknown): boolean {
  if (error instanceof DOMException && error.name === "AbortError") {
    return true;
  }
  return error instanceof TypeError;
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function getFetch(): typeof globalThis.fetch {
  if (typeof globalThis.fetch === "function") {
    return globalThis.fetch.bind(globalThis);
  }
  throw new Error("global fetch API not available; please provide a polyfill such as cross-fetch");
}
