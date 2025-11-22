# PII Detection Engine Extensions

## Overview

The PII detection system uses a plugin architecture that allows multiple detection engines to work together:

1. **RegexDetectionEngine** (default, always available)
2. **NERDetectionEngine** (optional, requires external dependencies)
3. **EmbeddingDetectionEngine** (optional, requires external dependencies)

## Current Status

✅ **Implemented:**
- Plugin architecture (`IPIIDetectionEngine` interface)
- RegexDetectionEngine with YAML configuration
- Engine factory and orchestration
- Runtime reload with validation

⏳ **Ready for Implementation:**
- NERDetectionEngine (requires MITIE or ONNX Runtime)
- EmbeddingDetectionEngine (requires fastText or word2vec)

## Future Engine: NER (Named Entity Recognition)

### Dependencies

**Option 1: MITIE (Recommended for C++)**
```bash
vcpkg install mitie
```

**Option 2: ONNX Runtime (For pre-trained BERT/RoBERTa models)**
```bash
vcpkg install onnxruntime
```

### YAML Configuration

```yaml
detection_engines:
  - type: "ner"
    enabled: true
    settings:
      model_path: "models/pii_ner.dat"  # MITIE model
      # OR
      model_path: "models/bert_ner.onnx"  # ONNX BERT model
      model_type: "mitie"  # or "onnx_bert"
      confidence_threshold: 0.85
      batch_size: 32  # For ONNX models
    
    entity_types:
      - name: "PERSON"
        pii_type: "PERSON_NAME"
        redaction_mode: "strict"
        enabled: true
      
      - name: "GPE"  # Geo-Political Entity (locations)
        pii_type: "LOCATION"
        redaction_mode: "partial"
        enabled: false
      
      - name: "ORG"
        pii_type: "ORGANIZATION"
        redaction_mode: "none"
        enabled: false
```

### Implementation Sketch

```cpp
class NERDetectionEngine : public IPIIDetectionEngine {
private:
    std::unique_ptr<MitieNER> ner_model_;  // or ONNXRuntime
    std::unordered_map<std::string, PIIType> entity_mapping_;
    
public:
    bool initialize(const nlohmann::json& config) override {
        std::string model_path = config["settings"]["model_path"];
        std::string model_type = config["settings"]["model_type"];
        
        if (model_type == "mitie") {
            ner_model_ = std::make_unique<MitieNER>(model_path);
        } else if (model_type == "onnx_bert") {
            ner_model_ = std::make_unique<OnnxBertNER>(model_path);
        }
        
        // Map entity types to PII types
        for (const auto& entity : config["entity_types"]) {
            if (entity["enabled"].get<bool>()) {
                entity_mapping_[entity["name"]] = 
                    PIITypeUtils::fromString(entity["pii_type"]);
            }
        }
        
        return ner_model_->isLoaded();
    }
    
    std::vector<PIIFinding> detectInText(const std::string& text) const override {
        auto entities = ner_model_->extract(text);
        std::vector<PIIFinding> findings;
        
        for (const auto& entity : entities) {
            auto it = entity_mapping_.find(entity.label);
            if (it != entity_mapping_.end()) {
                PIIFinding finding;
                finding.type = it->second;
                finding.value = entity.text;
                finding.start_offset = entity.start;
                finding.end_offset = entity.end;
                finding.confidence = entity.score;
                finding.pattern_name = entity.label;
                finding.engine_name = "ner";
                findings.push_back(finding);
            }
        }
        
        return findings;
    }
};
```

### Training Custom NER Models

**MITIE Training:**
```bash
# Prepare annotated data (CoNLL format)
# Train MITIE model
mitie-train ner_trainer pii_training_data.txt pii_ner.dat
```

**ONNX Models:**
- Use pre-trained models from Hugging Face
- Convert to ONNX format with `transformers` library
- Example models:
  - `dslim/bert-base-NER` (English)
  - `dbmdz/bert-large-cased-finetuned-conll03-english`
  - German: `deepset/gbert-base-germandpr`

## Future Engine: Embeddings (Semantic Similarity)

### Dependencies

**fastText (Recommended)**
```bash
vcpkg install fasttext
```

### YAML Configuration

```yaml
detection_engines:
  - type: "embedding"
    enabled: true
    settings:
      model_path: "models/cc.de.300.bin"  # fastText German model
      model_type: "fasttext"
      similarity_threshold: 0.80
      context_window: 5  # Words before/after to consider
    
    sensitive_keywords:
      - keyword: "gehalt"
        pii_type: "SALARY"
        similarity_threshold: 0.85
        redaction_mode: "strict"
      
      - keyword: "krankheit"
        pii_type: "HEALTH_INFO"
        similarity_threshold: 0.85
        redaction_mode: "strict"
      
      - keyword: "passwort"
        pii_type: "CREDENTIAL"
        similarity_threshold: 0.90
        redaction_mode: "strict"
```

### Implementation Sketch

```cpp
class EmbeddingDetectionEngine : public IPIIDetectionEngine {
private:
    std::unique_ptr<fasttext::FastText> model_;
    std::vector<SensitiveKeyword> keywords_;
    
    struct SensitiveKeyword {
        std::string keyword;
        PIIType type;
        double threshold;
        std::string redaction_mode;
    };
    
public:
    std::vector<PIIFinding> detectInText(const std::string& text) const override {
        auto words = tokenize(text);
        std::vector<PIIFinding> findings;
        
        for (size_t i = 0; i < words.size(); ++i) {
            auto word_vec = model_->getWordVector(words[i]);
            
            for (const auto& keyword : keywords_) {
                auto keyword_vec = model_->getWordVector(keyword.keyword);
                double similarity = cosineSimilarity(word_vec, keyword_vec);
                
                if (similarity >= keyword.threshold) {
                    // Extract context window
                    std::string context = extractContext(words, i, context_window_);
                    
                    PIIFinding finding;
                    finding.type = keyword.type;
                    finding.value = context;
                    finding.confidence = similarity;
                    finding.pattern_name = keyword.keyword;
                    finding.engine_name = "embedding";
                    findings.push_back(finding);
                }
            }
        }
        
        return findings;
    }
};
```

### Pre-trained Models

**fastText:**
- Download: https://fasttext.cc/docs/en/crawl-vectors.html
- German: `cc.de.300.bin` (6.7 GB)
- English: `cc.en.300.bin` (5.8 GB)

**word2vec:**
- Google News: `GoogleNews-vectors-negative300.bin`
- German: `german.model` (DeReWo)

## Integration Steps

### 1. Add Dependencies to vcpkg.json

```json
{
  "dependencies": [
    "mitie",        // For NER
    "onnxruntime",  // For BERT-based NER
    "fasttext"      // For embeddings
  ],
  "overrides": [
    {
      "name": "mitie",
      "version": "0.7"
    }
  ]
}
```

### 2. Update CMakeLists.txt

```cmake
# Optional NER support
option(ENABLE_PII_NER "Enable NER-based PII detection" OFF)
if(ENABLE_PII_NER)
    find_package(mitie CONFIG)
    if(mitie_FOUND)
        target_link_libraries(themis_core PRIVATE mitie::mitie)
        target_compile_definitions(themis_core PRIVATE THEMIS_ENABLE_NER)
    endif()
endif()

# Optional embedding support
option(ENABLE_PII_EMBEDDING "Enable embedding-based PII detection" OFF)
if(ENABLE_PII_EMBEDDING)
    find_package(fastText CONFIG)
    if(fastText_FOUND)
        target_link_libraries(themis_core PRIVATE fastText::fastText)
        target_compile_definitions(themis_core PRIVATE THEMIS_ENABLE_EMBEDDING)
    endif()
endif()
```

### 3. Conditional Compilation

```cpp
// In pii_detection_engine_factory.cpp
std::unique_ptr<IPIIDetectionEngine> PIIDetectionEngineFactory::create(
    const std::string& engine_type) {
    
    if (engine_type == "regex") {
        return std::make_unique<RegexDetectionEngine>();
    }
    
#ifdef THEMIS_ENABLE_NER
    if (engine_type == "ner") {
        return std::make_unique<NERDetectionEngine>();
    }
#endif
    
#ifdef THEMIS_ENABLE_EMBEDDING
    if (engine_type == "embedding") {
        return std::make_unique<EmbeddingDetectionEngine>();
    }
#endif
    
    return nullptr;
}
```

## Performance Considerations

| Engine | Speed | Accuracy | Memory | Use Case |
|--------|-------|----------|--------|----------|
| Regex | Very Fast | Good (95%+) | Low | Structured PII (email, SSN, cards) |
| NER | Medium | Excellent (98%+) | Medium | Names, locations, organizations |
| Embedding | Slow | Variable | High | Context-based, semantic PII |

**Recommendation:**
- Default: Regex only (fast, low overhead)
- Enhanced: Regex + NER (best balance)
- Advanced: All three (highest accuracy, higher latency)

## Testing Strategy

```cpp
TEST(PIIDetectorTest, MultiEngineDetection) {
    // Enable both regex and NER
    PIIDetector detector("config/pii_patterns_with_ner.yaml");
    
    std::string text = "Contact Max Mustermann at max@example.com";
    auto findings = detector.detectInText(text);
    
    // Should find:
    // 1. "Max Mustermann" via NER (PERSON_NAME)
    // 2. "max@example.com" via Regex (EMAIL)
    ASSERT_EQ(findings.size(), 2);
    
    EXPECT_EQ(findings[0].engine_name, "ner");
    EXPECT_EQ(findings[0].type, PIIType::PERSON_NAME);
    
    EXPECT_EQ(findings[1].engine_name, "regex");
    EXPECT_EQ(findings[1].type, PIIType::EMAIL);
}
```

## Deployment

**Production Checklist:**
1. ✅ Regex engine always enabled (safe default)
2. ⏳ NER engine optional (enable for high-value data)
3. ⏳ Embedding engine optional (enable for advanced use cases)
4. ✅ YAML config with engine sections
5. ✅ Fallback to embedded defaults
6. ⏳ Model files deployed to `models/` directory
7. ⏳ Memory limits configured (prevent OOM)
8. ⏳ Performance monitoring (track detection latency)

## Future Enhancements

- **Multi-language Support**: Load language-specific models per tenant
- **Custom Training**: API for training custom NER models on tenant data
- **Explainability**: Return detection reasoning (which words triggered)
- **Confidence Calibration**: Adjust thresholds based on false positive rates
- **GPU Acceleration**: Use CUDA for ONNX models in high-throughput scenarios
