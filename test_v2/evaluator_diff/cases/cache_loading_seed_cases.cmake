#@@CASE cache_loading_legacy_and_prefix_forms_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/cache_legacy_plain/CMakeCache.txt
FIRST:STRING=one
HIDE_PLAIN:INTERNAL=hidden-plain
#@@END_FILE_TEXT
#@@FILE_TEXT source/cache_legacy_filtered/CMakeCache.txt
KEEP:STRING=keep
DROP:STRING=drop-me
HIDE_FILTER:INTERNAL=secret-filter
#@@END_FILE_TEXT
#@@FILE_TEXT source/cache_prefix_empty/CMakeCache.txt
EMPTY:STRING=
KEEP:STRING=keep-prefix
#@@END_FILE_TEXT
#@@QUERY VAR FIRST
#@@QUERY VAR KEEP
#@@QUERY VAR HIDE_FILTER
#@@QUERY VAR PFX_KEEP
#@@QUERY VAR PFX_EMPTY
#@@QUERY CACHE_DEFINED DROP
#@@QUERY CACHE_DEFINED HIDE_PLAIN
#@@QUERY CACHE_DEFINED PFX_KEEP
set(PFX_EMPTY sentinel)
load_cache("${CMAKE_CURRENT_SOURCE_DIR}/cache_legacy_plain")
load_cache("${CMAKE_CURRENT_SOURCE_DIR}/cache_legacy_filtered" INCLUDE_INTERNALS HIDE_FILTER EXCLUDE DROP)
load_cache("${CMAKE_CURRENT_SOURCE_DIR}/cache_prefix_empty" READ_WITH_PREFIX PFX_ EMPTY KEEP)
#@@ENDCASE

#@@CASE cache_loading_invalid_shapes_are_rejected
#@@OUTCOME ERROR
load_cache()
#@@ENDCASE
