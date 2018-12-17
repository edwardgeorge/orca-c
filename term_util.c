#include "term_util.h"
#include <form.h>
#include <menu.h>

void term_util_init_colors() {
  if (has_colors()) {
    // Enable color
    start_color();
    use_default_colors();
    for (int ifg = 0; ifg < Colors_count; ++ifg) {
      for (int ibg = 0; ibg < Colors_count; ++ibg) {
        int res = init_pair((short int)(1 + ifg * Colors_count + ibg),
                            (short int)(ifg - 1), (short int)(ibg - 1));
        (void)res;
        // Might fail on Linux virtual console/terminal for a couple of colors.
        // Just ignore.
#if 0
        if (res == ERR) {
          endwin();
          fprintf(stderr, "Error initializing color pair: %d %d\n", ifg - 1,
                  ibg - 1);
          exit(1);
        }
#endif
      }
    }
  }
}

#define ORCA_CONTAINER_OF(ptr, type, member)                                   \
  ((type*)((char*)(1 ? (ptr) : &((type*)0)->member) - offsetof(type, member)))

struct Qmenu {
  Qblock qblock;
  MENU* ncurses_menu;
  ITEM* ncurses_items[32];
  Usz items_count;
  int id;
};

struct Qform {
  Qblock qblock;
  FORM* ncurses_form;
  FIELD* ncurses_fields[32];
  Usz fields_count;
  int id;
};

Qnav_stack qnav_stack;

static struct { int unused; } qmenu_spacer_user_unique;

void qnav_init() {
  qnav_stack.count = 0;
  qnav_stack.stack_changed = false;
  memset(qnav_stack.blocks, 0, sizeof(qnav_stack.blocks));
}
void qnav_deinit() {
  while (qnav_stack.count != 0)
    qnav_stack_pop();
}
void qnav_stack_push(Qblock_type_tag tag, int height, int width, Qblock* out) {
#ifndef NDEBUG
  for (Usz i = 0; i < qnav_stack.count; ++i) {
    assert(qnav_stack.blocks[i] != out);
  }
#endif
  int left;
  if (qnav_stack.count > 0) {
    WINDOW* w = qnav_stack.blocks[qnav_stack.count - 1]->outer_window;
    left = getbegx(w) + getmaxx(w) + 0;
  } else {
    left = 0;
  }
  qnav_stack.blocks[qnav_stack.count] = out;
  ++qnav_stack.count;
  out->title = NULL;
  out->outer_window = newwin(height + 2, width + 3, 0, left);
  out->content_window = derwin(out->outer_window, height, width, 1, 1);
  out->tag = tag;
  qnav_stack.stack_changed = true;
}

Qblock* qnav_top_block() {
  if (qnav_stack.count == 0)
    return NULL;
  return qnav_stack.blocks[qnav_stack.count - 1];
}
void qnav_free_block(Qblock* qb);
void qnav_stack_pop() {
  assert(qnav_stack.count > 0);
  if (qnav_stack.count == 0)
    return;
  Qblock* qb = qnav_stack.blocks[qnav_stack.count - 1];
  WINDOW* content_window = qb->content_window;
  WINDOW* outer_window = qb->outer_window;
  // erase any stuff underneath where this window is, in case it's outside of
  // the grid in an area that isn't actively redraw
  werase(outer_window);
  wnoutrefresh(outer_window);
  qnav_free_block(qb);
  delwin(content_window);
  delwin(outer_window);
  --qnav_stack.count;
  qnav_stack.blocks[qnav_stack.count] = NULL;
  qnav_stack.stack_changed = true;
}

void qmenu_free(Qmenu* qm);
void qform_free(Qform* qf);

void qnav_free_block(Qblock* qb) {
  switch (qb->tag) {
  case Qblock_type_qmsg: {
    Qmsg* qm = qmsg_of(qb);
    free(qm);
  } break;
  case Qblock_type_qmenu: {
    qmenu_free(qmenu_of(qb));
  } break;
  case Qblock_type_qform: {
    qform_free(qform_of(qb));
  } break;
  }
}

void qblock_print_border(Qblock* qb, unsigned int attr) {
  wborder(qb->outer_window, ACS_VLINE | attr, ACS_VLINE | attr,
          ACS_HLINE | attr, ACS_HLINE | attr, ACS_ULCORNER | attr,
          ACS_URCORNER | attr, ACS_LLCORNER | attr, ACS_LRCORNER | attr);
}

void qblock_print_title(Qblock* qb, char const* title, int attr) {
  wmove(qb->outer_window, 0, 1);
  attr_t attrs = A_NORMAL;
  short pair = 0;
  wattr_get(qb->outer_window, &attrs, &pair, NULL);
  wattrset(qb->outer_window, attr);
  waddch(qb->outer_window, ' ');
  wprintw(qb->outer_window, title);
  waddch(qb->outer_window, ' ');
  wattr_set(qb->outer_window, attrs, pair, NULL);
}

void qblock_set_title(Qblock* qb, char const* title) { qb->title = title; }

void qblock_print_frame(Qblock* qb, bool active) {
  qblock_print_border(qb, active ? A_NORMAL : A_DIM);
  if (qb->title) {
    qblock_print_title(qb, qb->title, active ? A_NORMAL : A_DIM);
  }
}

WINDOW* qmsg_window(Qmsg* qm) { return qm->qblock.content_window; }

void qmsg_set_title(Qmsg* qm, char const* title) {
  qblock_set_title(&qm->qblock, title);
}

Qmsg* qmsg_push(int height, int width) {
  Qmsg* qm = malloc(sizeof(Qmsg));
  qnav_stack_push(Qblock_type_qmsg, height, width, &qm->qblock);
  return qm;
}

bool qmsg_drive(Qmsg* qm, int key) {
  (void)qm;
  switch (key) {
  case ' ':
  case 27:
  case '\r':
  case KEY_ENTER:
    return true;
  }
  return false;
}

Qmsg* qmsg_of(Qblock* qb) { return ORCA_CONTAINER_OF(qb, Qmsg, qblock); }

Qmenu* qmenu_create(int id) {
  Qmenu* qm = (Qmenu*)malloc(sizeof(Qmenu));
  qm->ncurses_menu = NULL;
  qm->ncurses_items[0] = NULL;
  qm->items_count = 0;
  qm->id = id;
  return qm;
}
int qmenu_id(Qmenu const* qm) { return qm->id; }
void qmenu_set_title(Qmenu* qm, char const* title) {
  qblock_set_title(&qm->qblock, title);
}
void qmenu_add_choice(Qmenu* qm, char const* text, int id) {
  ITEM* item = new_item(text, NULL);
  set_item_userptr(item, (void*)(intptr_t)(id));
  qm->ncurses_items[qm->items_count] = item;
  ++qm->items_count;
  qm->ncurses_items[qm->items_count] = NULL;
}
void qmenu_add_spacer(Qmenu* qm) {
  ITEM* item = new_item(" ", NULL);
  item_opts_off(item, O_SELECTABLE);
  set_item_userptr(item, &qmenu_spacer_user_unique);
  qm->ncurses_items[qm->items_count] = item;
  ++qm->items_count;
  qm->ncurses_items[qm->items_count] = NULL;
}
void qmenu_push_to_nav(Qmenu* qm) {
  qm->ncurses_menu = new_menu(qm->ncurses_items);
  set_menu_mark(qm->ncurses_menu, " > ");
  set_menu_fore(qm->ncurses_menu, A_BOLD);
  set_menu_grey(qm->ncurses_menu, A_DIM);
  int menu_min_h, menu_min_w;
  scale_menu(qm->ncurses_menu, &menu_min_h, &menu_min_w);
  qnav_stack_push(Qblock_type_qmenu, menu_min_h, menu_min_w, &qm->qblock);
  set_menu_win(qm->ncurses_menu, qm->qblock.outer_window);
  set_menu_sub(qm->ncurses_menu, qm->qblock.content_window);
  post_menu(qm->ncurses_menu);
}

void qmenu_free(Qmenu* qm) {
  unpost_menu(qm->ncurses_menu);
  free_menu(qm->ncurses_menu);
  for (Usz i = 0; i < qm->items_count; ++i) {
    free_item(qm->ncurses_items[i]);
  }
  free(qm);
}

bool qmenu_drive(Qmenu* qm, int key, Qmenu_action* out_action) {
  switch (key) {
  case 27: {
    out_action->any.type = Qmenu_action_type_canceled;
    return true;
  }
  case ' ':
  case '\r':
  case KEY_ENTER: {
    ITEM* cur = current_item(qm->ncurses_menu);
    out_action->picked.type = Qmenu_action_type_picked;
    out_action->picked.id = cur ? (int)(intptr_t)item_userptr(cur) : 0;
    return true;
  } break;
  case KEY_UP: {
    ITEM* starting = current_item(qm->ncurses_menu);
    menu_driver(qm->ncurses_menu, REQ_UP_ITEM);
    for (;;) {
      ITEM* cur = current_item(qm->ncurses_menu);
      if (!cur || cur == starting)
        break;
      if (item_userptr(cur) != &qmenu_spacer_user_unique)
        break;
      menu_driver(qm->ncurses_menu, REQ_UP_ITEM);
    }
    return false;
  }
  case KEY_DOWN: {
    ITEM* starting = current_item(qm->ncurses_menu);
    menu_driver(qm->ncurses_menu, REQ_DOWN_ITEM);
    for (;;) {
      ITEM* cur = current_item(qm->ncurses_menu);
      if (!cur || cur == starting)
        break;
      if (item_userptr(cur) != &qmenu_spacer_user_unique)
        break;
      menu_driver(qm->ncurses_menu, REQ_DOWN_ITEM);
    }
    return false;
  }
  }
  return false;
}

Qmenu* qmenu_of(Qblock* qb) { return ORCA_CONTAINER_OF(qb, Qmenu, qblock); }

bool qmenu_top_is_menu(int id) {
  Qblock* qb = qnav_top_block();
  if (!qb)
    return false;
  if (qb->tag != Qblock_type_qmenu)
    return false;
  Qmenu* qm = qmenu_of(qb);
  return qm->id == id;
}

Qform* qform_create(int id) {
  Qform* qf = (Qform*)malloc(sizeof(Qform));
  qf->ncurses_form = NULL;
  qf->ncurses_fields[0] = NULL;
  qf->fields_count = 0;
  qf->id = id;
  return qf;
}

Qform* qform_of(Qblock* qb) { return ORCA_CONTAINER_OF(qb, Qform, qblock); }

int qform_id(Qform const* qf) { return qf->id; }

void qform_add_text_line(Qform* qf, int id, char const* initial) {
  FIELD* f = new_field(1, 20, 0, 0, 0, 0);
  set_field_buffer(f, 0, initial);
  set_field_userptr(f, (void*)(intptr_t)(id));
  qf->ncurses_fields[qf->fields_count] = f;
  ++qf->fields_count;
  qf->ncurses_fields[qf->fields_count] = NULL;
}

void qform_push_to_nav(Qform* qf) {
  qf->ncurses_form = new_form(qf->ncurses_fields);
  int form_min_h, form_min_w;
  scale_form(qf->ncurses_form, &form_min_h, &form_min_w);
  qnav_stack_push(Qblock_type_qform, form_min_h, form_min_w, &qf->qblock);
  set_form_win(qf->ncurses_form, qf->qblock.outer_window);
  set_form_sub(qf->ncurses_form, qf->qblock.content_window);
  post_form(qf->ncurses_form);
}

void qform_free(Qform* qf) {
  unpost_form(qf->ncurses_form);
  free_form(qf->ncurses_form);
  for (Usz i = 0; i < qf->fields_count; ++i) {
    free_field(qf->ncurses_fields[i]);
  }
  free(qf);
}

bool qform_drive(Qform* qf, int key, Qform_action* out_action) {
  (void)qf;
  switch (key) {
  case 27: {
    out_action->any.type = Qform_action_type_canceled;
    return true;
  }
  }
  return false;
}
