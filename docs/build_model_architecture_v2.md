# Build Model v2 Architecture Notes

## Scope
This note documents practical coupling points of the v2 build model pipeline:
`AST -> evaluator -> event_ir -> builder -> validate -> freeze`.

## Internal Utility Dependencies
The build model currently depends on internal utility layers:
- `arena`: ownership and lifetime model for all allocations.
- `nob`: `String_View`, logging, file helpers.
- `ds_adapter`: hash maps (`ds_sh*`) for indexes and intern maps.
- `logic_model`: conditional property predicates and evaluation.
- `genex`: generator-expression evaluation for selected target properties.

These dependencies are intentional in v2 and should be treated as required runtime/build-time contracts for this module.

## Boundary Rules
- Evaluator emits `event_ir` only; it does not mutate `Build_Model`.
- Builder is the only writer of `Build_Model`.
- Validator is read-only and may fail freeze on semantic errors.
- `builder_finish()` returns `NULL` after any fatal builder error; partial state is not valid for downstream consumers.

## Directory Scope Modeling
- Event IR now includes directory scope delimiters: `EV_DIR_PUSH` and `EV_DIR_POP`.
- Builder materializes a directory tree (`Build_Directory_Node`) and keeps target ownership (`owner_directory_index`).
- Flat directory lists in `Build_Model.directories` remain available for backward compatibility.

## Dependency Validation Policy
- Validation does not infer target references from `link_libraries` strings.
- Only explicit dependency edges are validated and used for cycle checks:
  `dependencies`, `object_dependencies`, `interface_dependencies`.
