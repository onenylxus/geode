// Include
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// Variables
struct termios origin;

// Throw error
void throw(const char* s) {
  perror(s);
  exit(1);
}

// Disable raw mode
void disableRawMode() {
  // Set attribute back to original
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &origin) == -1) {
    throw("tcsetattr");
  }
}

// Enable raw mode
void enableRawMode() {
  // Get original attributes and disable raw mode on exit
  if (tcgetattr(STDIN_FILENO, &origin) == -1) {
    throw("tcgetattr");
  }
  atexit(disableRawMode);

  // Configure termios and replace original with new attributes
  struct termios raw = origin;
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

// Main function
int main() {
  // Enable raw mode on editor start
  enableRawMode();

  while (1) {
    // Read user input
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
      throw("read");
    }

    // Display user input keys
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }

    // Exit editor when user presses Q
    if (c == 'q') {
      break;
    }
  }

  // Return
  return 0;
}
