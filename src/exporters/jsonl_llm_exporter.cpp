#include "exporters/jsonl_llm_exporter.h"
#include "utils/logger.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace themis::exporters {

std::string ExportStats::toJson() const {
    json j;
    j["total_entities"] = total_entities;
    j["exported_entities"] = exported_entities;
    j["failed_entities"] = failed_entities;
    j["bytes_written"] = bytes_written;
    j["duration_ms"] = duration.count();
    j["errors"] = errors;
    return j.dump(2);
}

JSONLLLMExporter::JSONLLLMExporter(const JSONLLLMConfig& config)
    : config_(config) {}

ExportStats JSONLLLMExporter::exportEntities(
    const std::vector<BaseEntity>& entities,
    const ExportOptions& options
) {
    ExportStats stats;
    auto start_time = std::chrono::steady_clock::now();
    
    std::ofstream output(options.output_path);
    if (!output.is_open()) {
        stats.errors.push_back("Failed to open output file: " + options.output_path);
        return stats;
    }
    
    std::set<std::string> seen_hashes;  // For duplicate detection
    
    for (const auto& entity : entities) {
        stats.total_entities++;
        
        try {
            // Quality filtering
            if (!passesQualityFilter(entity)) {
                continue;
            }
            
            // Calculate weight
            double weight = calculateWeight(entity);
            
            // Format based on style
            std::string line;
            switch (config_.style) {
                case JSONLFormat::Style::INSTRUCTION_TUNING:
                    line = formatInstructionTuning(entity, weight);
                    break;
                case JSONLFormat::Style::CHAT_COMPLETION:
                    line = formatChatCompletion(entity, weight);
                    break;
                case JSONLFormat::Style::TEXT_COMPLETION:
                    line = formatTextCompletion(entity, weight);
                    break;
                default:
                    line = formatInstructionTuning(entity, weight);
            }
            
            if (line.empty()) {
                continue;
            }
            
            // Schema validation (LoRax/Outlines integration)
            if (config_.structured_gen.enable_schema_validation) {
                std::string validation_error;
                if (!validateAgainstSchema(line, &validation_error)) {
                    if (config_.structured_gen.reject_invalid_samples) {
                        stats.failed_entities++;
                        stats.errors.push_back(
                            "Schema validation failed for " + entity.getPrimaryKey() + 
                            ": " + validation_error
                        );
                        continue;  // Skip this sample
                    }
                    // Otherwise, log but continue
                    THEMIS_WARN("Schema validation warning for {}: {}", 
                               entity.getPrimaryKey(), validation_error);
                }
                
                // Add schema to output if requested (for Outlines)
                if (config_.structured_gen.include_schema_in_output) {
                    try {
                        auto j = json::parse(line);
                        j["__schema__"] = json::parse(config_.structured_gen.json_schema);
                        line = j.dump();
                    } catch (const std::exception& e) {
                        THEMIS_WARN("Failed to add schema to output: {}", e.what());
                    }
                }
            }
            
            // Track quality metrics
            if (config_.quality_metrics.enable_metrics) {
                // Track length distribution
                if (config_.quality_metrics.track_length_distribution) {
                    constexpr size_t BUCKET_SIZE = 100;  // 100-char buckets
                    size_t bucket = (line.size() / BUCKET_SIZE) * BUCKET_SIZE;
                    runtime_metrics_.length_distribution[bucket]++;
                }
            }
            
            // Duplicate detection
            if (config_.quality.skip_duplicates) {
                std::hash<std::string> hasher;
                auto hash = std::to_string(hasher(line));
                if (seen_hashes.count(hash)) {
                    continue;
                }
                seen_hashes.insert(hash);
            }
            
            // Write line
            output << line << "\n";
            stats.bytes_written += line.size() + 1;
            stats.exported_entities++;
            
            // Progress reporting
            if (options.progress_callback && 
                stats.exported_entities % options.progress_interval == 0) {
                options.progress_callback(stats);
            }
            
        } catch (const std::exception& e) {
            stats.failed_entities++;
            stats.errors.push_back(
                "Entity " + entity.getPrimaryKey() + ": " + e.what()
            );
            
            if (stats.errors.size() >= options.max_errors) {
                THEMIS_ERROR("Max errors reached, stopping export");
                break;
            }
            
            if (!options.continue_on_error) {
                throw;
            }
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    stats.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time
    );
    
    output.close();
    
    THEMIS_INFO("JSONL export completed: {} entities in {}ms",
                stats.exported_entities, stats.duration.count());
    
    return stats;
}

std::string JSONLLLMExporter::formatInstructionTuning(
    const BaseEntity& entity,
    double& weight
) {
    json j;
    
    auto& mapping = config_.field_mapping;
    
    // Required fields
    auto instruction = entity.getFieldAsString(mapping.instruction_field);
    auto output = entity.getFieldAsString(mapping.output_field);
    
    if (!instruction || !output) {
        return "";  // Skip if missing required fields
    }
    
    j["instruction"] = *instruction;
    j["output"] = *output;
    
    // Optional input field
    auto input = entity.getFieldAsString(mapping.input_field);
    if (input && !input->empty()) {
        j["input"] = *input;
    }
    
    // Add weight
    if (config_.weighting.enable_weights) {
        j["weight"] = weight;
    }
    
    // Add metadata
    if (config_.include_metadata) {
        auto metadata_str = extractMetadata(entity);
        if (!metadata_str.empty()) {
            j["metadata"] = json::parse(metadata_str);
        }
    }
    
    return j.dump();
}

std::string JSONLLLMExporter::formatChatCompletion(
    const BaseEntity& entity,
    double& weight
) {
    json j;
    auto& mapping = config_.field_mapping;
    
    json messages = json::array();
    
    // System message (optional)
    auto system = entity.getFieldAsString(mapping.system_field);
    if (system && !system->empty()) {
        messages.push_back({
            {"role", "system"},
            {"content", *system}
        });
    }
    
    // User message (required)
    auto user = entity.getFieldAsString(mapping.user_field);
    if (!user) {
        return "";
    }
    
    messages.push_back({
        {"role", "user"},
        {"content", *user}
    });
    
    // Assistant response (required)
    auto assistant = entity.getFieldAsString(mapping.assistant_field);
    if (!assistant) {
        return "";
    }
    
    messages.push_back({
        {"role", "assistant"},
        {"content", *assistant}
    });
    
    j["messages"] = messages;
    
    // Add weight
    if (config_.weighting.enable_weights) {
        j["weight"] = weight;
    }
    
    // Add metadata
    if (config_.include_metadata) {
        auto metadata_str = extractMetadata(entity);
        if (!metadata_str.empty()) {
            j["metadata"] = json::parse(metadata_str);
        }
    }
    
    return j.dump();
}

std::string JSONLLLMExporter::formatTextCompletion(
    const BaseEntity& entity,
    double& weight
) {
    json j;
    auto& mapping = config_.field_mapping;
    
    auto text = entity.getFieldAsString(mapping.text_field);
    if (!text) {
        return "";
    }
    
    j["text"] = *text;
    
    // Add weight
    if (config_.weighting.enable_weights) {
        j["weight"] = weight;
    }
    
    // Add metadata
    if (config_.include_metadata) {
        auto metadata_str = extractMetadata(entity);
        if (!metadata_str.empty()) {
            j["metadata"] = json::parse(metadata_str);
        }
    }
    
    return j.dump();
}

double JSONLLLMExporter::calculateWeight(const BaseEntity& entity) {
    auto& weight_cfg = config_.weighting;
    
    // Check for explicit weight field
    if (entity.hasField(weight_cfg.weight_field)) {
        auto weight = entity.getFieldAsDouble(weight_cfg.weight_field);
        if (weight) {
            return std::clamp(*weight, 0.0, 1.0);
        }
    }
    
    double calculated_weight = weight_cfg.default_weight;
    
    // Auto-weight by length
    if (weight_cfg.auto_weight_by_length) {
        auto output = entity.getFieldAsString(config_.field_mapping.output_field);
        if (output) {
            size_t length = output->size();
            // Longer responses get slightly higher weight (up to 1.5x)
            calculated_weight *= (1.0 + std::min(0.5, length / 2000.0));
        }
    }
    
    // Auto-weight by freshness
    if (weight_cfg.auto_weight_by_freshness) {
        auto timestamp_str = entity.getFieldAsString(weight_cfg.timestamp_field);
        if (timestamp_str) {
            // TODO: Parse timestamp and calculate freshness factor
            // Newer data gets higher weight
        }
    }
    
    return std::clamp(calculated_weight, 0.0, 2.0);
}

bool JSONLLLMExporter::passesQualityFilter(const BaseEntity& entity) {
    auto& quality = config_.quality;
    
    // Get output field based on style
    std::string output_field;
    switch (config_.style) {
        case JSONLFormat::Style::INSTRUCTION_TUNING:
            output_field = config_.field_mapping.output_field;
            break;
        case JSONLFormat::Style::CHAT_COMPLETION:
            output_field = config_.field_mapping.assistant_field;
            break;
        case JSONLFormat::Style::TEXT_COMPLETION:
            output_field = config_.field_mapping.text_field;
            break;
        default:
            output_field = config_.field_mapping.output_field;
    }
    
    auto output = entity.getFieldAsString(output_field);
    
    // Skip empty outputs
    if (quality.skip_empty_outputs && (!output || output->empty())) {
        return false;
    }
    
    // Check length constraints
    if (output) {
        size_t length = output->size();
        if (length < quality.min_text_length || length > quality.max_text_length) {
            return false;
        }
    }
    
    return true;
}

std::string JSONLLLMExporter::extractMetadata(const BaseEntity& entity) {
    json metadata;
    
    for (const auto& field_name : config_.metadata_fields) {
        if (entity.hasField(field_name)) {
            auto value = entity.getFieldAsString(field_name);
            if (value) {
                metadata[field_name] = *value;
            }
        }
    }
    
    if (metadata.empty()) {
        return "";
    }
    
    return metadata.dump();
}

// ============================================================================
// LoRax/Outlines Integration - Schema Validation
// ============================================================================

bool JSONLLLMExporter::validateAgainstSchema(const std::string& json_str, std::string* error) const {
    if (!config_.structured_gen.enable_schema_validation) {
        return true;  // Validation disabled
    }
    
    if (config_.structured_gen.json_schema.empty()) {
        if (error) *error = "Schema validation enabled but no schema provided";
        return false;
    }
    
    return validateJsonSchema(json_str, config_.structured_gen.json_schema, error);
}

bool JSONLLLMExporter::validateJsonSchema(
    const std::string& json_str,
    const std::string& schema,
    std::string* error
) const {
    try {
        auto data = json::parse(json_str);
        auto schema_json = json::parse(schema);
        
        // Basic JSON schema validation
        // For production, integrate nlohmann/json-schema-validator or similar
        // This is a simplified version for demonstration
        
        runtime_metrics_.total_validated++;
        
        if (schema_json.contains("required")) {
            for (const auto& required_field : schema_json["required"]) {
                std::string field = required_field.get<std::string>();
                if (!data.contains(field)) {
                    runtime_metrics_.schema_violations++;
                    std::string err_msg = "Missing required field: " + field;
                    if (error) {
                        *error = err_msg;
                    }
                    if (config_.structured_gen.log_validation_errors) {
                        runtime_metrics_.validation_errors.push_back(err_msg);
                    }
                    return false;
                }
            }
        }
        
        runtime_metrics_.schema_compliant++;
        return true;
        
    } catch (const std::exception& e) {
        runtime_metrics_.schema_violations++;
        if (error) {
            *error = std::string("Schema validation error: ") + e.what();
        }
        if (config_.structured_gen.log_validation_errors) {
            runtime_metrics_.validation_errors.push_back(*error);
        }
        return false;
    }
}

// ============================================================================
// LoRA Adapter Metadata (LoRAExchange.ai compatibility)
// ============================================================================

std::string JSONLLLMExporter::getAdapterMetadataJson() const {
    if (!config_.adapter_metadata.enable_tracking) {
        return "{}";
    }
    
    auto& meta = config_.adapter_metadata;
    json j;
    
    // Core metadata
    j["adapter_id"] = meta.adapter_id;
    j["adapter_version"] = meta.adapter_version;
    j["base_model"] = {
        {"name", meta.base_model_name},
        {"version", meta.base_model_version}
    };
    
    // Task specification
    j["task"] = {
        {"type", meta.task_type},
        {"domain", meta.domain},
        {"language", meta.language}
    };
    
    // Training configuration
    auto& train = meta.training_config;
    j["training"] = {
        {"dataset_name", train.dataset_name},
        {"num_samples", train.num_samples},
        {"epochs", train.epochs},
        {"learning_rate", train.learning_rate},
        {"lora_rank", train.lora_rank},
        {"lora_alpha", train.lora_alpha},
        {"lora_dropout", train.lora_dropout},
        {"target_modules", train.target_modules}
    };
    
    // Provenance
    j["provenance"] = {
        {"created_by", meta.created_by},
        {"data_source_uri", meta.data_source_uri},
        {"parent_adapter_id", meta.parent_adapter_id}
    };
    
    // Custom metadata
    if (!meta.custom_metadata.empty()) {
        j["custom"] = meta.custom_metadata;
    }
    
    // vLLM-specific configuration
    if (meta.vllm_config.enabled) {
        j["vllm"] = {
            {"enabled", true},
            {"adapter_path", meta.vllm_config.adapter_path},
            {"vllm_version", meta.vllm_config.vllm_version},
            {"max_lora_rank", meta.vllm_config.max_lora_rank},
            {"enable_multi_lora", meta.vllm_config.enable_multi_lora}
        };
        
        if (!meta.vllm_config.serving_config.empty()) {
            j["vllm"]["serving_config"] = meta.vllm_config.serving_config;
        }
    }
    
    // Add export timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t));
    j["exported_at"] = buf;
    
    return j.dump(2);
}

bool JSONLLLMExporter::setAdapterMetadataFromJson(const std::string& json_str, std::string* error) {
    try {
        auto j = json::parse(json_str);
        auto& meta = config_.adapter_metadata;
        
        meta.enable_tracking = true;
        
        if (j.contains("adapter_id")) meta.adapter_id = j["adapter_id"];
        if (j.contains("adapter_version")) meta.adapter_version = j["adapter_version"];
        
        if (j.contains("base_model")) {
            if (j["base_model"].contains("name")) 
                meta.base_model_name = j["base_model"]["name"];
            if (j["base_model"].contains("version")) 
                meta.base_model_version = j["base_model"]["version"];
        }
        
        if (j.contains("task")) {
            if (j["task"].contains("type")) meta.task_type = j["task"]["type"];
            if (j["task"].contains("domain")) meta.domain = j["task"]["domain"];
            if (j["task"].contains("language")) meta.language = j["task"]["language"];
        }
        
        if (j.contains("training")) {
            auto& train_json = j["training"];
            auto& train = meta.training_config;
            
            if (train_json.contains("dataset_name")) 
                train.dataset_name = train_json["dataset_name"];
            if (train_json.contains("num_samples")) 
                train.num_samples = train_json["num_samples"];
            if (train_json.contains("epochs")) 
                train.epochs = train_json["epochs"];
            if (train_json.contains("learning_rate")) 
                train.learning_rate = train_json["learning_rate"];
            if (train_json.contains("lora_rank")) 
                train.lora_rank = train_json["lora_rank"];
            if (train_json.contains("lora_alpha")) 
                train.lora_alpha = train_json["lora_alpha"];
            if (train_json.contains("lora_dropout")) 
                train.lora_dropout = train_json["lora_dropout"];
            if (train_json.contains("target_modules")) 
                train.target_modules = train_json["target_modules"].get<std::vector<std::string>>();
        }
        
        if (j.contains("provenance")) {
            auto& prov = j["provenance"];
            if (prov.contains("created_by")) meta.created_by = prov["created_by"];
            if (prov.contains("data_source_uri")) meta.data_source_uri = prov["data_source_uri"];
            if (prov.contains("parent_adapter_id")) meta.parent_adapter_id = prov["parent_adapter_id"];
        }
        
        if (j.contains("custom")) {
            meta.custom_metadata = j["custom"].get<std::map<std::string, std::string>>();
        }
        
        // vLLM configuration
        if (j.contains("vllm")) {
            auto& vllm_json = j["vllm"];
            auto& vllm = meta.vllm_config;
            
            if (vllm_json.contains("enabled")) vllm.enabled = vllm_json["enabled"];
            if (vllm_json.contains("adapter_path")) vllm.adapter_path = vllm_json["adapter_path"];
            if (vllm_json.contains("vllm_version")) vllm.vllm_version = vllm_json["vllm_version"];
            if (vllm_json.contains("max_lora_rank")) vllm.max_lora_rank = vllm_json["max_lora_rank"];
            if (vllm_json.contains("enable_multi_lora")) vllm.enable_multi_lora = vllm_json["enable_multi_lora"];
            if (vllm_json.contains("serving_config")) {
                vllm.serving_config = vllm_json["serving_config"].get<std::map<std::string, std::string>>();
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        if (error) {
            *error = std::string("Failed to parse adapter metadata: ") + e.what();
        }
        return false;
    }
}

// ============================================================================
// Quality Metrics Reporting
// ============================================================================

std::string JSONLLLMExporter::getQualityMetricsReport() const {
    if (!config_.quality_metrics.enable_metrics) {
        return "{}";
    }
    
    json j;
    
    // Schema compliance metrics
    if (config_.quality_metrics.track_schema_compliance && 
        config_.structured_gen.enable_schema_validation) {
        j["schema_validation"] = {
            {"total_validated", runtime_metrics_.total_validated},
            {"compliant", runtime_metrics_.schema_compliant},
            {"violations", runtime_metrics_.schema_violations},
            {"compliance_rate", runtime_metrics_.total_validated > 0 
                ? (double)runtime_metrics_.schema_compliant / runtime_metrics_.total_validated 
                : 0.0}
        };
        
        if (!runtime_metrics_.validation_errors.empty() && 
            config_.structured_gen.log_validation_errors) {
            // Show only last 10 errors to avoid huge reports
            size_t start = runtime_metrics_.validation_errors.size() > 10 
                ? runtime_metrics_.validation_errors.size() - 10 
                : 0;
            j["schema_validation"]["recent_errors"] = json::array();
            for (size_t i = start; i < runtime_metrics_.validation_errors.size(); ++i) {
                j["schema_validation"]["recent_errors"].push_back(
                    runtime_metrics_.validation_errors[i]
                );
            }
        }
    }
    
    // Length distribution
    if (config_.quality_metrics.track_length_distribution) {
        j["length_distribution"] = runtime_metrics_.length_distribution;
    }
    
    // Diversity score
    if (config_.quality_metrics.track_diversity_score) {
        j["diversity_score"] = runtime_metrics_.diversity_score;
    }
    
    return j.dump(2);
}

} // namespace themis::exporters
