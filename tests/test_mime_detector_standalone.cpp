#include <gtest/gtest.h>
#include "content/mime_detector.h"
#include <algorithm>

using namespace themis::content;

class MimeDetectorTest : public ::testing::Test {
protected:
    MimeDetector detector;
};
TEST_F(MimeDetectorTest, IntegrityPlaceholderUnverified) {
    // Bei Platzhalter UNSIGNED darf Verifikation nicht erfolgreich sein
    EXPECT_FALSE(detector.isConfigVerified());
    EXPECT_EQ(detector.declaredHash(), "UNSIGNED");
}

TEST_F(MimeDetectorTest, ComputeCurrentHashNonEmpty) {
    auto hash = detector.computeCurrentHash();
    EXPECT_EQ(hash.size(), 64); // SHA256 hex
    EXPECT_TRUE(std::all_of(hash.begin(), hash.end(), [](unsigned char c){return std::isxdigit(c)!=0;}));
}

TEST_F(MimeDetectorTest, FromExtension_CommonFormats) {
    EXPECT_EQ(detector.fromExtension("document.pdf"), "application/pdf");
    EXPECT_EQ(detector.fromExtension("image.png"), "image/png");
    EXPECT_EQ(detector.fromExtension("photo.jpg"), "image/jpeg");
    EXPECT_EQ(detector.fromExtension("data.json"), "application/json");
    EXPECT_EQ(detector.fromExtension("archive.zip"), "application/zip");
}

TEST_F(MimeDetectorTest, FromExtension_CaseInsensitive) {
    EXPECT_EQ(detector.fromExtension("FILE.PDF"), "application/pdf");
    EXPECT_EQ(detector.fromExtension("Image.PNG"), "image/png");
}

TEST_F(MimeDetectorTest, FromContent_PDF) {
    std::vector<uint8_t> pdf_data = {0x25, 0x50, 0x44, 0x46, 0x2D, 0x31, 0x2E, 0x34};  // %PDF-1.4
    EXPECT_EQ(detector.fromContent(pdf_data), "application/pdf");
}

TEST_F(MimeDetectorTest, FromContent_PNG) {
    std::vector<uint8_t> png_data = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_EQ(detector.fromContent(png_data), "image/png");
}

TEST_F(MimeDetectorTest, FromContent_JPEG) {
    std::vector<uint8_t> jpeg_data = {0xFF, 0xD8, 0xFF, 0xE0};
    EXPECT_EQ(detector.fromContent(jpeg_data), "image/jpeg");
}

TEST_F(MimeDetectorTest, IsText) {
    EXPECT_TRUE(MimeDetector::isText("text/plain"));
    EXPECT_TRUE(MimeDetector::isText("text/html"));
    EXPECT_TRUE(MimeDetector::isText("application/json"));
    EXPECT_FALSE(MimeDetector::isText("image/png"));
}

TEST_F(MimeDetectorTest, IsImage) {
    EXPECT_TRUE(MimeDetector::isImage("image/png"));
    EXPECT_TRUE(MimeDetector::isImage("image/jpeg"));
    EXPECT_FALSE(MimeDetector::isImage("text/plain"));
}

TEST_F(MimeDetectorTest, Detect_PreferContentOverExtension) {
    // PDF content with wrong extension
    std::vector<uint8_t> pdf_data = {0x25, 0x50, 0x44, 0x46, 0x2D};
    EXPECT_EQ(detector.detect("file.txt", pdf_data), "application/pdf");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
