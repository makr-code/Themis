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
    
private:
    JSONLLLMConfig config_;
    
    // Export helpers
    std::string formatInstructionTuning(const BaseEntity& entity, double& weight);
    std::string formatChatCompletion(const BaseEntity& entity, double& weight);
    std::string formatTextCompletion(const BaseEntity& entity, double& weight);
    
    double calculateWeight(const BaseEntity& entity);
    bool passesQualityFilter(const BaseEntity& entity);
    std::string extractMetadata(const BaseEntity& entity);
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
