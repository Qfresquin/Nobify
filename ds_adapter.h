#ifndef DS_ADAPTER_H_
#define DS_ADAPTER_H_

#include "stb_ds.h"

#define ds_arrlen(a) ((size_t)stbds_arrlenu(a))
#define ds_arrcap(a) ((size_t)stbds_arrcap(a))
#define ds_arrput(a, v) stbds_arrput((a), (v))
#define ds_arrfree(a) stbds_arrfree((a))

#define ds_shput(map, key, value) stbds_shput((map), (key), (value))
#define ds_shgetp_null(map, key) stbds_shgetp_null((map), (key))
#define ds_shgeti(map, key) ((int)stbds_shgeti((map), (key)))
#define ds_shdel(map, key) stbds_shdel((map), (key))
#define ds_shlen(map) ((size_t)stbds_shlenu(map))
#define ds_shfree(map) stbds_shfree((map))

#endif // DS_ADAPTER_H_
