#include "utils/stemmer.h"
#include <algorithm>
#include <cctype>

namespace themis {
namespace utils {

std::string Stemmer::stem(std::string_view token, Language lang) {
    if (lang == Language::NONE || token.empty()) {
        return std::string(token);
    }
    
    // Convert to string for manipulation
    std::string word(token);
    
    // Ensure lowercase (should already be, but safety check)
    std::transform(word.begin(), word.end(), word.begin(), 
        [](unsigned char c) { return std::tolower(c); });
    
    // Minimum word length for stemming
    if (word.length() <= 2) {
        return word;
    }
    
    switch (lang) {
        case Language::EN:
            return stemEnglish(word);
        case Language::DE:
            return stemGerman(word);
        default:
            return word;
    }
}

Stemmer::Language Stemmer::parseLanguage(std::string_view langCode) {
    if (langCode == "en" || langCode == "EN") return Language::EN;
    if (langCode == "de" || langCode == "DE") return Language::DE;
    if (langCode == "none" || langCode == "NONE") return Language::NONE;
    return Language::NONE; // Default fallback
}

std::string Stemmer::languageToString(Language lang) {
    switch (lang) {
        case Language::EN: return "en";
        case Language::DE: return "de";
        case Language::NONE: return "none";
        default: return "none";
    }
}

// English Porter Stemmer (simplified - Step 1a, 1b, 1c only)
std::string Stemmer::stemEnglish(std::string word) {
    if (word.length() <= 2) return word;
    
    // Step 1a: plurals
    if (word.ends_with("sses")) {
        word = word.substr(0, word.length() - 2); // sses -> ss
    } else if (word.ends_with("ies")) {
        word = word.substr(0, word.length() - 2); // ies -> i
    } else if (word.ends_with("ss")) {
        // keep as is
    } else if (word.ends_with("s") && word.length() > 3) {
        word = word.substr(0, word.length() - 1); // s -> (empty)
    }
    
    // Step 1b: -ed, -ing
    if (word.ends_with("eed")) {
        if (hasVowel(word.substr(0, word.length() - 3))) {
            word = word.substr(0, word.length() - 1); // eed -> ee
        }
    } else if (word.ends_with("ed")) {
        std::string stem = word.substr(0, word.length() - 2);
        if (hasVowel(stem)) {
            word = stem;
            // Post-processing for double consonants
            if (endsWithDoubleConsonant(word) && 
                !word.ends_with("ll") && !word.ends_with("ss") && !word.ends_with("zz")) {
                word = word.substr(0, word.length() - 1);
            }
        }
    } else if (word.ends_with("ing")) {
        std::string stem = word.substr(0, word.length() - 3);
        if (hasVowel(stem)) {
            word = stem;
            // Post-processing
            if (endsWithDoubleConsonant(word) && 
                !word.ends_with("ll") && !word.ends_with("ss") && !word.ends_with("zz")) {
                word = word.substr(0, word.length() - 1);
            }
        }
    }
    
    // Step 1c: y -> i (only if preceded by a consonant)
    if (word.length() > 2 && word.ends_with("y")) {
        char prev = word[word.length() - 2];
        bool prevIsVowel = (prev == 'a' || prev == 'e' || prev == 'i' || prev == 'o' || prev == 'u');
        std::string stem = word.substr(0, word.length() - 1);
        if (!prevIsVowel && hasVowel(stem)) {
            word = stem + "i";
        }
    }
    
    // Step 2: common suffixes (simplified subset)
    word = replaceEnding(word, "ational", "ate");
    word = replaceEnding(word, "tional", "tion");
    word = replaceEnding(word, "alism", "al");
    word = replaceEnding(word, "ation", "ate");
    word = replaceEnding(word, "ness", "");
    // Minimal extra: enci -> enc (to handle 'valenci' -> 'valenc')
    word = replaceEnding(word, "enci", "enc");
    
    return word;
}

// German Stemmer (simplified - removes common suffixes)
std::string Stemmer::stemGerman(std::string word) {
    if (word.length() <= 3) return word;
    
    // Remove common German suffixes (order matters!)
    // Plurals
    if (word.ends_with("ern")) {
        word = word.substr(0, word.length() - 3);
    } else if (word.ends_with("em")) {
        word = word.substr(0, word.length() - 2);
    } else if (word.ends_with("en")) {
        word = word.substr(0, word.length() - 2);
    } else if (word.ends_with("er")) {
        word = word.substr(0, word.length() - 2);
    } else if (word.ends_with("es")) {
        word = word.substr(0, word.length() - 2);
    } else if (word.ends_with("e")) {
        word = word.substr(0, word.length() - 1);
    } else if (word.ends_with("s") && word.length() > 4) {
        word = word.substr(0, word.length() - 1);
    }
    
    // Additional suffix removal (simplified)
    if (word.length() > 5) {
        if (word.ends_with("ung")) {
            word = word.substr(0, word.length() - 3);
        } else if (word.ends_with("heit")) {
            word = word.substr(0, word.length() - 4);
        } else if (word.ends_with("keit")) {
            word = word.substr(0, word.length() - 4);
        } else if (word.ends_with("lich")) {
            word = word.substr(0, word.length() - 4);
        }
    }
    
    return word;
}

bool Stemmer::endsWithDoubleConsonant(const std::string& word) {
    if (word.length() < 2) return false;
    char last = word[word.length() - 1];
    char prev = word[word.length() - 2];
    return last == prev && 
           last != 'a' && last != 'e' && last != 'i' && last != 'o' && last != 'u';
}

bool Stemmer::hasVowel(const std::string& word) {
    for (char c : word) {
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') {
            return true;
        }
    }
    return false;
}

std::string Stemmer::replaceEnding(std::string word, std::string_view from, std::string_view to) {
    if (word.length() > from.length() && word.ends_with(from)) {
        std::string stem = word.substr(0, word.length() - from.length());
        if (hasVowel(stem)) {
            return stem + std::string(to);
        }
    }
    return word;
}

} // namespace utils
} // namespace themis
