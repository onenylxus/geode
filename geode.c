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

//// Defines ////

#define VERSION "0.1.0"
#define TAB_STOP 2
#define QUIT_CONFIRM 1

#define CTRL_KEY(key) ((key) & 0x1f)
#define ABUF_INIT {NULL, 0}

enum editorKeys {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_UP,
  ARROW_RIGHT,
  ARROW_DOWN,
  DELETE,
  HOME,
  END,
  PAGE_UP,
  PAGE_DOWN
};

//// Variables ////

typedef struct erow {
  int size;
  int rsize;
  char* chars;
  char* render;
} erow;

struct config {
  int cx, cy;
  int rx;
  int dx, dy;
  int rows, cols;
  int nrows;
  erow* row;
  char* filename;
  int dirty;
  char message[80];
  time_t time;
  struct termios origin;
};
struct config E;

struct abuf {
  char* b;
  int len;
};

//// Prototypes ////

void setStatusMessage(const char *fmt, ...);
void refreshScreen();
char* prompt(char* prompt);

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

// Read key from user input
int readKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
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

//// Row ////

// Convert character index to render index
int characterToRender(erow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
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
}

// Insert row
void insertRow(int at, char* s, size_t len) {
  if (at < 0 || at > E.nrows) return;
  E.row = realloc(E.row, sizeof(erow) * (E.nrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.nrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  updateRow(&E.row[at]);

  E.nrows++;
  E.dirty++;
}

// Free row
void freeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

// Delete row
void deleteRow(int at) {
  if (at < 0 || at >= E.nrows) return;
  freeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (--E.nrows - at));
  E.dirty++;
}

// Insert character to row
void rowInsertCharacter(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], ++row->size - at);
  row->chars[at] = c;
  updateRow(row);
  E.dirty++;
}

// Append string
void appendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  updateRow(row);
  E.dirty++;
}

// Delete character from row
void rowDeleteCharacter(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size-- - at);
  updateRow(row);
  E.dirty++;
}

//// Editor ////
void insertCharacter(int c) {
  if (E.cy == E.nrows) insertRow(E.nrows, "", 0);
  rowInsertCharacter(&E.row[E.cy], E.cx++, c);
}

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

// Open file
void openFile(char* filename) {
  free(E.filename);
  E.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
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
    E.filename = prompt("Save as: %s (Esc to cancel)");
    if (E.filename == NULL) {
      setStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char* buf = stringify(&len);

  int file = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (file != -1) {
    if (ftruncate(file, len) != -1 && write(file, buf, len) == len) {
      close(file);
      free(buf);
      E.dirty = 0;
      setStatusMessage("%d bytes written to disk", len);
      return;
    }
    close(file);
  }
  free(buf);
  setStatusMessage("Cannot save! I/O error: %s", strerror(errno));
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
      appendBuffer(ab, &E.row[filerow].render[E.dx], len);
    }

    appendBuffer(ab, "\x1b[K", 3);
    appendBuffer(ab, "\r\n", 2);
  }
}

// Draw status bar
void drawStatusBar(struct abuf *ab) {
  appendBuffer(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[untitled]", E.nrows, E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.nrows);
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
void drawMessageBar(struct abuf *ab) {
  appendBuffer(ab, "\x1b[K", 3);
  int len = strlen(E.message);
  if (len > E.cols) len = E.cols;
  if (len && time(NULL) - E.time < 5) appendBuffer(ab, E.message, len);
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
  appendBuffer(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  freeBuffer(&ab);
}

// Set status message
void setStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.message, sizeof(E.message), fmt, ap);
  va_end(ap);
  E.time = time(NULL);
}

//// Input ////

// Prompt user
char* prompt(char* prompt) {
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
      free(buf);
      return NULL;
    } else if (c == '\r' && len != 0) {
      setStatusMessage("");
      return buf;
    } else if (!iscntrl(c) && c < 128) {
      if (len == size - 1) {
        size *= 2;
        buf = realloc(buf, size);
      }
      buf[len++] = c;
      buf[len] = '\0';
    }
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
}

// Process key
void processKey() {
  static int qt = QUIT_CONFIRM;
  int c = readKey();

  switch (c) {
    // [A][D][S][W] move cursor
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

    // Deletions
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DELETE:
      if (c == DELETE) moveCursor(ARROW_RIGHT);
      deleteCharacter();
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
  E.message[0] = '\0';
  E.time = 0;

  if (getWindowSize(&E.rows, &E.cols) == -1) throw("getWindowSize");
  E.rows -= 2;
}

// Main function
int main(int argc, char* argv[]) {
  // Initialize
  enableRawMode();
  setupEditor();
  if (argc >= 2) openFile(argv[1]);
  setStatusMessage("HELP: Ctrl-Q = quit | Ctrl-S = save");

  // Iterate loop
  while (1) {
    refreshScreen();
    processKey();
  }

  // Return
  return 0;
}
