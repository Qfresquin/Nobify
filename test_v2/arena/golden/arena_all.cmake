#@@CASE create_and_destroy
#@@ENDCASE

#@@CASE basic_allocation
#@@ENDCASE

#@@CASE alloc_zero
#@@ENDCASE

#@@CASE alloc_array
#@@ENDCASE

#@@CASE multiple_blocks
#@@ENDCASE

#@@CASE reset
#@@ENDCASE

#@@CASE mark_and_rewind
#@@ENDCASE

#@@CASE reset_runs_cleanups_once_and_not_on_destroy_again
#@@ENDCASE

#@@CASE rewind_runs_only_cleanups_after_mark
#@@ENDCASE

#@@CASE rewind_restores_exact_mark_usage_across_blocks
#@@ENDCASE

#@@CASE strdup
#@@ENDCASE

#@@CASE strndup
#@@ENDCASE

#@@CASE memdup
#@@ENDCASE

#@@CASE alignment
#@@ENDCASE

#@@CASE large_allocation
#@@ENDCASE

#@@CASE realloc_last_grow_and_shrink
#@@ENDCASE

#@@CASE realloc_last_non_last_pointer
#@@ENDCASE

#@@CASE realloc_last_invalid_old_size_is_safe
#@@ENDCASE

#@@CASE reset_reuses_existing_blocks
#@@ENDCASE

#@@CASE rewind_reuses_existing_blocks
#@@ENDCASE

#@@CASE invalid_inputs_are_safe
#@@ENDCASE

#@@CASE overflow_alloc_returns_null_and_does_not_break_arena
#@@ENDCASE

#@@CASE dyn_reserve_basic_and_preserves_data
#@@ENDCASE

#@@CASE dyn_reserve_invalid_and_overflow
#@@ENDCASE
