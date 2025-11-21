# VCC-Clara JSONL Export API

REST API endpoint for VCC-Clara integration to export thematically and temporally filtered training data in JSONL format for LLM fine-tuning.

## Overview

The VCC-Clara system can query ThemisDB to export domain-specific knowledge (e.g., Rechtssprechung, Immissionsschutz) with temporal boundaries for AI training purposes.

**Use Cases:**
- Export legal case law (Rechtssprechung) from specific time periods
- Extract environmental protection (Immissionsschutz) documentation
- Generate weighted training datasets for domain-specific LLMs
- Support LoRA/QLoRA fine-tuning workflows

## Endpoint

```http
POST /api/export/jsonl_llm
```

## Authentication

```http
Authorization: Bearer <admin-token>
```

Admin token configured via `THEMIS_TOKEN_ADMIN` environment variable.

## Request Format

### Headers
```http
Authorization: Bearer <admin-token>
Content-Type: application/json
```

### Body Schema

```json
{
  "theme": "Rechtssprechung",
  "domain": "environmental_law",
  "subject": "immissionsschutz",
  "from_date": "2020-01-01",
  "to_date": "2024-12-31",
  "format": "instruction_tuning",
  "field_mapping": {
    "instruction_field": "question",
    "input_field": "context",
    "output_field": "answer"
  },
  "weighting": {
    "enable_weights": true,
    "weight_field": "importance",
    "auto_weight_by_length": true,
    "auto_weight_by_freshness": true,
    "freshness_half_life_days": 90
  },
  "quality_filters": {
    "min_output_length": 50,
    "max_output_length": 4096,
    "min_rating": 4.0,
    "remove_duplicates": true
  },
  "batch_size": 1000
}
```

### Request Parameters

#### Thematic Filtering (VCC-Clara Specific)

**`theme`** (string, optional)
- Main topic/category of exported data
- Examples: `"Rechtssprechung"`, `"Immissionsschutz"`, `"Datenschutz"`
- Maps to `category` field in ThemisDB

**`domain`** (string, optional)
- Specific domain within a theme
- Examples: `"environmental_law"`, `"labor_law"`, `"administrative_law"`
- Maps to `domain` field in ThemisDB

**`subject`** (string, optional)
- Fine-grained subject area
- Examples: `"immissionsschutz"`, `"luftqualität"`, `"lärmschutz"`
- Maps to `subject` field in ThemisDB

#### Temporal Boundaries (VCC-Clara Specific)

**`from_date`** (string, ISO 8601, optional)
- Start date for temporal filtering
- Format: `"YYYY-MM-DD"` or `"YYYY-MM-DDTHH:MM:SSZ"`
- Example: `"2020-01-01"` (includes all data from 2020 onwards)
- Maps to `created_at >= from_date` condition

**`to_date`** (string, ISO 8601, optional)
- End date for temporal filtering
- Format: `"YYYY-MM-DD"` or `"YYYY-MM-DDTHH:MM:SSZ"`
- Example: `"2024-12-31"` (includes all data up to end of 2024)
- Maps to `created_at <= to_date` condition

#### LLM Export Format

**`format`** (string, required)
- Training data format for LLM fine-tuning
- Values:
  - `"instruction_tuning"`: Q&A style (recommended for VCC-Clara)
  - `"chat_completion"`: Conversational format
  - `"text_completion"`: Document completion

**`field_mapping`** (object, required)
- Maps ThemisDB fields to LLM training format
- Required fields depend on chosen format:
  - Instruction tuning: `instruction_field`, `output_field`, `input_field` (optional)
  - Chat completion: `messages_field` or message components
  - Text completion: `text_field`

#### Weighting Configuration

**`weighting`** (object, optional)
- Controls sample importance for training
- `enable_weights` (boolean): Enable weighted sampling
- `weight_field` (string): BaseEntity field with explicit weights
- `auto_weight_by_length` (boolean): Weight by answer detail/length
- `auto_weight_by_freshness` (boolean): Weight by document recency
- `freshness_half_life_days` (number): Days for 50% weight decay (default: 90)

#### Quality Filters

**`quality_filters`** (object, optional)
- Filter low-quality training samples
- `min_output_length` (number): Minimum answer length (chars)
- `max_output_length` (number): Maximum answer length (chars)
- `min_rating` (number): Minimum quality rating (0.0-5.0)
- `remove_duplicates` (boolean): Hash-based deduplication

**`batch_size`** (number, optional, default: 1000)
- Records processed per batch (performance tuning)

## Response Format

### Success Response (200 OK)

**Headers:**
```http
Content-Type: application/x-ndjson
Content-Disposition: attachment; filename="export_exp_a1b2c3d4_Rechtssprechung.jsonl"
Transfer-Encoding: chunked
```

**Body (Streaming JSONL):**

For `format: "instruction_tuning"`:
```jsonl
{"instruction": "Was regelt das BImSchG?", "input": "", "output": "Das Bundes-Immissionsschutzgesetz (BImSchG) regelt...", "weight": 1.2, "metadata": {"theme": "Rechtssprechung", "source": "BVerwG", "date": "2023-05-15"}}
{"instruction": "Welche Grenzwerte gelten für Luftschadstoffe?", "input": "Bezogen auf Feinstaub PM10", "output": "Für Feinstaub PM10 gilt gemäß 39. BImSchV...", "weight": 1.5, "metadata": {"theme": "Immissionsschutz", "source": "TA Luft", "date": "2024-01-10"}}
```

### Error Responses

**400 Bad Request**
```json
{
  "status": "error",
  "error": "Missing required field: format"
}
```

**401 Unauthorized**
```json
{
  "status": "error",
  "error": "Unauthorized: Admin token required"
}
```

**500 Internal Server Error**
```json
{
  "status": "error",
  "error": "JSONL LLM exporter plugin not found"
}
```

## VCC-Clara Integration Examples

### Example 1: Export Rechtssprechung (Case Law) 2020-2024

```bash
curl -X POST https://themisdb.example.com/api/export/jsonl_llm \
  -H "Authorization: Bearer ${VCC_CLARA_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "theme": "Rechtssprechung",
    "domain": "environmental_law",
    "from_date": "2020-01-01",
    "to_date": "2024-12-31",
    "format": "instruction_tuning",
    "field_mapping": {
      "instruction_field": "legal_question",
      "input_field": "case_context",
      "output_field": "court_decision"
    },
    "weighting": {
      "enable_weights": true,
      "auto_weight_by_freshness": true,
      "freshness_half_life_days": 180
    },
    "quality_filters": {
      "min_output_length": 100,
      "min_rating": 4.0,
      "remove_duplicates": true
    }
  }' \
  --output rechtssprechung_2020-2024.jsonl
```

### Example 2: Export Immissionsschutz Documentation

```bash
curl -X POST https://themisdb.example.com/api/export/jsonl_llm \
  -H "Authorization: Bearer ${VCC_CLARA_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "theme": "Immissionsschutz",
    "subject": "luftqualität",
    "from_date": "2022-01-01",
    "format": "instruction_tuning",
    "field_mapping": {
      "instruction_field": "question",
      "output_field": "guideline_text"
    },
    "weighting": {
      "enable_weights": true,
      "auto_weight_by_length": true,
      "weight_field": "regulatory_importance"
    },
    "quality_filters": {
      "min_output_length": 50,
      "max_output_length": 4096
    }
  }' \
  --output immissionsschutz_guidelines.jsonl
```

### Example 3: Python Integration for VCC-Clara

```python
import requests
from datetime import datetime, timedelta

class VCCClaraExporter:
    def __init__(self, base_url, token):
        self.base_url = base_url
        self.headers = {
            'Authorization': f'Bearer {token}',
            'Content-Type': 'application/json'
        }
    
    def export_thematic_data(self, theme, domain=None, years=5):
        """
        Export thematic data with temporal boundaries.
        
        Args:
            theme: Main topic (e.g., "Rechtssprechung")
            domain: Optional domain filter
            years: Number of years back from today
        """
        to_date = datetime.now()
        from_date = to_date - timedelta(days=years*365)
        
        request_body = {
            "theme": theme,
            "from_date": from_date.strftime("%Y-%m-%d"),
            "to_date": to_date.strftime("%Y-%m-%d"),
            "format": "instruction_tuning",
            "field_mapping": {
                "instruction_field": "question",
                "output_field": "answer"
            },
            "weighting": {
                "enable_weights": True,
                "auto_weight_by_freshness": True,
                "freshness_half_life_days": 90
            },
            "quality_filters": {
                "min_output_length": 50,
                "min_rating": 4.0,
                "remove_duplicates": True
            }
        }
        
        if domain:
            request_body["domain"] = domain
        
        response = requests.post(
            f'{self.base_url}/api/export/jsonl_llm',
            headers=self.headers,
            json=request_body,
            stream=True
        )
        
        filename = f'{theme}_{from_date.year}-{to_date.year}.jsonl'
        
        with open(filename, 'wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)
        
        return filename

# Usage
exporter = VCCClaraExporter(
    base_url='https://themisdb.example.com',
    token='your-admin-token'
)

# Export Rechtssprechung from last 5 years
rechtssprechung_file = exporter.export_thematic_data(
    theme='Rechtssprechung',
    domain='environmental_law',
    years=5
)

# Export Immissionsschutz from last 3 years
immissionsschutz_file = exporter.export_thematic_data(
    theme='Immissionsschutz',
    years=3
)

print(f"Exported: {rechtssprechung_file}, {immissionsschutz_file}")
```

### Example 4: Training with Exported Data

```python
from datasets import load_dataset
from transformers import AutoTokenizer, AutoModelForCausalLM
from peft import LoraConfig, get_peft_model, TaskType

# Load VCC-Clara exported training data
rechtssprechung_dataset = load_dataset(
    'json',
    data_files='rechtssprechung_2020-2024.jsonl'
)

immissionsschutz_dataset = load_dataset(
    'json',
    data_files='immissionsschutz_guidelines.jsonl'
)

# Combine datasets (weighted by theme importance)
from datasets import concatenate_datasets
combined = concatenate_datasets([
    rechtssprechung_dataset['train'],
    immissionsschutz_dataset['train']
])

# Setup LLM
model_name = "mistralai/Mistral-7B-v0.1"
tokenizer = AutoTokenizer.from_pretrained(model_name)
model = AutoModelForCausalLM.from_pretrained(model_name)

# Configure LoRA for VCC-Clara domain adaptation
lora_config = LoraConfig(
    task_type=TaskType.CAUSAL_LM,
    r=16,
    lora_alpha=32,
    lora_dropout=0.1,
    target_modules=["q_proj", "v_proj"]
)

model = get_peft_model(model, lora_config)

# Tokenize with instruction format
def tokenize_instruction(example):
    prompt = f"### Instruction:\n{example['instruction']}\n\n"
    if example.get('input'):
        prompt += f"### Input:\n{example['input']}\n\n"
    prompt += f"### Response:\n{example['output']}"
    
    return tokenizer(prompt, truncation=True, max_length=2048)

tokenized = combined.map(tokenize_instruction, remove_columns=combined.column_names)

# Train with weighted loss (using weights from ThemisDB)
from transformers import Trainer, TrainingArguments

def compute_weighted_loss(model, inputs):
    outputs = model(**inputs)
    weights = inputs.get('weight', 1.0)
    return (outputs.loss * weights).mean()

training_args = TrainingArguments(
    output_dir='./vcc-clara-lora',
    num_train_epochs=3,
    per_device_train_batch_size=4,
    gradient_accumulation_steps=4,
    learning_rate=2e-4,
    logging_steps=10,
    save_steps=100
)

trainer = Trainer(
    model=model,
    args=training_args,
    train_dataset=tokenized,
    compute_loss=compute_weighted_loss
)

trainer.train()

# Save VCC-Clara adapted model
model.save_pretrained('./vcc-clara-rechtssprechung-adapter')
```

## Query Building Logic

The API automatically builds optimized AQL queries from request parameters:

**Example 1: Thematic + Temporal**
```json
{
  "theme": "Rechtssprechung",
  "from_date": "2020-01-01",
  "to_date": "2024-12-31"
}
```
→ AQL: `category='Rechtssprechung' AND created_at>='2020-01-01' AND created_at<='2024-12-31'`

**Example 2: Multi-level Filtering**
```json
{
  "theme": "Immissionsschutz",
  "domain": "environmental_law",
  "subject": "luftqualität",
  "min_rating": 4.5
}
```
→ AQL: `category='Immissionsschutz' AND domain='environmental_law' AND subject='luftqualität' AND rating>=4.5`

## Performance Considerations

### Recommended Settings for VCC-Clara

**Large Exports (>100k records):**
```json
{
  "batch_size": 5000,
  "quality_filters": {
    "min_output_length": 100,
    "remove_duplicates": true
  }
}
```

**Quality over Quantity:**
```json
{
  "min_rating": 4.5,
  "weighting": {
    "enable_weights": true,
    "auto_weight_by_freshness": true
  },
  "quality_filters": {
    "min_output_length": 200,
    "max_output_length": 2048
  }
}
```

### Throughput
- ~10,000 records/second (streaming)
- ~2GB/minute for typical legal documents
- Concurrent exports: Max 5 parallel requests

## Security & Access Control

1. **Token Management**: VCC-Clara should use dedicated service tokens
2. **Rate Limiting**: 100 export requests per hour per token
3. **Data Isolation**: Thematic filters ensure only authorized data is exported
4. **Audit Logging**: All export requests logged with theme, date range, and requester

## Monitoring & Observability

Export requests generate structured logs:

```json
{
  "timestamp": "2024-11-21T10:30:45Z",
  "event": "jsonl_export_requested",
  "theme": "Rechtssprechung",
  "from_date": "2020-01-01",
  "to_date": "2024-12-31",
  "requester": "vcc-clara-service",
  "export_id": "exp_a1b2c3d4e5f6",
  "status": "completed",
  "records": 15234,
  "duration_ms": 3456
}
```

## Troubleshooting

**Empty export:**
- Verify theme/domain values match ThemisDB categories
- Check temporal boundaries aren't too restrictive
- Review quality filter settings

**Timeout:**
- Reduce date range
- Increase batch_size
- Add more specific filters (theme + domain + subject)

**Low-quality samples:**
- Increase min_rating threshold
- Enable auto_weight_by_length for detailed answers
- Set higher min_output_length

## API Versioning

Current version: `v1`

Future enhancements:
- `v2`: Real-time streaming (WebSocket)
- `v2`: Async export with webhook callbacks
- `v2`: Custom weighting formulas
- `v2`: Multi-theme exports in single request
