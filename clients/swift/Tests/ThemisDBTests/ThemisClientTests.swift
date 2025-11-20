import XCTest
@testable import ThemisDB

final class ThemisClientTests: XCTestCase {
    func testClientCreation() {
        let client = ThemisClient(endpoints: ["http://localhost:8080"])
        XCTAssertNotNil(client)
    }
    
    func testClientCreationWithMultipleEndpoints() {
        let client = ThemisClient(endpoints: [
            "http://localhost:8080",
            "http://localhost:8081"
        ])
        XCTAssertNotNil(client)
    }
    
    func testClientCreationWithTimeout() {
        let client = ThemisClient(endpoints: ["http://localhost:8080"], timeout: 60.0)
        XCTAssertNotNil(client)
    }
    
    func testIsolationLevelValues() {
        XCTAssertEqual(IsolationLevel.readCommitted.rawValue, "READ_COMMITTED")
        XCTAssertEqual(IsolationLevel.snapshot.rawValue, "SNAPSHOT")
    }
    
    func testTransactionOptionsDefaults() {
        let options = TransactionOptions()
        XCTAssertEqual(options.isolationLevel, .readCommitted)
        XCTAssertNil(options.timeout)
    }
    
    func testTransactionOptionsWithIsolationLevel() {
        let options = TransactionOptions(isolationLevel: .snapshot)
        XCTAssertEqual(options.isolationLevel, .snapshot)
        XCTAssertNil(options.timeout)
    }
    
    func testTransactionOptionsWithTimeout() {
        let options = TransactionOptions(timeout: 30.0)
        XCTAssertEqual(options.isolationLevel, .readCommitted)
        XCTAssertEqual(options.timeout, 30.0)
    }
    
    func testThemisErrorDescriptions() {
        XCTAssertNotNil(ThemisError.invalidURL.errorDescription)
        XCTAssertNotNil(ThemisError.invalidResponse.errorDescription)
        XCTAssertNotNil(ThemisError.transactionNotActive.errorDescription)
        XCTAssertNotNil(ThemisError.transactionAlreadyActive.errorDescription)
    }
    
    // Integration tests (require running server)
    func testGetDocument() async throws {
        throw XCTSkip("Requires running ThemisDB server")
        
        // let client = ThemisClient(endpoints: ["http://localhost:8080"])
        // struct TestDoc: Codable {
        //     let name: String
        // }
        // let doc: TestDoc = try await client.get(model: "relational", collection: "test", uuid: "test-id")
        // XCTAssertEqual(doc.name, "test")
    }
    
    func testPutDocument() async throws {
        throw XCTSkip("Requires running ThemisDB server")
        
        // let client = ThemisClient(endpoints: ["http://localhost:8080"])
        // struct TestDoc: Codable {
        //     let name: String
        // }
        // try await client.put(model: "relational", collection: "test", uuid: "test-id", data: TestDoc(name: "test"))
    }
    
    func testTransactionCommit() async throws {
        throw XCTSkip("Requires running ThemisDB server")
        
        // let client = ThemisClient(endpoints: ["http://localhost:8080"])
        // let tx = try await client.beginTransaction()
        // XCTAssertTrue(await tx.isActive())
        // try await tx.commit()
        // XCTAssertFalse(await tx.isActive())
    }
}
