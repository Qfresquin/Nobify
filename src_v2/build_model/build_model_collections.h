#ifndef BUILD_MODEL_COLLECTIONS_H_
#define BUILD_MODEL_COLLECTIONS_H_

#include "build_model_types.h"

// String and property container helpers.
void string_list_init(String_List *list);
void string_list_add(String_List *list, Arena *arena, String_View item);
bool string_list_contains(const String_List *list, String_View item);
bool string_list_add_unique(String_List *list, Arena *arena, String_View item);

void property_list_init(Property_List *list);
void property_list_add(Property_List *list, Arena *arena, String_View key, String_View value);
String_View property_list_find(Property_List *list, String_View key);

// Canonical ownership helper: duplicates content into arena-owned storage.
String_View build_model_copy_string(Arena *arena, String_View value);

#endif // BUILD_MODEL_COLLECTIONS_H_

