#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

typedef struct {
    char *id, *title, *body;
    GtkWidget *row, *label;
} Note;

typedef struct {
    GtkWidget *window, *list, *search, *title, *editor, *status;
    GtkWidget *new_button, *delete_button, *save_button;
    GtkTextBuffer *buffer;
    GPtrArray *notes;
    Note *current;
    char *path;
    int lock_fd;
    guint save_timer;
    gboolean updating, dirty, writable, warned;
} Notes;

static gboolean lock_notes(Notes *app, GError **error)
{
    char *path = g_strconcat(app->path, ".lock", NULL);
    int fd = g_open(path, O_CREAT | O_RDWR, 0600);
    g_free(path);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
            "Cannot open the notes lock: %s", g_strerror(errno));
        return FALSE;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;
        close(fd);
        if (saved_errno == EWOULDBLOCK || saved_errno == EAGAIN)
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_AGAIN,
                "Another LimeNotes window is using this store. Close it before reopening LimeNotes.");
        else
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                "Cannot lock the notes store: %s", g_strerror(saved_errno));
        return FALSE;
    }
    app->lock_fd = fd;
    return TRUE;
}

static void note_free(gpointer data)
{
    Note *note = data;
    g_free(note->id);
    g_free(note->title);
    g_free(note->body);
    g_free(note);
}

static gboolean fail_load(GError **error, const char *message)
{
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, message);
    return FALSE;
}

static gboolean load_notes(Notes *app, GError **error)
{
    GStatBuf statbuf;
    if (g_stat(app->path, &statbuf) == 0 && statbuf.st_size > 32 * 1024 * 1024)
        return fail_load(error, "The notes file exceeds the 32 MiB safety limit.");
    GKeyFile *file = g_key_file_new();
    GError *local = NULL;
    if (!g_key_file_load_from_file(file, app->path, G_KEY_FILE_NONE, &local)) {
        g_key_file_unref(file);
        if (g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_clear_error(&local);
            return TRUE;
        }
        g_propagate_error(error, local);
        return FALSE;
    }
    int version = g_key_file_get_integer(file, "store", "version", &local);
    if (local || version != 1) {
        g_clear_error(&local);
        g_key_file_unref(file);
        return fail_load(error, "The notes file has an unsupported or missing format version.");
    }
    gsize count = 0;
    char **groups = g_key_file_get_groups(file, &count);
    GPtrArray *loaded = g_ptr_array_new_with_free_func(note_free);
    gboolean valid = count <= 10001;
    for (gsize i = 0; valid && i < count; ++i) {
        if (strcmp(groups[i], "store") == 0)
            continue;
        if (!g_str_has_prefix(groups[i], "note:") ||
            !g_uuid_string_is_valid(groups[i] + 5)) {
            valid = FALSE;
            break;
        }
        Note *note = g_new0(Note, 1);
        note->id = g_strdup(groups[i] + 5);
        note->title = g_key_file_get_string(file, groups[i], "title", &local);
        if (!local)
            note->body = g_key_file_get_string(file, groups[i], "body", &local);
        if (local || !note->title || !note->body ||
            !g_utf8_validate(note->title, -1, NULL) ||
            !g_utf8_validate(note->body, -1, NULL) ||
            g_utf8_strlen(note->title, -1) > 256) {
            g_clear_error(&local);
            note_free(note);
            valid = FALSE;
            break;
        }
        g_ptr_array_add(loaded, note);
    }
    g_strfreev(groups);
    g_key_file_unref(file);
    if (!valid) {
        g_ptr_array_unref(loaded);
        return fail_load(error, "The notes file contains malformed data. It has been left untouched.");
    }
    g_ptr_array_unref(app->notes);
    app->notes = loaded;
    return TRUE;
}

static gboolean save_notes(Notes *app)
{
    if (!app->dirty || !app->writable)
        return !app->dirty;
    GKeyFile *file = g_key_file_new();
    g_key_file_set_integer(file, "store", "version", 1);
    for (guint i = 0; i < app->notes->len; ++i) {
        Note *note = g_ptr_array_index(app->notes, i);
        char *group = g_strconcat("note:", note->id, NULL);
        g_key_file_set_string(file, group, "title", note->title);
        g_key_file_set_string(file, group, "body", note->body);
        g_free(group);
    }
    gsize length = 0;
    char *contents = g_key_file_to_data(file, &length, NULL);
    GError *error = NULL;
    gboolean saved;
    if (length > 32 * 1024 * 1024) {
        g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_NOSPC,
            "The notes store exceeds 32 MiB. Shorten or remove a note before saving.");
        saved = FALSE;
    } else {
        saved = lime_save(app->path, contents, (gssize)length, &error);
    }
    g_free(contents);
    g_key_file_unref(file);
    if (saved) {
        app->dirty = FALSE;
        app->warned = FALSE;
        gtk_label_set_text(GTK_LABEL(app->status), "Saved locally · Plain text storage");
    } else {
        gtk_label_set_text(GTK_LABEL(app->status), "Could not save · Your changes are still open");
        if (!app->warned) {
            app->warned = TRUE;
            char *message = g_strdup_printf("Your notes could not be saved:\n%s\n\nYour changes remain open. Check the location and try Save again.", error->message);
            lime_error(GTK_WINDOW(app->window), message);
            g_free(message);
        }
        g_clear_error(&error);
    }
    return saved;
}

static gboolean save_timeout(gpointer data)
{
    Notes *app = data;
    app->save_timer = 0;
    save_notes(app);
    return G_SOURCE_REMOVE;
}

static void changed(Notes *app)
{
    app->dirty = TRUE;
    gtk_label_set_text(GTK_LABEL(app->status), "Saving…");
    if (app->save_timer)
        g_source_remove(app->save_timer);
    app->save_timer = g_timeout_add(700, save_timeout, app);
}

static gboolean filter_note(GtkListBoxRow *row, gpointer data)
{
    Notes *app = data;
    Note *note = g_object_get_data(G_OBJECT(row), "note");
    const char *query = gtk_entry_get_text(GTK_ENTRY(app->search));
    if (!*query)
        return TRUE;
    char *needle = g_utf8_casefold(query, -1);
    char *title = g_utf8_casefold(note->title, -1);
    char *body = g_utf8_casefold(note->body, -1);
    gboolean visible = strstr(title, needle) || strstr(body, needle);
    g_free(needle);
    g_free(title);
    g_free(body);
    return visible;
}

static void search_changed(GtkEditable *entry, gpointer data)
{
    (void)entry;
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(((Notes *)data)->list));
}

static void select_note(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    (void)list;
    Notes *app = data;
    app->current = row ? g_object_get_data(G_OBJECT(row), "note") : NULL;
    app->updating = TRUE;
    gtk_entry_set_text(GTK_ENTRY(app->title), app->current ? app->current->title : "");
    gtk_text_buffer_set_text(app->buffer, app->current ? app->current->body : "", -1);
    app->updating = FALSE;
    gboolean editable = app->current && app->writable;
    gtk_widget_set_sensitive(app->title, editable);
    gtk_widget_set_sensitive(app->editor, editable);
    gtk_widget_set_sensitive(app->delete_button, editable);
}

static void add_row(Notes *app, Note *note)
{
    note->row = gtk_list_box_row_new();
    note->label = lime_label(*note->title ? note->title : "Untitled note", NULL);
    gtk_label_set_xalign(GTK_LABEL(note->label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(note->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(note->label, 12);
    gtk_widget_set_margin_end(note->label, 12);
    gtk_widget_set_margin_top(note->label, 14);
    gtk_widget_set_margin_bottom(note->label, 14);
    gtk_container_add(GTK_CONTAINER(note->row), note->label);
    g_object_set_data(G_OBJECT(note->row), "note", note);
    gtk_container_add(GTK_CONTAINER(app->list), note->row);
    gtk_widget_show_all(note->row);
}

static void new_note(GtkButton *button, gpointer data)
{
    (void)button;
    Notes *app = data;
    if (!app->writable)
        return;
    if (app->notes->len >= 10000) {
        lime_error(GTK_WINDOW(app->window), "You have reached the limit of 10,000 notes. Remove a note before creating another.");
        return;
    }
    Note *note = g_new0(Note, 1);
    note->id = g_uuid_string_random();
    note->title = g_strdup("Untitled note");
    note->body = g_strdup("");
    g_ptr_array_add(app->notes, note);
    gtk_entry_set_text(GTK_ENTRY(app->search), "");
    add_row(app, note);
    gtk_list_box_select_row(GTK_LIST_BOX(app->list), GTK_LIST_BOX_ROW(note->row));
    gtk_widget_grab_focus(app->title);
    gtk_editable_select_region(GTK_EDITABLE(app->title), 0, -1);
    changed(app);
}

static void title_changed(GtkEditable *editable, gpointer data)
{
    Notes *app = data;
    if (app->updating || !app->current || !app->writable)
        return;
    g_free(app->current->title);
    app->current->title = g_strdup(gtk_entry_get_text(GTK_ENTRY(editable)));
    gtk_label_set_text(GTK_LABEL(app->current->label), *app->current->title ? app->current->title : "Untitled note");
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(app->list));
    changed(app);
}

static void body_changed(GtkTextBuffer *buffer, gpointer data)
{
    Notes *app = data;
    if (app->updating || !app->current || !app->writable)
        return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    g_free(app->current->body);
    app->current->body = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(app->list));
    changed(app);
}

static void delete_note(GtkButton *button, gpointer data)
{
    (void)button;
    Notes *app = data;
    if (!app->current || !app->writable)
        return;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE, "Delete this note?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "The note and its contents will be permanently removed.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL, "Delete", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_ACCEPT)
        return;
    Note *note = app->current;
    GtkWidget *row = note->row;
    gtk_list_box_unselect_all(GTK_LIST_BOX(app->list));
    app->current = NULL;
    gtk_widget_destroy(row);
    g_ptr_array_remove(app->notes, note);
    if (app->notes->len) {
        Note *first = g_ptr_array_index(app->notes, 0);
        gtk_list_box_select_row(GTK_LIST_BOX(app->list), GTK_LIST_BOX_ROW(first->row));
    }
    changed(app);
}

static void save_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Notes *app = data;
    if (app->save_timer) {
        g_source_remove(app->save_timer);
        app->save_timer = 0;
    }
    app->warned = FALSE;
    save_notes(app);
}

static gboolean close_window(GtkWidget *window, GdkEvent *event, gpointer data)
{
    (void)event;
    Notes *app = data;
    if (app->save_timer) {
        g_source_remove(app->save_timer);
        app->save_timer = 0;
    }
    if (save_notes(app))
        return FALSE;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "Close with unsaved changes?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "Your latest changes could not be saved. Keep this window open to retry.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Keep open", GTK_RESPONSE_CANCEL, "Close without saving", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response != GTK_RESPONSE_ACCEPT;
}

static gboolean key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    Notes *app = data;
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
        gtk_widget_grab_focus(app->search);
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    if (lime_cli(argc, argv, "LimeNotes"))
        return 0;
    gtk_init(&argc, &argv);
    lime_init();
    Notes app = {0};
    app.lock_fd = -1;
    app.notes = g_ptr_array_new_with_free_func(note_free);
    app.writable = TRUE;
    GError *error = NULL;
    app.path = lime_data_path("notes.ini", &error);
    if (!app.path || !lock_notes(&app, &error) || !load_notes(&app, &error))
        app.writable = FALSE;
    app.window = lime_window("LimeNotes", "A quiet place for your thoughts", 880, 580);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), root);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 12);
    app.search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.search), "Search notes…");
    gtk_box_pack_start(GTK_BOX(toolbar), app.search, TRUE, TRUE, 0);
    app.new_button = lime_button("New note", "suggested-action");
    app.save_button = lime_button("Save", "flat");
    app.delete_button = lime_button("Delete", "flat");
    gtk_box_pack_start(GTK_BOX(toolbar), app.new_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app.save_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app.delete_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 225);
    GtkWidget *sidebar = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    app.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(app.list), filter_note, &app, NULL);
    GtkWidget *placeholder = lime_label("No notes here.\nCreate one or adjust your search.", "muted");
    gtk_label_set_justify(GTK_LABEL(placeholder), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_top(placeholder, 30);
    gtk_widget_show(placeholder);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(app.list), placeholder);
    gtk_container_add(GTK_CONTAINER(sidebar), app.list);
    gtk_paned_pack1(GTK_PANED(paned), sidebar, FALSE, FALSE);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(page), 20);
    app.title = gtk_entry_new();
    lime_class(app.title, "title");
    gtk_entry_set_has_frame(GTK_ENTRY(app.title), FALSE);
    gtk_entry_set_max_length(GTK_ENTRY(app.title), 256);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.title), "Select or create a note");
    gtk_box_pack_start(GTK_BOX(page), app.title, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    app.editor = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app.editor), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app.editor), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app.editor), 16);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(app.editor), 5);
    app.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.editor));
    gtk_container_add(GTK_CONTAINER(scroll), app.editor);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), page, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);
    app.status = lime_label(app.writable ? "Saved locally · Plain text storage" : "Storage unavailable · Existing file left untouched", "muted");
    gtk_label_set_xalign(GTK_LABEL(app.status), 0);
    gtk_widget_set_margin_start(app.status, 14);
    gtk_widget_set_margin_top(app.status, 8);
    gtk_widget_set_margin_bottom(app.status, 10);
    gtk_box_pack_end(GTK_BOX(root), app.status, FALSE, FALSE, 0);
    g_signal_connect(app.list, "row-selected", G_CALLBACK(select_note), &app);
    g_signal_connect(app.search, "changed", G_CALLBACK(search_changed), &app);
    g_signal_connect(app.title, "changed", G_CALLBACK(title_changed), &app);
    g_signal_connect(app.buffer, "changed", G_CALLBACK(body_changed), &app);
    g_signal_connect(app.new_button, "clicked", G_CALLBACK(new_note), &app);
    g_signal_connect(app.delete_button, "clicked", G_CALLBACK(delete_note), &app);
    g_signal_connect(app.save_button, "clicked", G_CALLBACK(save_clicked), &app);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(close_window), &app);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(key_pressed), &app);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    GtkAccelGroup *accels = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(app.window), accels);
    gtk_widget_add_accelerator(app.new_button, "clicked", accels, GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(app.save_button, "clicked", accels, GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_object_unref(accels);
    for (guint i = 0; i < app.notes->len; ++i)
        add_row(&app, g_ptr_array_index(app.notes, i));
    select_note(GTK_LIST_BOX(app.list), NULL, &app);
    if (app.notes->len) {
        Note *first = g_ptr_array_index(app.notes, 0);
        gtk_list_box_select_row(GTK_LIST_BOX(app.list), GTK_LIST_BOX_ROW(first->row));
    }
    gtk_widget_set_sensitive(app.new_button, app.writable);
    gtk_widget_set_sensitive(app.save_button, app.writable);
    gtk_widget_show_all(app.window);
    lime_smoke_test(app.window);
    if (error) {
        char *message = g_strdup_printf("The notes store could not be opened:\n%s\n\nEditing is disabled to protect your saved notes. Close other LimeNotes windows or fix the file or permissions, then reopen LimeNotes.", error->message);
        lime_error(GTK_WINDOW(app.window), message);
        g_free(message);
        g_clear_error(&error);
    }
    gtk_main();
    if (app.save_timer)
        g_source_remove(app.save_timer);
    g_ptr_array_unref(app.notes);
    if (app.lock_fd >= 0)
        close(app.lock_fd);
    g_free(app.path);
    return 0;
}
