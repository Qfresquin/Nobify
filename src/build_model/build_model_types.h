#ifndef BUILD_MODEL_TYPES_H_
#define BUILD_MODEL_TYPES_H_

#include "arena.h"
#include "nob.h"
#include "logic_model.h"

// ============================================================================
// TIPOS BÁSICOS E ESTRUTURAS DO BUILD MODEL
// ============================================================================
typedef enum {
    TARGET_EXECUTABLE,
    TARGET_STATIC_LIB,
    TARGET_SHARED_LIB,
    TARGET_OBJECT_LIB,
    TARGET_INTERFACE_LIB,
    TARGET_UTILITY,
    TARGET_IMPORTED,
    TARGET_ALIAS
} Target_Type;

typedef enum {
    VISIBILITY_PUBLIC,
    VISIBILITY_PRIVATE,
    VISIBILITY_INTERFACE
} Visibility;

typedef enum {
    CONFIG_DEBUG,
    CONFIG_RELEASE,
    CONFIG_RELWITHDEBINFO,
    CONFIG_MINSIZEREL,
    CONFIG_ALL
} Build_Config;

typedef struct {
    String_View name;
    String_View value;
} Property;

typedef struct Property_List {
    Property *items;
    size_t count;
    size_t capacity;
} Property_List;

typedef struct Build_Property_Index_Entry {
    char *key;
    int value;
} Build_Property_Index_Entry;

// String_List type definition (moved from build_model.c)
typedef struct String_List {
    String_View *items;
    size_t count;
    size_t capacity;
} String_List;

typedef struct {
    String_View value;
    Logic_Node *condition; // NULL => always true
} Conditional_Property;

typedef struct Conditional_Property_List {
    Conditional_Property *items;
    size_t count;
    size_t capacity;
} Conditional_Property_List;

// ============================================================================
// COMANDOS PERSONALIZADOS
// ============================================================================

typedef enum {
    CUSTOM_COMMAND_SHELL,
    CUSTOM_COMMAND_SCRIPT
} Custom_Command_Type;

typedef struct {
    Custom_Command_Type type;
    String_View command;      // Comando shell ou script
    String_List outputs;      // Arquivos de saida
    String_List byproducts;   // Arquivos gerados adicionais
    String_List inputs;       // Arquivos de entrada
    String_List depends;      // Dependências adicionais
    String_View main_dependency;
    String_View depfile;
    String_View working_dir;  // Diretório de trabalho
    String_View comment;      // Comentário para logging
    bool echo;                // Mostrar comando no output
    bool append;
    bool verbatim;
    bool uses_terminal;
    bool command_expand_lists;
    bool depends_explicit_only;
    bool codegen;
} Custom_Command;

typedef struct {
    String_View name;
    String_View command;
    String_View working_directory;
    bool command_expand_lists;
} Build_Test;

typedef struct {
    String_View name;
    String_View display_name;
    String_View description;
    String_View parent_group;
    bool expanded;
    bool bold_title;
} CPack_Component_Group;

typedef struct {
    String_View name;
    String_View display_name;
} CPack_Install_Type;

typedef struct {
    String_View name;
    String_View display_name;
    String_View description;
    String_View group;
    String_List depends;
    String_List install_types;
    bool required;
    bool hidden;
    bool disabled;
    bool downloaded;
} CPack_Component;

// ============================================================================
// TARGETS
// ============================================================================

typedef struct {
    String_View name;
    Target_Type type;
    void *owner_model;
    
    // Fontes
    String_List sources;
    String_List source_groups;  // Para organização no IDE
    
    // Dependências
    String_List dependencies;     // Nomes dos targets dependentes
    String_List object_dependencies; // Dependencias via $<TARGET_OBJECTS:...>
    String_List interface_dependencies; // Dependencias transitivas (PUBLIC/INTERFACE)
    String_List link_libraries;   // Bibliotecas para linkar
    String_List interface_libs;   // Bibliotecas de interface (PUBLIC/INTERFACE)
    String_List interface_compile_definitions;
    String_List interface_compile_options;
    String_List interface_include_directories;
    String_List interface_link_options;
    String_List interface_link_directories;
    
    // Fase 3B: propriedades condicionais (fonte unica de verdade)
    Conditional_Property_List conditional_compile_definitions;
    Conditional_Property_List conditional_compile_options;
    Conditional_Property_List conditional_include_directories;
    Conditional_Property_List conditional_link_libraries;
    Conditional_Property_List conditional_link_options;
    Conditional_Property_List conditional_link_directories;

    // Propriedades gerais
    String_View output_name;      // Nome do arquivo de saída
    String_View output_directory;
    String_View runtime_output_directory;
    String_View archive_output_directory;
    String_View prefix;
    String_View suffix;
    
    // Flags especiais
    bool win32_executable;
    bool macosx_bundle;
    bool exclude_from_all;
    bool imported;
    bool alias;
    
    // Comandos personalizados associados
    Custom_Command *pre_build_commands;
    Custom_Command *post_build_commands;
    size_t pre_build_count;
    size_t post_build_count;
    size_t pre_build_capacity;
    size_t post_build_capacity;
    
    // Propriedades genéricas (key-value)
    Property_List custom_properties;
    Build_Property_Index_Entry *custom_property_index;
} Build_Target;

// ============================================================================
// DIRETÓRIOS E INCLUSÕES
// ============================================================================

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    String_List include_dirs;      // -I directories
    String_List system_include_dirs;
    String_List link_dirs;         // -L directories
} Directory_Info;

// ============================================================================
// PACOTES ENCONTRADOS
// ============================================================================

typedef struct {
    String_View name;
    bool found;
    String_View version;
    String_List include_dirs;
    String_List libraries;
    String_List definitions;
    String_List options;
    Property_List properties;
} Found_Package;

typedef struct Build_Target_Index_Entry {
    char *key;
    int value;
} Build_Target_Index_Entry;

// ============================================================================
// MODELO COMPLETO DO BUILD
// ============================================================================

typedef struct {
    Arena *arena;  // Arena principal para todas as alocações
    
    // Informações do projeto
    String_View project_name;
    String_View project_version;
    String_View project_description;
    String_List project_languages;  // C, CXX, ASM, etc.
    
    // Targets
    Build_Target **targets;
    size_t target_count;
    size_t target_capacity;
    Build_Target_Index_Entry *target_index_by_name;
    
    // Diretórios
    Directory_Info directories;
    
    // Variáveis de cache (persistentes entre runs)
    Property_List cache_variables;
    Build_Property_Index_Entry *cache_variable_index;
    
    // Variáveis de ambiente
    Property_List environment_variables;
    Build_Property_Index_Entry *environment_variable_index;
    
    // Pacotes encontrados
    Found_Package *found_packages;
    size_t package_count;
    size_t package_capacity;
    
    // Configurações suportadas
    bool config_debug;
    bool config_release;
    bool config_relwithdebinfo;
    bool config_minsizerel;
    String_View default_config;  // CMAKE_BUILD_TYPE padrão
    
    // Opções de compilação globais
    String_List global_definitions;
    String_List global_compile_options;
    String_List global_link_options;
    String_List global_link_libraries;
    
    // Padrões de linguagem
    struct {
        String_View c_standard;
        String_View cxx_standard;
        bool c_extensions;
        bool cxx_extensions;
    } language_standards;
    
    // Toolchain
    String_View c_compiler;
    String_View cxx_compiler;
    String_View assembler;
    String_View linker;
    
    // Flags do sistema
    bool is_windows;
    bool is_unix;
    bool is_apple;
    bool is_linux;
    
    // Estado do build
    bool enable_testing;
    bool enable_install;
    
    // Instalação
    struct {
        String_List targets;
        String_List files;
        String_List directories;
        String_List programs;
        String_View prefix;
    } install_rules;

    Custom_Command *output_custom_commands;
    size_t output_custom_command_count;
    size_t output_custom_command_capacity;

    Build_Test *tests;
    size_t test_count;
    size_t test_capacity;

    CPack_Component_Group *cpack_component_groups;
    size_t cpack_component_group_count;
    size_t cpack_component_group_capacity;

    CPack_Install_Type *cpack_install_types;
    size_t cpack_install_type_count;
    size_t cpack_install_type_capacity;

    CPack_Component *cpack_components;
    size_t cpack_component_count;
    size_t cpack_component_capacity;
} Build_Model;

// ============================================================================
// ENUMS AUXILIARES DA API PÚBLICA
// ============================================================================
typedef enum {
    TARGET_FLAG_WIN32_EXECUTABLE,
    TARGET_FLAG_MACOSX_BUNDLE,
    TARGET_FLAG_EXCLUDE_FROM_ALL,
    TARGET_FLAG_IMPORTED,
    TARGET_FLAG_ALIAS,
} Target_Flag;

typedef enum {
    INSTALL_RULE_TARGET,
    INSTALL_RULE_FILE,
    INSTALL_RULE_PROGRAM,
    INSTALL_RULE_DIRECTORY,
} Install_Rule_Type;

typedef enum {
    BUILD_MODEL_LIST_INCLUDE_DIRS,
    BUILD_MODEL_LIST_SYSTEM_INCLUDE_DIRS,
    BUILD_MODEL_LIST_LINK_DIRS,
    BUILD_MODEL_LIST_GLOBAL_DEFINITIONS,
    BUILD_MODEL_LIST_GLOBAL_COMPILE_OPTIONS,
    BUILD_MODEL_LIST_GLOBAL_LINK_OPTIONS,
    BUILD_MODEL_LIST_GLOBAL_LINK_LIBRARIES,
} Build_Model_List_Kind;

typedef enum {
    BUILD_TARGET_LIST_SOURCES,
    BUILD_TARGET_LIST_DEPENDENCIES,
    BUILD_TARGET_LIST_OBJECT_DEPENDENCIES,
    BUILD_TARGET_LIST_INTERFACE_DEPENDENCIES,
    BUILD_TARGET_LIST_INTERFACE_LIBS,
    BUILD_TARGET_LIST_INTERFACE_COMPILE_DEFINITIONS,
    BUILD_TARGET_LIST_INTERFACE_COMPILE_OPTIONS,
    BUILD_TARGET_LIST_INTERFACE_INCLUDE_DIRECTORIES,
    BUILD_TARGET_LIST_INTERFACE_LINK_OPTIONS,
    BUILD_TARGET_LIST_INTERFACE_LINK_DIRECTORIES,
} Build_Target_List_Kind;

typedef enum {
    BUILD_TARGET_DERIVED_INTERFACE_COMPILE_DEFINITIONS,
    BUILD_TARGET_DERIVED_INTERFACE_COMPILE_OPTIONS,
    BUILD_TARGET_DERIVED_INTERFACE_INCLUDE_DIRECTORIES,
    BUILD_TARGET_DERIVED_INTERFACE_LINK_OPTIONS,
    BUILD_TARGET_DERIVED_INTERFACE_LINK_DIRECTORIES,
    BUILD_TARGET_DERIVED_INTERFACE_LINK_LIBRARIES,
} Build_Target_Derived_Property_Kind;

#endif // BUILD_MODEL_TYPES_H_
