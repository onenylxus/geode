#pragma once

// Extensions
char* HL_CPP_extensions[] = {
  ".cc",
  ".cpp",
  ".hpp",
  NULL
};

// Keywords
char* HL_CPP_keywords[] = {
  "#include",
  "#pragma",
  "#define",
  "#undef",
  "#ifdef",
  "#ifndef",
  "#endif",
  "#error",

  "alignas",
  "alignof",
  "and",
  "and_eq",
  "asm",
  "auto",
  "bitand",
  "bitor",
  "break",
  "case",
  "class",
  "compl",
  "constexpr",
  "const_cast",
  "continue",
  "default",
  "delete",
  "deltype",
  "do",
  "dynamic_cast",
  "else",
  "enum",
  "explicit",
  "export",
  "extern",
  "false",
  "for",
  "friend",
  "goto",
  "if",
  "inline",
  "mutable",
  "namespace",
  "new",
  "noexcept",
  "not",
  "not_eq",
  "nullptr",
  "operator",
  "or",
  "or_eq",
  "private",
  "protected",
  "public",
  "register",
  "reinterpret_cast",
  "return",
  "sizeof",
  "static",
  "static_assert",
  "static_cast",
  "struct",
  "switch",
  "template",
  "this",
  "thread_local",
  "throw",
  "true",
  "try",
  "typedef",
  "typeid",
  "typename",
  "union",
  "virtual",
  "volatile",
  "while",
  "xor",
  "xor_eq",
  "NULL",

  "int|",
  "long|",
  "double|",
  "float|",
  "char|",
  "unsigned|",
  "signed|",
  "void|",
  "short|",
  "auto|",
  "const|",
  "bool|",
  NULL
};

// Operators
char* HL_CPP_operators[] = {
  "+",
  "-",
  "*",
  "/",
  "%",
  "=",
  "<",
  ">",
  "!",
  "&",
  "|",
  "^",
  "~",
  NULL
};

// Comments
#define HL_CPP_slCommentStart "//"
#define HL_CPP_mlCommentStart "/*"
#define HL_CPP_mlCommentEnd   "*/"

// Database
#define HLDB_CPP {\
  "cpp",\
  HL_CPP_extensions,\
  HL_CPP_keywords,\
  HL_CPP_operators,\
  HL_CPP_slCommentStart,\
  HL_CPP_mlCommentStart,\
  HL_CPP_mlCommentEnd,\
  HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS\
}
