#include "utils/tracing.h"
#include "utils/logger.h"

#ifdef THEMIS_ENABLE_TRACING
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/resource/semantic_conventions.h>
#include <opentelemetry/trace/provider.h>

namespace otel_sdk = opentelemetry::sdk;
namespace otel_trace = opentelemetry::trace;
namespace otel_resource = opentelemetry::sdk::resource;
namespace otel_exporter = opentelemetry::exporter::otlp;
#endif

namespace themis {

#ifdef THEMIS_ENABLE_TRACING
otel::nostd::shared_ptr<otel::trace::Tracer> Tracer::tracer_;
#endif
bool Tracer::initialized_ = false;

bool Tracer::initialize(const std::string& serviceName, const std::string& endpoint) {
#ifdef THEMIS_ENABLE_TRACING
    if (initialized_) {
        THEMIS_WARN("Tracer already initialized");
        return false;
    }
    
    try {
        // Create OTLP HTTP exporter
        otel_exporter::OtlpHttpExporterOptions opts;
        opts.url = endpoint + "/v1/traces"; // OTLP HTTP traces endpoint
        
        auto exporter = otel_exporter::OtlpHttpExporterFactory::Create(opts);
        
        // Create simple span processor
        auto processor = otel_sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
        
        // Create resource with service name
        auto resource_attributes = otel_resource::ResourceAttributes{
            {otel_resource::SemanticConventions::kServiceName, serviceName},
            {otel_resource::SemanticConventions::kServiceVersion, "0.1.0"}
        };
        auto resource = otel_resource::Resource::Create(resource_attributes);
        
        // Create tracer provider
        auto provider = otel_sdk::trace::TracerProviderFactory::Create(std::move(processor), resource);
        
        // Set as global provider
        otel::trace::Provider::SetTracerProvider(provider);
        
        // Get tracer instance
        tracer_ = provider->GetTracer(serviceName, "0.1.0");
        
        initialized_ = true;
        THEMIS_INFO("OpenTelemetry tracer initialized: service={}, endpoint={}", serviceName, endpoint);
        return true;
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to initialize OpenTelemetry tracer: {}", e.what());
        return false;
    }
#else
    (void)serviceName;
    (void)endpoint;
    THEMIS_INFO("Tracing disabled (THEMIS_ENABLE_TRACING not defined)");
    initialized_ = true; // Mark as "initialized" to prevent repeated logs
    return true;
#endif
}

void Tracer::shutdown() {
#ifdef THEMIS_ENABLE_TRACING
    if (!initialized_) {
        return;
    }
    
    // Shutdown will flush remaining spans
    auto provider = otel::trace::Provider::GetTracerProvider();
    if (provider) {
        auto sdk_provider = static_cast<otel_sdk::trace::TracerProvider*>(provider.get());
        if (sdk_provider) {
            sdk_provider->Shutdown();
        }
    }
    
    initialized_ = false;
    tracer_.reset();
    THEMIS_INFO("OpenTelemetry tracer shut down");
#endif
}

#ifdef THEMIS_ENABLE_TRACING
otel::nostd::shared_ptr<otel::trace::Tracer> Tracer::getTracer() {
    if (!initialized_ || !tracer_) {
        THEMIS_WARN("Tracer not initialized, call Tracer::initialize() first");
        return nullptr;
    }
    return tracer_;
}
#endif

Tracer::Span Tracer::startSpan(const std::string& name) {
#ifdef THEMIS_ENABLE_TRACING
    auto tracer = getTracer();
    if (!tracer) {
        return Span(); // Return invalid span
    }
    
    auto span = tracer->StartSpan(name);
    return Span(span);
#else
    (void)name;
    return Span(); // No-op span
#endif
}

Tracer::Span Tracer::startChildSpan(const std::string& name, const Span& parent) {
#ifdef THEMIS_ENABLE_TRACING
    auto tracer = getTracer();
    if (!tracer || !parent.valid_) {
        return Span();
    }
    
    // Start span with parent context
    otel::trace::StartSpanOptions options;
    options.parent = parent.context_;
    
    auto span = tracer->StartSpan(name, options);
    return Span(span);
#else
    (void)name;
    (void)parent;
    return Span();
#endif
}

// Span implementation
#ifdef THEMIS_ENABLE_TRACING
Tracer::Span::Span(otel::nostd::shared_ptr<otel::trace::Span> span)
    : span_(span), valid_(span != nullptr), ended_(false) {
    if (span_) {
        context_ = otel::context::RuntimeContext::GetCurrent().SetValue(
            otel::trace::kSpanKey, span_);
    }
}
#endif

Tracer::Span::~Span() {
    if (valid_ && !ended_) {
        end();
    }
}

Tracer::Span::Span(Span&& other) noexcept
    : valid_(other.valid_), ended_(other.ended_) {
#ifdef THEMIS_ENABLE_TRACING
    span_ = std::move(other.span_);
    context_ = std::move(other.context_);
#endif
    other.valid_ = false;
}

Tracer::Span& Tracer::Span::operator=(Span&& other) noexcept {
    if (this != &other) {
        if (valid_ && !ended_) {
            end();
        }
        
        valid_ = other.valid_;
        ended_ = other.ended_;
#ifdef THEMIS_ENABLE_TRACING
        span_ = std::move(other.span_);
        context_ = std::move(other.context_);
#endif
        other.valid_ = false;
    }
    return *this;
}

void Tracer::Span::setAttribute(const std::string& key, const std::string& value) {
#ifdef THEMIS_ENABLE_TRACING
    if (span_) {
        span_->SetAttribute(key, value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void Tracer::Span::setAttribute(const std::string& key, int64_t value) {
#ifdef THEMIS_ENABLE_TRACING
    if (span_) {
        span_->SetAttribute(key, value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void Tracer::Span::setAttribute(const std::string& key, double value) {
#ifdef THEMIS_ENABLE_TRACING
    if (span_) {
        span_->SetAttribute(key, value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void Tracer::Span::setAttribute(const std::string& key, bool value) {
#ifdef THEMIS_ENABLE_TRACING
    if (span_) {
        span_->SetAttribute(key, value);
    }
#else
    (void)key;
    (void)value;
#endif
}

void Tracer::Span::recordError(const std::string& errorMessage) {
#ifdef THEMIS_ENABLE_TRACING
    if (span_) {
        span_->AddEvent("exception", {{"exception.message", errorMessage}});
        span_->SetStatus(otel::trace::StatusCode::kError, errorMessage);
    }
#else
    (void)errorMessage;
#endif
}

void Tracer::Span::setStatus(bool ok, const std::string& description) {
#ifdef THEMIS_ENABLE_TRACING
    if (span_) {
        auto status_code = ok ? otel::trace::StatusCode::kOk : otel::trace::StatusCode::kError;
        span_->SetStatus(status_code, description);
    }
#else
    (void)ok;
    (void)description;
#endif
}

void Tracer::Span::end() {
#ifdef THEMIS_ENABLE_TRACING
    if (span_ && !ended_) {
        span_->End();
        ended_ = true;
    }
#endif
    ended_ = true;
}

} // namespace themis
