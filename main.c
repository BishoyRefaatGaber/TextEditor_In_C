/**
 * @file main.c
 * @author Bishoy Refaat Gaber (refaatbishoy455@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-10-08
 *
 * @copyright Copyright (c) 2024
 *
 */

// #define _DEFAULT_SOURCE
// #define _BSD_SOURCE
// #define _GNU_SOURCE

#include <asm-generic/ioctls.h>
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

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define BEDIT_VERSION "0.0.1"
#define BEDIT_TAB_STOP 8
#define CURSOR_XBEGIN 1
#define CURSOR_YBEGIN 3
#define BEDIT_QUIT_TIMES 1

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 200,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN

};
/*** data ***/

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig {

  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
};

struct editorConfig E;

char *editorPrompt(char *prompt);

/*** terminal ***/
void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {

  struct termios raw;
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  atexit(disableRawMode); /* Register a function to be called when `exit' is
                             called.  */

  raw = E.orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey() {
  int nread;
  unsigned char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b') {
    unsigned char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[0] == '[') {

      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
          case '7':
            return HOME_KEY;
            break;
          case '2':
            break;
          case '3':
            return DEL_KEY;
            break;
          case '4':
          case '8':
            return END_KEY;
            break;
          case '5':
            return PAGE_UP;
            break;
          case '6':
            return PAGE_DOWN;
            break;
          case '9':
            break;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == '0') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getWindowSize(int *rows, int *cols) {

  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j - 1] == '\t')
      rx += (BEDIT_TAB_STOP - 1) - (rx % BEDIT_TAB_STOP);
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row) {

  int tabs = 0;
  int j;

  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t')
      tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (BEDIT_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {

    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % BEDIT_TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;

  editorUpdateRow(&E.row[at]);
  E.numrows++;
  E.dirty++;
}

void editorInsertRow(int at, char *s, size_t len) {

  if (at < 0 || at > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;

  editorUpdateRow(&E.row[at]);
  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);

  // shif characters
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}
/*** editor operations ***/

void editorInsertChar(int c) {
  if (E.cy - CURSOR_YBEGIN == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy - CURSOR_YBEGIN], E.cx - CURSOR_XBEGIN, c);
  E.cx++;
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorInsertNewline() {
  if (E.cx - CURSOR_XBEGIN == 0) {
    editorInsertRow(E.cy - CURSOR_YBEGIN, "", 0);
  } else if (E.cx > CURSOR_XBEGIN &&
             E.cx - CURSOR_XBEGIN < E.row[E.cy - CURSOR_YBEGIN].size) {
    erow *row = &E.row[E.cy - CURSOR_YBEGIN];
    editorInsertRow(E.cy - CURSOR_YBEGIN + 1, &row->chars[E.cx - CURSOR_XBEGIN],
                    row->size + 1 - E.cx - CURSOR_XBEGIN);
    row = &E.row[E.cy - CURSOR_YBEGIN];
    row->size = E.cx - CURSOR_XBEGIN;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = CURSOR_XBEGIN;
}

void editorDelChar() {

  if (E.cy - CURSOR_YBEGIN == E.numrows)
    return;
  if (E.cx == CURSOR_XBEGIN && E.cy == CURSOR_YBEGIN)
    return;

  erow *row = &E.row[E.cy - CURSOR_YBEGIN];
  if (E.cx - CURSOR_XBEGIN > 0) {
    editorRowDelChar(row, E.cx - CURSOR_XBEGIN - 1);
    E.cx--;
  } else if (E.cx == CURSOR_XBEGIN) {

    E.cx = E.row[E.cy - CURSOR_YBEGIN - 1].size + CURSOR_XBEGIN;
    editorRowAppendString(&E.row[E.cy - CURSOR_YBEGIN - 1], row->chars,
                          row->size);
    editorDelRow(E.cy - CURSOR_YBEGIN);

    if (E.cy - E.rowoff > CURSOR_YBEGIN) {
      E.cy--;
    }
  }
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT                                                              \
  { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {

  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;

  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
  ab->b = NULL;
  ab->len = 0;
}
/*** output ***/

void editScroll() {

  E.rx = CURSOR_XBEGIN;
  if (E.cy - CURSOR_YBEGIN < E.numrows) {

    E.rx = editorRowCxToRx(&E.row[E.cy - CURSOR_YBEGIN], E.cx); //  <-----
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }

  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editDrawRows(struct abuf *ab) {

  int y;
  char welcome[50];
  char welcomeUP[] = "+--------------------------------------+";
  int welcomeUPlen = sizeof(welcomeUP);
  int welcomelen =
      snprintf(welcome, sizeof(welcome), "  BeDIT editor         --version %s",
               BEDIT_VERSION);

  if (welcomelen > E.screencols) {

    welcomelen = E.screencols - 2;
    memcpy(welcome, welcome, welcomelen);
    memcpy(welcomeUP, welcomeUP, welcomelen);
  }

  int cspace = (E.screencols - welcomeUPlen) / 2;
  char *space = malloc(cspace);

  int spacelen = 0;
  for (spacelen = 0; spacelen < cspace + 1; spacelen++) {
    memcpy(&space[spacelen], " ", 1);
  }
  abAppend(ab, "\x1b[1m", 6); // print inverted color
  abAppend(ab, "\0", 1);
  abAppend(ab, "\0", 1);
  abAppend(ab, space, spacelen);
  abAppend(ab, welcomeUP, welcomeUPlen);
  abAppend(ab, "\r\n", 2);
  abAppend(ab, space, spacelen);
  abAppend(ab, welcome, welcomelen);
  abAppend(ab, "\r\n", 2);
  abAppend(ab, space, spacelen);
  abAppend(ab, welcomeUP, welcomeUPlen);
  abAppend(ab, "\r\n", 2);
  abAppend(ab, "\x1b[m", 3); // back to normal colors

  for (y = 0; y < E.screenrows - CURSOR_YBEGIN; y++) {

    int filerow = y + E.rowoff;
    if (y <= E.numrows) {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, "~", 1);
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    } else {
      abAppend(ab, "~", 1);
    }
    abAppend(ab, "\r\n", 2);
    abAppend(ab, "\x1b[K", 3); // Erase in Line
  }
}

void editorDrawStatusBar(struct abuf *ab) {

  /**
   * m command make some changes in the font
   *
   * 1 bold
   * 4 underscore
   * 5 blink
   * 7 inverted colors
   * 0 clear all attributes or use [m
   */
  abAppend(ab, "\x1b[1m", 4); // print bold font

  char status[80], rstatus[80];

  int len = snprintf(status, sizeof(status), "%.20s - %d lines  %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(status), "%d/%d",
                      E.cy + 1 - CURSOR_YBEGIN, E.numrows);

  if (len > E.screencols)
    len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); // back to normal colors
}

void editorDrawMessageBar(struct abuf *ab) {

  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {

  write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
  editScroll();
  struct abuf ab = ABUF_INIT;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
  // write(STDOUT_FILENO , "\x1b[2J",4); //clear screen
  // write(STDOUT_FILENO, "\x1b[H", 3); // set the cursor at the begining

  abAppend(&ab, "\x1b[?25l", 6); // hide the cursor.
  // abAppend(&ab,  "\x1b[2J",4);
  abAppend(&ab, "\x1b[H", 3); // set the cursor at begining
  editDrawRows(&ab);

  editorDrawMessageBar(&ab);
  abAppend(&ab, "\r\n", 2);

  editorDrawStatusBar(&ab);
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1); // set the cursor at the begining

  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); //  shows the cursor
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** file i/o ***/
char *editorRowsToString(int *buflen) {

  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void editorOpen(char *filename) {

  free(E.filename);
  E.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }

  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as : %s (ESC to cancel)");
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** input ***/

char *editorPrompt(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0)
        buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}
void editorMoveCursor(int key) {

  erow *row =
      (E.cy > E.numrows + CURSOR_YBEGIN) ? NULL : &E.row[E.cy - CURSOR_YBEGIN];
  switch (key) {
  case ARROW_LEFT:

    if (E.cx > CURSOR_XBEGIN + E.coloff) {
      E.cx--;
    } else if (E.coloff > 0 && E.cx - CURSOR_XBEGIN > 0) {
      E.coloff--;
      E.cx--;
    } else if (E.rowoff > 0 && E.cy - CURSOR_YBEGIN > 0) {
      E.rowoff--;
      E.cy--;
      E.cx = E.row[E.cy - CURSOR_YBEGIN].size;
    } else if (E.cy > CURSOR_YBEGIN) {
      E.cy--;
      E.cx = E.row[E.cy - CURSOR_YBEGIN].size + CURSOR_XBEGIN;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size + 1) {
      E.cx++;
    } else if (row && E.cx == row->size + 1 && E.cy < E.numrows) {
      E.cy++;
      E.cx = CURSOR_XBEGIN;
      E.coloff = 0;
    }
    break;
  case ARROW_UP:
    if (E.cy > CURSOR_YBEGIN + E.rowoff) {
      E.cy--;
    } else if (E.rowoff > 0 && E.cy > 0) {
      E.rowoff--;
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows + CURSOR_YBEGIN - 1) {
      E.cy++;
    }
    break;
  }

  row =
      (E.cy >= E.numrows + CURSOR_YBEGIN) ? NULL : &E.row[E.cy - CURSOR_YBEGIN];
  int rowlen = row ? row->size + 1 : CURSOR_XBEGIN;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {

  static int quit_times = BEDIT_QUIT_TIMES;
  unsigned char c = editorReadKey();
  switch (c) {
  case '\r':
    editorInsertNewline();
    break;
  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0) {

      editorSetStatusMessage("\x1b[1;31m WARNING!! File has unsaved changes. "
                             "Press CTRL+Q %d more time to quit.",
                             quit_times);
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    if (c == DEL_KEY) {
      editorMoveCursor(ARROW_RIGHT);
    }
    editorDelChar();
    break;
  case PAGE_DOWN:
  case PAGE_UP: {

    if (c == PAGE_UP) {
      E.cy = E.rowoff + CURSOR_YBEGIN;
    } else if (c == PAGE_DOWN) {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }

    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);

  } break;
  case CTRL_KEY('l'):
  case '\x1b':
    break;
  case HOME_KEY:
    E.cx = 1;
    break;
  case END_KEY:
    if (E.cy < E.numrows) {
      E.cx = E.row[E.cy - CURSOR_YBEGIN].size + 1;
    }
    break;

  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
  case ARROW_UP:
    editorMoveCursor(c);
    break;
  default:
    editorInsertChar(c);
  }
  quit_times = BEDIT_QUIT_TIMES;
}

/*** init ***/

void initEditor() {

  E.cx = CURSOR_XBEGIN;
  E.cy = CURSOR_YBEGIN;
  E.rx = CURSOR_XBEGIN;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.dirty = 0;
  E.row = NULL;
  E.filename = NULL;

  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {

  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  } else {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  }
  unsigned char c;

  editorSetStatusMessage("HELP:  Ctrl+Q = quit  |  Ctrl+s = save");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}