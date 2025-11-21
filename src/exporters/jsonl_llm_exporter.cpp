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

} // namespace themis::exporters
