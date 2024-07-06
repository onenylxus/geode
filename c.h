#pragma once

// Extensions
char* HL_C_extensions[] = {
  ".c",
  ".h",
  NULL
};

// Keywords
char* HL_C_keywords[] = {
  "#include",
  "#pragma",
  "#define",
  "#undef",
  "#ifdef",
  "#ifndef",
  "#endif",
  "#error",

  "auto",
  "break",
  "case",
  "continue",
  "default",
  "do",
  "else",
  "enum",
  "extern",
  "for",
  "goto",
  "if",
  "register",
  "return",
  "sizeof",
  "static",
  "struct",
  "switch",
  "typedef",
  "union",
  "volatile",
  "while",
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
  NULL
};

// Operators
char* HL_C_operators[] = {
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
#define HL_C_slCommentStart "//"
#define HL_C_mlCommentStart "/*"
#define HL_C_mlCommentEnd   "*/"

// Database
#define HLDB_C {\
  "c",\
  HL_C_extensions,\
  HL_C_keywords,\
  HL_C_operators,\
  HL_C_slCommentStart,\
  HL_C_mlCommentStart,\
  HL_C_mlCommentEnd,\
  HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS\
}
