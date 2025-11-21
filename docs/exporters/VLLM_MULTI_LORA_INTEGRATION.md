# vLLM Multi-LoRA Integration for VCC-Clara

## Overview

ThemisDB provides native support for **vLLM multi-LoRA inference**, designed to power the VCC-Clara AI system. This integration enables efficient serving of multiple domain-specific LoRA adapters with dynamic loading and batching.

## Background: vLLM Multi-LoRA Architecture

### What is vLLM?
vLLM is a fast and memory-efficient inference engine for LLMs with:
- PagedAttention for efficient KV cache management
- Continuous batching for high throughput
- Multi-LoRA support for efficient adapter serving
- CUDA/ROCm GPU acceleration

### Multi-LoRA Inference
vLLM can serve multiple LoRA adapters simultaneously:
- **Single Base Model**: One base model loaded in VRAM
- **Multiple Adapters**: LoRA adapters dynamically loaded per request
- **Efficient Batching**: Batch requests across different adapters
- **Zero Overhead**: Adapters add minimal memory/compute cost

**Key Benefit for VCC-Clara**: Serve specialized adapters (legal, medical, environmental) without deploying separate model instances.

## ThemisDB → vLLM Integration

### Architecture

```
┌─────────────────┐
│   VCC-Clara     │
│   Frontend      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐      ┌──────────────────┐
│   vLLM Server   │◄─────┤   ThemisDB       │
│   Multi-LoRA    │      │   JSONL Export   │
│   Inference     │      │   + Metadata     │
└────────┬────────┘      └──────────────────┘
         │                         │
         ▼                         ▼
┌─────────────────┐      ┌──────────────────┐
│  Base Model:    │      │ Training Data:   │
│  Mistral-7B     │      │ - Legal (50k)    │
│  Llama-3-8B     │      │ - Medical (30k)  │
└─────────────────┘      │ - Env Law (40k)  │
         │               └──────────────────┘
         ▼
┌─────────────────────────────────┐
│  LoRA Adapters (Auto-Loaded)    │
│  ├─ legal-qa-v1 (rank=8)        │
│  ├─ medical-diagnosis-v1 (r=16) │
│  └─ environmental-law-v1 (r=8)  │
└─────────────────────────────────┘
```

### Workflow

1. **Training Phase** (ThemisDB → vLLM)
   - Export domain-specific training data with metadata
   - Train LoRA adapters using exported JSONL
   - Store adapter metadata in ThemisDB

2. **Serving Phase** (vLLM → VCC-Clara)
   - vLLM loads base model + all adapters
   - VCC-Clara requests specify adapter via `lora_name`
   - vLLM dynamically routes to correct adapter

3. **Metadata Sync** (Continuous)
   - ThemisDB tracks adapter versions and performance
   - vLLM serves adapters registered in metadata store
   - VCC-Clara queries available adapters from ThemisDB

## Configuration

### 1. Export Training Data with vLLM Metadata

```cpp
#include "exporters/jsonl_llm_exporter.h"

JSONLLLMConfig config;
config.style = JSONLFormat::Style::INSTRUCTION_TUNING;

// vLLM-specific metadata
config.adapter_metadata.enable_tracking = true;
config.adapter_metadata.adapter_id = "legal-qa-v1";
config.adapter_metadata.base_model_name = "mistralai/Mistral-7B-v0.1";
config.adapter_metadata.task_type = "question-answering";
config.adapter_metadata.domain = "legal";

// vLLM training parameters (for PEFT)
auto& train = config.adapter_metadata.training_config;
train.dataset_name = "themis_legal_2024";
train.lora_rank = 8;
train.lora_alpha = 16.0;
train.lora_dropout = 0.1;
train.target_modules = {"q_proj", "v_proj", "k_proj", "o_proj", 
                        "gate_proj", "up_proj", "down_proj"};

// vLLM-specific custom metadata
config.adapter_metadata.custom_metadata = {
    {"vllm_compatible", "true"},
    {"vllm_version", ">=0.4.0"},
    {"max_lora_rank", "16"},
    {"adapter_path", "/models/adapters/legal-qa-v1"}
};

JSONLLLMExporter exporter(config);
```

### 2. Export Metadata in vLLM Format

```cpp
// Export adapter registry for vLLM
std::string metadata = exporter.getAdapterMetadataJson();

// Save to vLLM adapter config directory
std::ofstream config_file("/models/adapters/legal-qa-v1/adapter_config.json");
config_file << metadata;
```

### 3. vLLM Server Configuration

Create `vllm_config.yaml`:

```yaml
# vLLM Multi-LoRA Configuration for VCC-Clara
model: mistralai/Mistral-7B-v0.1
tensor_parallel_size: 1
max_num_seqs: 256
max_model_len: 4096

# Enable multi-LoRA
enable_lora: true
max_loras: 8
max_lora_rank: 16
lora_dtype: auto

# LoRA adapters from ThemisDB exports
lora_modules:
  - name: legal-qa-v1
    path: /models/adapters/legal-qa-v1
    base_model: mistralai/Mistral-7B-v0.1
    
  - name: medical-diagnosis-v1
    path: /models/adapters/medical-diagnosis-v1
    base_model: mistralai/Mistral-7B-v0.1
    
  - name: environmental-law-v1
    path: /models/adapters/environmental-law-v1
    base_model: mistralai/Mistral-7B-v0.1

# Performance tuning
gpu_memory_utilization: 0.9
swap_space: 4
```

## Usage Examples

### Training Workflow

#### Step 1: Export Training Data from ThemisDB

```bash
# Export legal domain training data
curl -X POST http://themisdb:8765/api/export/jsonl_llm \
  -H "Authorization: Bearer $ADMIN_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "theme": "Rechtssprechung",
    "from_date": "2020-01-01",
    "format": "instruction_tuning",
    "field_mapping": {
      "instruction_field": "question",
      "output_field": "answer"
    },
    "weighting": {
      "enable_weights": true,
      "auto_weight_by_freshness": true
    }
  }' > legal_qa_training.jsonl

# Export adapter metadata
curl -X GET http://themisdb:8765/api/adapters/legal-qa-v1/metadata \
  -H "Authorization: Bearer $ADMIN_TOKEN" \
  > adapter_config.json
```

#### Step 2: Train LoRA Adapter

```python
from transformers import AutoModelForCausalLM, AutoTokenizer
from peft import LoraConfig, get_peft_model, TaskType
from datasets import load_dataset
import json

# Load ThemisDB adapter metadata
with open("adapter_config.json") as f:
    adapter_meta = json.load(f)

# Load training data
dataset = load_dataset("json", data_files="legal_qa_training.jsonl")

# Configure LoRA from ThemisDB metadata
lora_config = LoraConfig(
    task_type=TaskType.CAUSAL_LM,
    r=adapter_meta["training"]["lora_rank"],
    lora_alpha=adapter_meta["training"]["lora_alpha"],
    lora_dropout=adapter_meta["training"]["lora_dropout"],
    target_modules=adapter_meta["training"]["target_modules"],
    bias="none"
)

# Load base model
model = AutoModelForCausalLM.from_pretrained(
    adapter_meta["base_model"]["name"],
    device_map="auto",
    torch_dtype=torch.float16
)

# Apply LoRA
peft_model = get_peft_model(model, lora_config)

# Train
from transformers import Trainer, TrainingArguments

training_args = TrainingArguments(
    output_dir="./legal-qa-v1",
    num_train_epochs=adapter_meta["training"]["epochs"],
    learning_rate=adapter_meta["training"]["learning_rate"],
    per_device_train_batch_size=4,
    gradient_accumulation_steps=4,
    save_strategy="epoch",
    logging_steps=100,
)

trainer = Trainer(
    model=peft_model,
    args=training_args,
    train_dataset=dataset["train"],
)

trainer.train()
peft_model.save_pretrained("./legal-qa-v1")
```

#### Step 3: Deploy to vLLM

```bash
# Start vLLM server with multi-LoRA
python -m vllm.entrypoints.openai.api_server \
  --model mistralai/Mistral-7B-v0.1 \
  --enable-lora \
  --lora-modules legal-qa-v1=/models/adapters/legal-qa-v1 \
             medical-diagnosis-v1=/models/adapters/medical-diagnosis-v1 \
             environmental-law-v1=/models/adapters/environmental-law-v1 \
  --max-loras 8 \
  --max-lora-rank 16 \
  --port 8000
```

### Inference Workflow (VCC-Clara)

#### Query with Specific Adapter

```python
from openai import OpenAI

# VCC-Clara connects to vLLM server
client = OpenAI(
    base_url="http://vllm-server:8000/v1",
    api_key="dummy"  # vLLM doesn't require auth by default
)

# Legal query → legal-qa-v1 adapter
response = client.completions.create(
    model="mistralai/Mistral-7B-v0.1",
    prompt="Was sind die Voraussetzungen für eine Baugenehmigung?",
    max_tokens=512,
    temperature=0.7,
    extra_body={
        "lora_name": "legal-qa-v1"  # Specify adapter
    }
)

print(response.choices[0].text)

# Medical query → medical-diagnosis-v1 adapter
response = client.completions.create(
    model="mistralai/Mistral-7B-v0.1",
    prompt="What are the symptoms of pneumonia?",
    max_tokens=512,
    extra_body={
        "lora_name": "medical-diagnosis-v1"
    }
)

# Environmental law → environmental-law-v1 adapter  
response = client.completions.create(
    model="mistralai/Mistral-7B-v0.1",
    prompt="Welche Grenzwerte gelten für Lärmemissionen?",
    max_tokens=512,
    extra_body={
        "lora_name": "environmental-law-v1"
    }
)
```

#### Dynamic Adapter Selection (VCC-Clara Logic)

```python
class VCCClaraAdapter:
    """VCC-Clara intelligent adapter selection"""
    
    def __init__(self, vllm_endpoint, themisdb_endpoint):
        self.vllm = OpenAI(base_url=vllm_endpoint)
        self.themisdb = themisdb_endpoint
        self.adapter_registry = self._load_adapters()
    
    def _load_adapters(self):
        """Load available adapters from ThemisDB"""
        response = requests.get(
            f"{self.themisdb}/api/adapters/registry",
            headers={"Authorization": f"Bearer {ADMIN_TOKEN}"}
        )
        return response.json()["adapters"]
    
    def select_adapter(self, query: str, context: dict) -> str:
        """Select best adapter based on query and context"""
        # Simple domain detection (VCC-Clara can use NLP classifier)
        query_lower = query.lower()
        
        if any(word in query_lower for word in ["gericht", "urteil", "klage", "recht"]):
            return "legal-qa-v1"
        elif any(word in query_lower for word in ["patient", "diagnose", "symptom"]):
            return "medical-diagnosis-v1"
        elif any(word in query_lower for word in ["emission", "umwelt", "lärm"]):
            return "environmental-law-v1"
        else:
            return None  # Use base model
    
    def query(self, prompt: str, context: dict = None):
        """Query vLLM with automatic adapter selection"""
        adapter = self.select_adapter(prompt, context or {})
        
        extra_body = {}
        if adapter:
            extra_body["lora_name"] = adapter
            print(f"Using adapter: {adapter}")
        
        response = self.vllm.completions.create(
            model="mistralai/Mistral-7B-v0.1",
            prompt=prompt,
            max_tokens=512,
            temperature=0.7,
            extra_body=extra_body
        )
        
        return response.choices[0].text

# Usage
clara = VCCClaraAdapter(
    vllm_endpoint="http://vllm-server:8000/v1",
    themisdb_endpoint="http://themisdb:8765"
)

# Automatic adapter selection
answer = clara.query("Was sind die Voraussetzungen für eine Baugenehmigung?")
# → Automatically uses legal-qa-v1

answer = clara.query("What are the symptoms of pneumonia?")
# → Automatically uses medical-diagnosis-v1
```

## Adapter Registry API

### List Available Adapters

```http
GET /api/adapters/registry
Authorization: Bearer <admin-token>
```

**Response:**
```json
{
  "adapters": [
    {
      "adapter_id": "legal-qa-v1",
      "adapter_version": "1.0.0",
      "base_model": "mistralai/Mistral-7B-v0.1",
      "domain": "legal",
      "task_type": "question-answering",
      "status": "active",
      "created_at": "2024-11-20T10:00:00Z",
      "metrics": {
        "num_samples": 50000,
        "compliance_rate": 0.99,
        "avg_response_quality": 4.5
      }
    },
    {
      "adapter_id": "medical-diagnosis-v1",
      "adapter_version": "1.0.0",
      "base_model": "mistralai/Mistral-7B-v0.1",
      "domain": "medical",
      "task_type": "diagnosis-support",
      "status": "active",
      "created_at": "2024-11-19T15:30:00Z"
    }
  ],
  "total": 2
}
```

### Get Adapter Metadata

```http
GET /api/adapters/{adapter_id}/metadata
Authorization: Bearer <admin-token>
```

**Response:** Full adapter metadata (same as `getAdapterMetadataJson()`)

### Register New Adapter

```http
POST /api/adapters/register
Authorization: Bearer <admin-token>
Content-Type: application/json

{
  "adapter_metadata": {
    "adapter_id": "environmental-law-v1",
    "base_model_name": "mistralai/Mistral-7B-v0.1",
    "domain": "environmental",
    ...
  }
}
```

## Performance Optimization

### 1. Batch Processing Across Adapters

vLLM can batch requests for different adapters efficiently:

```python
# VCC-Clara can batch queries
queries = [
    {"prompt": "Legal question...", "adapter": "legal-qa-v1"},
    {"prompt": "Medical question...", "adapter": "medical-diagnosis-v1"},
    {"prompt": "Another legal question...", "adapter": "legal-qa-v1"},
]

# vLLM batches these efficiently (continuous batching)
responses = await asyncio.gather(*[
    clara.query_async(q["prompt"], adapter=q["adapter"]) 
    for q in queries
])
```

### 2. Adapter Caching

vLLM caches frequently used adapters in GPU memory:

```yaml
# vLLM config
max_loras: 8  # Keep up to 8 adapters in GPU memory
lora_dtype: float16  # Use FP16 for adapters (saves memory)
```

### 3. Memory Management

Calculate GPU memory requirements:

```python
# Base model: ~14GB (Mistral-7B FP16)
# Per adapter: ~16MB (rank=8), ~32MB (rank=16)
# For 8 adapters (rank=8): 14GB + 8*16MB = ~14.13GB

# Recommended: A100 (40GB) or H100 (80GB) for production
```

## VCC-Clara Deployment Architecture

### Production Setup

```yaml
# docker-compose.yml
version: '3.8'

services:
  themisdb:
    image: themisdb/themis:latest
    ports:
      - "8765:8765"
    volumes:
      - ./data:/data
      - ./config:/etc/themis
    environment:
      - THEMIS_TOKEN_ADMIN=${ADMIN_TOKEN}
  
  vllm:
    image: vllm/vllm-openai:latest
    runtime: nvidia
    environment:
      - CUDA_VISIBLE_DEVICES=0
    ports:
      - "8000:8000"
    volumes:
      - ./models:/models
    command: >
      --model mistralai/Mistral-7B-v0.1
      --enable-lora
      --lora-modules
        legal-qa-v1=/models/adapters/legal-qa-v1
        medical-diagnosis-v1=/models/adapters/medical-diagnosis-v1
        environmental-law-v1=/models/adapters/environmental-law-v1
      --max-loras 8
      --max-lora-rank 16
      --gpu-memory-utilization 0.9
  
  vcc-clara:
    image: vcc-clara:latest
    ports:
      - "3000:3000"
    environment:
      - VLLM_ENDPOINT=http://vllm:8000
      - THEMISDB_ENDPOINT=http://themisdb:8765
      - ADMIN_TOKEN=${ADMIN_TOKEN}
    depends_on:
      - themisdb
      - vllm
```

### Kubernetes Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: vllm-multi-lora
spec:
  replicas: 1
  selector:
    matchLabels:
      app: vllm
  template:
    metadata:
      labels:
        app: vllm
    spec:
      containers:
      - name: vllm
        image: vllm/vllm-openai:latest
        resources:
          limits:
            nvidia.com/gpu: 1
            memory: 48Gi
          requests:
            nvidia.com/gpu: 1
            memory: 48Gi
        ports:
        - containerPort: 8000
        volumeMounts:
        - name: models
          mountPath: /models
        command:
        - python
        - -m
        - vllm.entrypoints.openai.api_server
        args:
        - --model=mistralai/Mistral-7B-v0.1
        - --enable-lora
        - --lora-modules=legal-qa-v1=/models/adapters/legal-qa-v1
        - --lora-modules=medical-diagnosis-v1=/models/adapters/medical-diagnosis-v1
        - --max-loras=8
      volumes:
      - name: models
        persistentVolumeClaim:
          claimName: vllm-models-pvc
```

## Monitoring & Observability

### Adapter Performance Metrics

Track adapter performance in ThemisDB:

```sql
-- Store inference metrics
INSERT INTO adapter_metrics (
  adapter_id,
  timestamp,
  num_requests,
  avg_latency_ms,
  p95_latency_ms,
  error_rate,
  throughput_qps
) VALUES (
  'legal-qa-v1',
  NOW(),
  1000,
  45.2,
  89.5,
  0.001,
  22.1
);
```

### vLLM Metrics Integration

```python
# Collect vLLM metrics
import requests

vllm_metrics = requests.get("http://vllm:8000/metrics").text

# Parse Prometheus format
# vllm_num_requests_total{adapter="legal-qa-v1"} 1000
# vllm_request_duration_seconds{adapter="legal-qa-v1",quantile="0.95"} 0.0895

# Store in ThemisDB for analysis
```

## Best Practices

### 1. Adapter Management
- **Versioning**: Always increment version for adapter updates
- **A/B Testing**: Deploy v1 and v2 simultaneously, compare metrics
- **Rollback**: Keep previous versions for quick rollback

### 2. Training Data Quality
- Use schema validation for all exports
- Monitor compliance rates (target >99%)
- Regularly update adapters with fresh data

### 3. Resource Planning
- 1 adapter (rank=8) ≈ 16MB GPU memory
- 1 adapter (rank=16) ≈ 32MB GPU memory
- Plan for 2-3x base model memory for buffers

### 4. Domain Separation
- One adapter per distinct domain/task
- Avoid mixing domains (legal + medical in same adapter)
- Use adapter_id naming: `{domain}-{task}-v{version}`

## Future Enhancements

- [ ] Automatic adapter selection using embedding similarity
- [ ] Multi-adapter ensembling (query multiple adapters, merge results)
- [ ] Dynamic adapter loading based on usage patterns
- [ ] Integration with vLLM metrics for automatic quality tracking
- [ ] Adapter marketplace (publish/discover adapters)
- [ ] Cross-lingual adapters (German ↔ English)

## References

- [vLLM Documentation](https://docs.vllm.ai/)
- [vLLM Multi-LoRA Example](https://docs.vllm.ai/en/v0.4.1/getting_started/examples/multilora_inference.html)
- [PEFT Library](https://github.com/huggingface/peft)
- [ThemisDB JSONL Export API](./JSONL_LLM_EXPORTER.md)
- [LoRA Adapter Metadata](./LORA_ADAPTER_METADATA.md)
