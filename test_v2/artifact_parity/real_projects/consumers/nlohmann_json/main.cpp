#include <nlohmann/json.hpp>

int main() {
    nlohmann::json value = nlohmann::json::parse("{\"answer\":42}");
    return value["answer"].get<int>() == 42 ? 0 : 1;
}
