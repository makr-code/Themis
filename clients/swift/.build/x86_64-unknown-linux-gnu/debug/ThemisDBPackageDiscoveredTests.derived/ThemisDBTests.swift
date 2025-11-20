import XCTest
@testable import ThemisDBTests

fileprivate extension ThemisClientTests {
    @available(*, deprecated, message: "Not actually deprecated. Marked as deprecated to allow inclusion of deprecated tests (which test deprecated functionality) without warnings")
    static nonisolated(unsafe) let __allTests__ThemisClientTests = [
        ("testClientCreation", testClientCreation),
        ("testClientCreationWithMultipleEndpoints", testClientCreationWithMultipleEndpoints),
        ("testClientCreationWithTimeout", testClientCreationWithTimeout),
        ("testGetDocument", asyncTest(testGetDocument)),
        ("testIsolationLevelValues", testIsolationLevelValues),
        ("testPutDocument", asyncTest(testPutDocument)),
        ("testThemisErrorDescriptions", testThemisErrorDescriptions),
        ("testTransactionCommit", asyncTest(testTransactionCommit)),
        ("testTransactionOptionsDefaults", testTransactionOptionsDefaults),
        ("testTransactionOptionsWithIsolationLevel", testTransactionOptionsWithIsolationLevel),
        ("testTransactionOptionsWithTimeout", testTransactionOptionsWithTimeout)
    ]
}
@available(*, deprecated, message: "Not actually deprecated. Marked as deprecated to allow inclusion of deprecated tests (which test deprecated functionality) without warnings")
func __ThemisDBTests__allTests() -> [XCTestCaseEntry] {
    return [
        testCase(ThemisClientTests.__allTests__ThemisClientTests)
    ]
}