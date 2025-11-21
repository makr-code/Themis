# LoRA Adapter Metadata & Structured Generation

## Overview

ThemisDB's JSONL LLM Exporter has been enhanced with support for **LoRax/Outlines integration** and **LoRAExchange.ai-compatible metadata tracking**. These improvements enable:

1. **Structured Generation** - JSON schema validation for training data (Outlines compatibility)
2. **Adapter Provenance** - Complete metadata tracking for LoRA adapters (LoRAExchange.ai standard)
3. **Quality Assurance** - Automated quality metrics and compliance reporting

## Inspiration & Background

### LoRax (Predibase)
LoRax is a multi-LoRA serving infrastructure that enables efficient serving of multiple fine-tuned adapters with dynamic loading and batching. Key benefits:
- Reduced serving costs through adapter sharing
- Dynamic adapter loading/unloading
- Efficient multi-tenant serving

### Outlines (Structured Generation)
Outlines ensures LLMs generate valid, structured output through:
- JSON schema-guided generation
- Regex-based constraints
- Context-free grammar (CFG) support
- Guaranteed valid JSON output

### LoRAExchange.ai
A marketplace and standard for LoRA adapter discovery, versioning, and metadata. Defines standards for:
- Adapter metadata (base model, task, performance)
- Provenance tracking (training data, hyperparameters)
- Version control and discovery

## Features

### 1. JSON Schema Validation (Outlines Integration)

Validate training samples against JSON schemas before export to ensure data quality and Outlines compatibility.

```cpp
#include "exporters/jsonl_llm_exporter.h"

JSONLLLMConfig config;
config.style = JSONLFormat::Style::INSTRUCTION_TUNING;

// Enable schema validation
config.structured_gen.enable_schema_validation = true;
config.structured_gen.reject_invalid_samples = true;
config.structured_gen.log_validation_errors = true;

// Define JSON schema for validation
config.structured_gen.json_schema = R"({
  "type": "object",
  "required": ["instruction", "output"],
  "properties": {
    "instruction": {"type": "string", "minLength": 10},
    "output": {"type": "string", "minLength": 20},
    "input": {"type": "string"}
  }
})";

// Optional: Include schema in output (for Outlines runtime)
config.structured_gen.include_schema_in_output = true;

JSONLLLMExporter exporter(config);
```

**Benefits:**
- Ensures training data quality
- Prevents invalid samples
- Compatible with Outlines for constrained decoding
- Detailed validation error reporting

**Example Output with Schema:**
```json
{
  "instruction": "What is the capital of France?",
  "output": "Paris is the capital and largest city of France.",
  "weight": 1.0,
  "__schema__": {
    "type": "object",
    "required": ["instruction", "output"],
    "properties": {...}
  }
}
```

### 2. LoRA Adapter Metadata Tracking

Track complete adapter provenance following LoRAExchange.ai standards.

```cpp
JSONLLLMConfig config;

// Enable metadata tracking
config.adapter_metadata.enable_tracking = true;

// Core identification
config.adapter_metadata.adapter_id = "themis-legal-qa-v1";
config.adapter_metadata.adapter_version = "1.2.0";

// Base model information
config.adapter_metadata.base_model_name = "mistral-7b-v0.1";
config.adapter_metadata.base_model_version = "v0.1";

// Task specification
config.adapter_metadata.task_type = "question-answering";
config.adapter_metadata.domain = "legal";
config.adapter_metadata.language = "de";

// Training configuration
auto& train = config.adapter_metadata.training_config;
train.dataset_name = "themis_legal_corpus_2024";
train.num_samples = 150000;
train.epochs = 3;
train.learning_rate = 2e-4;
train.lora_rank = 8;
train.lora_alpha = 16.0;
train.lora_dropout = 0.1;
train.target_modules = {"q_proj", "v_proj", "k_proj", "o_proj"};

// Provenance
config.adapter_metadata.created_by = "themis-ml-team";
config.adapter_metadata.data_source_uri = "themisdb://prod/legal_corpus?version=2024.11";
config.adapter_metadata.parent_adapter_id = "themis-legal-qa-v1.1";

// Custom metadata
config.adapter_metadata.custom_metadata = {
    {"compliance", "GDPR-compliant"},
    {"quality_score", "0.92"},
    {"use_case", "legal-document-qa"}
};

JSONLLLMExporter exporter(config);
```

**Export Metadata:**
```cpp
// Get metadata as JSON (for storage or publishing)
std::string metadata_json = exporter.getAdapterMetadataJson();

// Save to file for LoRAExchange.ai or model registry
std::ofstream meta_file("adapter_metadata.json");
meta_file << metadata_json;
```

**Example Metadata Output:**
```json
{
  "adapter_id": "themis-legal-qa-v1",
  "adapter_version": "1.2.0",
  "base_model": {
    "name": "mistral-7b-v0.1",
    "version": "v0.1"
  },
  "task": {
    "type": "question-answering",
    "domain": "legal",
    "language": "de"
  },
  "training": {
    "dataset_name": "themis_legal_corpus_2024",
    "num_samples": 150000,
    "epochs": 3,
    "learning_rate": 0.0002,
    "lora_rank": 8,
    "lora_alpha": 16.0,
    "lora_dropout": 0.1,
    "target_modules": ["q_proj", "v_proj", "k_proj", "o_proj"]
  },
  "provenance": {
    "created_by": "themis-ml-team",
    "data_source_uri": "themisdb://prod/legal_corpus?version=2024.11",
    "parent_adapter_id": "themis-legal-qa-v1.1"
  },
  "custom": {
    "compliance": "GDPR-compliant",
    "quality_score": "0.92",
    "use_case": "legal-document-qa"
  },
  "exported_at": "2024-11-21T08:30:00Z"
}
```

### 3. Quality Metrics Tracking

Monitor training data quality with automated metrics.

```cpp
JSONLLLMConfig config;

// Enable quality metrics
config.quality_metrics.enable_metrics = true;
config.quality_metrics.track_schema_compliance = true;
config.quality_metrics.track_length_distribution = true;
config.quality_metrics.track_diversity_score = true;

JSONLLLMExporter exporter(config);

// After export
ExportStats stats = exporter.exportEntities(entities, options);

// Get quality report
std::string quality_report = exporter.getQualityMetricsReport();
```

**Example Quality Report:**
```json
{
  "schema_validation": {
    "total_validated": 150000,
    "compliant": 148500,
    "violations": 1500,
    "compliance_rate": 0.99,
    "recent_errors": [
      "Missing required field: output",
      "Field 'instruction' too short (min 10 chars)"
    ]
  },
  "length_distribution": {
    "0": 150,
    "100": 2500,
    "200": 15000,
    "300": 45000,
    "400": 60000,
    "500": 25000,
    "600": 2350
  },
  "diversity_score": 0.87
}
```

## Use Cases

### 1. Multi-Domain LoRA Training with Provenance

Train domain-specific adapters while tracking complete lineage:

```cpp
// Legal domain adapter
JSONLLLMConfig legal_config;
legal_config.adapter_metadata.adapter_id = "themis-legal-v1";
legal_config.adapter_metadata.domain = "legal";
legal_config.adapter_metadata.task_type = "question-answering";

// Medical domain adapter  
JSONLLLMConfig medical_config;
medical_config.adapter_metadata.adapter_id = "themis-medical-v1";
medical_config.adapter_metadata.domain = "medical";
medical_config.adapter_metadata.task_type = "diagnosis-support";

// Both adapters tracked independently with full provenance
```

### 2. Incremental Training with Version Control

Track adapter evolution through versions:

```cpp
// Version 1.0
config.adapter_metadata.adapter_version = "1.0.0";
config.adapter_metadata.parent_adapter_id = "";  // Initial version

// Version 1.1 (incremental training on new data)
config.adapter_metadata.adapter_version = "1.1.0";
config.adapter_metadata.parent_adapter_id = "themis-legal-qa-v1.0";
config.adapter_metadata.custom_metadata["changelog"] = "Added 20k recent court decisions";

// Version 2.0 (major update with different base model)
config.adapter_metadata.adapter_version = "2.0.0";
config.adapter_metadata.base_model_name = "mistral-7b-v0.2";  // Updated base
config.adapter_metadata.parent_adapter_id = "themis-legal-qa-v1.1";
```

### 3. Quality-Assured Export with Schema Validation

Ensure only high-quality, schema-compliant data is exported:

```cpp
JSONLLLMConfig config;

// Strict quality requirements
config.quality.min_text_length = 50;
config.quality.max_text_length = 4096;
config.quality.skip_empty_outputs = true;
config.quality.skip_duplicates = true;

// Schema validation
config.structured_gen.enable_schema_validation = true;
config.structured_gen.reject_invalid_samples = true;  // Drop non-compliant samples
config.structured_gen.json_schema = load_schema("qa_schema.json");

// Quality tracking
config.quality_metrics.enable_metrics = true;

JSONLLLMExporter exporter(config);
auto stats = exporter.exportEntities(entities, options);

// Review quality
std::cout << "Compliance rate: " << exporter.getQualityMetricsReport() << std::endl;
std::cout << "Rejected samples: " << stats.failed_entities << std::endl;
```

### 4. Outlines-Compatible Export

Generate training data that works seamlessly with Outlines for constrained decoding:

```cpp
JSONLLLMConfig config;
config.style = JSONLFormat::Style::INSTRUCTION_TUNING;

// Include schema in each sample for Outlines runtime
config.structured_gen.enable_schema_validation = true;
config.structured_gen.include_schema_in_output = true;
config.structured_gen.json_schema = R"({
  "type": "object",
  "required": ["instruction", "output"],
  "properties": {
    "instruction": {"type": "string"},
    "output": {
      "type": "object",
      "required": ["answer", "confidence"],
      "properties": {
        "answer": {"type": "string"},
        "confidence": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "sources": {"type": "array", "items": {"type": "string"}}
      }
    }
  }
})";

// During inference with Outlines, the schema ensures structured output
```

## API Reference

### Configuration Structures

#### `StructuredGeneration`
```cpp
struct StructuredGeneration {
    bool enable_schema_validation = false;
    std::string json_schema;
    bool include_schema_in_output = false;
    bool reject_invalid_samples = true;
    bool log_validation_errors = true;
};
```

#### `AdapterMetadata`
```cpp
struct AdapterMetadata {
    bool enable_tracking = false;
    std::string adapter_id;
    std::string adapter_version = "1.0.0";
    std::string base_model_name;
    std::string base_model_version;
    std::string task_type;
    std::string domain;
    std::string language = "en";
    
    struct TrainingConfig {
        std::string dataset_name;
        size_t num_samples = 0;
        int epochs = 0;
        double learning_rate = 0.0;
        int lora_rank = 8;
        double lora_alpha = 16.0;
        double lora_dropout = 0.1;
        std::vector<std::string> target_modules;
    } training_config;
    
    std::string created_by;
    std::string data_source_uri;
    std::string parent_adapter_id;
    std::map<std::string, std::string> custom_metadata;
};
```

#### `QualityMetrics`
```cpp
struct QualityMetrics {
    bool enable_metrics = false;
    bool track_per_sample = false;
    bool aggregate_stats = true;
    bool track_schema_compliance = true;
    bool track_length_distribution = true;
    bool track_diversity_score = true;
};
```

### Methods

#### `validateAgainstSchema()`
```cpp
bool validateAgainstSchema(const std::string& json_str, std::string* error = nullptr) const;
```
Validate a JSON string against the configured schema.

#### `getAdapterMetadataJson()`
```cpp
std::string getAdapterMetadataJson() const;
```
Export adapter metadata as JSON (LoRAExchange.ai format).

#### `setAdapterMetadataFromJson()`
```cpp
bool setAdapterMetadataFromJson(const std::string& json_str, std::string* error = nullptr);
```
Load adapter metadata from JSON.

#### `getQualityMetricsReport()`
```cpp
std::string getQualityMetricsReport() const;
```
Get quality metrics report as JSON.

## Integration Examples

### With HuggingFace PEFT

```python
from transformers import AutoModelForCausalLM
from peft import LoraConfig, get_peft_model
import json

# Load adapter metadata from ThemisDB export
with open("adapter_metadata.json") as f:
    metadata = json.load(f)

# Configure PEFT LoRA with ThemisDB metadata
lora_config = LoraConfig(
    r=metadata["training"]["lora_rank"],
    lora_alpha=metadata["training"]["lora_alpha"],
    lora_dropout=metadata["training"]["lora_dropout"],
    target_modules=metadata["training"]["target_modules"],
    task_type="CAUSAL_LM"
)

# Load base model
model = AutoModelForCausalLM.from_pretrained(
    metadata["base_model"]["name"]
)

# Apply LoRA
peft_model = get_peft_model(model, lora_config)
```

### With Outlines for Structured Generation

```python
import outlines
import json

# Load training sample with schema
with open("training_sample.jsonl") as f:
    sample = json.loads(f.readline())

# Schema embedded in training data
schema = sample.get("__schema__")

# Use same schema during inference
model = outlines.models.transformers("mistral-7b")
generator = outlines.generate.json(model, schema)

# Guaranteed schema-compliant output
result = generator(sample["instruction"])
```

## Best Practices

### 1. Schema Design
- Keep schemas simple and focused
- Use `required` fields sparingly
- Include `minLength`/`maxLength` constraints
- Test schemas with representative samples

### 2. Metadata Management
- Use semantic versioning (MAJOR.MINOR.PATCH)
- Always link to parent adapters for incremental training
- Include meaningful custom metadata
- Store metadata alongside adapter weights

### 3. Quality Assurance
- Enable schema validation for production exports
- Review quality metrics reports regularly
- Set appropriate length thresholds
- Monitor compliance rates (target >95%)

### 4. Versioning
- Increment PATCH for bug fixes
- Increment MINOR for new data/features
- Increment MAJOR for base model changes
- Document changes in custom_metadata["changelog"]

## Performance Considerations

- Schema validation adds ~5-10% overhead
- Metadata tracking has negligible impact
- Quality metrics tracking adds ~2-3% overhead
- Disable metrics for maximum throughput

## Future Enhancements

- [ ] Integration with nlohmann/json-schema-validator for full JSON Schema support
- [ ] Automatic schema inference from sample data
- [ ] Performance metrics (eval_loss, accuracy, perplexity)
- [ ] Integration with model registries (MLflow, Weights & Biases)
- [ ] Diversity metrics (unique n-grams, topic distribution)
- [ ] Support for Outlines regex and CFG constraints

## References

- [LoRax by Predibase](https://predibase.com/blog/lorax-outlines-better-json-extraction-with-structured-generation-and-lora)
- [Outlines - Structured Generation](https://github.com/outlines-dev/outlines)
- [LoRAExchange.ai](https://loraexchange.ai/)
- [PEFT - Parameter-Efficient Fine-Tuning](https://github.com/huggingface/peft)
- [JSON Schema](https://json-schema.org/)
