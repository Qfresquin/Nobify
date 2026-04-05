#include <fmt/format.h>
#include <string>

int main() {
    std::string text = fmt::format("fmt-{}", 12);
    return text == "fmt-12" ? 0 : 1;
}
