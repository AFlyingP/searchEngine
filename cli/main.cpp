#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    std::cout << "needlefish v0.1.0 - high performance full-text search engine\n";
    if (argc < 2) {
        std::cout << "Usage: needlefish <command> [options]\n";
        std::cout << "Commands:\n";
        std::cout << "  index    Build an index from documents\n";
        std::cout << "  search   Search an existing index\n";
        std::cout << "  suggest  Autocomplete and fuzzy suggest\n";
        std::cout << "  stats    Display index statistics\n";
        return 0;
    }
    std::string_view cmd = argv[1];
    std::cout << "Executing command: " << cmd << "\n";
    return 0;
}
