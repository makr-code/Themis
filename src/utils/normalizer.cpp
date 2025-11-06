#include "utils/normalizer.h"

namespace themis {
namespace utils {

// Helper: check two-byte sequence equals a given pair
static inline bool is2(unsigned char a, unsigned char b, unsigned char x, unsigned char y) {
    return a == x && b == y;
}

std::string Normalizer::normalizeUmlauts(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
    size_t n = text.size();
    for (size_t i = 0; i < n; ) {
        unsigned char c = p[i];
        if (c == 0xC3 && i + 1 < n) {
            unsigned char d = p[i+1];
            // UTF-8 sequences for umlauts/ß
            // ä: C3 A4, ö: C3 B6, ü: C3 BC
            // Ä: C3 84, Ö: C3 96, Ü: C3 9C
            // ß: C3 9F
            if (is2(c, d, 0xC3, 0xA4)) { out.push_back('a'); i += 2; continue; } // ä
            if (is2(c, d, 0xC3, 0xB6)) { out.push_back('o'); i += 2; continue; } // ö
            if (is2(c, d, 0xC3, 0xBC)) { out.push_back('u'); i += 2; continue; } // ü
            if (is2(c, d, 0xC3, 0x84)) { out.push_back('A'); i += 2; continue; } // Ä
            if (is2(c, d, 0xC3, 0x96)) { out.push_back('O'); i += 2; continue; } // Ö
            if (is2(c, d, 0xC3, 0x9C)) { out.push_back('U'); i += 2; continue; } // Ü
            if (is2(c, d, 0xC3, 0x9F)) { out.push_back('s'); out.push_back('s'); i += 2; continue; } // ß -> ss
        }
        // default: copy byte as-is
        out.push_back(static_cast<char>(c));
        ++i;
    }
    return out;
}

} // namespace utils
} // namespace themis
