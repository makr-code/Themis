import { describe, it, expect } from "vitest";
import { ThemisClient } from "../src/index.js";

describe("ThemisClient", () => {
  it("should create a client with valid configuration", () => {
    const client = new ThemisClient({
      endpoints: ["http://localhost:8080"],
      namespace: "default",
    });
    
    expect(client).toBeDefined();
  });

  it("should throw error with empty endpoints", () => {
    expect(() => {
      new ThemisClient({
        endpoints: [],
      });
    }).toThrow("endpoints must not be empty");
  });
});

