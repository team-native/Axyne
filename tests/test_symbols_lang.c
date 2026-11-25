#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/symbols.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static char kind_letter(AxyneSymbolKind kind)
{
    switch (kind) {
    case AXYNE_SYMBOL_FUNCTION: return 'f';
    case AXYNE_SYMBOL_MACRO: return '#';
    case AXYNE_SYMBOL_TYPE: return 'T';
    case AXYNE_SYMBOL_VARIABLE: return 'v';
    case AXYNE_SYMBOL_METHOD: return 'm';
    case AXYNE_SYMBOL_CONSTANT: return 'c';
    }
    return '?';
}

/* Scans `text` as file `name` and compares "kind name@line" joined by spaces. */
static int expect(const char *name, const char *text, const char *expected)
{
    AxyneSymbolList list = {0};
    char actual[1024];
    size_t used = 0, i;
    AxyneStatus status = axyne_symbols_scan_file(name, text, strlen(text), &list, NULL);
    if (status != AXYNE_STATUS_OK) {
        fprintf(stderr, "FAIL %s: scan status %d\n", name, (int)status);
        return 0;
    }
    actual[0] = '\0';
    for (i = 0; i < list.count; ++i) {
        int n = snprintf(actual + used, sizeof(actual) - used, "%s%c %s@%zu",
                         i == 0 ? "" : " ", kind_letter(list.items[i].kind),
                         list.items[i].name, list.items[i].line);
        if (n < 0 || (size_t)n >= sizeof(actual) - used) break;
        used += (size_t)n;
    }
    axyne_symbols_destroy(&list);
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL %s\n  expected: %s\n  actual:   %s\n", name, expected, actual);
        return 0;
    }
    return 1;
}

static int test_javascript(void)
{
    CHECK(expect("a.js",
        "import x from 'y';\n"                       /* 1 */
        "const MAX = 10;\n"                          /* 2 */
        "let counter = 0;\n"                         /* 3 */
        "const add = (a, b) => {\n"                  /* 4 */
        "  const inner = 1;\n"
        "  return a + b;\n"
        "};\n"
        "export function run(opts) {\n"              /* 8 */
        "  function nested() {}\n"
        "  return `${opts}`;\n"
        "}\n"
        "// function commented() {}\n"
        "const text = \"function fake() {}\";\n"     /* 13 */
        "export default class Widget extends Base {\n" /* 14 */
        "  static create() { return 1; }\n"          /* 15 */
        "  async load(url) { await 1; }\n"           /* 16 */
        "  get size() { return 1; }\n"               /* 17 */
        "  #hidden() {}\n"                           /* 18 */
        "  handler = () => {};\n"                    /* 19 */
        "  value = 3;\n"
        "}\n"
        "module.exports = { run };\n",
        "c MAX@2 v counter@3 f add@4 f run@8 c text@13 T Widget@14 m create@15 "
        "m load@16 m size@17 m #hidden@18 m handler@19"));
    CHECK(expect("b.mjs", "function a() {}\nfunction b()\n{\n}\n", "f a@1 f b@2"));
    return 1;
}

static int test_typescript(void)
{
    CHECK(expect("a.ts",
        "export interface Options { a: number; }\n"           /* 1 */
        "export type Id = string | number;\n"                  /* 2 */
        "export enum Mode { A, B }\n"                          /* 3 */
        "export abstract class Repo<T> implements Store {\n"   /* 4 */
        "  private cache = new Map<string, T>();\n"
        "  constructor(private readonly db: Db) {}\n"          /* 6 */
        "  public async find(id: Id): Promise<T> { return 1 as any; }\n" /* 7 */
        "  abstract remove(id: Id): void;\n"                   /* 8 */
        "}\n"
        "namespace Util {\n"                                   /* 10 */
        "  export function helper() {}\n"                      /* 11 */
        "}\n",
        "T Options@1 T Id@2 T Mode@3 T Repo@4 m constructor@6 m find@7 m remove@8 "
        "T Util@10 f helper@11"));
    return 1;
}

static int test_python(void)
{
    CHECK(expect("a.py",
        "import os\n"                                  /* 1 */
        "MAX_SIZE = 10\n"                              /* 2 */
        "name: str = 'x'\n"                            /* 3 */
        "counter = 0\n"                                /* 4 */
        "\n"
        "def helper(a, b):\n"                          /* 6 */
        "    local = 1\n"
        "    def inner():\n"
        "        pass\n"
        "    return local\n"
        "\n"
        "async def fetch():\n"                         /* 12 */
        "    pass\n"
        "\n"
        "@decorator\n"
        "class Service(Base):\n"                       /* 16 */
        "    attr = 1\n"
        "    def __init__(self):\n"                    /* 18 */
        "        self.x = 1\n"
        "    async def run(self):\n"                   /* 20 */
        "        pass\n"
        "    class Meta:\n"                            /* 22 */
        "        pass\n"
        "\n"
        "TEXT = \"\"\"\n"                              /* 25 */
        "def not_real():\n"
        "\"\"\"\n"
        "def after():\n",                              /* 28 */
        "c MAX_SIZE@2 v name@3 v counter@4 f helper@6 f fetch@12 T Service@16 "
        "m __init__@18 m run@20 T Meta@22 c TEXT@25 f after@28"));
    return 1;
}

static int test_go(void)
{
    CHECK(expect("a.go",
        "package main\n"
        "import \"fmt\"\n"
        "const Pi = 3.14\n"                            /* 3 */
        "const (\n"
        "\tA = 1\n"                                    /* 5 */
        "\tB\n"                                        /* 6 */
        ")\n"
        "var global int\n"                             /* 8 */
        "type Point struct {\n"                        /* 9 */
        "\tX, Y int\n"
        "}\n"
        "type Shape interface {\n"                     /* 12 */
        "\tArea() float64\n"
        "}\n"
        "type Alias = int\n"                           /* 15 */
        "func main() {\n"                              /* 16 */
        "\tfmt.Println(\"hi\")\n"
        "}\n"
        "func (p *Point) Move(dx int) {\n"             /* 19 */
        "}\n",
        "c Pi@3 c A@5 c B@6 v global@8 T Point@9 T Shape@12 T Alias@15 f main@16 m Move@19"));
    return 1;
}

static int test_rust(void)
{
    CHECK(expect("a.rs",
        "use std::fmt;\n"
        "pub const LIMIT: usize = 4;\n"                /* 2 */
        "static mut COUNT: u32 = 0;\n"                 /* 3 */
        "#[derive(Debug)]\n"
        "pub struct Point { x: i32 }\n"                /* 5 */
        "pub enum Mode { A, B }\n"                     /* 6 */
        "pub trait Shape {\n"                          /* 7 */
        "    fn area(&self) -> f64;\n"                 /* 8 */
        "}\n"
        "impl Point {\n"
        "    pub fn new() -> Self { Point { x: 0 } }\n" /* 11 */
        "    fn inner<'a>(&'a self) -> &'a str { \"x\" }\n" /* 12 */
        "}\n"
        "pub(crate) fn run() { let c = 'x'; let r = r#\"fn fake() {}\"#; }\n" /* 14 */
        "pub const fn make() -> u8 { 0 }\n"            /* 15 */
        "mod inner {\n"                                /* 16 */
        "    pub fn nested() {}\n"                     /* 17 */
        "}\n"
        "macro_rules! square { ($x:expr) => { $x * $x }; }\n" /* 19 */
        "type Result<T> = std::result::Result<T, E>;\n", /* 20 */
        "c LIMIT@2 c COUNT@3 T Point@5 T Mode@6 T Shape@7 m area@8 m new@11 m inner@12 "
        "f run@14 f make@15 T inner@16 f nested@17 # square@19 T Result@20"));
    return 1;
}

static int test_java(void)
{
    CHECK(expect("A.java",
        "package demo;\n"
        "import java.util.List;\n"
        "public class Account extends Base {\n"        /* 3 */
        "    public static final int MAX = 5;\n"       /* 4 */
        "    private int balance = compute();\n"
        "    @Override\n"
        "    public String toString() { return \"x\"; }\n" /* 7 */
        "    public Account(int start) { this.balance = start; }\n" /* 8 */
        "    @SuppressWarnings(\"unchecked\") public <T> T get(Class<T> k) {\n" /* 9 */
        "        if (k == null) { return null; }\n"
        "        return null;\n"
        "    }\n"
        "    interface Visitor { void visit(); }\n"    /* 13 */
        "}\n"
        "enum Color { RED, GREEN }\n",                 /* 15 */
        "T Account@3 c MAX@4 m toString@7 m Account@8 m get@9 T Visitor@13 m visit@13 T Color@15"));
    return 1;
}

static int test_kotlin(void)
{
    CHECK(expect("a.kt",
        "package demo\n"
        "import kotlin.math.max\n"
        "const val LIMIT = 5\n"                        /* 3 */
        "val name = \"x\"\n"                           /* 4 */
        "var counter = 0\n"                            /* 5 */
        "fun top(a: Int): Int = a + 1\n"               /* 6 */
        "data class User(val id: Int)\n"               /* 7 */
        "open class Service(private val repo: Repo) {\n" /* 8 */
        "    val cache = mutableMapOf<String, Int>()\n"
        "    fun load(id: Int): User {\n"              /* 10 */
        "        val local = 1\n"
        "        return User(id)\n"
        "    }\n"
        "    override fun toString() = \"s\"\n"        /* 14 */
        "    companion object {\n"
        "        fun create() = Service(Repo())\n"     /* 16 */
        "    }\n"
        "}\n"
        "object Registry\n"                            /* 19 */
        "fun String.shout(): String { return this }\n", /* 20 */
        "c LIMIT@3 v name@4 v counter@5 f top@6 T User@7 T Service@8 m load@10 m toString@14 "
        "m create@16 T Registry@19 f shout@20"));
    return 1;
}

static int test_swift(void)
{
    CHECK(expect("a.swift",
        "import Foundation\n"
        "let limit = 5\n"                              /* 2 */
        "var counter = 0\n"                            /* 3 */
        "typealias Id = Int\n"                         /* 4 */
        "func top(_ a: Int) -> Int { return a }\n"     /* 5 */
        "public struct Point {\n"                      /* 6 */
        "    var x: Int\n"
        "    mutating func move(by dx: Int) { x += dx }\n" /* 8 */
        "    init(x: Int) { self.x = x }\n"            /* 9 */
        "    static func origin() -> Point { Point(x: 0) }\n" /* 10 */
        "}\n"
        "protocol Drawable {\n"                        /* 12 */
        "    func draw()\n"                            /* 13 */
        "}\n"
        "extension Point: Drawable {\n"                /* 15 */
        "    func draw() {}\n"                         /* 16 */
        "}\n"
        "class Box\n"                                  /* 18 */
        "{\n"
        "    class func make() -> Box { Box() }\n"     /* 20 */
        "}\n",
        "c limit@2 v counter@3 T Id@4 f top@5 T Point@6 m move@8 m init@9 m origin@10 "
        "T Drawable@12 m draw@13 T Point@15 m draw@16 T Box@18 m make@20"));
    return 1;
}

static int test_slint(void)
{
    CHECK(expect("a.slint",
        "import { Button } from \"std-widgets.slint\";\n"
        "export struct Item { name: string }\n"        /* 2 */
        "export enum Mode { a, b }\n"                  /* 3 */
        "export global Theme {\n"                      /* 4 */
        "    in-out property <color> accent: red;\n"
        "}\n"
        "export component App inherits Window {\n"     /* 7 */
        "    callback clicked();\n"
        "    pure function total() -> int { return 1; }\n" /* 9 */
        "    Rectangle {\n"
        "        Button { clicked => { root.clicked(); } }\n"
        "    }\n"
        "}\n",
        "T Item@2 T Mode@3 T Theme@4 T App@7 m total@9"));
    return 1;
}

static int test_edges(void)
{
    AxyneSymbolList list = {0};
    CHECK(axyne_symbols_supports_file("x.PY") && axyne_symbols_supports_file("dir\\a.Kt") &&
          axyne_symbols_supports_file("a.tsx") && axyne_symbols_supports_file("a.c") &&
          !axyne_symbols_supports_file("a.txt") && !axyne_symbols_supports_file("Makefile") &&
          !axyne_symbols_supports_file(NULL));
    CHECK(axyne_symbols_scan_file("a.txt", "x", 1, &list, NULL) == AXYNE_STATUS_UNSUPPORTED);
    CHECK(axyne_symbols_scan_file("a.js", "", 0, &list, NULL) == AXYNE_STATUS_OK && list.count == 0);
    axyne_symbols_destroy(&list);
    /* C files still use the C scanner. */
    CHECK(expect("a.c", "int main(void)\n{\n return 0;\n}\n", "f main@1"));
    /* CRLF and a missing final newline. */
    CHECK(expect("a.py", "def a():\r\n    pass\r\ndef b():\r\n    pass", "f a@1 f b@3"));
    CHECK(expect("a.go", "func a() {\r\n}\r\nfunc b() {}", "f a@1 f b@3"));
    /* Unterminated string / comment do not read past the end. */
    CHECK(expect("a.js", "function a() {}\nconst s = \"unterminated", "f a@1 c s@2"));
    CHECK(expect("a.rs", "fn a() {}\n/* open comment fn b() {}", "f a@1"));
    /* Reads stay inside the buffer for a lone `b` / `async `. */
    CHECK(expect("a.rs", "b", ""));
    CHECK(expect("a.py", "async ", ""));
    /* Callbacks are not function initializers; module.exports is not a module. */
    CHECK(expect("a.js", "const names = users.map(u => u.name);\nconst f = async (a) => a;\n"
                         "const g = x => x;\n", "c names@1 f f@2 f g@3"));
    CHECK(expect("a.js", "module.exports = { run };\nfunction after() {}\n", "f after@2"));
    /* Regex literals do not unbalance braces. */
    CHECK(expect("a.js", "function f(){ s.replace(/{/g,\"\") }\nfunction g(){}\n", "f f@1 f g@2"));
    /* Trailing `?` / `>` are type suffixes, not operators. */
    CHECK(expect("a.swift", "class A {\n var name: String?\n func foo() {}\n}\n", "T A@1 m foo@3"));
    CHECK(expect("a.ts", "type A = Array<string>\ntype B = number\n", "T A@1 T B@2"));
    /* Supertypes on the line after the header keep the body a type body. */
    CHECK(expect("a.kt", "class Foo\n    : Bar() {\n    fun m() {}\n}\n", "T Foo@1 m m@3"));
    CHECK(expect("A.java", "public non-sealed class A {\n void m() {}\n}\n", "T A@1 m m@2"));
    return 1;
}

int axyne_test_symbols_lang(const char *root)
{
    (void)root;
    CHECK(test_javascript());
    CHECK(test_typescript());
    CHECK(test_python());
    CHECK(test_go());
    CHECK(test_rust());
    CHECK(test_java());
    CHECK(test_kotlin());
    CHECK(test_swift());
    CHECK(test_slint());
    CHECK(test_edges());
    return 1;
}
