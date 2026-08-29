#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include "terminal_internal.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "confit/limits.h"
#include "confit/model.h"

enum {
  CONFIT_TERMINAL_MIN_COLUMNS = 40,
  CONFIT_TERMINAL_MIN_ROWS = 10,
  CONFIT_TERMINAL_SPLIT_COLUMNS = 80,
  CONFIT_TERMINAL_ESCAPE_BYTES = 16,
  CONFIT_TERMINAL_POLL_MILLISECONDS = 80,
};

typedef struct ConfitTerminalSize {
  size_t columns;
  size_t rows;
} ConfitTerminalSize;

typedef struct ConfitTerminalWriter {
  char *bytes;
  size_t capacity;
  size_t used;
  int valid;
} ConfitTerminalWriter;

typedef struct ConfitTerminalSession {
  int input_fd;
  int output_fd;
  struct termios original;
  int raw_active;
  int screen_active;
  struct sigaction old_winch;
  struct sigaction old_int;
  struct sigaction old_term;
  struct sigaction old_hup;
  int handlers_active;
} ConfitTerminalSession;

typedef enum ConfitTerminalEventKind {
  CONFIT_TERMINAL_EVENT_NONE = 0,
  CONFIT_TERMINAL_EVENT_CHARACTER,
  CONFIT_TERMINAL_EVENT_ENTER,
  CONFIT_TERMINAL_EVENT_ESCAPE,
  CONFIT_TERMINAL_EVENT_BACKSPACE,
  CONFIT_TERMINAL_EVENT_UP,
  CONFIT_TERMINAL_EVENT_DOWN,
  CONFIT_TERMINAL_EVENT_LEFT,
  CONFIT_TERMINAL_EVENT_RIGHT,
  CONFIT_TERMINAL_EVENT_TAB,
  CONFIT_TERMINAL_EVENT_CTRL_R,
} ConfitTerminalEventKind;

typedef struct ConfitTerminalEvent {
  ConfitTerminalEventKind kind;
  unsigned char byte;
} ConfitTerminalEvent;

typedef struct ConfitTerminalDecoder {
  unsigned state;
  unsigned char sequence[CONFIT_TERMINAL_ESCAPE_BYTES];
  size_t sequence_size;
  ConfitTerminalEvent pending;
  int has_pending;
} ConfitTerminalDecoder;

static volatile sig_atomic_t g_confit_terminal_signal;
static volatile sig_atomic_t g_confit_terminal_resize;

static const char kTerminalInvalid[] = "invalid terminal frontend argument";
static const char kTerminalRequired[] =
    "menuconfig requires POSIX TTY input and output";
static const char kTerminalTooSmall[] =
    "terminal must be at least 40 columns by 10 rows";
static const char kTerminalQueryFailed[] = "failed to query terminal state";
static const char kTerminalRawFailed[] = "failed to enter terminal raw mode";
static const char kTerminalRestoreFailed[] =
    "failed to restore the original terminal state";
static const char kTerminalWriteFailed[] = "failed to render menuconfig";
static const char kTerminalReadFailed[] = "failed to read menuconfig input";
static const char kTerminalEnded[] = "menuconfig terminal input ended";
static const char kTerminalInterrupted[] =
    "menuconfig was interrupted after restoring the terminal";
static const char kTerminalSaveMissing[] =
    "menuconfig has no configuration save controller";
static const char kTerminalInternal[] = "terminal frontend invariant failed";

static ConfitStatus confit_terminal_fail(ConfitDiagnostic *diagnostic,
                                         ConfitStatus status,
                                         const char *message) {
  confit_diagnostic_set(diagnostic, status, 0, 0U, 0U, message);
  return status;
}

static void confit_terminal_signal_handler(int signal_number) {
  if (signal_number == SIGWINCH)
    g_confit_terminal_resize = 1;
  else
    g_confit_terminal_signal = signal_number;
}

static int confit_terminal_install_handlers(ConfitTerminalSession *session) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = confit_terminal_signal_handler;
  if (sigemptyset(&action.sa_mask) != 0)
    return 0;
  if (sigaction(SIGWINCH, &action, &session->old_winch) != 0)
    return 0;
  if (sigaction(SIGINT, &action, &session->old_int) != 0) {
    (void)sigaction(SIGWINCH, &session->old_winch, 0);
    return 0;
  }
  if (sigaction(SIGTERM, &action, &session->old_term) != 0) {
    (void)sigaction(SIGINT, &session->old_int, 0);
    (void)sigaction(SIGWINCH, &session->old_winch, 0);
    return 0;
  }
  if (sigaction(SIGHUP, &action, &session->old_hup) != 0) {
    (void)sigaction(SIGTERM, &session->old_term, 0);
    (void)sigaction(SIGINT, &session->old_int, 0);
    (void)sigaction(SIGWINCH, &session->old_winch, 0);
    return 0;
  }
  session->handlers_active = 1;
  return 1;
}

static void confit_terminal_restore_handlers(ConfitTerminalSession *session) {
  if (!session->handlers_active)
    return;
  (void)sigaction(SIGHUP, &session->old_hup, 0);
  (void)sigaction(SIGTERM, &session->old_term, 0);
  (void)sigaction(SIGINT, &session->old_int, 0);
  (void)sigaction(SIGWINCH, &session->old_winch, 0);
  session->handlers_active = 0;
}

static int confit_terminal_write_all(int fd, const char *bytes, size_t size) {
  size_t offset = 0U;
  while (offset < size) {
    const ssize_t written = write(fd, bytes + offset, size - offset);
    if (written > 0) {
      offset += (size_t)written;
      continue;
    }
    if (written < 0 && errno == EINTR)
      continue;
    return 0;
  }
  return 1;
}

static int confit_terminal_query_size(int fd, ConfitTerminalSize *out_size) {
  struct winsize size;
  if (out_size == 0 || ioctl(fd, TIOCGWINSZ, &size) != 0 || size.ws_col == 0U ||
      size.ws_row == 0U)
    return 0;
  out_size->columns = size.ws_col;
  out_size->rows = size.ws_row;
  if (out_size->columns > CONFIT_LIMIT_RENDER_COLUMNS)
    out_size->columns = CONFIT_LIMIT_RENDER_COLUMNS;
  if (out_size->rows > CONFIT_LIMIT_RENDER_ROWS)
    out_size->rows = CONFIT_LIMIT_RENDER_ROWS;
  return 1;
}

static int confit_terminal_enter(ConfitTerminalSession *session) {
  struct termios raw;
  static const char enter_screen[] = "\033[?1049h\033[?25l\033[2J\033[H";
  if (tcgetattr(session->input_fd, &session->original) != 0)
    return 0;
  raw = session->original;
  raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= (tcflag_t)~OPOST;
  raw.c_cflag |= CS8;
  raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(session->input_fd, TCSAFLUSH, &raw) != 0)
    return 0;
  session->raw_active = 1;
  session->screen_active = 1;
  if (!confit_terminal_write_all(session->output_fd, enter_screen,
                                 sizeof(enter_screen) - 1U))
    return 0;
  return 1;
}

static int confit_terminal_leave(ConfitTerminalSession *session) {
  static const char leave_screen[] = "\033[?25h\033[?1049l";
  int ok = 1;
  if (session->screen_active) {
    if (!confit_terminal_write_all(session->output_fd, leave_screen,
                                   sizeof(leave_screen) - 1U))
      ok = 0;
    session->screen_active = 0;
  }
  if (session->raw_active) {
    if (tcsetattr(session->input_fd, TCSAFLUSH, &session->original) != 0)
      ok = 0;
    session->raw_active = 0;
  }
  return ok;
}

static void confit_terminal_writer_bytes(ConfitTerminalWriter *writer,
                                         const char *bytes, size_t size) {
  if (!writer->valid || bytes == 0 || writer->used > writer->capacity ||
      size > writer->capacity - writer->used) {
    writer->valid = 0;
    return;
  }
  if (size != 0U)
    memcpy(writer->bytes + writer->used, bytes, size);
  writer->used += size;
}

static void confit_terminal_writer_text(ConfitTerminalWriter *writer,
                                        const char *text) {
  confit_terminal_writer_bytes(writer, text, text != 0 ? strlen(text) : 0U);
}

static void confit_terminal_writer_repeat(ConfitTerminalWriter *writer,
                                          char byte, size_t count) {
  while (count-- != 0U)
    confit_terminal_writer_bytes(writer, &byte, 1U);
}

static size_t confit_terminal_utf8_width(const unsigned char *bytes,
                                         size_t remaining, size_t *out_bytes) {
  uint32_t scalar;
  size_t width;
  if (remaining == 0U) {
    *out_bytes = 0U;
    return 0U;
  }
  if (bytes[0] < 0x80U) {
    *out_bytes = 1U;
    return bytes[0] >= 0x20U && bytes[0] != 0x7fU ? 1U : 0U;
  }
  if (bytes[0] >= 0xc2U && bytes[0] <= 0xdfU) {
    width = 2U;
    scalar = (uint32_t)(bytes[0] & 0x1fU);
  } else if (bytes[0] >= 0xe0U && bytes[0] <= 0xefU) {
    width = 3U;
    scalar = (uint32_t)(bytes[0] & 0x0fU);
  } else if (bytes[0] >= 0xf0U && bytes[0] <= 0xf4U) {
    width = 4U;
    scalar = (uint32_t)(bytes[0] & 0x07U);
  } else {
    *out_bytes = 1U;
    return 1U;
  }
  if (width > remaining) {
    *out_bytes = 1U;
    return 1U;
  }
  for (size_t index = 1U; index < width; ++index) {
    if ((bytes[index] & 0xc0U) != 0x80U) {
      *out_bytes = 1U;
      return 1U;
    }
    scalar = (scalar << 6U) | (uint32_t)(bytes[index] & 0x3fU);
  }
  if ((width == 3U && ((bytes[0] == 0xe0U && bytes[1] < 0xa0U) ||
                       (bytes[0] == 0xedU && bytes[1] > 0x9fU))) ||
      (width == 4U && ((bytes[0] == 0xf0U && bytes[1] < 0x90U) ||
                       (bytes[0] == 0xf4U && bytes[1] > 0x8fU)))) {
    *out_bytes = 1U;
    return 1U;
  }
  *out_bytes = width;
  if ((scalar >= 0x0300U && scalar <= 0x036fU) ||
      (scalar >= 0x1ab0U && scalar <= 0x1affU) ||
      (scalar >= 0x1dc0U && scalar <= 0x1dffU) ||
      (scalar >= 0xfe20U && scalar <= 0xfe2fU))
    return 0U;
  if ((scalar >= 0x1100U && scalar <= 0x115fU) ||
      (scalar >= 0x2e80U && scalar <= 0xa4cfU) ||
      (scalar >= 0xac00U && scalar <= 0xd7a3U) ||
      (scalar >= 0xf900U && scalar <= 0xfaffU) ||
      (scalar >= 0x1f300U && scalar <= 0x1faffU) ||
      (scalar >= 0x20000U && scalar <= 0x3fffdU))
    return 2U;
  return 1U;
}

static void confit_terminal_writer_safe_text(ConfitTerminalWriter *writer,
                                             const char *text, size_t columns) {
  const unsigned char *bytes = (const unsigned char *)text;
  size_t remaining = text != 0 ? strlen(text) : 0U;
  size_t cells = 0U;
  while (remaining != 0U && cells < columns) {
    size_t scalar_bytes;
    size_t scalar_width =
        confit_terminal_utf8_width(bytes, remaining, &scalar_bytes);
    if (scalar_bytes == 0U)
      break;
    if (scalar_width == 0U) {
      bytes += scalar_bytes;
      remaining -= scalar_bytes;
      continue;
    }
    if (cells + scalar_width > columns)
      break;
    if (bytes[0] < 0x20U || bytes[0] == 0x7fU ||
        (scalar_bytes == 1U && bytes[0] >= 0x80U) ||
        (scalar_bytes == 2U && bytes[0] == 0xc2U && bytes[1] <= 0x9fU)) {
      confit_terminal_writer_text(writer, "?");
      scalar_width = 1U;
    } else {
      confit_terminal_writer_bytes(writer, (const char *)bytes, scalar_bytes);
    }
    cells += scalar_width;
    bytes += scalar_bytes;
    remaining -= scalar_bytes;
  }
  confit_terminal_writer_repeat(writer, ' ', columns - cells);
}

static const char *confit_terminal_state_name(ConfitUiState state) {
  switch (state) {
  case CONFIT_UI_EDIT:
    return "EDIT";
  case CONFIT_UI_SEARCH:
    return "SEARCH";
  case CONFIT_UI_COMMAND:
    return "COMMAND";
  case CONFIT_UI_HELP:
    return "HELP";
  case CONFIT_UI_DIFF:
    return "DIFF";
  case CONFIT_UI_ENUM_PICKER:
    return "ENUM";
  case CONFIT_UI_NORMAL:
  default:
    return "NORMAL";
  }
}

static void confit_terminal_value_text(const ConfitValue *value, char *out,
                                       size_t capacity) {
  const char *text = 0;
  size_t size = 0U;
  if (out == 0 || capacity == 0U)
    return;
  out[0] = '\0';
  if (value == 0)
    return;
  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    (void)snprintf(out, capacity, "%s",
                   value->data.boolean != 0 ? "true" : "false");
    break;
  case CONFIT_VALUE_INT:
    (void)snprintf(out, capacity, "%lld", (long long)value->data.integer);
    break;
  case CONFIT_VALUE_HEX:
    (void)snprintf(out, capacity, "0x%llx",
                   (unsigned long long)value->data.hexadecimal);
    break;
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
    if (confit_value_text(value, &text, &size)) {
      const size_t copied = size < capacity - 1U ? size : capacity - 1U;
      memcpy(out, text, copied);
      out[copied] = '\0';
    }
    break;
  case CONFIT_VALUE_INVALID:
  default:
    break;
  }
}

static const char *confit_terminal_value_kind_name(ConfitValueKind kind) {
  switch (kind) {
  case CONFIT_VALUE_BOOL:
    return "bool";
  case CONFIT_VALUE_INT:
    return "int";
  case CONFIT_VALUE_HEX:
    return "hex";
  case CONFIT_VALUE_STRING:
    return "string";
  case CONFIT_VALUE_ENUM:
    return "enum";
  case CONFIT_VALUE_INVALID:
  default:
    return "invalid";
  }
}

static int confit_terminal_selected_row(const ConfitUiModel *model,
                                        ConfitUiRowView *out) {
  return confit_ui_cursor(model) < confit_ui_row_count(model) &&
         confit_ui_row_at(model, confit_ui_cursor(model), out);
}

static void confit_terminal_render_list_line(ConfitTerminalWriter *writer,
                                             const ConfitUiModel *model,
                                             size_t row_index, size_t columns) {
  ConfitUiRowView row;
  char prefix[8];
  char value[96];
  char line[CONFIT_LIMIT_PROMPT_BYTES + 240U];
  size_t depth;
  if (!confit_ui_row_at(model, row_index, &row)) {
    confit_terminal_writer_repeat(writer, ' ', columns);
    return;
  }
  depth = row.depth > CONFIT_LIMIT_VISIBLE_MENU_DEPTH
              ? CONFIT_LIMIT_VISIBLE_MENU_DEPTH
              : row.depth;
  (void)snprintf(prefix, sizeof(prefix), "%c%c ",
                 row_index == confit_ui_cursor(model) ? '>' : ' ',
                 row.kind == CONFIT_UI_ROW_MENU
                     ? '+'
                     : (!row.available ? '-' : (row.changed ? '*' : ' ')));
  value[0] = '\0';
  if (row.kind == CONFIT_UI_ROW_CONFIG)
    confit_terminal_value_text(row.effective_value, value, sizeof(value));
  (void)snprintf(line, sizeof(line), "%s%*s%s%s%s", prefix, (int)(depth * 2U),
                 "", row.prompt != 0 ? row.prompt : "",
                 value[0] != '\0' ? " = " : "", value);
  confit_terminal_writer_safe_text(writer, line, columns);
}

static void confit_terminal_render_detail(ConfitTerminalWriter *writer,
                                          const ConfitUiModel *model,
                                          size_t columns, size_t line) {
  ConfitUiRowView row;
  char value[96];
  char other[96];
  char text[CONFIT_LIMIT_HELP_BYTES + 256U];
  const ConfitUiState state = confit_ui_state(model);
  text[0] = '\0';
  if (state == CONFIT_UI_HELP) {
    static const char *const help_lines[] = {
        "Menuconfig help (Esc returns)",
        "j/k or arrows: move selection",
        "Enter/right: open menu or edit value",
        "Space: toggle bool   e: typed editor",
        "/: search   n/N: next/previous match",
        "u/Ctrl-R: undo/redo   d: semantic diff",
        ": enters command mode; q never exits",
        ":w save   :q clean exit   :q! discard exit",
        ":wq save+exit   :x save only when modified",
        ":set unavailable | :set nounavailable",
        "No shell, pipe, mapping, macro, or plugin commands.",
    };
    if (line < sizeof(help_lines) / sizeof(help_lines[0]))
      confit_terminal_writer_safe_text(writer, help_lines[line], columns);
    else
      confit_terminal_writer_repeat(writer, ' ', columns);
    return;
  }
  if (state == CONFIT_UI_DIFF) {
    if (line == 0U) {
      (void)snprintf(text, sizeof(text), "Semantic diff (%zu changed)",
                     confit_ui_diff_count(model));
    } else if (line >= 2U) {
      ConfitUiDiffView diff;
      const size_t diff_index = line - 2U;
      if (confit_ui_diff_at(model, diff_index, &diff)) {
        confit_terminal_value_text(diff.saved_value, value, sizeof(value));
        confit_terminal_value_text(diff.working_value, other, sizeof(other));
        (void)snprintf(text, sizeof(text), "%s: %s -> %s", diff.symbol, value,
                       other);
      }
    }
    confit_terminal_writer_safe_text(writer, text, columns);
    return;
  }
  if (!confit_terminal_selected_row(model, &row)) {
    confit_terminal_writer_repeat(writer, ' ', columns);
    return;
  }
  if (line == 0U) {
    (void)snprintf(text, sizeof(text), "%s", row.prompt != 0 ? row.prompt : "");
  } else if (line == 1U && row.kind == CONFIT_UI_ROW_CONFIG) {
    (void)snprintf(text, sizeof(text), "%s  type: %s",
                   row.symbol != 0 ? row.symbol : "",
                   confit_terminal_value_kind_name(row.value_kind));
  } else if (line == 2U && row.kind == CONFIT_UI_ROW_CONFIG) {
    confit_terminal_value_text(row.effective_value, value, sizeof(value));
    confit_terminal_value_text(row.default_value, other, sizeof(other));
    (void)snprintf(text, sizeof(text), "value: %s  default: %s", value, other);
  } else if (line == 3U && row.kind == CONFIT_UI_ROW_CONFIG) {
    (void)snprintf(text, sizeof(text), "origin: %s  %s%s",
                   row.origin == CONFIT_ORIGIN_USER ? "user" : "default",
                   row.available ? "available" : "UNAVAILABLE",
                   row.changed ? "  MODIFIED" : "");
  } else if (state == CONFIT_UI_ENUM_PICKER && line == 4U) {
    (void)snprintf(text, sizeof(text),
                   "Choose a value (Enter applies; Esc cancels)");
  } else if (state == CONFIT_UI_ENUM_PICKER && line == 5U &&
             confit_ui_enum_selection(model) < row.enum_value_count) {
    const size_t index = confit_ui_enum_selection(model);
    (void)snprintf(text, sizeof(text), "> %s  (%zu/%zu)",
                   row.enum_values[index], index + 1U, row.enum_value_count);
  } else if (state == CONFIT_UI_ENUM_PICKER && line == 6U) {
    (void)snprintf(text, sizeof(text), "values:");
  } else if (state == CONFIT_UI_ENUM_PICKER && line >= 7U &&
             line - 7U < row.enum_value_count) {
    const size_t index = line - 7U;
    (void)snprintf(text, sizeof(text), "  %s", row.enum_values[index]);
  } else if (line == 4U) {
    (void)snprintf(text, sizeof(text), "help: %s",
                   row.help != 0 ? row.help : "");
  } else if (line == 5U && row.kind == CONFIT_UI_ROW_CONFIG &&
             row.dependency_text != 0) {
    (void)snprintf(text, sizeof(text), "depends on: %s", row.dependency_text);
  } else if (line == 6U && row.kind == CONFIT_UI_ROW_CONFIG &&
             row.reason_detail != 0) {
    (void)snprintf(text, sizeof(text), "reason: %s", row.reason_detail);
  } else if (line == 7U && row.kind == CONFIT_UI_ROW_CONFIG && row.has_range) {
    confit_terminal_value_text(row.range_minimum, value, sizeof(value));
    confit_terminal_value_text(row.range_maximum, other, sizeof(other));
    (void)snprintf(text, sizeof(text), "range: %s .. %s", value, other);
  }
  confit_terminal_writer_safe_text(writer, text, columns);
}

static ConfitStatus confit_terminal_render(ConfitUiModel *model, int output_fd,
                                           const ConfitTerminalSize *size,
                                           int narrow_detail,
                                           const char *message,
                                           ConfitDiagnostic *diagnostic) {
  const int split = size->columns >= CONFIT_TERMINAL_SPLIT_COLUMNS;
  const ConfitUiState state = confit_ui_state(model);
  const int special_detail = state == CONFIT_UI_HELP ||
                             state == CONFIT_UI_DIFF ||
                             state == CONFIT_UI_ENUM_PICKER;
  const int show_detail = !split && (narrow_detail || special_detail);
  const size_t body_rows = size->rows - 4U;
  const size_t list_columns = split ? (size->columns * 3U) / 5U : size->columns;
  const size_t detail_columns = split ? size->columns - list_columns - 3U : 0U;
  const size_t capacity =
      size->columns * size->rows * 4U + size->rows * 16U + 256U;
  ConfitTerminalWriter writer;
  char *frame = (char *)malloc(capacity);
  char title[160];
  char footer[256];
  const char *input;
  size_t input_size;
  size_t row;
  ConfitStatus status = CONFIT_OK;
  if (frame == 0)
    return confit_terminal_fail(diagnostic, CONFIT_ERR_INTERNAL,
                                kTerminalInternal);
  writer.bytes = frame;
  writer.capacity = capacity;
  writer.used = 0U;
  writer.valid = 1;
  (void)snprintf(title, sizeof(title),
                 "+-- Confit menuconfig [%s]%s [%s]%s --+",
                 split ? "split" : "tabbed",
                 split ? "" : (show_detail ? " [detail]" : " [list]"),
                 confit_terminal_state_name(state),
                 confit_ui_dirty(model) ? " [modified]" : "");
  confit_terminal_writer_text(&writer, "\033[H");
  confit_terminal_writer_safe_text(&writer, title, size->columns);
  for (row = 0U; row < body_rows; ++row) {
    const size_t model_row = confit_ui_viewport_offset(model) + row;
    confit_terminal_writer_text(&writer, "\r\n");
    if (show_detail) {
      confit_terminal_render_detail(&writer, model, size->columns, row);
    } else if (model_row < confit_ui_row_count(model)) {
      confit_terminal_render_list_line(&writer, model, model_row, list_columns);
    } else {
      confit_terminal_writer_repeat(&writer, ' ', list_columns);
    }
    if (split) {
      confit_terminal_writer_text(&writer, " | ");
      confit_terminal_render_detail(&writer, model, detail_columns, row);
    }
  }
  confit_terminal_writer_text(&writer, "\r\n");
  input = confit_ui_input(model, &input_size);
  if (state == CONFIT_UI_EDIT || state == CONFIT_UI_SEARCH ||
      state == CONFIT_UI_COMMAND) {
    (void)snprintf(
        footer, sizeof(footer), "%s> %.*s  [Enter apply, Esc cancel]",
        confit_terminal_state_name(state),
        (int)(input_size < 180U ? input_size : 180U), input != 0 ? input : "");
  } else if (state == CONFIT_UI_ENUM_PICKER) {
    (void)snprintf(footer, sizeof(footer),
                   "j/k choose  Enter apply  Esc cancel");
  } else if (state == CONFIT_UI_HELP || state == CONFIT_UI_DIFF) {
    (void)snprintf(footer, sizeof(footer), "Esc returns to Normal mode");
  } else {
    if (split)
      (void)snprintf(
          footer, sizeof(footer),
          "j/k move  Enter open  Space bool  e edit  / search  : commands");
    else
      (void)snprintf(
          footer, sizeof(footer),
          "j/k move Enter open Space bool e edit / search : cmd Tab pane");
  }
  confit_terminal_writer_safe_text(&writer, footer, size->columns);
  confit_terminal_writer_text(&writer, "\r\n");
  confit_terminal_writer_safe_text(
      &writer,
      message != 0
          ? message
          : (confit_ui_notice(model) != 0
                 ? confit_ui_notice(model)
                 : (state == CONFIT_UI_COMMAND
                        ? ":w :q :q! :wq :x :help :set unavailable :set "
                          "nounavailable"
                        : "Esc cancels a mode; exit with :q, :wq, :q!, or :x")),
      size->columns);
  confit_terminal_writer_text(&writer, "\033[J");
  if (!writer.valid ||
      !confit_terminal_write_all(output_fd, writer.bytes, writer.used))
    status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                  kTerminalWriteFailed);
  free(frame);
  return status;
}

static ConfitTerminalEvent confit_terminal_event(ConfitTerminalEventKind kind,
                                                 unsigned char byte) {
  ConfitTerminalEvent event;
  event.kind = kind;
  event.byte = byte;
  return event;
}

static ConfitTerminalEvent
confit_terminal_decode(ConfitTerminalDecoder *decoder, unsigned char byte) {
  if (decoder->state == 0U) {
    if (byte == 0x1bU) {
      decoder->state = 1U;
      return confit_terminal_event(CONFIT_TERMINAL_EVENT_NONE, 0U);
    }
    if (byte == '\r' || byte == '\n')
      return confit_terminal_event(CONFIT_TERMINAL_EVENT_ENTER, 0U);
    if (byte == '\t')
      return confit_terminal_event(CONFIT_TERMINAL_EVENT_TAB, 0U);
    if (byte == 0x7fU || byte == 0x08U)
      return confit_terminal_event(CONFIT_TERMINAL_EVENT_BACKSPACE, 0U);
    if (byte == 0x12U)
      return confit_terminal_event(CONFIT_TERMINAL_EVENT_CTRL_R, 0U);
    return confit_terminal_event(CONFIT_TERMINAL_EVENT_CHARACTER, byte);
  }
  if (decoder->state == 1U) {
    if (byte != '[') {
      decoder->state = 0U;
      if (byte == '\r' || byte == '\n')
        decoder->pending =
            confit_terminal_event(CONFIT_TERMINAL_EVENT_ENTER, 0U);
      else if (byte == '\t')
        decoder->pending = confit_terminal_event(CONFIT_TERMINAL_EVENT_TAB, 0U);
      else if (byte == 0x7fU || byte == 0x08U)
        decoder->pending =
            confit_terminal_event(CONFIT_TERMINAL_EVENT_BACKSPACE, 0U);
      else if (byte == 0x12U)
        decoder->pending =
            confit_terminal_event(CONFIT_TERMINAL_EVENT_CTRL_R, 0U);
      else
        decoder->pending =
            confit_terminal_event(CONFIT_TERMINAL_EVENT_CHARACTER, byte);
      decoder->has_pending = 1;
      return confit_terminal_event(CONFIT_TERMINAL_EVENT_ESCAPE, 0U);
    }
    decoder->state = 2U;
    decoder->sequence_size = 0U;
    return confit_terminal_event(CONFIT_TERMINAL_EVENT_NONE, 0U);
  }
  if (decoder->sequence_size >= sizeof(decoder->sequence)) {
    decoder->state = 0U;
    decoder->sequence_size = 0U;
    return confit_terminal_event(CONFIT_TERMINAL_EVENT_NONE, 0U);
  }
  decoder->sequence[decoder->sequence_size++] = byte;
  if (byte >= 0x40U && byte <= 0x7eU) {
    ConfitTerminalEventKind kind = CONFIT_TERMINAL_EVENT_NONE;
    if (decoder->sequence_size == 1U) {
      if (byte == 'A')
        kind = CONFIT_TERMINAL_EVENT_UP;
      if (byte == 'B')
        kind = CONFIT_TERMINAL_EVENT_DOWN;
      if (byte == 'C')
        kind = CONFIT_TERMINAL_EVENT_RIGHT;
      if (byte == 'D')
        kind = CONFIT_TERMINAL_EVENT_LEFT;
    }
    decoder->state = 0U;
    decoder->sequence_size = 0U;
    return confit_terminal_event(kind, 0U);
  }
  return confit_terminal_event(CONFIT_TERMINAL_EVENT_NONE, 0U);
}

static ConfitTerminalEvent
confit_terminal_decoder_pending(ConfitTerminalDecoder *decoder) {
  ConfitTerminalEvent event =
      confit_terminal_event(CONFIT_TERMINAL_EVENT_NONE, 0U);
  if (decoder->has_pending) {
    event = decoder->pending;
    decoder->has_pending = 0;
  }
  return event;
}

static ConfitTerminalEvent
confit_terminal_decoder_timeout(ConfitTerminalDecoder *decoder) {
  if (decoder->state == 1U) {
    decoder->state = 0U;
    return confit_terminal_event(CONFIT_TERMINAL_EVENT_ESCAPE, 0U);
  }
  if (decoder->state == 2U) {
    decoder->state = 0U;
    decoder->sequence_size = 0U;
  }
  return confit_terminal_event(CONFIT_TERMINAL_EVENT_NONE, 0U);
}

static ConfitStatus
confit_terminal_input_replace(ConfitUiModel *model, unsigned char byte,
                              int backspace, ConfitDiagnostic *diagnostic) {
  char candidate[CONFIT_LIMIT_STRING_BYTES + 1U];
  size_t size = 0U;
  const char *input = confit_ui_input(model, &size);
  if (input == 0 || size > CONFIT_LIMIT_STRING_BYTES)
    return confit_terminal_fail(diagnostic, CONFIT_ERR_INTERNAL,
                                kTerminalInternal);
  memcpy(candidate, input, size);
  if (backspace) {
    if (size != 0U) {
      --size;
      while (size != 0U && ((unsigned char)candidate[size] & 0xc0U) == 0x80U)
        --size;
    }
  } else {
    if (byte < 0x20U || byte == 0x7fU || size >= CONFIT_LIMIT_STRING_BYTES)
      return CONFIT_OK;
    candidate[size++] = (char)byte;
  }
  candidate[size] = '\0';
  return confit_ui_set_input(model, candidate, size, diagnostic);
}

static ConfitStatus
confit_terminal_complete_command(ConfitUiModel *model,
                                 ConfitDiagnostic *diagnostic) {
  static const char *const commands[] = {
      ":w",
      ":q",
      ":q!",
      ":wq",
      ":x",
      ":help",
      ":set unavailable",
      ":set nounavailable",
  };
  const char *input;
  const char *match = 0;
  size_t input_size;
  size_t matches = 0U;
  size_t index;
  input = confit_ui_input(model, &input_size);
  if (input == 0)
    return CONFIT_OK;
  for (index = 0U; index < sizeof(commands) / sizeof(commands[0]); ++index) {
    if (strlen(commands[index]) >= input_size &&
        memcmp(commands[index], input, input_size) == 0) {
      match = commands[index];
      ++matches;
    }
  }
  if (matches == 1U)
    return confit_ui_set_input(model, match, strlen(match), diagnostic);
  return CONFIT_OK;
}

static ConfitStatus
confit_terminal_dispatch_event(ConfitUiModel *model,
                               const ConfitTerminalEvent *event, int split,
                               int *narrow_detail, ConfitUiEffect *out_effect,
                               ConfitDiagnostic *diagnostic) {
  const ConfitUiState state = confit_ui_state(model);
  ConfitUiAction action = (ConfitUiAction)0;
  *out_effect = CONFIT_UI_EFFECT_NONE;
  if (event->kind == CONFIT_TERMINAL_EVENT_NONE)
    return CONFIT_OK;
  if (event->kind == CONFIT_TERMINAL_EVENT_ESCAPE)
    return confit_ui_action(model, CONFIT_UI_ACTION_CANCEL, out_effect,
                            diagnostic);
  if (event->kind == CONFIT_TERMINAL_EVENT_TAB) {
    if (state == CONFIT_UI_COMMAND)
      return confit_terminal_complete_command(model, diagnostic);
    if (!split && state == CONFIT_UI_NORMAL && narrow_detail != 0)
      *narrow_detail = !*narrow_detail;
    return CONFIT_OK;
  }
  if (state == CONFIT_UI_EDIT || state == CONFIT_UI_SEARCH ||
      state == CONFIT_UI_COMMAND) {
    if (event->kind == CONFIT_TERMINAL_EVENT_ENTER)
      return confit_ui_accept(model, out_effect, diagnostic);
    if (event->kind == CONFIT_TERMINAL_EVENT_BACKSPACE)
      return confit_terminal_input_replace(model, 0U, 1, diagnostic);
    if (event->kind == CONFIT_TERMINAL_EVENT_CHARACTER)
      return confit_terminal_input_replace(model, event->byte, 0, diagnostic);
    return CONFIT_OK;
  }
  if (state == CONFIT_UI_HELP || state == CONFIT_UI_DIFF)
    return CONFIT_OK;
  if (event->kind == CONFIT_TERMINAL_EVENT_UP ||
      (event->kind == CONFIT_TERMINAL_EVENT_CHARACTER && event->byte == 'k'))
    action = CONFIT_UI_ACTION_PREVIOUS;
  else if (event->kind == CONFIT_TERMINAL_EVENT_DOWN ||
           (event->kind == CONFIT_TERMINAL_EVENT_CHARACTER &&
            event->byte == 'j'))
    action = CONFIT_UI_ACTION_NEXT;
  else if (event->kind == CONFIT_TERMINAL_EVENT_LEFT ||
           (event->kind == CONFIT_TERMINAL_EVENT_CHARACTER &&
            event->byte == 'h'))
    action = CONFIT_UI_ACTION_PARENT;
  else if (event->kind == CONFIT_TERMINAL_EVENT_RIGHT ||
           event->kind == CONFIT_TERMINAL_EVENT_ENTER ||
           (event->kind == CONFIT_TERMINAL_EVENT_CHARACTER &&
            event->byte == 'l'))
    action = CONFIT_UI_ACTION_OPEN;
  else if (event->kind == CONFIT_TERMINAL_EVENT_CTRL_R)
    action = CONFIT_UI_ACTION_REDO;
  else if (event->kind == CONFIT_TERMINAL_EVENT_CHARACTER) {
    switch (event->byte) {
    case ' ':
      action = CONFIT_UI_ACTION_TOGGLE;
      break;
    case 'e':
      action = CONFIT_UI_ACTION_BEGIN_EDIT;
      break;
    case '/':
      action = CONFIT_UI_ACTION_BEGIN_SEARCH;
      break;
    case 'n':
      action = CONFIT_UI_ACTION_SEARCH_NEXT;
      break;
    case 'N':
      action = CONFIT_UI_ACTION_SEARCH_PREVIOUS;
      break;
    case 'u':
      action = CONFIT_UI_ACTION_UNDO;
      break;
    case 'd':
      action = CONFIT_UI_ACTION_SHOW_DIFF;
      break;
    case '?':
      action = CONFIT_UI_ACTION_SHOW_HELP;
      break;
    case ':':
      action = CONFIT_UI_ACTION_BEGIN_COMMAND;
      break;
    case 'q':
      action = CONFIT_UI_ACTION_QUIT_HINT;
      break;
    default:
      return CONFIT_OK;
    }
  }
  if ((int)action == 0)
    return CONFIT_OK;
  {
    ConfitStatus status =
        confit_ui_action(model, action, out_effect, diagnostic);
    if (status == CONFIT_OK && action == CONFIT_UI_ACTION_BEGIN_COMMAND)
      status = confit_ui_set_input(model, ":", 1U, diagnostic);
    return status;
  }
}

static void confit_terminal_copy_message(char *out, size_t capacity,
                                         const ConfitDiagnostic *diagnostic) {
  if (capacity == 0U)
    return;
  if (diagnostic == 0 || diagnostic->message == 0) {
    out[0] = '\0';
    return;
  }
  (void)snprintf(out, capacity, "error: %s", diagnostic->message);
}

static ConfitStatus
confit_terminal_handle_effect(ConfitUiModel *model, ConfitUiEffect effect,
                              const ConfitTerminalController *controller,
                              int *out_done, char *message, size_t message_size,
                              ConfitDiagnostic *diagnostic) {
  if (effect == CONFIT_UI_EFFECT_EXIT ||
      effect == CONFIT_UI_EFFECT_DISCARD_AND_EXIT) {
    *out_done = 1;
    return CONFIT_OK;
  }
  if (effect == CONFIT_UI_EFFECT_REQUEST_SAVE) {
    ConfitDiagnostic save_diagnostic;
    ConfitUiEffect followup = CONFIT_UI_EFFECT_NONE;
    ConfitStatus save_status;
    if (controller == 0 || controller->save == 0) {
      (void)confit_ui_save_result(model, 0, &followup, diagnostic);
      return confit_terminal_fail(diagnostic, CONFIT_ERR_INTERNAL,
                                  kTerminalSaveMissing);
    }
    confit_diagnostic_init(&save_diagnostic);
    save_status = controller->save(
        controller->context, confit_ui_resolution(model), &save_diagnostic);
    if (save_status != CONFIT_OK)
      confit_terminal_copy_message(message, message_size, &save_diagnostic);
    if (confit_ui_save_result(model, save_status == CONFIT_OK, &followup,
                              diagnostic) != CONFIT_OK)
      return diagnostic->status;
    if (followup == CONFIT_UI_EFFECT_EXIT)
      *out_done = 1;
  }
  return CONFIT_OK;
}

ConfitStatus confit_terminal_run(ConfitUiModel *model, int input_fd,
                                 int output_fd,
                                 const ConfitTerminalController *controller,
                                 ConfitDiagnostic *diagnostic) {
  ConfitTerminalSession session;
  ConfitTerminalDecoder decoder;
  ConfitTerminalSize size;
  struct pollfd input;
  char message[512];
  ConfitStatus status = CONFIT_OK;
  int done = 0;
  int narrow_detail = 0;
  int redraw = 1;
  int restored;
  if (model == 0 || input_fd < 0 || output_fd < 0)
    return confit_terminal_fail(diagnostic, CONFIT_ERR_USAGE, kTerminalInvalid);
  if (!isatty(input_fd) || !isatty(output_fd))
    return confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                kTerminalRequired);
  if (!confit_terminal_query_size(output_fd, &size))
    return confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                kTerminalQueryFailed);
  if (size.columns < CONFIT_TERMINAL_MIN_COLUMNS ||
      size.rows < CONFIT_TERMINAL_MIN_ROWS)
    return confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                kTerminalTooSmall);
  memset(&session, 0, sizeof(session));
  memset(&decoder, 0, sizeof(decoder));
  message[0] = '\0';
  session.input_fd = input_fd;
  session.output_fd = output_fd;
  g_confit_terminal_signal = 0;
  g_confit_terminal_resize = 0;
  if (!confit_terminal_install_handlers(&session))
    return confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                kTerminalQueryFailed);
  if (!confit_terminal_enter(&session)) {
    status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                  kTerminalRawFailed);
    goto cleanup;
  }
  while (!done && status == CONFIT_OK) {
    unsigned char bytes[64];
    ssize_t count;
    int polled;
    if (g_confit_terminal_signal != 0) {
      status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                    kTerminalInterrupted);
      break;
    }
    if (g_confit_terminal_resize != 0) {
      g_confit_terminal_resize = 0;
      if (!confit_terminal_query_size(output_fd, &size)) {
        status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                      kTerminalQueryFailed);
        break;
      }
      if (size.columns < CONFIT_TERMINAL_MIN_COLUMNS ||
          size.rows < CONFIT_TERMINAL_MIN_ROWS) {
        status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                      kTerminalTooSmall);
        break;
      }
      redraw = 1;
    }
    if (redraw) {
      status = confit_ui_set_viewport_rows(model, size.rows - 4U, diagnostic);
      if (status != CONFIT_OK)
        break;
      status =
          confit_terminal_render(model, output_fd, &size, narrow_detail,
                                 message[0] != '\0' ? message : 0, diagnostic);
      message[0] = '\0';
      redraw = 0;
      if (status != CONFIT_OK)
        break;
    }
    input.fd = input_fd;
    input.events = POLLIN;
    input.revents = 0;
    polled = poll(&input, 1U, CONFIT_TERMINAL_POLL_MILLISECONDS);
    if (polled < 0) {
      if (errno == EINTR)
        continue;
      status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                    kTerminalReadFailed);
      break;
    }
    if (polled == 0) {
      ConfitTerminalEvent event = confit_terminal_decoder_timeout(&decoder);
      ConfitUiEffect effect = CONFIT_UI_EFFECT_NONE;
      ConfitDiagnostic action_diagnostic;
      if (event.kind == CONFIT_TERMINAL_EVENT_NONE)
        continue;
      confit_diagnostic_init(&action_diagnostic);
      status = confit_terminal_dispatch_event(
          model, &event, size.columns >= CONFIT_TERMINAL_SPLIT_COLUMNS,
          &narrow_detail, &effect, &action_diagnostic);
      if (status == CONFIT_ERR_VALIDATION || status == CONFIT_ERR_USAGE) {
        confit_terminal_copy_message(message, sizeof(message),
                                     &action_diagnostic);
        status = CONFIT_OK;
      } else if (status == CONFIT_OK) {
        status =
            confit_terminal_handle_effect(model, effect, controller, &done,
                                          message, sizeof(message), diagnostic);
      }
      redraw = 1;
      continue;
    }
    if ((input.revents & (POLLERR | POLLNVAL)) != 0) {
      status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                    kTerminalReadFailed);
      break;
    }
    if ((input.revents & POLLHUP) != 0 && (input.revents & POLLIN) == 0) {
      status =
          confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL, kTerminalEnded);
      break;
    }
    count = read(input_fd, bytes, sizeof(bytes));
    if (count == 0) {
      status =
          confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL, kTerminalEnded);
      break;
    }
    if (count < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                    kTerminalReadFailed);
      break;
    }
    for (ssize_t index = 0; index < count && status == CONFIT_OK && !done;
         ++index) {
      ConfitTerminalEvent event =
          confit_terminal_decode(&decoder, bytes[index]);
      unsigned pass;
      for (pass = 0U; pass < 2U && status == CONFIT_OK && !done; ++pass) {
        ConfitUiEffect effect = CONFIT_UI_EFFECT_NONE;
        ConfitDiagnostic action_diagnostic;
        if (pass != 0U)
          event = confit_terminal_decoder_pending(&decoder);
        if (event.kind == CONFIT_TERMINAL_EVENT_NONE)
          break;
        confit_diagnostic_init(&action_diagnostic);
        status = confit_terminal_dispatch_event(
            model, &event, size.columns >= CONFIT_TERMINAL_SPLIT_COLUMNS,
            &narrow_detail, &effect, &action_diagnostic);
        if (status == CONFIT_ERR_VALIDATION || status == CONFIT_ERR_USAGE) {
          confit_terminal_copy_message(message, sizeof(message),
                                       &action_diagnostic);
          status = CONFIT_OK;
        } else if (status == CONFIT_OK) {
          status = confit_terminal_handle_effect(model, effect, controller,
                                                 &done, message,
                                                 sizeof(message), diagnostic);
        }
      }
    }
    redraw = 1;
  }

cleanup:
  restored = confit_terminal_leave(&session);
  confit_terminal_restore_handlers(&session);
  g_confit_terminal_signal = 0;
  g_confit_terminal_resize = 0;
  if (!restored)
    status = confit_terminal_fail(diagnostic, CONFIT_ERR_TERMINAL,
                                  kTerminalRestoreFailed);
  return status;
}
