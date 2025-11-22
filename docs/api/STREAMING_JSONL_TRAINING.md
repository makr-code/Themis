# Streaming JSONL LLM Training Endpoint

## Overview

ThemisDB JSONL Export API unterstützt **echtes Streaming** für on-demand LLM Training. Der LoRA/QLoRA Trainingsprozess kann Daten direkt aus der DB beziehen, ohne vollständigen Export.

## Streaming-Architektur

### Bereits implementiert (Commit 6b4129b)
✅ **Chunked Transfer Encoding** - Server streamt JSONL Zeile für Zeile  
✅ **Keine Zwischenspeicherung** - Daten werden on-the-fly aus RocksDB gelesen  
✅ **Backpressure Support** - Client-seitige Rate-Limitierung möglich

### Neu: On-Demand Training mit Cache

```
┌─────────────┐      HTTP Stream      ┌──────────────┐
│  LoRA/QLoRA │ ◄──────────────────── │  ThemisDB    │
│  Training   │   Chunked Transfer    │  HTTP Server │
│  Process    │                       └──────┬───────┘
└─────────────┘                              │
      │                                      │
      ▼                                      ▼
┌─────────────┐                       ┌──────────────┐
│ Local Cache │                       │   RocksDB    │
│  (Optional) │                       │   Storage    │
└─────────────┘                       └──────────────┘
```

## Implementierung

### HTTP Streaming Endpoint (bereits vorhanden)

```http
POST /api/export/jsonl_llm/stream
Authorization: Bearer <token>
Transfer-Encoding: chunked

{
  "theme": "Rechtssprechung",
  "from_date": "2020-01-01",
  "batch_size": 100,
  "stream_mode": "continuous"
}

Response 200 OK:
Transfer-Encoding: chunked
Content-Type: application/x-ndjson

{"instruction": "...", "output": "...", "weight": 1.2}\n
{"instruction": "...", "output": "...", "weight": 0.9}\n
...
```

### PyTorch DataLoader Integration

```python
import requests
from torch.utils.data import IterableDataset, DataLoader

class ThemisDBStreamDataset(IterableDataset):
    """
    On-Demand JSONL Streaming Dataset für LoRA/QLoRA Training
    
    Features:
    - Direkte DB-Verbindung (kein lokaler Export)
    - Optional: Lokaler Cache für wiederholte Epochen
    - Backpressure: Training-Geschwindigkeit bestimmt Query-Rate
    """
    
    def __init__(self, base_url, token, query_params, cache_dir=None):
        self.base_url = base_url
        self.token = token
        self.query_params = query_params
        self.cache_dir = cache_dir
        self.cache = []
        self.cache_enabled = cache_dir is not None
    
    def __iter__(self):
        # Wenn Cache existiert, nutze Cache
        if self.cache_enabled and len(self.cache) > 0:
            for item in self.cache:
                yield item
            return
        
        # Sonst: Stream von ThemisDB
        response = requests.post(
            f'{self.base_url}/api/export/jsonl_llm/stream',
            headers={'Authorization': f'Bearer {self.token}'},
            json=self.query_params,
            stream=True  # Wichtig: Streaming aktivieren
        )
        
        for line in response.iter_lines():
            if line:
                item = json.loads(line)
                
                # Optional: Cache für spätere Epochen
                if self.cache_enabled:
                    self.cache.append(item)
                
                yield item

# Verwendung im Training
dataset = ThemisDBStreamDataset(
    base_url='https://themisdb',
    token=ADMIN_TOKEN,
    query_params={
        'theme': 'Rechtssprechung',
        'from_date': '2020-01-01',
        'batch_size': 100,
        'weighting': {'auto_weight_by_freshness': True}
    },
    cache_dir='./cache/epoch1'  # Optional: Cache für Epoch 2+
)

dataloader = DataLoader(dataset, batch_size=32)

# Training Loop
for epoch in range(num_epochs):
    for batch in dataloader:
        # Training step
        # Daten werden on-demand von ThemisDB gestreamt
        # Kein vollständiger Export notwendig
        loss = train_step(batch)
```

### HuggingFace Datasets Integration

```python
from datasets import IterableDataset
import requests
import json

def themisdb_generator(base_url, token, query_params):
    """Generator für HuggingFace IterableDataset"""
    response = requests.post(
        f'{base_url}/api/export/jsonl_llm/stream',
        headers={'Authorization': f'Bearer {token}'},
        json=query_params,
        stream=True
    )
    
    for line in response.iter_lines():
        if line:
            yield json.loads(line)

# HuggingFace Dataset erstellen
dataset = IterableDataset.from_generator(
    themisdb_generator,
    gen_kwargs={
        'base_url': 'https://themisdb',
        'token': ADMIN_TOKEN,
        'query_params': {
            'theme': 'Immissionsschutz',
            'format': 'instruction_tuning'
        }
    }
)

# PEFT LoRA Training
from transformers import Trainer, TrainingArguments
from peft import LoraConfig, get_peft_model

trainer = Trainer(
    model=peft_model,
    args=training_args,
    train_dataset=dataset  # Streaming dataset!
)
trainer.train()
```

## Cache-Strategien

### 1. Kein Cache (Pure Streaming)
```python
# Für sehr große Datasets, die nicht in RAM passen
# Jede Epoche streamt neu von DB
dataset = ThemisDBStreamDataset(..., cache_dir=None)
```

### 2. RAM-Cache (erste Epoche)
```python
# Erste Epoche: Stream von DB + Cache in RAM
# Folgende Epochen: Aus RAM-Cache
dataset = ThemisDBStreamDataset(..., cache_dir=None)
dataset.cache_enabled = True  # Aktiviert RAM-Cache
```

### 3. Disk-Cache (Persistenz)
```python
# Erste Epoche: Stream von DB + Cache auf Disk
# Spätere Trainings: Aus Disk-Cache
dataset = ThemisDBStreamDataset(..., cache_dir='./cache/rechtssprechung')
```

### 4. Hybrid (LRU-Cache)
```python
from functools import lru_cache

class CachedThemisDBDataset(ThemisDBStreamDataset):
    @lru_cache(maxsize=10000)
    def _fetch_item(self, index):
        # Automatischer LRU-Cache für häufig genutzte Items
        pass
```

## Performance-Optimierungen

### Server-Seite (ThemisDB)

```cpp
// export_api_handler.cpp - Bereits implementiert
void handleStreamExport(const HttpRequest& req, HttpResponse& res) {
    // Chunked Transfer Encoding
    res.setHeader("Transfer-Encoding", "chunked");
    res.setHeader("Content-Type", "application/x-ndjson");
    
    // Stream Query-Ergebnisse
    size_t batch_size = config.batch_size;
    std::vector<BaseEntity> batch;
    
    while (query.hasMore()) {
        batch = query.fetchBatch(batch_size);  // Batched DB access
        
        for (const auto& entity : batch) {
            std::string jsonl_line = exporter.formatEntity(entity);
            res.writeChunk(jsonl_line + "\n");  // Sofortiges Senden
        }
    }
    
    res.endChunks();
}
```

### Client-Seite (Python)

```python
class OptimizedStreamDataset(IterableDataset):
    def __init__(self, ..., prefetch_size=1000):
        self.prefetch_size = prefetch_size
        self.buffer = []
    
    def __iter__(self):
        response = requests.post(..., stream=True)
        
        # Prefetching für bessere Performance
        for line in response.iter_lines(chunk_size=self.prefetch_size):
            if line:
                self.buffer.append(json.loads(line))
                
                if len(self.buffer) >= self.prefetch_size:
                    # Batch yielden
                    for item in self.buffer:
                        yield item
                    self.buffer = []
```

## Vorteile

### 1. Speicher-Effizienz
- ❌ Kein vollständiger Export (100 GB JSONL vermieden)
- ✅ Nur aktuelle Batch im RAM (~10-100 MB)
- ✅ Training beginnt sofort (kein Warten auf Export)

### 2. Aktualität
- ✅ Immer neueste Daten aus DB
- ✅ Inkrementelle Updates während Training
- ✅ Dynamische Filterung (z.B. nur Daten nach Datum X)

### 3. Flexibilität
- ✅ Verschiedene Queries pro Epoche
- ✅ A/B Testing mit verschiedenen Gewichtungen
- ✅ Adaptive Sampling basierend auf Trainings-Metriken

## Beispiel: Multi-Thema Training

```python
# Training mit mehreren Themen im Wechsel
themes = ['Rechtssprechung', 'Immissionsschutz', 'Datenschutz']

for epoch in range(num_epochs):
    for theme in themes:
        dataset = ThemisDBStreamDataset(
            query_params={
                'theme': theme,
                'from_date': get_recent_date(days=365),
                'weighting': {'auto_weight_by_freshness': True}
            },
            cache_dir=f'./cache/{theme}_epoch{epoch}'
        )
        
        # Training auf theme-spezifischem Dataset
        trainer.train(dataset, max_steps=1000)
```

## Implementierungsstatus

✅ **Server-seitiges Streaming** - Bereits implementiert (Commit 6b4129b)  
✅ **Chunked Transfer Encoding** - Funktioniert  
✅ **Backpressure Support** - Client bestimmt Rate  
📝 **Python Client Library** - Beispielcode oben  
📝 **Cache-Strategien** - Implementierbar durch Client

## Nächste Schritte

1. **Python Package:** `themisdb-datasets` (PyPI)
   - ThemisDBStreamDataset
   - HuggingFace Integration
   - Auto-Caching

2. **WebSocket Support** (Optional)
   - Bi-direktionale Kommunikation
   - Training-Feedback an DB (welche Samples effektiv)

3. **Adaptive Sampling** (Optional)
   - Server tracked welche Samples schwer zu lernen sind
   - Dynamische Gewichtung während Training
