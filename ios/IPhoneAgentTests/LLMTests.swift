import XCTest
@testable import IPhoneAgent

/// An in-memory endpoint: records request bodies and answers with `respond`.
final class StubProtocol: URLProtocol {
    nonisolated(unsafe) static var respond: ([String: Any]) -> (Int, String, String) = { _ in (200, "application/json", "{}") }
    nonisolated(unsafe) static var seen: [[String: Any]] = []

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        var body = request.httpBody ?? Data()
        if body.isEmpty, let stream = request.httpBodyStream {
            stream.open()
            var buf = [UInt8](repeating: 0, count: 65536)
            while stream.hasBytesAvailable {
                let n = stream.read(&buf, maxLength: buf.count)
                if n <= 0 { break }
                body.append(buf, count: n)
            }
            stream.close()
        }
        let json = (try? JSONSerialization.jsonObject(with: body)) as? [String: Any] ?? [:]
        Self.seen.append(json)
        let (status, type, text) = Self.respond(json)
        let response = HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: nil,
                                       headerFields: ["Content-Type": type])!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Data(text.utf8))
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}
}

/// Port of server/tests/test_llm.py, plus streaming.
@MainActor final class LLMTests: XCTestCase {
    func client(_ model: String, extra: [String: Any] = [:], tokenParam: String = "auto", temperatureMode: TemperatureMode = .automatic,
                temperatureValue: Double = 0) -> LLMClient {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [StubProtocol.self]
        return LLMClient(baseURL: "http://test/v1/", model: model, extra: extra, tokenParam: tokenParam, temperatureMode: temperatureMode,
                         temperatureValue: temperatureValue, session: URLSession(configuration: config))
    }

    /// Only accept the `wants` token parameter, like servers that reject the other one.
    func fakeServer(wants: String) {
        StubProtocol.seen = []
        StubProtocol.respond = { body in
            guard body[wants] != nil else {
                return (400, "application/json", "{\"error\": {\"message\": \"Unsupported parameter. Use '\(wants)' instead.\"}}")
            }
            return (200, "application/json", "{\"choices\": [{\"message\": {\"content\": \"DO: wait\"}}]}")
        }
    }

    func testClassicServersGetMaxTokensOnce() async throws {
        let llm = client("qwen")
        fakeServer(wants: "max_tokens")
        let reply = try await llm.chat(system: "sys", text: "hi", image: nil, maxTokens: 600, temperature: 0)
        XCTAssertEqual(reply.text, "DO: wait")
        XCTAssertEqual(StubProtocol.seen.count, 1)
        XCTAssertEqual(StubProtocol.seen[0]["max_tokens"] as? Int, 600)
        XCTAssertEqual(StubProtocol.seen[0]["stream"] as? Bool, true)
    }

    func testAutoSwitchesToMaxCompletionTokensAndRemembers() async throws {
        let llm = client("gpt")
        fakeServer(wants: "max_completion_tokens")
        _ = try await llm.chat(system: "sys", text: "hi", image: nil, maxTokens: 600, temperature: 0)
        _ = try await llm.chat(system: "sys", text: "again", image: nil, maxTokens: 600, temperature: 0)
        let flags = StubProtocol.seen.map { [$0["max_tokens"] != nil, $0["max_completion_tokens"] != nil] }
        XCTAssertEqual(flags, [[true, false], [false, true], [false, true]])
    }

    func testCorrectsTokenAndTemperatureParametersAndRemembers() async throws {
        StubProtocol.seen = []
        StubProtocol.respond = { body in
            if body["max_completion_tokens"] == nil {
                return (400, "application/json", #"{"error":{"message":"Use max_completion_tokens"}}"#)
            }
            if body["temperature"] != nil {
                return (400, "application/json", #"{"error":{"message":"Only the default is supported","param":"temperature","code":"unsupported_value"}}"#)
            }
            return (200, "application/json", #"{"choices":[{"message":{"content":"OK"}}]}"#)
        }
        let llm = client("test-model")
        let reply = try await llm.chat(system:"sys", text:"ping", image:nil, maxTokens:600, temperature:0)
        XCTAssertEqual(reply.text, "OK")
        _ = try await llm.chat(system:"sys", text:"again", image:nil, maxTokens:600, temperature:0)
        XCTAssertEqual(StubProtocol.seen.count, 4)
        XCTAssertNil(StubProtocol.seen[3]["temperature"])
        XCTAssertNotNil(StubProtocol.seen[3]["max_completion_tokens"])
    }

    func testDoesNotRetryUnrelatedBadRequest() async {
        StubProtocol.seen = []
        StubProtocol.respond = { _ in
            (400, "application/json", #"{"error":{"message":"Unsupported value","param":"model","code":"unsupported_value"}}"#)
        }
        do {
            _ = try await client("test-model").chat(system:"sys", text:"ping", image:nil, maxTokens:600, temperature:0)
            XCTFail("expected error")
        } catch { XCTAssertEqual(StubProtocol.seen.count, 1) }
    }

    func testTemperatureOmitAndCustomOverrideExtraBody() async throws {
        fakeServer(wants:"max_tokens")
        _ = try await client("test", extra:["temperature":0.7], temperatureMode:.omit)
            .chat(system:"sys", text:"hi", image:nil, maxTokens:600, temperature:0)
        XCTAssertNil(StubProtocol.seen[0]["temperature"])
        fakeServer(wants:"max_tokens")
        _ = try await client("test", extra:["temperature":0.7], temperatureMode:.custom, temperatureValue:1.25)
            .chat(system:"sys", text:"hi", image:nil, maxTokens:600, temperature:0)
        XCTAssertEqual(StubProtocol.seen[0]["temperature"] as? Double, 1.25)
    }

    func testCustomTemperatureIsNeverSilentlyChanged() async {
        StubProtocol.seen = []
        StubProtocol.respond = { _ in
            (400, "application/json", #"{"error":{"param":"temperature","code":"unsupported_value"}}"#)
        }
        do {
            _ = try await client("test", temperatureMode:.custom, temperatureValue:0.3)
                .chat(system:"sys", text:"hi", image:nil, maxTokens:600, temperature:0)
            XCTFail("expected error")
        } catch {
            XCTAssertEqual(StubProtocol.seen.count, 1)
            XCTAssertEqual(StubProtocol.seen[0]["temperature"] as? Double, 0.3)
        }
    }

    func testTemperatureSettingsPersist() {
        let name = UUID().uuidString
        let defaults = UserDefaults(suiteName:name)!
        defer { defaults.removePersistentDomain(forName:name) }
        let settings = Settings(defaults:defaults, info:nil)
        XCTAssertEqual(settings.temperatureMode, .automatic)
        settings.temperatureMode = .custom
        settings.temperatureValue = 0.25
        let restored = Settings(defaults:defaults, info:nil)
        XCTAssertEqual(restored.temperatureMode, .custom)
        XCTAssertEqual(restored.temperatureValue, 0.25)
    }

    func testExplicitTokenParamIsUsedDirectly() async throws {
        let llm = client("gpt", tokenParam: "max_completion_tokens")
        fakeServer(wants: "max_completion_tokens")
        _ = try await llm.chat(system: "sys", text: "hi", image: nil, maxTokens: 600, temperature: 0)
        XCTAssertEqual(StubProtocol.seen.count, 1)
    }

    func testExtraBodyAndImageAreSent() async throws {
        let llm = client("gpt", extra: ["reasoning_effort": "none"])
        fakeServer(wants: "max_tokens")
        _ = try await llm.chat(system: "sys", text: "hi", image: Data([0xff, 0xd8]) + Data("jpeg".utf8),
                               maxTokens: 600, temperature: 0)
        let body = StubProtocol.seen[0]
        XCTAssertEqual(body["reasoning_effort"] as? String, "none")
        let user = (body["messages"] as? [[String: Any]])?[1]["content"] as? [[String: Any]]
        let url = (user?[0]["image_url"] as? [String: Any])?["url"] as? String
        XCTAssertTrue(url?.hasPrefix("data:image/jpeg;base64,") ?? false)
    }

    func testStreamedReplyIsJoinedAndTimed() async throws {
        let llm = client("qwen")
        StubProtocol.seen = []
        StubProtocol.respond = { _ in
            let events = ["THINK: ", "ok\n", "DO: wait"].map {
                "data: {\"choices\": [{\"delta\": {\"content\": \"\($0.replacingOccurrences(of: "\n", with: "\\n"))\"}}]}\n\n"
            }
            return (200, "text/event-stream", events.joined() + "data: [DONE]\n\n")
        }
        let reply = try await llm.chat(system: "sys", text: "hi", image: nil, maxTokens: 600, temperature: 0)
        XCTAssertEqual(reply.text, "THINK: ok\nDO: wait")
        XCTAssertNotNil(reply.firstToken)
    }

    func testEmptyReplyIsAskedOnceMoreWithItsReasonLogged() async throws {
        let llm = client("qwen")
        StubProtocol.seen = []
        StubProtocol.respond = { _ in
            let first = StubProtocol.seen.count == 1
            let delta = first ? "{\"reasoning_content\": \"hmm\"}" : "{\"content\": \"DO: wait\"}"
            let finish = first ? "length" : "stop"
            return (200, "text/event-stream", "data: {\"choices\": [{\"delta\": \(delta)}]}\n\n"
                    + "data: {\"choices\": [{\"delta\": {}, \"finish_reason\": \"\(finish)\"}], \"usage\": {\"completion_tokens\": 9}}\n\n"
                    + "data: [DONE]\n\n")
        }
        let reply = try await llm.chat(system: "sys", text: "hi", image: nil, maxTokens: 600, temperature: 0)
        XCTAssertEqual(StubProtocol.seen.count, 2)
        XCTAssertEqual(reply.text, "DO: wait")
        XCTAssertTrue(reply.retried)
        XCTAssertEqual(StubProtocol.seen[1]["max_tokens"] as? Int, 2400)
        XCTAssertEqual(reply.finishReason, "stop")
        XCTAssertEqual(reply.usage["completion_tokens"], 9)
    }

    func testRepeatedEmptyReasoningIsNotReportedAsSuccess() async {
        StubProtocol.seen = []
        StubProtocol.respond = { _ in
            (200,"application/json",#"{"choices":[{"message":{"content":"","reasoning_content":"thinking"},"finish_reason":"length"}]}"#)
        }
        do {
            _ = try await client("reasoner").chat(system:"sys",text:"hi",image:nil,maxTokens:1000,temperature:0)
            XCTFail("expected empty-content error")
        } catch {
            XCTAssertEqual(StubProtocol.seen.count,2)
            XCTAssertEqual(StubProtocol.seen[1]["max_tokens"] as? Int,4000)
            XCTAssertTrue(error.localizedDescription.contains("reasoning"))
        }
    }

    func testHTTPErrorsAreThrown() async {
        let llm = client("qwen")
        StubProtocol.respond = { _ in (500, "text/plain", "boom") }
        do {
            _ = try await llm.chat(system: "sys", text: "hi", image: nil, maxTokens: 600, temperature: 0)
            XCTFail("expected an error")
        } catch {
            XCTAssertTrue(error.localizedDescription.contains("500"))
        }
    }
}
