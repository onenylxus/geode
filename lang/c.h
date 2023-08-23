// Extensions
char* HL_C_extensions[] = {
  ".c",
  ".h",
  ".cpp",
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

  "switch",
  "if",
  "while",
  "for",
  "break",
  "continue",
  "return",
  "else",
  "struct",
  "union",
  "typedef",
  "static",
  "enum",
  "class",
  "case",

  "int|",
  "long|",
  "double|",
  "float|",
  "char|",
  "unsigned|",
  "signed|",
  "void|",
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
