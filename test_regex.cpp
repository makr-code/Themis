#include <iostream>
#include <regex>
#include <string>

int main() {
    std::string text = "Card: 4532-0151-4488-0342";
    std::regex pattern(R"((?:\b|^)[3456][0-9]{3}(?:-[0-9]{4}){3}(?:\b|$))");
    std::sregex_iterator it(text.begin(), text.end(), pattern);
    std::sregex_iterator end;
    
    for (; it != end; ++it) {
        std::cout << "Match: " << it->str() << std::endl;
    }
    
    if (it == end) {
        std::cout << "No match found" << std::endl;
    }
    
    return 0;
}
