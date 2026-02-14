#ifndef BUILD_MODEL_H_
#define BUILD_MODEL_H_

#include "arena.h"
#include "nob.h"

// ============================================================================
// TIPOS BÁSICOS
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

// String_List type definition (moved from build_model.c)
typedef struct String_List {
    String_View *items;
    size_t count;
    size_t capacity;
} String_List;

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
    
    // Fontes
    String_List sources;
    String_List source_groups;  // Para organização no IDE
    
    // Dependências
    String_List dependencies;     // Nomes dos targets dependentes
    String_List interface_dependencies; // Dependencias transitivas (PUBLIC/INTERFACE)
    String_List link_libraries;   // Bibliotecas para linkar
    String_List interface_libs;   // Bibliotecas de interface (PUBLIC/INTERFACE)
    String_List interface_compile_definitions;
    String_List interface_compile_options;
    String_List interface_include_directories;
    String_List interface_link_options;
    String_List interface_link_directories;
    
    // Propriedades por configuração
    struct {
        String_List compile_definitions;
        String_List compile_options;
        String_List include_directories;
        String_List link_options;
        String_List link_directories;
    } properties[CONFIG_ALL + 1];  // Índice por Build_Config (inclui CONFIG_ALL)
    
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
    Build_Target *targets;
    size_t target_count;
    size_t target_capacity;
    
    // Diretórios
    Directory_Info directories;
    
    // Variáveis de cache (persistentes entre runs)
    Property_List cache_variables;
    
    // Variáveis de ambiente
    Property_List environment_variables;
    
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
// FUNÇÕES PÚBLICAS
// ============================================================================

// Cria um novo modelo vazio
Build_Model* build_model_create(Arena *arena);

// Cria um novo target
Build_Target* build_model_add_target(Build_Model *model, 
                                     String_View name, 
                                     Target_Type type);

// Encontra um target pelo nome (retorna NULL se não existir)
Build_Target* build_model_find_target(Build_Model *model, String_View name);
int build_model_find_target_index(const Build_Model *model, String_View name);

// Utilitarios unificados para String_List
bool string_list_contains(const String_List *list, String_View item);
bool string_list_add_unique(String_List *list, Arena *arena, String_View item);

// Adiciona uma fonte a um target
void build_target_add_source(Build_Target *target, Arena *arena, String_View source);

// Adiciona uma dependência a um target
void build_target_add_dependency(Build_Target *target, Arena *arena, String_View dep_name);

// Adiciona uma definição de compilação
void build_target_add_definition(Build_Target *target, 
                                 Arena *arena,
                                 String_View definition, 
                                 Visibility visibility,
                                 Build_Config config);

// Adiciona um diretório de include
void build_target_add_include_directory(Build_Target *target,
                                        Arena *arena,
                                        String_View directory,
                                        Visibility visibility,
                                        Build_Config config);

// Adiciona uma biblioteca para linkar
void build_target_add_library(Build_Target *target,
                              Arena *arena,
                              String_View library,
                              Visibility visibility);

// Adiciona uma opção de compilação
void build_target_add_compile_option(Build_Target *target,
                                     Arena *arena,
                                     String_View option,
                                     Visibility visibility,
                                     Build_Config config);

// Adiciona uma opcao de link
void build_target_add_link_option(Build_Target *target,
                                  Arena *arena,
                                  String_View option,
                                  Visibility visibility,
                                  Build_Config config);

// Adiciona um diretorio de link
void build_target_add_link_directory(Build_Target *target,
                                     Arena *arena,
                                     String_View directory,
                                     Visibility visibility,
                                     Build_Config config);

// Adiciona uma dependencia de interface para propagacao transitiva
void build_target_add_interface_dependency(Build_Target *target,
                                           Arena *arena,
                                           String_View dep_name);

// Define uma propriedade do target
void build_target_set_property(Build_Target *target,
                               Arena *arena,
                               String_View key,
                               String_View value);

// Obtém uma propriedade do target
String_View build_target_get_property(Build_Target *target, String_View key);

// Adiciona um pacote encontrado
Found_Package* build_model_add_package(Build_Model *model,
                                       String_View name,
                                       bool found);

// Registra um teste vindo de add_test()
Build_Test* build_model_add_test(Build_Model *model,
                                 String_View name,
                                 String_View command,
                                 String_View working_directory,
                                 bool command_expand_lists);

CPack_Component_Group* build_model_add_cpack_component_group(Build_Model *model, String_View name);
CPack_Install_Type* build_model_add_cpack_install_type(Build_Model *model, String_View name);
CPack_Component* build_model_add_cpack_component(Build_Model *model, String_View name);

// Define uma variável de cache
void build_model_set_cache_variable(Build_Model *model,
                                    String_View key,
                                    String_View value,
                                    String_View type,   // BOOL, STRING, PATH, FILEPATH
                                    String_View docstring);

// Obtém uma variável de cache
String_View build_model_get_cache_variable(Build_Model *model, String_View key);

// Gera um grafo de dependências (para validação e ordenação)
bool build_model_validate_dependencies(Build_Model *model);

// Ordena targets por dependência (topological sort)
Build_Target** build_model_topological_sort(Build_Model *model, size_t *count);

// Exporta para debugging
void build_model_dump(Build_Model *model, FILE *output);

// ============================================================================
// FUNÇÕES AUXILIARES
// ============================================================================

// Funções para manipulação de String_List
void string_list_init(String_List *list);
void string_list_add(String_List *list, Arena *arena, String_View item);

// Funções para manipulação de Property_List
void property_list_init(Property_List *list);
void property_list_add(Property_List *list, Arena *arena, 
                       String_View key, String_View value);
String_View property_list_find(Property_List *list, String_View key);

// ============================
// Fase 1: APIs movidas do evaluator para o modelo
// ============================

typedef enum {
    TARGET_FLAG_WIN32_EXECUTABLE,
    TARGET_FLAG_MACOSX_BUNDLE,
    TARGET_FLAG_EXCLUDE_FROM_ALL,
    TARGET_FLAG_IMPORTED,
    TARGET_FLAG_ALIAS,
} Target_Flag;

void build_target_set_flag(Build_Target *target, Target_Flag flag, bool value);
void build_target_set_alias(Build_Target *target, Arena *arena, String_View aliased_name);

typedef enum {
    INSTALL_RULE_TARGET,
    INSTALL_RULE_FILE,
    INSTALL_RULE_PROGRAM,
    INSTALL_RULE_DIRECTORY,
} Install_Rule_Type;

void build_model_set_project_info(Build_Model *model, String_View name, String_View version);
void build_model_set_default_config(Build_Model *model, String_View config);
void build_model_enable_language(Build_Model *model, Arena *arena, String_View lang);
void build_model_set_testing_enabled(Build_Model *model, bool enabled);
void build_model_set_env_var(Build_Model *model, Arena *arena, String_View key, String_View value);

void build_model_add_global_definition(Build_Model *model, Arena *arena, String_View def);
void build_model_add_global_compile_option(Build_Model *model, Arena *arena, String_View opt);
void build_model_add_global_link_option(Build_Model *model, Arena *arena, String_View opt);
void build_model_process_global_definition_arg(Build_Model *model, Arena *arena, String_View arg);

void build_model_add_include_directory(Build_Model *model, Arena *arena, String_View dir, bool is_system);
void build_model_add_link_directory(Build_Model *model, Arena *arena, String_View dir);

void build_model_add_global_link_library(Build_Model *model, Arena *arena, String_View lib);

void build_model_set_install_prefix(Build_Model *model, String_View prefix);
void build_model_add_install_rule(Build_Model *model, Arena *arena, Install_Rule_Type type, String_View item, String_View destination);

// Helpers adicionais (necessários para eliminar mutação direta do evaluator)
void build_model_set_install_enabled(Build_Model *model, bool enabled);
void build_model_remove_global_definition(Build_Model *model, String_View def);

// Config helpers
Build_Config build_model_config_from_string(String_View cfg);
String_View build_model_config_suffix(Build_Config cfg);

// Target property routing helpers
void build_target_set_property_smart(Build_Target *target,
                                     Arena *arena,
                                     String_View key,
                                     String_View value);
String_View build_target_get_property_computed(Build_Target *target,
                                               String_View key,
                                               String_View default_config);

// Test helper wrapper (Fase 1 compat)
Build_Test* build_model_add_test_ex(Build_Model *model,
                                    Arena *arena,
                                    String_View name,
                                    String_View command,
                                    String_View working_dir);

// CPack get-or-create wrappers + setters
CPack_Component_Group* build_model_get_or_create_cpack_group(Build_Model *model,
                                                             Arena *arena,
                                                             String_View name);
CPack_Component* build_model_get_or_create_cpack_component(Build_Model *model,
                                                           Arena *arena,
                                                           String_View name);
CPack_Install_Type* build_model_get_or_create_cpack_install_type(Build_Model *model,
                                                                  Arena *arena,
                                                                  String_View name);

void build_cpack_install_type_set_display_name(CPack_Install_Type *install_type,
                                               String_View display_name);

void build_cpack_group_set_display_name(CPack_Component_Group *group, String_View display_name);
void build_cpack_group_set_description(CPack_Component_Group *group, String_View description);
void build_cpack_group_set_parent_group(CPack_Component_Group *group, String_View parent_group);
void build_cpack_group_set_expanded(CPack_Component_Group *group, bool expanded);
void build_cpack_group_set_bold_title(CPack_Component_Group *group, bool bold_title);

void build_cpack_component_clear_dependencies(CPack_Component *component);
void build_cpack_component_clear_install_types(CPack_Component *component);
void build_cpack_component_set_display_name(CPack_Component *component, String_View display_name);
void build_cpack_component_set_description(CPack_Component *component, String_View description);
void build_cpack_component_set_group(CPack_Component *component, String_View group);
void build_cpack_component_add_dependency(CPack_Component *component, Arena *arena, String_View dependency);
void build_cpack_component_add_install_type(CPack_Component *component, Arena *arena, String_View install_type);
void build_cpack_component_set_required(CPack_Component *component, bool required);
void build_cpack_component_set_hidden(CPack_Component *component, bool hidden);
void build_cpack_component_set_disabled(CPack_Component *component, bool disabled);
void build_cpack_component_set_downloaded(CPack_Component *component, bool downloaded);

// Custom command constructors moved from evaluator
Custom_Command* build_target_add_custom_command_ex(Build_Target *target,
                                                   Arena *arena,
                                                   bool pre_build,
                                                   String_View command,
                                                   String_View working_dir,
                                                   String_View comment);
Custom_Command* build_target_add_custom_command(Build_Target *target,
                                                Arena *arena,
                                                bool pre_build,
                                                String_View command);
Custom_Command* build_model_add_custom_command_output_ex(Build_Model *model,
                                                         Arena *arena,
                                                         String_View command,
                                                         String_View working_dir,
                                                         String_View comment);
Custom_Command* build_model_add_custom_command_output(Build_Model *model,
                                                      Arena *arena,
                                                      String_View output,
                                                      String_View command);

// Path helpers moved from evaluator
bool build_path_is_absolute(String_View path);
String_View build_path_join(Arena *arena, String_View base, String_View rel);
String_View build_path_parent_dir(Arena *arena, String_View full_path);
String_View build_path_make_absolute(Arena *arena, String_View path);

#endif // BUILD_MODEL_H_
