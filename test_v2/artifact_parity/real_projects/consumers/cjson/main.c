#include <cJSON.h>

int main(void) {
    cJSON *json = cJSON_Parse("{\"answer\":42}");
    int ok = json != NULL && cJSON_GetObjectItemCaseSensitive(json, "answer")->valueint == 42;
    cJSON_Delete(json);
    return ok ? 0 : 1;
}
