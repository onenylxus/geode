// Include //
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Defines //
#define VERSION "0.1.0"
#define CTRL_KEY(key) ((key) & 0x1f)
#define ABUF_INIT {NULL, 0}

enum editorKeys {
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

// Variables //
struct config {
  int rows, cols;
  int cx, cy;
  struct termios origin;
};
struct config E;

struct abuf {
  char* b;
  int len;
};

// Terminal //

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
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.origin) == -1) {
    throw("tcsetattr");
  }
}

// Enable raw mode
void enableRawMode() {
  // Get original attributes and disable raw mode on exit
  if (tcgetattr(STDIN_FILENO, &E.origin) == -1) {
    throw("tcgetattr");
  }
  atexit(disableRawMode);

  // Configure termios and replace original with new attributes
  struct termios raw = E.origin;
  raw.c_cflag |= (CS8);
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_oflag &= ~(OPOST);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    throw("tcsetattr");
  }
}

// Read key from user input
int readKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      throw("read");
    }
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      return '\x1b';
    }

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) {
          return '\x1b';
        }
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
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') {
    return -1;
  }
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
    return -1;
  }
  return 0;
}

// Get window size
int getWindowSize(int* rows, int* cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[9998", 12) != 12) {
      return getCursorPosition(rows, cols);
    }
    readKey();
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

// Buffer //

// Append buffer
void appendBuffer(struct abuf* ab, const char* s, int len) {
  char* n = realloc(ab->b, ab->len + len);
  if (n == NULL) {
    return;
  }
  memcpy(&n[ab->len], s, len);
  ab->b = n;
  ab->len += len;
}

// Free buffer
void freeBuffer(struct abuf* ab) {
  free(ab->b);
}

// Output //

// Draw editor layout
void drawLayout(struct abuf* ab) {
  for (int j = 0; j < E.rows; j++) {
    if (j == 0) {
      // Setup title
      char welcome[80];
      int len = snprintf(welcome, sizeof(welcome), "Geode: Minimal code editor -- version %s", VERSION);
      if (len > E.cols) {
        len = E.cols;
      }

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

    appendBuffer(ab, "\x1b[K", 3);
    if (j < E.rows - 1) {
      appendBuffer(ab, "\r\n", 2);
    }
  }
}

// Clear screen
void clearScreen() {
  struct abuf ab = ABUF_INIT;

  appendBuffer(&ab, "\x1b[?25l", 6);
  appendBuffer(&ab, "\x1b[H", 3);
  drawLayout(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  appendBuffer(&ab, buf, strlen(buf));
  appendBuffer(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  freeBuffer(&ab);
}

// Input //

// Move cursor
void moveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      }
      break;

    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;

    case ARROW_RIGHT:
      if (E.cx != E.cols - 1) {
        E.cx++;
      }
      break;

    case ARROW_DOWN:
      if (E.cy != E.rows - 1) {
        E.cy++;
      }
      break;
  }
}

// Process key
void processKey() {
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
      E.cx = c == HOME ? 0 : E.cols - 1;
      break;

    // [PageUp][PageDown] move cursor to top or bottom edges
    case PAGE_UP:
    case PAGE_DOWN:
      E.cy = c == PAGE_UP ? 0 : E.rows - 1;
      break;

    // [Ctrl+Q] exit editor
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  }
}

// Main //

// Setup editor
void setupEditor() {
  E.cx = 0;
  E.cy = 0;
  if (getWindowSize(&E.rows, &E.cols) == -1) {
    throw("getWindowSize");
  }
}

// Main function
int main() {
  // Initialize
  enableRawMode();
  setupEditor();

  // Iterate loop
  while (1) {
    clearScreen();
    processKey();
  }

  // Return
  return 0;
}
