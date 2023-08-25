//// Include ////

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "lang/c.h"
#include "lang/cpp.h"

//// Defines ////

#define VERSION "0.1.1"
#define TAB_STOP 2
#define QUIT_CONFIRM 1

#define CTRL_KEY(key) ((key) & 0x1f)
#define ABUF_INIT { NULL, 0 }
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

enum keys {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_UP,
  ARROW_RIGHT,
  ARROW_DOWN,
  INSERT,
  DELETE,
  HOME,
  END,
  PAGE_UP,
  PAGE_DOWN
};

enum highlights {
  HL_NORMAL,
  HL_COMMENT_SINGLE,
  HL_COMMENT_MULTIPLE,
  HL_KEYWORD_ACTUAL,
  HL_KEYWORD_COMMON,
  HL_OPERATOR,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

//// Variables ////

struct syntax {
  char* filetype;
  char** match;
  char** keywords;
  char** operators;
  char* slCommentStart;
  char* mlCommentStart;
  char* mlCommentEnd;
  int flags;
};

typedef struct erow {
  int index;
  int size;
  int rsize;
  char* chars;
  char* render;
  unsigned char* hl;
  int hlOpenComment;
} erow;

struct config {
  int cx, cy;
  int rx;
  int dx, dy;
  int rows, cols;
  int nrows;
  erow* row;
  char* filename;
  int cursor;
  int dirty;
  int insert;
  char message[80];
  time_t cursorTime;
  time_t messageTime;
  struct syntax* syntax;
  struct termios origin;
};
struct config E;

struct abuf {
  char* b;
  int len;
};

//// Filetypes ////

struct syntax HLDB[] = {
  {
    "c",
    HL_C_extensions,
    HL_C_keywords,
    HL_C_operators,
    HL_C_slCommentStart,
    HL_C_mlCommentStart,
    HL_C_mlCommentEnd,
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
  {
    "cpp",
    HL_CPP_extensions,
    HL_CPP_keywords,
    HL_CPP_operators,
    HL_CPP_slCommentStart,
    HL_CPP_mlCommentStart,
    HL_CPP_mlCommentEnd,
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  }
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

//// Prototypes ////

void setStatusMessage(const char* fmt, ...);
void refreshScreen();
void refreshConfig();
char* prompt(char* prompt, void (*callback)(char*, int));

//// Terminal ////

// Throw error
void throw(const char* s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

// Disable raw mode
void disableRawMode() {
  // Set attribute back to original
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.origin) == -1) throw("tcsetattr");
}

// Enable raw mode
void enableRawMode() {
  // Get original attributes and disable raw mode on exit
  if (tcgetattr(STDIN_FILENO, &E.origin) == -1) throw("tcgetattr");
  atexit(disableRawMode);

  // Configure termios and replace original with new attributes
  struct termios raw = E.origin;
  raw.c_cflag |= (CS8);
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_oflag &= ~(OPOST);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) throw("tcsetattr");
}

// Get time
struct tm* getTime() {
  time_t now;
  time(&now);
  return localtime(&now);
}

// Read key from user input
int readKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    refreshConfig();
    if (nread == -1 && errno != EAGAIN) throw("read");
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1 || read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME;
            case '2': return INSERT;
            case '3': return DELETE;
            case '4': return END;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME;
            case '8': return END;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'F': return END;
          case 'H': return HOME;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'F': return END;
        case 'H': return HOME;
      }
    }
    return '\x1b';
  }
  return c;
}

// Get cursor position
int getCursorPosition(int* rows, int* cols) {
  char buf[32];
  unsigned int i = 0;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1 || buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[' || sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

// Get window size
int getWindowSize(int* rows, int* cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[9998", 12) != 12) return getCursorPosition(rows, cols);
    readKey();
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

// Reset cursor
void resetCursor() {
  E.cursor = 1;
  E.cursorTime = time(NULL);
  refreshScreen();
}

//// Highlight ////

// Check separator
int isSeparator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

// Update syntax
void updateSyntax(erow* row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);
  if (E.syntax == NULL) return;

  char** keywords = E.syntax->keywords;
  char** operators = E.syntax->operators;
  char* scs = E.syntax->slCommentStart;
  char* mcs = E.syntax->mlCommentStart;
  char* mce = E.syntax->mlCommentEnd;
  int scslen = scs ? strlen(scs) : 0;
  int mcslen = mcs ? strlen(mcs) : 0;
  int mcelen = mce ? strlen(mce) : 0;

  int prevSep = 1;
  int inString = 0;
  int inComment = (row->index > 0 && E.row[row->index - 1].hlOpenComment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prevHl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scslen && !inString && !inComment && !strncmp(&row->render[i], scs, scslen)) {
      memset(&row->hl[i], HL_COMMENT_SINGLE, row->rsize - i);
      break;
    }

    if (mcslen && mcelen && !inString) {
      if (inComment) {
        row->hl[i] = HL_COMMENT_MULTIPLE;
        if (!strncmp(&row->render[i], mce, mcelen)) {
          memset(&row->hl[i], HL_COMMENT_MULTIPLE, mcelen);
          i += mcelen;
          inComment = 0;
          prevSep = 0;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcslen)) {
        memset(&row->hl[i], HL_COMMENT_MULTIPLE, mcslen);
        i += mcslen;
        inComment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (inString) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == inString) inString = 0;
        i++;
        prevSep = 1;
        continue;
      } else if (c == '"' || c == '\'') {
        inString = c;
        row->hl[i] = HL_STRING;
        i++;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prevSep || prevHl == HL_NUMBER)) || (c == '.' && prevHl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prevSep = 0;
        continue;
      }
    }

    if (prevSep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kwc = keywords[j][klen - 1] == '|';
        if (kwc) klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) && isSeparator(row->render[i + klen])) {
          memset(&row->hl[i], kwc ? HL_KEYWORD_COMMON : HL_KEYWORD_ACTUAL, klen);
          i += klen;
          break;
        }
      }

      if (keywords[j] != NULL) {
        prevSep = 0;
        continue;
      }
    }

    {
      int j;
      for (j = 0; operators[j]; j++) {
        int olen = strlen(operators[j]);
        if (!strncmp(&row->render[i], operators[j], olen)) {
          memset(&row->hl[i], HL_OPERATOR, olen);
          i += olen - 1;
          break;
        }
      }
    }

    prevSep = isSeparator(c);
    i++;
  }

  int changed = (row->hlOpenComment != inComment);
  row->hlOpenComment = inComment;
  if (changed && row->index + 1 < E.nrows) updateSyntax(&E.row[row->index + 1]);
}

// Convert syntax to color
int syntaxToColor(int hl) {
  switch (hl) {
    case HL_NUMBER: return 33;
    case HL_KEYWORD_COMMON: return 31;
    case HL_KEYWORD_ACTUAL: return 35;
    case HL_OPERATOR: return 36;
    case HL_MATCH: return 34;
    case HL_STRING: return 32;
    case HL_COMMENT_SINGLE: return 30;
    case HL_COMMENT_MULTIPLE: return 30;
    default: return 37;
  }
}

// Select syntax highlight
void selectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL) return;

  char* ext = strrchr(E.filename, '.');
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct syntax* s = &HLDB[j];
    unsigned int i = 0;
    while (s->match[i]) {
      int isExt = s->match[i][0] == '.';
      if ((isExt && ext && !strcmp(ext, s->match[i])) || (!isExt && strstr(E.filename, s->match[i]))) {
        E.syntax = s;

        for (int fr = 0; fr < E.nrows; fr++) {
          updateSyntax(&E.row[fr]);
        }
        return;
      }
      i++;
    }
  }
}

//// Row ////

// Convert character index to render index
int characterToRender(erow* row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

// Convert render index to character index
int renderToCharacter(erow* row, int rx) {
  int cur = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t') cur += (TAB_STOP - 1) - (cur % TAB_STOP);
    cur++;
    if (cur > rx) return cx;
  }
  return cx;
}

// Update row
void updateRow(erow* row) {
  int tabs = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (TAB_STOP - 1) + 1);

  int i = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[i++] = ' ';
      while (i % TAB_STOP != 0) row->render[i++] = ' ';
    } else {
      row->render[i++] = row->chars[j];
    }
  }
  row->render[i] = '\0';
  row->rsize = i;
  updateSyntax(row);
}

// Insert row
void insertRow(int at, char* s, size_t len) {
  if (at < 0 || at > E.nrows) return;
  E.row = realloc(E.row, sizeof(erow) * (E.nrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.nrows - at));
  for (int j = at + 1; j <= E.nrows; j++) E.row[j].index++;

  E.row[at].index = at;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hlOpenComment = 0;
  updateRow(&E.row[at]);

  E.nrows++;
  E.dirty++;
}

// Free row
void freeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

// Delete row
void deleteRow(int at) {
  if (at < 0 || at >= E.nrows) return;
  freeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.nrows - at - 1));
  for (int j = at; j < E.nrows - 1; j++) E.row[j].index--;
  E.nrows--;
  E.dirty++;
}

// Insert character to row
void rowInsertCharacter(erow* row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  if (E.insert == 0 || at == row->size) {
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], ++row->size - at);
  }
  row->chars[at] = c;
  updateRow(row);
  E.dirty++;
}

// Append string
void appendString(erow* row, char* s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  updateRow(row);
  E.dirty++;
}

// Delete character from row
void rowDeleteCharacter(erow* row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size-- - at);
  updateRow(row);
  E.dirty++;
}

//// Editor ////

// Insert character
void insertCharacter(int c) {
  if (c == CTRL_KEY(c)) return;
  if (E.cy == E.nrows) insertRow(E.nrows, "", 0);
  rowInsertCharacter(&E.row[E.cy], E.cx++, c);
  resetCursor();
}

// Insert line
void insertLine() {
  if (E.cx == 0) {
    insertRow(E.cy, "", 0);
  } else {
    erow* row = &E.row[E.cy];
    insertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    updateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

// Delete character
void deleteCharacter() {
  if (E.cy == E.nrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow* row = &E.row[E.cy];
  if (E.cx > 0) {
    rowDeleteCharacter(row, --E.cx);
  } else {
    E.cx = E.row[E.cy - 1].size;
    appendString(&E.row[E.cy - 1], row->chars, row->size);
    deleteRow(E.cy--);
  }
  resetCursor();
}

//// File ////

// Stringify rows
char* stringify(int* len) {
  int size = 0;
  for (int j = 0; j < E.nrows; j++) size += E.row[j].size + 1;
  *len = size;

  char* buf = malloc(size);
  char* p = buf;
  for (int j = 0; j < E.nrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

// Display file size
char* displayFileSize() {
  int len;
  stringify(&len);

  double size = len;
  char* unit = "B";
  if (size >= 1000) { size /= 1024; unit = "KB"; }
  if (size >= 1000) { size /= 1024; unit = "MB"; }
  if (size >= 1000) { size /= 1024; unit = "GB"; }
  static char str[80];
  snprintf(str, sizeof(str), len < 1000 ? "%.0f %s" : "%.02f %s", size, unit);
  return str;
}

// Open file
void openFile(char* filename) {
  free(E.filename);
  E.filename = strdup(filename);
  selectSyntaxHighlight();

  FILE* fp = fopen(filename, "r");
  if (!fp) throw("fopen");

  char* line = NULL;
  size_t cap = 0;
  ssize_t len;
  while ((len = getline(&line, &cap, fp)) != -1) {
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    insertRow(E.nrows, line, len);
  }

  free(line);
  fclose(fp);
  E.dirty = 0;
}

// Save file
void saveFile() {
  if (E.filename == NULL) {
    E.filename = prompt("Save as: %s (Esc to cancel)", NULL);
    if (E.filename == NULL) {
      setStatusMessage("Save aborted");
      return;
    }
    selectSyntaxHighlight();
  }

  int len;
  char* buf = stringify(&len);

  int file = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (file != -1) {
    if (ftruncate(file, len) != -1 && write(file, buf, len) == len) {
      close(file);
      free(buf);
      E.dirty = 0;
      setStatusMessage("File saved successfully");
      return;
    }
    close(file);
  }
  free(buf);
  setStatusMessage("Cannot save! I/O error: %s", strerror(errno));
}

//// Find ////

// Find callback
void findCallback(char* query, int key) {
  static int lastMatch = -1;
  static int direction = 1;
  static int savedHlLine;
  static char* savedHl = NULL;

  if (savedHl) {
    memcpy(E.row[savedHlLine].hl, savedHl, E.row[savedHlLine].rsize);
    free(savedHl);
    savedHl = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    lastMatch = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    lastMatch = -1;
    direction = 1;
  }

  if (lastMatch == -1) direction = 1;
  int current = lastMatch;

  for (int i = 0; i < E.nrows; i++) {
    current += direction;
    if (current == -1) {
      current = E.nrows - 1;
    } else if (current == E.nrows) {
      current = 0;
    }
    erow* row = &E.row[current];
    char* match = strstr(row->render, query);
    if (match) {
      lastMatch = current;
      E.cy = current;
      E.cx = renderToCharacter(row, match - row->render);
      E.dy = E.nrows;

      savedHlLine = current;
      savedHl = malloc(row->rsize);
      memcpy(savedHl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

// Find query
void find() {
  int savedCx = E.cx;
  int savedCy = E.cy;
  int savedDx = E.dx;
  int savedDy = E.dy;

  char* query = prompt("Search: %s (Use Esc/Arrows/Enter)", findCallback);
  if (query) {
    free(query);
  } else {
    E.cx = savedCx;
    E.cy = savedCy;
    E.dx = savedDx;
    E.dy = savedDy;
  }
}

//// Buffer ////

// Append buffer
void appendBuffer(struct abuf* ab, const char* s, int len) {
  char* n = realloc(ab->b, ab->len + len);
  if (n == NULL) return;
  memcpy(&n[ab->len], s, len);
  ab->b = n;
  ab->len += len;
}

// Free buffer
void freeBuffer(struct abuf* ab) {
	free(ab->b);
}

//// Output ////

// Refresh config
void refreshConfig() {
  if (getTime()->tm_sec == 0) refreshScreen();
  if (time(NULL) - E.cursorTime > 1) {
    E.cursor = (E.cursor + 1) % 2;
    E.cursorTime = time(NULL);
    refreshScreen();
  }
}

// Set editor scroll
void scroll() {
  E.rx = 0;
  if (E.cy < E.nrows) E.rx = characterToRender(&E.row[E.cy], E.cx);

  if (E.rx < E.dx) E.dx = E.rx;
  if (E.rx >= E.dx + E.cols) E.dx = E.rx - E.cols + 1;
  if (E.cy < E.dy) E.dy = E.cy;
  if (E.cy >= E.dy + E.rows) E.dy = E.cy - E.rows + 1;
}

// Draw editor layout
void drawLayout(struct abuf* ab) {
  for (int j = 0; j < E.rows; j++) {
    int filerow = j + E.dy;
    if (filerow >= E.nrows) {
      if (E.nrows == 0 && j == E.rows / 3) {
        // Setup title
        char welcome[80];
        int len = snprintf(welcome, sizeof(welcome), "Geode: Minimal code editor -- version %s", VERSION);
        if (len > E.cols) len = E.cols;

        // Add padding to title
        int padding = (E.cols - len) / 2;
        if (padding) {
          appendBuffer(ab, "~", 1);
          padding--;
        }
        while (padding--) {
          appendBuffer(ab, " ", 1);
        }

        // Append
        appendBuffer(ab, welcome, len);
      } else {
        appendBuffer(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.dx;
      if (len < 0) len = 0;
      if (len > E.cols) len = E.cols;

      char* c = &E.row[filerow].render[E.dx];
      unsigned char* hl = &E.row[filerow].hl[E.dx];
      int currentColor = -1;
      for (int j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          appendBuffer(ab, "\x1b[7m", 4);
          appendBuffer(ab, &sym, 1);
          appendBuffer(ab, "\x1b[m", 3);
          if (currentColor != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", currentColor);
            appendBuffer(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (currentColor != -1) {
            appendBuffer(ab, "\x1b[39m", 5);
            currentColor = -1;
          }
          appendBuffer(ab, &c[j], 1);
        } else {
          int color = syntaxToColor(hl[j]);
          if (color != currentColor) {
            currentColor = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            appendBuffer(ab, buf, clen);
          }
          appendBuffer(ab, &c[j], 1);
        }
      }
      appendBuffer(ab, "\x1b[39m", 5);
    }

    appendBuffer(ab, "\x1b[K", 3);
    appendBuffer(ab, "\r\n", 2);
  }
}

// Draw status bar
void drawStatusBar(struct abuf* ab) {
  appendBuffer(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  char* dirty = E.dirty ? "(modified)" : "";
  char* ftype = E.syntax ? E.syntax->filetype : "*";
  char* fsize = displayFileSize();
  char* insert = E.insert ? "SUB" : "INS";
  struct tm* ti = getTime();
  int len = snprintf(status, sizeof(status), " %.20s - %d lines %s", E.filename ? E.filename : "[untitled]", E.nrows, dirty);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %s | %d:%d | %s | %02d:%02d ", ftype, fsize, E.cy + 1, E.cx + 1, insert, ti->tm_hour, ti->tm_min);

  if (len > E.cols) len = E.cols;
  appendBuffer(ab, status, len);
  while (len < E.cols) {
    if (E.cols - len == rlen) {
      appendBuffer(ab, rstatus, rlen);
      break;
    } else {
      appendBuffer(ab, " ", 1);
      len++;
    }
  }
  appendBuffer(ab, "\x1b[m", 3);
  appendBuffer(ab, "\r\n", 2);
}

// Draw message bar
void drawMessageBar(struct abuf* ab) {
  appendBuffer(ab, "\x1b[K", 3);
  int len = strlen(E.message);
  if (len > E.cols) len = E.cols;
  if (len && time(NULL) - E.messageTime < 5) appendBuffer(ab, E.message, len);
}

// Refresh screen
void refreshScreen() {
  struct abuf ab = ABUF_INIT;
  scroll();
  appendBuffer(&ab, "\x1b[?25l", 6);
  appendBuffer(&ab, "\x1b[H", 3);

  drawLayout(&ab);
  drawStatusBar(&ab);
  drawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.dy) + 1, (E.rx - E.dx) + 1);
  appendBuffer(&ab, buf, strlen(buf));
  appendBuffer(&ab, E.cursor ? "\x1b[?25h" : "\x1b[?25l", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  freeBuffer(&ab);
}

// Set status message
void setStatusMessage(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.message, sizeof(E.message), fmt, ap);
  va_end(ap);
  E.messageTime = time(NULL);
}

//// Input ////

// Prompt user
char* prompt(char* prompt, void (*callback)(char*, int)) {
  size_t size = 128;
  char* buf = malloc(size);
  size_t len = 0;
  buf[0] = '\0';

  while (1) {
    setStatusMessage(prompt, buf);
    refreshScreen();

    int c = readKey();
    if (c == DELETE || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (len != 0) buf[--len] = '\0';
    } else if (c == '\x1b') {
      setStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r' && len != 0) {
      setStatusMessage("");
      if (callback) callback(buf, c);
      return buf;
    } else if (!iscntrl(c) && c < 128) {
      if (len == size - 1) {
        size *= 2;
        buf = realloc(buf, size);
      }
      buf[len++] = c;
      buf[len] = '\0';
    }

    if (callback) callback(buf, c);
  }
}

// Move cursor
void moveCursor(int key) {
  erow* row = (E.cy >= E.nrows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;

    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;

    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;

    case ARROW_DOWN:
      if (E.cy < E.nrows) {
        E.cy++;
      }
      break;
  }

  row = (E.cy >= E.nrows) ? NULL : &E.row[E.cy];
  int len = row ? row->size : 0;
  if (E.cx > len) E.cx = len;
  resetCursor();
}

// Process key
void processKey() {
  static int qt = QUIT_CONFIRM;
  int c = readKey();

  switch (c) {
    // [←][↑][→][↓] move cursor
    case ARROW_LEFT:
    case ARROW_UP:
    case ARROW_RIGHT:
    case ARROW_DOWN:
      moveCursor(c);
      break;

    // [Home][End] move cursor to left or right edges
    case HOME:
    case END:
      E.cx = c == HOME ? 0 : E.row[E.cy].size;
      break;

    // [PageUp][PageDown] move cursor to top or bottom edges
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.dy;
        } else {
          E.cy = E.dy + E.rows - 1;
          if (E.cy > E.nrows) E.cy = E.nrows;
        }

        int times = E.rows;
        while (times--) moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    // Carriage return
    case '\r':
      insertLine();
      break;

    // Insert mode
    case INSERT:
      E.insert = (E.insert + 1) % 2;
      break;

    // Deletions
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DELETE:
      if (c == DELETE) moveCursor(ARROW_RIGHT);
      deleteCharacter();
      break;

    // [Ctrl-F] find query
    case CTRL_KEY('f'):
      find();
      break;

    // [Ctrl-Q] exit editor
    case CTRL_KEY('q'):
      if (E.dirty && qt > 0) {
        setStatusMessage("File has unsaved changes. Press Ctrl-Q again to quit.");
        qt--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    // [Ctrl+S] save file
    case CTRL_KEY('s'):
      saveFile();
      break;

    // Sequences
    case CTRL_KEY('l'):
    case '\x1b':
      break;

    // Default
    default:
      insertCharacter(c);
      break;
  }

  qt = QUIT_CONFIRM;
}

//// Main ////

// Setup editor
void setupEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.dx = 0;
  E.dy = 0;
  E.nrows = 0;
  E.row = NULL;
  E.filename = NULL;
  E.dirty = 0;
  E.insert = 0;
  E.message[0] = '\0';
  E.messageTime = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.rows, &E.cols) == -1) throw("getWindowSize");
  E.rows -= 2;
}

// Main function
int main(int argc, char* argv[]) {
  // Initialize
  enableRawMode();
  setupEditor();
  if (argc >= 2) openFile(argv[1]);
  setStatusMessage("HELP: Ctrl-F = find | Ctrl-H = backspace | Ctrl-Q = quit | Ctrl-S = save");

  // Iterate loop
  while (1) {
    refreshScreen();
    processKey();
  }

  // Return
  return 0;
}
