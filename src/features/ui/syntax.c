#include "axyne/syntax.h"

#include <ctype.h>
#include <string.h>

#define COMMENT 0x7a828e
#define KEYWORD 0xd9b36c
#define ORANGE 0xd98e73
#define GREEN 0xa3c98a
#define PURPLE 0xc79ad9
#define PLAIN AXYNE_SYNTAX_PLAIN
#define BLUE 0x7db5e3
#define TEAL 0x8cc7c0

#define STYLES(name) name, sizeof(name) / sizeof((name)[0])

/* SCE_C_*: comment 1, line 2, doc 3, line doc 15, number 4, keyword 5,
 * string 6, char 7, preprocessor 9, operator 10, identifier 11, keyword2 16,
 * global class 19. */
static const AxyneSyntaxStyle cpp_styles[] = {
    { 1, COMMENT }, { 2, COMMENT }, { 3, COMMENT }, { 15, COMMENT },
    { 4, KEYWORD }, { 5, ORANGE }, { 6, GREEN }, { 7, GREEN },
    { 9, PURPLE }, { 10, PLAIN }, { 11, PLAIN }, { 16, BLUE }, { 19, TEAL }
};
/* SCE_P_*: comment 1, number 2, strings 3/4/6/7, keyword 5, class 8, def 9,
 * operator 10, identifier 11, block comment 12, keyword2 14, decorator 15,
 * f-strings 16-19. */
static const AxyneSyntaxStyle python_styles[] = {
    { 1, COMMENT }, { 12, COMMENT }, { 2, KEYWORD }, { 3, GREEN }, { 4, GREEN },
    { 6, GREEN }, { 7, GREEN }, { 16, GREEN }, { 17, GREEN }, { 18, GREEN },
    { 19, GREEN }, { 5, ORANGE }, { 8, BLUE }, { 9, BLUE }, { 10, PLAIN },
    { 11, PLAIN }, { 14, TEAL }, { 15, PURPLE }
};
/* SCE_JSON_*: number 1, string 2, property 4, escape 5, comments 6/7,
 * operator 8, keyword 11. */
static const AxyneSyntaxStyle json_styles[] = {
    { 1, KEYWORD }, { 2, GREEN }, { 4, BLUE }, { 5, ORANGE }, { 6, COMMENT },
    { 7, COMMENT }, { 8, PLAIN }, { 11, ORANGE }, { 12, PURPLE }
};
/* SCE_CSS_*: tag 1, class 2, pseudo-class 3, operator 5, identifier 6,
 * value 8, comment 9, id 10, important 11, directive 12, strings 13/14,
 * pseudo-element 18, variable 23. */
static const AxyneSyntaxStyle css_styles[] = {
    { 1, ORANGE }, { 2, BLUE }, { 3, PURPLE }, { 4, PURPLE }, { 5, PLAIN },
    { 6, KEYWORD }, { 7, KEYWORD }, { 8, PLAIN }, { 9, COMMENT }, { 10, BLUE },
    { 11, ORANGE }, { 12, PURPLE }, { 13, GREEN }, { 14, GREEN },
    { 15, KEYWORD }, { 16, TEAL }, { 18, PURPLE }, { 23, TEAL }
};
/* SCE_H_*: tag 1, unknown tag 2, attribute 3, number 5, strings 6/7,
 * comment 9, entity 10, tag end 11, xml 12/13, script 14; embedded
 * JavaScript SCE_HJ_*: 42-44 comments, 45 number, 47 keyword, 48/49 strings. */
static const AxyneSyntaxStyle html_styles[] = {
    { 1, ORANGE }, { 2, ORANGE }, { 3, KEYWORD }, { 4, KEYWORD }, { 5, BLUE },
    { 6, GREEN }, { 7, GREEN }, { 9, COMMENT }, { 10, PURPLE }, { 11, ORANGE },
    { 12, PURPLE }, { 13, PURPLE }, { 14, PURPLE }, { 17, GREEN },
    { 42, COMMENT }, { 43, COMMENT }, { 44, COMMENT }, { 45, KEYWORD },
    { 47, ORANGE }, { 48, GREEN }, { 49, GREEN }
};
/* SCE_SH_*: error 1, comment 2, number 3, keyword 4, strings 5/6,
 * operator 7, identifier 8, scalar 9, param 10, backticks 11. */
static const AxyneSyntaxStyle bash_styles[] = {
    { 2, COMMENT }, { 3, KEYWORD }, { 4, ORANGE }, { 5, GREEN }, { 6, GREEN },
    { 7, PLAIN }, { 8, PLAIN }, { 9, BLUE }, { 10, BLUE }, { 11, TEAL }
};
/* SCE_MARKDOWN_*: strong 2/3, em 4/5, headers 6-11, lists 13/14,
 * blockquote 15, strikeout 16, hrule 17, link 18, code 19/20/21. */
static const AxyneSyntaxStyle markdown_styles[] = {
    { 2, KEYWORD }, { 3, KEYWORD }, { 4, PURPLE }, { 5, PURPLE },
    { 6, ORANGE }, { 7, ORANGE }, { 8, ORANGE }, { 9, ORANGE }, { 10, ORANGE },
    { 11, ORANGE }, { 13, BLUE }, { 14, BLUE }, { 15, COMMENT },
    { 16, COMMENT }, { 17, COMMENT }, { 18, BLUE }, { 19, GREEN },
    { 20, GREEN }, { 21, GREEN }
};
/* SCE_CMAKE_*: comment 1, strings 2/3/4, commands 5, parameters 6,
 * variable 7, user defined 8, control blocks 9-12, string var 13, number 14. */
static const AxyneSyntaxStyle cmake_styles[] = {
    { 1, COMMENT }, { 2, GREEN }, { 3, GREEN }, { 4, GREEN }, { 5, ORANGE },
    { 6, BLUE }, { 7, TEAL }, { 8, PURPLE }, { 9, ORANGE }, { 10, ORANGE },
    { 11, ORANGE }, { 12, ORANGE }, { 13, TEAL }, { 14, KEYWORD }
};
/* SCE_YAML_*: comment 1, identifier(key) 2, keyword 3, number 4,
 * reference 5, document 6, text 7, operator 9. */
static const AxyneSyntaxStyle yaml_styles[] = {
    { 1, COMMENT }, { 2, BLUE }, { 3, ORANGE }, { 4, KEYWORD }, { 5, PURPLE },
    { 6, PURPLE }, { 7, GREEN }, { 9, PLAIN }
};

/* SCE_DIFF_*: comment 1 (text outside the diff, e.g. the commit heading),
 * command 2 (diff --git), header 3 (---/+++), position 4 (@@ ... @@),
 * deleted 5, added 6, changed 7, patch add/delete 8/9, removed patch
 * add/delete 10/11. Used by the read-only Git diff tabs. */
static const AxyneSyntaxStyle diff_styles[] = {
    { 1, COMMENT }, { 2, KEYWORD }, { 3, ORANGE }, { 4, PURPLE },
    { 5, 0xe07f7f }, { 6, GREEN }, { 7, KEYWORD }, { 8, GREEN },
    { 9, 0xe07f7f }, { 10, GREEN }, { 11, 0xe07f7f }
};

#define C_KW "auto break case const continue default do else enum extern for " \
    "goto if inline register restrict return sizeof static struct switch " \
    "typedef union volatile while"
#define C_TYPES "void char short int long float double signed unsigned bool " \
    "size_t ssize_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t " \
    "int64_t uint8_t uint16_t uint32_t uint64_t FILE"

static const AxyneSyntaxLanguage languages[] = {
    { "cpp", "cpp",
      { C_KW " class namespace new delete template typename this public "
        "private protected virtual override final try catch throw using "
        "constexpr nullptr static_assert operator explicit mutable "
        "@interface @implementation @end @property @selector @protocol",
        C_TYPES " HWND HDC HMENU HFONT HBRUSH HICON HANDLE HINSTANCE LRESULT "
        "WPARAM LPARAM UINT DWORD WORD BYTE BOOL WCHAR LPCSTR LPCWSTR LPSTR "
        "LPWSTR COLORREF RECT POINT SIZE id NSString NSInteger NSUInteger "
        "BOOL SEL IMP",
        NULL, "NULL nullptr true false TRUE FALSE YES NO nil" },
      STYLES(cpp_styles) },
    { "java", "cpp",
      { "abstract assert break case catch class const continue default do "
        "else enum extends final finally for goto if implements import "
        "instanceof interface native new package private protected public "
        "return static strictfp super switch synchronized this throw throws "
        "transient try var volatile while record sealed permits yield",
        "boolean byte char double float int long short void String Object "
        "Integer Long Double Boolean List Map Set",
        NULL, "null true false" },
      STYLES(cpp_styles) },
    { "javascript", "cpp",
      { "async await break case catch class const continue debugger default "
        "delete do else export extends finally for function if import in "
        "instanceof let new of return static super switch this throw try "
        "typeof var void while with yield",
        "Array Object String Number Boolean Map Set Promise Symbol Date "
        "RegExp Error JSON Math console",
        NULL, "null undefined true false NaN Infinity" },
      STYLES(cpp_styles) },
    { "typescript", "cpp",
      { "abstract as async await break case catch class const continue "
        "debugger declare default delete do else enum export extends "
        "finally for from function if implements import in instanceof "
        "interface keyof let namespace new of private protected public "
        "readonly return static super switch this throw try type typeof var "
        "void while yield",
        "any boolean never number object string symbol unknown void Array "
        "Record Partial Promise Map Set",
        NULL, "null undefined true false NaN Infinity" },
      STYLES(cpp_styles) },
    { "swift", "cpp",
      { "actor as associatedtype async await break case catch class continue "
        "default defer deinit do else enum extension fallthrough fileprivate "
        "for func guard if import in indirect init inout internal is let "
        "mutating nonmutating open operator override private protocol public "
        "repeat rethrows return self static struct subscript super switch "
        "throw throws try typealias var where while",
        "Int Int8 Int16 Int32 Int64 UInt Float Double Bool String Character "
        "Array Dictionary Set Optional Void Any",
        NULL, "nil true false" },
      STYLES(cpp_styles) },
    { "kotlin", "cpp",
      { "as break by catch class companion constructor continue crossinline "
        "data do dynamic else enum external fun get final finally for if "
        "import in infix init inline inner interface internal is lateinit "
        "noinline object open operator out override package private "
        "protected public reified return sealed set super suspend tailrec "
        "this throw try typealias val value var vararg when where while "
        "abstract annotation const expect actual",
        "Int Long Short Byte Float Double Boolean Char String Unit Any "
        "Nothing Number Array List MutableList Map MutableMap Set "
        "MutableSet Pair Triple Sequence IntArray",
        NULL, "null true false" },
      STYLES(cpp_styles) },
    { "slint", "cpp",
      { "import export from component inherits global struct enum property "
        "callback function pure public private protected in out in-out "
        "animate states transitions for if else when return root parent "
        "self this init changed forward-focus as",
        "int float string bool length color brush image duration angle "
        "percent physical-length relative-font-size easing "
        "Window Dialog Rectangle Text Image TouchArea Button CheckBox "
        "LineEdit TextInput Flickable VerticalLayout HorizontalLayout "
        "GridLayout FocusScope Path PopupWindow Rectangle ComboBox Slider "
        "Switch ScrollView ListView StandardButton",
        NULL, "true false" },
      STYLES(cpp_styles) },
    { "go", "cpp",
      { "break case chan const continue default defer else fallthrough for "
        "func go goto if import interface map package range return select "
        "struct switch type var",
        "bool byte complex64 complex128 error float32 float64 int int8 int16 "
        "int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr any",
        NULL, "nil true false iota" },
      STYLES(cpp_styles) },
    { "rust", "cpp",
      { "as async await break const continue crate dyn else enum extern fn "
        "for if impl in let loop match mod move mut pub ref return self "
        "static struct super trait type unsafe use where while",
        "bool char f32 f64 i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 "
        "usize str String Vec Option Result Box Self",
        NULL, "true false None Some Ok Err" },
      STYLES(cpp_styles) },
    { "python", "python",
      { "and as assert async await break class continue def del elif else "
        "except finally for from global if import in is lambda nonlocal not "
        "or pass raise return try while with yield match case",
        "True False None self cls int str float bool list dict set tuple "
        "bytes object print len range",
        NULL, NULL },
      STYLES(python_styles) },
    { "json", "json",
      { "true false null", NULL, NULL, NULL }, STYLES(json_styles) },
    { "css", "css",
      { "color background background-color border margin padding font "
        "font-family font-size font-weight display position width height "
        "top left right bottom flex grid align-items justify-content "
        "overflow opacity z-index transition transform content cursor",
        NULL, NULL, NULL },
      STYLES(css_styles) },
    { "html", "hypertext",
      { "a abbr article aside body br button canvas div em footer form h1 "
        "h2 h3 h4 h5 h6 head header html i img input label li link main meta "
        "nav ol p pre script section select span strong style table tbody td "
        "textarea th thead title tr ul video !doctype class id href src",
        "async await break case catch class const continue default delete do "
        "else for function if in let new return switch this throw try typeof "
        "var while null true false undefined",
        NULL, NULL },
      STYLES(html_styles) },
    { "xml", "xml", { NULL, NULL, NULL, NULL }, STYLES(html_styles) },
    { "bash", "bash",
      { "if then elif else fi for while until do done case esac in function "
        "select time return exit break continue export local readonly unset "
        "source alias echo cd set shift trap eval exec test",
        NULL, NULL, NULL },
      STYLES(bash_styles) },
    { "markdown", "markdown", { NULL, NULL, NULL, NULL }, STYLES(markdown_styles) },
    { "cmake", "cmake",
      { "add_custom_command add_custom_target add_definitions add_executable "
        "add_library add_subdirectory add_test cmake_minimum_required "
        "configure_file enable_testing endfunction endif endforeach "
        "endmacro endwhile else elseif foreach function if include "
        "install list macro message option project set string target_compile_definitions "
        "target_compile_options target_include_directories target_link_libraries "
        "target_sources while find_package find_library file get_filename_component "
        "set_target_properties set_tests_properties return unset",
        "PUBLIC PRIVATE INTERFACE STATIC SHARED MODULE REQUIRED COMPONENTS "
        "STATUS FATAL_ERROR WARNING ON OFF TRUE FALSE NOT AND OR DEFINED "
        "EXISTS STREQUAL MATCHES COMMAND PROPERTIES DESTINATION",
        NULL, NULL },
      STYLES(cmake_styles) },
    { "yaml", "yaml",
      { "true false yes no on off null", NULL, NULL, NULL }, STYLES(yaml_styles) },
    { "diff", "diff", { NULL, NULL, NULL, NULL }, STYLES(diff_styles) },
    { "text", "null", { NULL, NULL, NULL, NULL }, NULL, 0 }
};

typedef struct ExtensionMap {
    const char *extension; /* without the dot, lower case */
    const char *id;
} ExtensionMap;

static const ExtensionMap extensions[] = {
    { "c", "cpp" }, { "h", "cpp" }, { "cc", "cpp" }, { "cpp", "cpp" },
    { "cxx", "cpp" }, { "hpp", "cpp" }, { "hh", "cpp" }, { "hxx", "cpp" },
    { "m", "cpp" }, { "mm", "cpp" }, { "inc", "cpp" }, { "rc", "cpp" },
    { "java", "java" },
    { "js", "javascript" }, { "jsx", "javascript" }, { "mjs", "javascript" },
    { "cjs", "javascript" },
    { "ts", "typescript" }, { "tsx", "typescript" }, { "mts", "typescript" },
    { "kt", "kotlin" }, { "kts", "kotlin" }, { "slint", "slint" },
    { "swift", "swift" }, { "go", "go" }, { "rs", "rust" },
    { "py", "python" }, { "pyw", "python" },
    { "json", "json" }, { "jsonc", "json" },
    { "css", "css" },
    { "html", "html" }, { "htm", "html" },
    { "xml", "xml" }, { "plist", "xml" }, { "svg", "xml" }, { "xib", "xml" },
    { "sh", "bash" }, { "bash", "bash" }, { "zsh", "bash" },
    { "md", "markdown" }, { "markdown", "markdown" },
    { "cmake", "cmake" },
    { "yml", "yaml" }, { "yaml", "yaml" },
    { "diff", "diff" }, { "patch", "diff" },
    { "txt", "text" }
};

static int equal_nocase(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

const AxyneSyntaxLanguage *axyne_syntax_by_id(const char *id)
{
    size_t i;
    if (id == NULL) return NULL;
    for (i = 0; i < sizeof(languages) / sizeof(languages[0]); ++i)
        if (strcmp(languages[i].id, id) == 0) return &languages[i];
    return NULL;
}

const AxyneSyntaxLanguage *axyne_syntax_for_path(const char *path)
{
    const char *base = path;
    const char *p;
    const char *dot;
    size_t i;
    if (path == NULL || path[0] == '\0') return axyne_syntax_by_id("cpp");
    for (p = path; *p != '\0'; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    if (equal_nocase(base, "CMakeLists.txt")) return axyne_syntax_by_id("cmake");
    dot = strrchr(base, '.');
    if (dot == NULL || dot[1] == '\0') return axyne_syntax_by_id("text");
    for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
        if (equal_nocase(dot + 1, extensions[i].extension))
            return axyne_syntax_by_id(extensions[i].id);
    return axyne_syntax_by_id("text");
}
