import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

// MARK: - Response Types

/// Generic query response
private struct QueryResponse<Result: Decodable>: Decodable {
    let results: [Result]
}

// MARK: - Public Types

/// Isolation level for transactions
public enum IsolationLevel: String, Sendable {
    case readCommitted = "READ_COMMITTED"
    case snapshot = "SNAPSHOT"
}

/// Options for configuring a transaction
public struct TransactionOptions: Sendable {
    public let isolationLevel: IsolationLevel
    public let timeout: TimeInterval?
    
    public init(isolationLevel: IsolationLevel = .readCommitted, timeout: TimeInterval? = nil) {
        self.isolationLevel = isolationLevel
        self.timeout = timeout
    }
}

/// Errors that can occur when using ThemisDB
public enum ThemisError: Error, LocalizedError {
    case invalidURL
    case invalidResponse
    case httpError(statusCode: Int, message: String)
    case decodingError(Error)
    case encodingError(Error)
    case transactionNotActive
    case transactionAlreadyActive
    case networkError(Error)
    
    public var errorDescription: String? {
        switch self {
        case .invalidURL:
            return "Invalid URL"
        case .invalidResponse:
            return "Invalid response from server"
        case .httpError(let statusCode, let message):
            return "HTTP error \(statusCode): \(message)"
        case .decodingError(let error):
            return "Failed to decode response: \(error.localizedDescription)"
        case .encodingError(let error):
            return "Failed to encode request: \(error.localizedDescription)"
        case .transactionNotActive:
            return "Transaction is not active"
        case .transactionAlreadyActive:
            return "Transaction is already active"
        case .networkError(let error):
            return "Network error: \(error.localizedDescription)"
        }
    }
}

/// Main client for interacting with ThemisDB
public actor ThemisClient {
    private let endpoints: [String]
    private var currentEndpointIndex = 0
    private let session: URLSession
    private let timeout: TimeInterval
    
    public init(endpoints: [String], timeout: TimeInterval = 30.0) {
        self.endpoints = endpoints
        self.timeout = timeout
        
        let configuration = URLSessionConfiguration.default
        configuration.timeoutIntervalForRequest = timeout
        configuration.timeoutIntervalForResource = timeout
        self.session = URLSession(configuration: configuration)
    }
    
    /// Get the current endpoint URL
    private func getCurrentEndpoint() -> String {
        endpoints[currentEndpointIndex % endpoints.count]
    }
    
    /// Perform an HTTP request
    func request<T: Decodable>(
        method: String,
        path: String,
        body: [String: Any]? = nil,
        headers: [String: String]? = nil
    ) async throws -> T {
        guard let url = URL(string: "\(getCurrentEndpoint())\(path)") else {
            throw ThemisError.invalidURL
        }
        
        var request = URLRequest(url: url)
        request.httpMethod = method
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        
        // Add custom headers
        if let headers = headers {
            for (key, value) in headers {
                request.setValue(value, forHTTPHeaderField: key)
            }
        }
        
        // Add body if present
        if let body = body {
            do {
                request.httpBody = try JSONSerialization.data(withJSONObject: body)
            } catch {
                throw ThemisError.encodingError(error)
            }
        }
        
        do {
            let (data, response) = try await session.data(for: request)
            
            guard let httpResponse = response as? HTTPURLResponse else {
                throw ThemisError.invalidResponse
            }
            
            guard (200..<300).contains(httpResponse.statusCode) else {
                let message = String(data: data, encoding: .utf8) ?? "Unknown error"
                throw ThemisError.httpError(statusCode: httpResponse.statusCode, message: message)
            }
            
            do {
                return try JSONDecoder().decode(T.self, from: data)
            } catch {
                throw ThemisError.decodingError(error)
            }
        } catch let error as ThemisError {
            throw error
        } catch {
            throw ThemisError.networkError(error)
        }
    }
    
    /// Get a document by UUID
    public func get<T: Decodable>(model: String, collection: String, uuid: String) async throws -> T {
        return try await request(
            method: "GET",
            path: "/api/\(model)/\(collection)/\(uuid)"
        )
    }
    
    /// Put (create or update) a document
    public func put<T: Encodable>(model: String, collection: String, uuid: String, data: T) async throws {
        let bodyDict: [String: Any]
        do {
            let jsonData = try JSONEncoder().encode(data)
            bodyDict = try JSONSerialization.jsonObject(with: jsonData) as? [String: Any] ?? [:]
        } catch {
            throw ThemisError.encodingError(error)
        }
        
        let _: [String: String] = try await request(
            method: "PUT",
            path: "/api/\(model)/\(collection)/\(uuid)",
            body: bodyDict
        )
    }
    
    /// Delete a document by UUID
    public func delete(model: String, collection: String, uuid: String) async throws {
        let _: [String: String] = try await request(
            method: "DELETE",
            path: "/api/\(model)/\(collection)/\(uuid)"
        )
    }
    
    /// Execute an AQL query
    public func query<T: Decodable>(aql: String) async throws -> [T] {
        let bodyDict = ["query": aql]
        let data = try await requestData(
            method: "POST",
            path: "/api/query",
            body: bodyDict
        )
        
        let response = try JSONDecoder().decode(QueryResponse<T>.self, from: data)
        return response.results
    }
    
    /// Begin a new transaction
    public func beginTransaction(options: TransactionOptions = TransactionOptions()) async throws -> Transaction {
        struct BeginResponse: Codable {
            let transactionId: String
        }
        
        let timeoutMs = options.timeout.map { Int($0 * 1000) }
        var bodyDict: [String: Any] = [
            "isolationLevel": options.isolationLevel.rawValue
        ]
        if let timeout = timeoutMs {
            bodyDict["timeout"] = timeout
        }
        
        let response: BeginResponse = try await request(
            method: "POST",
            path: "/transaction/begin",
            body: bodyDict
        )
        
        return Transaction(client: self, transactionId: response.transactionId)
    }
    
    // MARK: - Private Request Helpers
    
    fileprivate func requestData(
        method: String,
        path: String,
        body: [String: Any]? = nil,
        headers: [String: String]? = nil
    ) async throws -> Data {
        guard let url = URL(string: "\(getCurrentEndpoint())\(path)") else {
            throw ThemisError.invalidURL
        }
        
        var request = URLRequest(url: url)
        request.httpMethod = method
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        
        headers?.forEach { key, value in
            request.setValue(value, forHTTPHeaderField: key)
        }
        
        if let body = body {
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        }
        
        let (data, response) = try await session.data(for: request)
        
        guard let httpResponse = response as? HTTPURLResponse else {
            throw ThemisError.invalidResponse
        }
        
        guard (200..<300).contains(httpResponse.statusCode) else {
            let message = String(data: data, encoding: .utf8) ?? "Unknown error"
            throw ThemisError.httpError(statusCode: httpResponse.statusCode, message: message)
        }
        
        return data
    }
}

/// Represents an active transaction
public actor Transaction {
    private let client: ThemisClient
    private let transactionId: String
    private var active = true
    
    init(client: ThemisClient, transactionId: String) {
        self.client = client
        self.transactionId = transactionId
        self.active = true
    }
    
    /// Get the transaction ID
    public func getTransactionId() -> String {
        transactionId
    }
    
    /// Check if the transaction is active
    public func isActive() -> Bool {
        active
    }
    
    /// Perform a transactional HTTP request (returns raw data)
    private func transactionalRequestData(
        method: String,
        path: String,
        body: [String: Any]? = nil
    ) async throws -> Data {
        guard active else {
            throw ThemisError.transactionNotActive
        }
        
        return try await client.requestData(
            method: method,
            path: path,
            body: body,
            headers: ["X-Transaction-Id": transactionId]
        )
    }
    
    /// Perform a transactional HTTP request
    private func transactionalRequest<T: Decodable>(
        method: String,
        path: String,
        body: [String: Any]? = nil
    ) async throws -> T {
        guard active else {
            throw ThemisError.transactionNotActive
        }
        
        return try await client.request(
            method: method,
            path: path,
            body: body,
            headers: ["X-Transaction-Id": transactionId]
        )
    }
    
    /// Get a document within the transaction
    public func get<T: Decodable>(model: String, collection: String, uuid: String) async throws -> T {
        return try await transactionalRequest(
            method: "GET",
            path: "/api/\(model)/\(collection)/\(uuid)"
        )
    }
    
    /// Put (create or update) a document within the transaction
    public func put<T: Encodable>(model: String, collection: String, uuid: String, data: T) async throws {
        let bodyDict: [String: Any]
        do {
            let jsonData = try JSONEncoder().encode(data)
            bodyDict = try JSONSerialization.jsonObject(with: jsonData) as? [String: Any] ?? [:]
        } catch {
            throw ThemisError.encodingError(error)
        }
        
        let _: [String: String] = try await transactionalRequest(
            method: "PUT",
            path: "/api/\(model)/\(collection)/\(uuid)",
            body: bodyDict
        )
    }
    
    /// Delete a document within the transaction
    public func delete(model: String, collection: String, uuid: String) async throws {
        let _: [String: String] = try await transactionalRequest(
            method: "DELETE",
            path: "/api/\(model)/\(collection)/\(uuid)"
        )
    }
    
    /// Execute an AQL query within the transaction
    public func query<T: Decodable>(aql: String) async throws -> [T] {
        let bodyDict = ["query": aql]
        let data = try await transactionalRequestData(
            method: "POST",
            path: "/api/query",
            body: bodyDict
        )
        
        let response = try JSONDecoder().decode(QueryResponse<T>.self, from: data)
        return response.results
    }
    
    /// Commit the transaction
    public func commit() async throws {
        guard active else {
            throw ThemisError.transactionNotActive
        }
        
        let _: [String: String] = try await client.request(
            method: "POST",
            path: "/transaction/commit",
            body: nil,
            headers: ["X-Transaction-Id": transactionId]
        )
        
        active = false
    }
    
    /// Rollback the transaction
    public func rollback() async throws {
        guard active else {
            return // Already rolled back or committed
        }
        
        let _: [String: String] = try await client.request(
            method: "POST",
            path: "/transaction/rollback",
            body: nil,
            headers: ["X-Transaction-Id": transactionId]
        )
        
        active = false
    }
}
