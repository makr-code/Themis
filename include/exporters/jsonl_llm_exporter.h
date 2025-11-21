#pragma once

#include "exporter_interface.h"
#include "plugins/plugin_interface.h"
#include <map>

namespace themis::exporters {

/// JSONL format for LLM fine-tuning (LoRA/QLoRA)
/// Exports BaseEntity data as weighted training samples
struct JSONLFormat {
    enum class Style {
        INSTRUCTION_TUNING,  // {"instruction": ..., "input": ..., "output": ...}
        CHAT_COMPLETION,     // {"messages": [{"role": ..., "content": ...}]}
        TEXT_COMPLETION,     // {"text": ...}
        CUSTOM              // User-defined mapping
    };
};

/// Configuration for JSONL LLM export
struct JSONLLLMConfig {
    JSONLFormat::Style style = JSONLFormat::Style::INSTRUCTION_TUNING;
    
    // Field mappings for different styles
    struct FieldMapping {
        std::string instruction_field = "question";
        std::string input_field = "context";
        std::string output_field = "answer";
        std::string text_field = "content";
        std::string system_field = "system_prompt";
        std::string user_field = "user_message";
        std::string assistant_field = "assistant_response";
    } field_mapping;
    
    // Weighting strategy for training importance
    struct WeightConfig {
        bool enable_weights = true;
        std::string weight_field = "importance";  // Field containing weight (0.0-1.0)
        double default_weight = 1.0;
        
        // Auto-weighting strategies
        bool auto_weight_by_length = false;       // Longer responses = higher weight
        bool auto_weight_by_freshness = false;    // Newer data = higher weight
        std::string timestamp_field = "created_at";
    } weighting;
    
    // Quality filtering
    struct QualityFilter {
        size_t min_text_length = 10;
        size_t max_text_length = 8192;
        bool skip_empty_outputs = true;
        bool skip_duplicates = true;
    } quality;
    
    // Metadata enrichment
    bool include_metadata = true;
    std::vector<std::string> metadata_fields = {"source", "category", "tags"};
    
    // Structured generation support (Outlines open-source integration)
    struct StructuredGeneration {
        bool enable_schema_validation = false;
        std::string json_schema;  // JSON Schema for output validation
        bool include_schema_in_output = false;  // Add schema field to JSONL
        bool reject_invalid_samples = true;  // Skip samples that don't match schema
        bool log_validation_errors = true;
    } structured_gen;
    
    // LoRA adapter metadata tracking
    struct AdapterMetadata {
        bool enable_tracking = false;
        std::string adapter_id;  // Unique identifier for this LoRA adapter
        std::string adapter_version = "1.0.0";
        std::string base_model_name;  // e.g., "llama-2-7b", "mistralai/Mistral-7B-v0.1"
        std::string base_model_version;
        std::string task_type;  // e.g., "question-answering", "summarization"
        std::string domain;  // e.g., "legal", "medical", "general"
        std::string language = "en";
        
        // vLLM-specific configuration
        struct VLLMConfig {
            bool enabled = false;
            std::string adapter_path;  // Path where adapter will be deployed for vLLM
            std::string vllm_version = ">=0.4.0";  // Minimum vLLM version required
            int max_lora_rank = 16;  // Maximum LoRA rank supported
            bool enable_multi_lora = true;  // Support multi-LoRA batching
            std::map<std::string, std::string> serving_config;  // vLLM serving parameters
        } vllm_config;
        
        // Training metadata (to be filled during training)
        struct TrainingConfig {
            std::string dataset_name;
            size_t num_samples = 0;
            int epochs = 0;
            double learning_rate = 0.0;
            int lora_rank = 8;
            double lora_alpha = 16.0;
            double lora_dropout = 0.1;
            std::vector<std::string> target_modules;  // e.g., ["q_proj", "v_proj"]
        } training_config;
        
        // Provenance
        std::string created_by;
        std::string data_source_uri;  // ThemisDB connection string or query
        std::string parent_adapter_id;  // For incremental training
        std::map<std::string, std::string> custom_metadata;
    } adapter_metadata;
    
    // Quality metrics tracking
    struct QualityMetrics {
        bool enable_metrics = false;
        bool track_per_sample = false;
        bool aggregate_stats = true;
        
        // Metrics to track
        bool track_schema_compliance = true;
        bool track_length_distribution = true;
        bool track_diversity_score = true;  // Unique n-grams ratio
    } quality_metrics;
};

/// JSONL exporter for LLM fine-tuning (LoRA/QLoRA)
class JSONLLLMExporter : public IExporter {
public:
    explicit JSONLLLMExporter(const JSONLLLMConfig& config = {});
    
    ExportStats exportEntities(
        const std::vector<BaseEntity>& entities,
        const ExportOptions& options
    ) override;
    
    std::vector<std::string> getSupportedFormats() const override {
        return {"jsonl", "llm_jsonl", "lora_jsonl", "qlora_jsonl"};
    }
    
    std::string getName() const override { return "jsonl_llm_exporter"; }
    std::string getVersion() const override { return "1.0.0"; }
    
    /// Set custom configuration
    void setConfig(const JSONLLLMConfig& config) { config_ = config; }
    
    /// Get current configuration
    const JSONLLLMConfig& getConfig() const { return config_; }
    
    /// Validate sample against JSON schema (Outlines compatibility)
    bool validateAgainstSchema(const std::string& json_str, std::string* error = nullptr) const;
    
    /// Get adapter metadata as JSON (for LoRAExchange compatibility)
    std::string getAdapterMetadataJson() const;
    
    /// Set adapter metadata from JSON
    bool setAdapterMetadataFromJson(const std::string& json_str, std::string* error = nullptr);
    
    /// Get quality metrics report
    std::string getQualityMetricsReport() const;
    
private:
    JSONLLLMConfig config_;
    
    // Export helpers
    std::string formatInstructionTuning(const BaseEntity& entity, double& weight);
    std::string formatChatCompletion(const BaseEntity& entity, double& weight);
    std::string formatTextCompletion(const BaseEntity& entity, double& weight);
    
    double calculateWeight(const BaseEntity& entity);
    bool passesQualityFilter(const BaseEntity& entity);
    std::string extractMetadata(const BaseEntity& entity);
    
    // Schema validation helpers
    bool validateJsonSchema(const std::string& json_str, const std::string& schema, std::string* error) const;
    
    // Quality metrics tracking
    mutable struct RuntimeMetrics {
        size_t total_validated = 0;
        size_t schema_compliant = 0;
        size_t schema_violations = 0;
        std::map<size_t, size_t> length_distribution;  // bucket -> count
        double diversity_score = 0.0;
        std::vector<std::string> validation_errors;
    } runtime_metrics_;
};

/// Plugin wrapper for JSONL LLM Exporter
class JSONLLLMExporterPlugin : public IThemisPlugin {
public:
    explicit JSONLLLMExporterPlugin(const JSONLLLMConfig& config = {})
        : exporter_(std::make_unique<JSONLLLMExporter>(config)) {}
    
    const char* getName() const override { return "jsonl_llm_exporter"; }
    const char* getVersion() const override { return "1.0.0"; }
    PluginType getType() const override { return PluginType::EXPORTER; }
    
    bool initialize(const std::string& config_path) override {
        // TODO: Parse config from JSON file
        return true;
    }
    
    void shutdown() override {
        exporter_.reset();
    }
    
    void* getInstance() override {
        return static_cast<IExporter*>(exporter_.get());
    }
    
private:
    std::unique_ptr<JSONLLLMExporter> exporter_;
};

} // namespace themis::exporters
