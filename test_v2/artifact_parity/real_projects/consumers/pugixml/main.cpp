#include <pugixml.hpp>

int main() {
    pugi::xml_document doc;
    pugi::xml_parse_result ok = doc.load_string("<root value='7'/>");
    if (!ok) return 1;
    return doc.child("root").attribute("value").as_int() == 7 ? 0 : 1;
}
