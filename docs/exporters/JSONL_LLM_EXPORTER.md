# JSONL LLM Exporter - LoRA/QLoRA Training Data Export

## Overview

The JSONL LLM Exporter exports ThemisDB BaseEntity data as **weighted training samples** in JSONL format for fine-tuning Large Language Models with **LoRA** (Low-Rank Adaptation) and **QLoRA** (Quantized LoRA).

### Key Features

✅ **Multiple LLM Formats**
- Instruction Tuning (`{"instruction": ..., "input": ..., "output": ...}`)
- Chat Completion (`{"messages": [{"role": ..., "content": ...}]}`)
- Text Completion (`{"text": ...}`)

✅ **Weighted Training Samples**
- Explicit weight field (e.g., `importance: 0.8`)
- Auto-weighting by text length
- Auto-weighting by data freshness
- Custom weighting strategies

✅ **Quality Filtering**
- Min/max text length constraints
- Empty output detection
- Duplicate detection
- Configurable quality thresholds

✅ **Metadata Enrichment**
- Source tracking
- Category/tag preservation
- Custom metadata fields

## Installation

### As Plugin

```bash
# Load via PluginManager
auto& pm = PluginManager::instance();
pm.scanPluginDirectory("./plugins");
auto* plugin = pm.loadPlugin("jsonl_llm_exporter");
auto* exporter = static_cast<IExporter*>(plugin->getInstance());
```

### Direct Usage

```cpp
#include "exporters/jsonl_llm_exporter.h"

JSONLLLMConfig config;
config.style = JSONLFormat::Style::INSTRUCTION_TUNING;
config.weighting.enable_weights = true;
config.weighting.auto_weight_by_length = true;

JSONLLLMExporter exporter(config);
```

## Configuration

### Instruction Tuning Format

Best for question-answering, task completion:

```cpp
JSONLLLMConfig config;
config.style = JSONLFormat::Style::INSTRUCTION_TUNING;
config.field_mapping.instruction_field = "question";
config.field_mapping.input_field = "context";
config.field_mapping.output_field = "answer";
```

**BaseEntity Example:**
```json
{
  "pk": "qa_001",
  "question": "What is the capital of France?",
  "context": "France is a country in Western Europe",
  "answer": "Paris is the capital of France.",
  "importance": 0.9
}
```

**JSONL Output:**
```json
{"instruction": "What is the capital of France?", "input": "France is a country in Western Europe", "output": "Paris is the capital of France.", "weight": 0.9}
```

### Chat Completion Format

Best for conversational AI:

```cpp
JSONLLLMConfig config;
config.style = JSONLFormat::Style::CHAT_COMPLETION;
config.field_mapping.system_field = "system_prompt";
config.field_mapping.user_field = "user_message";
config.field_mapping.assistant_field = "assistant_response";
```

**BaseEntity Example:**
```json
{
  "pk": "chat_001",
  "system_prompt": "You are a helpful assistant.",
  "user_message": "Explain quantum computing",
  "assistant_response": "Quantum computing uses quantum bits...",
  "importance": 1.2
}
```

**JSONL Output:**
```json
{"messages": [{"role": "system", "content": "You are a helpful assistant."}, {"role": "user", "content": "Explain quantum computing"}, {"role": "assistant", "content": "Quantum computing uses quantum bits..."}], "weight": 1.2}
```

### Text Completion Format

Best for text generation, next-word prediction:

```cpp
JSONLLLMConfig config;
config.style = JSONLFormat::Style::TEXT_COMPLETION;
config.field_mapping.text_field = "content";
```

## Weighting Strategies

### 1. Explicit Weights

```cpp
config.weighting.enable_weights = true;
config.weighting.weight_field = "importance";  // Field in BaseEntity
config.weighting.default_weight = 1.0;         // If field missing
```

**Use Case:** Domain experts manually assign importance scores.

### 2. Auto-Weight by Length

```cpp
config.weighting.auto_weight_by_length = true;
```

**Formula:** `weight *= (1.0 + min(0.5, length / 2000.0))`

**Use Case:** Longer, more detailed responses get higher weights (up to 1.5x).

### 3. Auto-Weight by Freshness

```cpp
config.weighting.auto_weight_by_freshness = true;
config.weighting.timestamp_field = "created_at";
```

**Use Case:** Newer data is more valuable (recent trends, updated information).

### 4. Combined Strategies

```cpp
config.weighting.enable_weights = true;
config.weighting.auto_weight_by_length = true;
config.weighting.auto_weight_by_freshness = true;
```

Weights are multiplied: `final_weight = explicit_weight × length_factor × freshness_factor`

## Quality Filtering

### Length Constraints

```cpp
config.quality.min_text_length = 50;      // Skip very short responses
config.quality.max_text_length = 8192;    // Skip excessively long responses
```

### Empty Output Detection

```cpp
config.quality.skip_empty_outputs = true;  // Skip if output field is empty
```

### Duplicate Detection

```cpp
config.quality.skip_duplicates = true;  // Hash-based duplicate removal
```

## Metadata Enrichment

```cpp
config.include_metadata = true;
config.metadata_fields = {"source", "category", "tags", "author"};
```

**Output with metadata:**
```json
{"instruction": "...", "output": "...", "weight": 1.0, "metadata": {"source": "wikipedia", "category": "science", "tags": ["physics", "quantum"]}}
```

## Usage Examples

### Example 1: Export FAQ Database for LoRA Training

```cpp
// Load entities from ThemisDB
std::vector<BaseEntity> faqs = db.query("category=faq");

// Configure exporter
JSONLLLMConfig config;
config.style = JSONLFormat::Style::INSTRUCTION_TUNING;
config.field_mapping.instruction_field = "question";
config.field_mapping.output_field = "answer";
config.weighting.enable_weights = true;
config.weighting.weight_field = "upvotes";  // Use upvotes as weights

JSONLLLMExporter exporter(config);

// Export
ExportOptions options;
options.output_path = "training_data/faq_lora.jsonl";
options.progress_callback = [](const ExportStats& stats) {
    std::cout << "Exported: " << stats.exported_entities << " entities\n";
};

auto stats = exporter.exportEntities(faqs, options);
std::cout << stats.toJson() << std::endl;
```

### Example 2: Export Chat Logs for QLoRA

```cpp
// Load chat conversations
std::vector<BaseEntity> chats = db.query("type=conversation AND rating>4");

// Configure for chat format
JSONLLLMConfig config;
config.style = JSONLFormat::Style::CHAT_COMPLETION;
config.field_mapping.user_field = "user_query";
config.field_mapping.assistant_field = "bot_response";
config.weighting.auto_weight_by_length = true;  // Detailed responses weighted higher
config.quality.min_text_length = 100;           // Skip short exchanges

JSONLLLMExporter exporter(config);

// Export for QLoRA training
ExportOptions options;
options.output_path = "training_data/chat_qlora.jsonl";

auto stats = exporter.exportEntities(chats, options);
```

### Example 3: Export Knowledge Base with Freshness Weighting

```cpp
// Load recent knowledge articles
std::vector<BaseEntity> articles = db.query("type=article");

// Prioritize recent content
JSONLLLMConfig config;
config.style = JSONLFormat::Style::TEXT_COMPLETION;
config.field_mapping.text_field = "full_text";
config.weighting.auto_weight_by_freshness = true;
config.weighting.timestamp_field = "published_date";
config.include_metadata = true;
config.metadata_fields = {"author", "topic", "published_date"};

JSONLLLMExporter exporter(config);

ExportOptions options;
options.output_path = "training_data/kb_weighted.jsonl";

auto stats = exporter.exportEntities(articles, options);
```

## Training with Exported Data

### LoRA Training (HuggingFace PEFT)

```python
from datasets import load_dataset
from peft import LoraConfig, get_peft_model
from transformers import AutoModelForCausalLM, TrainingArguments, Trainer

# Load exported JSONL
dataset = load_dataset("json", data_files="faq_lora.jsonl")

# Configure LoRA
lora_config = LoraConfig(
    r=16,
    lora_alpha=32,
    target_modules=["q_proj", "v_proj"],
    lora_dropout=0.05,
    bias="none",
    task_type="CAUSAL_LM"
)

# Load base model
model = AutoModelForCausalLM.from_pretrained("meta-llama/Llama-2-7b")
model = get_peft_model(model, lora_config)

# Use weights from JSONL
def compute_loss(model, inputs, weights):
    outputs = model(**inputs)
    loss = outputs.loss
    return (loss * weights).mean()  # Weight by importance

# Train
trainer = Trainer(model=model, args=training_args, train_dataset=dataset)
trainer.train()
```

### QLoRA Training (bitsandbytes)

```python
from transformers import AutoModelForCausalLM, BitsAndBytesConfig
import torch

# 4-bit quantization for QLoRA
bnb_config = BitsAndBytesConfig(
    load_in_4bit=True,
    bnb_4bit_use_double_quant=True,
    bnb_4bit_quant_type="nf4",
    bnb_4bit_compute_dtype=torch.bfloat16
)

# Load quantized model
model = AutoModelForCausalLM.from_pretrained(
    "meta-llama/Llama-2-7b",
    quantization_config=bnb_config,
    device_map="auto"
)

# Apply LoRA on quantized model
from peft import prepare_model_for_kbit_training, LoraConfig

model = prepare_model_for_kbit_training(model)
model = get_peft_model(model, lora_config)

# Train with weighted samples from JSONL
# (Same as above)
```

## Output Statistics

```json
{
  "total_entities": 10000,
  "exported_entities": 9500,
  "failed_entities": 500,
  "bytes_written": 15728640,
  "duration_ms": 2300,
  "errors": [
    "Entity qa_123: Missing required field 'output'",
    "Entity qa_456: Text too short (5 chars)"
  ]
}
```

## Limitations

- **No streaming**: Entire entity set loaded in memory
- **Single file output**: No sharding for very large datasets
- **Fixed field mappings**: Custom transformations require code changes

## Planned Enhancements (v2.0)

- [ ] Streaming export for large datasets
- [ ] Automatic dataset sharding
- [ ] Data augmentation (paraphrasing, back-translation)
- [ ] Multi-turn conversation support
- [ ] Token counting for optimal batch sizes
- [ ] Integration with HuggingFace Hub

## See Also

- [BaseEntity Documentation](../storage/base_entity.md)
- [Plugin Architecture](../plugins/PLUGIN_MIGRATION.md)
- [Importer Interface](importer_interface.h)
