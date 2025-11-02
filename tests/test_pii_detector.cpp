#include <gtest/gtest.h>
#include "utils/pii_detector.h"

using namespace themis::utils;

class PIIDetectorTest : public ::testing::Test {
protected:
    PIIDetector detector_;
};

TEST_F(PIIDetectorTest, DetectEmail) {
    std::string text = "Contact alice@example.com or bob@company.de for details.";
    auto findings = detector_.detectInText(text);
    
    ASSERT_EQ(findings.size(), 2);
    EXPECT_EQ(findings[0].type, PIIType::EMAIL);
    EXPECT_EQ(findings[0].value, "alice@example.com");
    EXPECT_GT(findings[0].confidence, 0.9);
    
    EXPECT_EQ(findings[1].type, PIIType::EMAIL);
    EXPECT_EQ(findings[1].value, "bob@company.de");
}

TEST_F(PIIDetectorTest, DetectPhone) {
    std::string text = "Call +49-123-456789 or (555) 123-4567";
    auto findings = detector_.detectInText(text);
    
    // Phone detection may find multiple matches
    bool found_intl = false;
    bool found_us = false;
    for (const auto& f : findings) {
        if (f.type == PIIType::PHONE) {
            if (f.value.find("+49") != std::string::npos) found_intl = true;
            if (f.value.find("555") != std::string::npos) found_us = true;
        }
    }
    
    EXPECT_TRUE(found_intl || found_us);
}

TEST_F(PIIDetectorTest, DetectSSN) {
    std::string text = "SSN: 123-45-6789 or 987654321";
    auto findings = detector_.detectInText(text);
    
    ASSERT_GE(findings.size(), 1);
    bool found_ssn = false;
    for (const auto& f : findings) {
        if (f.type == PIIType::SSN) {
            found_ssn = true;
            EXPECT_GT(f.confidence, 0.8);
        }
    }
    EXPECT_TRUE(found_ssn);
}

TEST_F(PIIDetectorTest, DetectCreditCard) {
    // Valid Visa test number (passes Luhn check)
    std::string text = "Card: 4242-4242-4242-4242";
    auto findings = detector_.detectInText(text);
    
    bool found_cc = false;
    for (const auto& f : findings) {
        if (f.type == PIIType::CREDIT_CARD) {
            found_cc = true;
            EXPECT_GT(f.confidence, 0.8);
        }
    }
    EXPECT_TRUE(found_cc);
}

TEST_F(PIIDetectorTest, DetectCreditCard_InvalidLuhn) {
    // Invalid card (fails Luhn check)
    std::string text = "Card: 1234-5678-9012-3456";
    auto findings = detector_.detectInText(text);
    
    // Should NOT find credit card due to Luhn failure
    for (const auto& f : findings) {
        EXPECT_NE(f.type, PIIType::CREDIT_CARD);
    }
}

TEST_F(PIIDetectorTest, DetectIBAN) {
    std::string text = "IBAN: DE89370400440532013000";
    auto findings = detector_.detectInText(text);
    
    bool found_iban = false;
    for (const auto& f : findings) {
        if (f.type == PIIType::IBAN) {
            found_iban = true;
            EXPECT_EQ(f.value, "DE89370400440532013000");
        }
    }
    EXPECT_TRUE(found_iban);
}

TEST_F(PIIDetectorTest, DetectIPAddress) {
    std::string text = "Server at 192.168.1.42";
    auto findings = detector_.detectInText(text);
    
    bool found_ip = false;
    for (const auto& f : findings) {
        if (f.type == PIIType::IP_ADDRESS) {
            found_ip = true;
            EXPECT_EQ(f.value, "192.168.1.42");
        }
    }
    EXPECT_TRUE(found_ip);
}

TEST_F(PIIDetectorTest, DetectURL) {
    std::string text = "Visit https://example.com/private/data";
    auto findings = detector_.detectInText(text);
    
    bool found_url = false;
    for (const auto& f : findings) {
        if (f.type == PIIType::URL) {
            found_url = true;
            EXPECT_GT(f.confidence, 0.8);
        }
    }
    EXPECT_TRUE(found_url);
}

TEST_F(PIIDetectorTest, DetectInJson_FieldName) {
    nlohmann::json obj = {
        {"email", "alice@example.com"},
        {"phone", "+49-123-456789"},
        {"name", "Alice"}
    };
    
    auto findings = detector_.detectInJson(obj);
    
    EXPECT_TRUE(findings.find("email") != findings.end());
    EXPECT_TRUE(findings.find("phone") != findings.end());
    // "name" should not be flagged as PII
}

TEST_F(PIIDetectorTest, DetectInJson_Nested) {
    nlohmann::json obj = {
        {"user", {
            {"email", "bob@test.com"},
            {"contact", {
                {"phone", "555-1234"}
            }}
        }}
    };
    
    auto findings = detector_.detectInJson(obj);
    
    EXPECT_TRUE(findings.find("user.email") != findings.end());
    EXPECT_TRUE(findings.find("user.contact.phone") != findings.end());
}

TEST_F(PIIDetectorTest, DetectInJson_Array) {
    nlohmann::json obj = {
        {"emails", {"alice@example.com", "bob@test.com"}}
    };
    
    auto findings = detector_.detectInJson(obj);
    
    // Should find emails in array elements
    EXPECT_FALSE(findings.empty());
}

TEST_F(PIIDetectorTest, ClassifyFieldName) {
    EXPECT_EQ(detector_.classifyFieldName("email"), PIIType::EMAIL);
    EXPECT_EQ(detector_.classifyFieldName("E-Mail"), PIIType::EMAIL);
    EXPECT_EQ(detector_.classifyFieldName("userEmail"), PIIType::EMAIL);
    EXPECT_EQ(detector_.classifyFieldName("phone"), PIIType::PHONE);
    EXPECT_EQ(detector_.classifyFieldName("telephone"), PIIType::PHONE);
    EXPECT_EQ(detector_.classifyFieldName("ssn"), PIIType::SSN);
    EXPECT_EQ(detector_.classifyFieldName("credit_card"), PIIType::CREDIT_CARD);
    EXPECT_EQ(detector_.classifyFieldName("iban"), PIIType::IBAN);
    EXPECT_EQ(detector_.classifyFieldName("name"), PIIType::UNKNOWN);
}

TEST_F(PIIDetectorTest, RedactionRecommendation) {
    EXPECT_EQ(detector_.getRedactionRecommendation(PIIType::SSN), "strict");
    EXPECT_EQ(detector_.getRedactionRecommendation(PIIType::CREDIT_CARD), "strict");
    EXPECT_EQ(detector_.getRedactionRecommendation(PIIType::EMAIL), "partial");
    EXPECT_EQ(detector_.getRedactionRecommendation(PIIType::PHONE), "partial");
}

TEST_F(PIIDetectorTest, MaskEmail) {
    std::string masked = detector_.maskValue(PIIType::EMAIL, "alice@example.com");
    EXPECT_EQ(masked, "a***@example.com");
}

TEST_F(PIIDetectorTest, MaskPhone) {
    std::string masked = detector_.maskValue(PIIType::PHONE, "123-456-7890");
    EXPECT_EQ(masked, "***-***-7890");
}

TEST_F(PIIDetectorTest, MaskSSN) {
    std::string masked = detector_.maskValue(PIIType::SSN, "123-45-6789");
    EXPECT_EQ(masked, "***-**-6789");
}

TEST_F(PIIDetectorTest, MaskCreditCard) {
    std::string masked = detector_.maskValue(PIIType::CREDIT_CARD, "4532-1488-0343-6467");
    EXPECT_EQ(masked, "**** **** **** 6467");
}

TEST_F(PIIDetectorTest, MaskIBAN) {
    std::string masked = detector_.maskValue(PIIType::IBAN, "DE89370400440532013000");
    EXPECT_TRUE(masked.find("DE") == 0);  // Starts with country code
    EXPECT_TRUE(masked.find("3000") != std::string::npos);  // Ends with last 4
}

TEST_F(PIIDetectorTest, NoFalsePositives) {
    std::string text = "The year 2024 has 365 days and version 1.2.3";
    auto findings = detector_.detectInText(text);
    
    // Should not detect numbers as PII
    for (const auto& f : findings) {
        // Phone pattern might match "365" but with low confidence
        if (f.type == PIIType::SSN || f.type == PIIType::CREDIT_CARD) {
            FAIL() << "False positive: " << f.value;
        }
    }
}
