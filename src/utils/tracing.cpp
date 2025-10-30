#include "utils/tracing.h"
#include "utils/logger.h"

#include <regex>
#include <string>
#include <utility>

// Ensure correct WinSock include order on Windows before including Boost.Asio
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#endif

#include <boost/asio.hpp>

#ifdef THEMIS_ENABLE_TRACING
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/sdk/resource/resource.h>
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
        // Probe collector reachability first to avoid noisy exporter errors
        auto parse_host_port = [](const std::string& url) -> std::pair<std::string, uint16_t> {
            std::regex re(R"((?:http|https)://([^/:]+)(?::(\d+))?)", std::regex::icase);
            std::smatch m;
            if (std::regex_search(url, m, re)) {
                std::string host = m[1].str();
                uint16_t port = 4318; // default OTLP HTTP
                if (m.size() > 2 && m[2].matched) {
                    port = static_cast<uint16_t>(std::stoi(m[2].str()));
                }
                return {host, port};
            }
            return {url, static_cast<uint16_t>(4318)};
        };

        auto [host, port] = parse_host_port(endpoint);
        try {
            namespace net = boost::asio;
            using tcp = net::ip::tcp;
            net::io_context io;
            tcp::resolver resolver(io);
            boost::system::error_code ec;
            auto results = resolver.resolve(host, std::to_string(port), ec);
            if (ec) {
                THEMIS_WARN("Tracing collector resolve failed ({}:{}): {}. Tracing disabled.", host, port, ec.message());
                initialized_ = true; // mark to avoid repeat warnings
                return false;
            }
            tcp::socket socket(io);
            socket.connect(*results.begin(), ec);
            if (ec) {
                THEMIS_WARN("Tracing collector unreachable ({}:{}): {}. Tracing disabled.", host, port, ec.message());
                initialized_ = true;
                return false;
            }
        } catch (const std::exception& e) {
            THEMIS_WARN("Tracing probe failed: {}. Tracing disabled.", e.what());
            initialized_ = true;
            return false;
        }

        // Create OTLP HTTP exporter
        otel_exporter::OtlpHttpExporterOptions opts;
        opts.url = endpoint + "/v1/traces"; // OTLP HTTP traces endpoint
        
        auto exporter = otel_exporter::OtlpHttpExporterFactory::Create(opts);
        
        // Create simple span processor
        auto processor = otel_sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
        
        // Create resource with service name
        auto resource_attributes = otel_resource::ResourceAttributes{
            {"service.name", serviceName},
            {"service.version", "0.1.0"}
        };
        auto resource = otel_resource::Resource::Create(resource_attributes);
        
        // Create tracer provider
        std::shared_ptr<otel_sdk::trace::TracerProvider> provider = 
            otel_sdk::trace::TracerProviderFactory::Create(std::move(processor), resource);
        
        // Set as global provider (convert to nostd::shared_ptr)
        otel::trace::Provider::SetTracerProvider(
            otel::nostd::shared_ptr<otel::trace::TracerProvider>(provider));
        
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
    // Note: nostd::shared_ptr doesn't have reset(), just assign nullptr
    tracer_ = nullptr;
    THEMIS_INFO("OpenTelemetry tracer shut down");
#endif
}

#ifdef THEMIS_ENABLE_TRACING
otel::nostd::shared_ptr<otel::trace::Tracer> Tracer::getTracer() {
    if (!initialized_ || tracer_ == nullptr) {
        THEMIS_WARN("Tracer not initialized, call Tracer::initialize() first");
        // Return empty shared_ptr (OTEL doesn't accept nullptr in return)
        return otel::nostd::shared_ptr<otel::trace::Tracer>();
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
