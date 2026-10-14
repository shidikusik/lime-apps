#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

typedef struct Tasks Tasks;
typedef struct {
    Tasks *app;
    char *id, *text;
    gboolean done;
    GtkWidget *row, *check, *label;
} Task;

struct Tasks {
    GtkWidget *window, *entry, *add, *list, *counter, *status, *undo_bar;
    GPtrArray *tasks;
    Task *removed;
    guint removed_index, save_timer;
    int filter;
    char *path;
    int lock_fd;
    gboolean dirty, writable, warned;
};

static gboolean lock_tasks(Tasks *app, GError **error)
{
    char *path = g_strconcat(app->path, ".lock", NULL);
    int fd = g_open(path, O_CREAT | O_RDWR, 0600);
    g_free(path);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
            "Cannot open the tasks lock: %s", g_strerror(errno));
        return FALSE;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;
        close(fd);
        if (saved_errno == EWOULDBLOCK || saved_errno == EAGAIN)
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_AGAIN,
                "Another LimeTasks window is using this store. Close it before reopening LimeTasks.");
        else
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                "Cannot lock the tasks store: %s", g_strerror(saved_errno));
        return FALSE;
    }
    app->lock_fd = fd;
    return TRUE;
}

static void task_free(gpointer data)
{
    Task *task = data;
    g_free(task->id);
    g_free(task->text);
    g_free(task);
}

static gboolean load_error(GError **error, const char *message)
{
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, message);
    return FALSE;
}

static gboolean load_tasks(Tasks *app, GError **error)
{
    GStatBuf statbuf;
    if (g_stat(app->path, &statbuf) == 0 && statbuf.st_size > 8 * 1024 * 1024)
        return load_error(error, "The tasks file exceeds the 8 MiB safety limit.");
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
        return load_error(error, "The tasks file has an unsupported or missing format version.");
    }
    gsize count = 0;
    char **groups = g_key_file_get_groups(file, &count);
    GPtrArray *loaded = g_ptr_array_new_with_free_func(task_free);
    gboolean valid = count <= 10001;
    for (gsize i = 0; valid && i < count; ++i) {
        if (strcmp(groups[i], "store") == 0)
            continue;
        if (!g_str_has_prefix(groups[i], "task:") || !g_uuid_string_is_valid(groups[i] + 5)) {
            valid = FALSE;
            break;
        }
        Task *task = g_new0(Task, 1);
        task->app = app;
        task->id = g_strdup(groups[i] + 5);
        task->text = g_key_file_get_string(file, groups[i], "text", &local);
        if (!local)
            task->done = g_key_file_get_boolean(file, groups[i], "done", &local);
        if (local || !task->text || !g_utf8_validate(task->text, -1, NULL) ||
            g_utf8_strlen(task->text, -1) > 512 || strchr(task->text, '\n') || strchr(task->text, '\r')) {
            g_clear_error(&local);
            task_free(task);
            valid = FALSE;
            break;
        }
        char *trimmed = g_strdup(task->text);
        gboolean empty = !*g_strstrip(trimmed);
        g_free(trimmed);
        if (empty) {
            task_free(task);
            valid = FALSE;
            break;
        }
        g_ptr_array_add(loaded, task);
    }
    g_strfreev(groups);
    g_key_file_unref(file);
    if (!valid) {
        g_ptr_array_unref(loaded);
        return load_error(error, "The tasks file contains malformed data. It has been left untouched.");
    }
    g_ptr_array_unref(app->tasks);
    app->tasks = loaded;
    return TRUE;
}

static gboolean save_tasks(Tasks *app)
{
    if (!app->dirty || !app->writable)
        return !app->dirty;
    GKeyFile *file = g_key_file_new();
    g_key_file_set_integer(file, "store", "version", 1);
    for (guint i = 0; i < app->tasks->len; ++i) {
        Task *task = g_ptr_array_index(app->tasks, i);
        char *group = g_strconcat("task:", task->id, NULL);
        g_key_file_set_string(file, group, "text", task->text);
        g_key_file_set_boolean(file, group, "done", task->done);
        g_free(group);
    }
    gsize length = 0;
    char *contents = g_key_file_to_data(file, &length, NULL);
    GError *error = NULL;
    gboolean saved;
    if (length > 8 * 1024 * 1024) {
        g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_NOSPC,
            "The tasks store exceeds 8 MiB. Shorten or remove a task before saving.");
        saved = FALSE;
    } else {
        saved = lime_save(app->path, contents, (gssize)length, &error);
    }
    g_free(contents);
    g_key_file_unref(file);
    if (saved) {
        app->dirty = FALSE;
        app->warned = FALSE;
        gtk_label_set_text(GTK_LABEL(app->status), "Saved locally · One thing at a time");
    } else {
        gtk_label_set_text(GTK_LABEL(app->status), "Could not save · Press Ctrl+S to retry");
        if (!app->warned) {
            app->warned = TRUE;
            char *message = g_strdup_printf("Your tasks could not be saved:\n%s\n\nYour changes remain open. Check the location and press Ctrl+S to retry.", error->message);
            lime_error(GTK_WINDOW(app->window), message);
            g_free(message);
        }
        g_clear_error(&error);
    }
    return saved;
}

static gboolean save_timeout(gpointer data)
{
    Tasks *app = data;
    app->save_timer = 0;
    save_tasks(app);
    return G_SOURCE_REMOVE;
}

static void changed(Tasks *app)
{
    app->dirty = TRUE;
    gtk_label_set_text(GTK_LABEL(app->status), "Saving…");
    if (app->save_timer)
        g_source_remove(app->save_timer);
    app->save_timer = g_timeout_add(450, save_timeout, app);
}

static void update_count(Tasks *app)
{
    guint active = 0;
    for (guint i = 0; i < app->tasks->len; ++i) {
        Task *task = g_ptr_array_index(app->tasks, i);
        if (!task->done)
            ++active;
    }
    char *text = g_strdup_printf("%u active · %u completed", active, app->tasks->len - active);
    gtk_label_set_text(GTK_LABEL(app->counter), text);
    g_free(text);
}

static gboolean filter_task(GtkListBoxRow *row, gpointer data)
{
    Tasks *app = data;
    Task *task = g_object_get_data(G_OBJECT(row), "task");
    return app->filter == 0 || (app->filter == 1 && !task->done) || (app->filter == 2 && task->done);
}

static void update_task_style(Task *task)
{
    PangoAttrList *attrs = pango_attr_list_new();
    if (task->done) {
        pango_attr_list_insert(attrs, pango_attr_strikethrough_new(TRUE));
        gtk_style_context_add_class(gtk_widget_get_style_context(task->label), "muted");
    } else {
        gtk_style_context_remove_class(gtk_widget_get_style_context(task->label), "muted");
    }
    gtk_label_set_attributes(GTK_LABEL(task->label), attrs);
    pango_attr_list_unref(attrs);
}

static void toggle_task(GtkToggleButton *button, gpointer data)
{
    Task *task = data;
    if (!task->app->writable)
        return;
    task->done = gtk_toggle_button_get_active(button);
    update_task_style(task);
    update_count(task->app);
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(task->app->list));
    changed(task->app);
}

static void remove_task(GtkButton *button, gpointer data)
{
    (void)button;
    Task *task = data;
    Tasks *app = task->app;
    if (!app->writable)
        return;
    if (app->removed)
        task_free(app->removed);
    app->removed = task;
    for (guint i = 0; i < app->tasks->len; ++i) {
        if (g_ptr_array_index(app->tasks, i) == task) {
            app->removed_index = i;
            g_ptr_array_steal_index(app->tasks, i);
            break;
        }
    }
    gtk_widget_destroy(task->row);
    task->row = task->check = task->label = NULL;
    gtk_widget_show(app->undo_bar);
    update_count(app);
    changed(app);
}

static void add_row(Tasks *app, Task *task, int index)
{
    task->row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    task->check = gtk_check_button_new();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(task->check), task->done);
    char *accessible = g_strdup_printf("Mark %s complete", task->text);
    atk_object_set_name(gtk_widget_get_accessible(task->check), accessible);
    g_free(accessible);
    gtk_widget_set_tooltip_text(task->check, "Toggle completed");
    gtk_widget_set_sensitive(task->check, app->writable);
    task->label = lime_label(task->text, NULL);
    gtk_label_set_xalign(GTK_LABEL(task->label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(task->label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(task->label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable(GTK_LABEL(task->label), TRUE);
    gtk_widget_set_hexpand(task->label, TRUE);
    GtkWidget *remove = lime_button("×", "flat");
    gtk_widget_set_tooltip_text(remove, "Remove task (can be undone)");
    atk_object_set_name(gtk_widget_get_accessible(remove), "Remove task");
    gtk_widget_set_sensitive(remove, app->writable);
    gtk_box_pack_start(GTK_BOX(box), task->check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), task->label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), remove, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(task->row), box);
    g_object_set_data(G_OBJECT(task->row), "task", task);
    gtk_list_box_insert(GTK_LIST_BOX(app->list), task->row, index);
    g_signal_connect(task->check, "toggled", G_CALLBACK(toggle_task), task);
    g_signal_connect(remove, "clicked", G_CALLBACK(remove_task), task);
    update_task_style(task);
    gtk_widget_show_all(task->row);
}

static void add_task(GtkWidget *widget, gpointer data)
{
    (void)widget;
    Tasks *app = data;
    if (!app->writable)
        return;
    if (app->tasks->len >= 10000) {
        lime_error(GTK_WINDOW(app->window), "You have reached the limit of 10,000 tasks. Remove a task before creating another.");
        return;
    }
    char *text = g_strdup(gtk_entry_get_text(GTK_ENTRY(app->entry)));
    g_strstrip(text);
    if (!*text) {
        g_free(text);
        gtk_widget_grab_focus(app->entry);
        return;
    }
    Task *task = g_new0(Task, 1);
    task->app = app;
    task->id = g_uuid_string_random();
    task->text = text;
    g_ptr_array_add(app->tasks, task);
    add_row(app, task, -1);
    gtk_entry_set_text(GTK_ENTRY(app->entry), "");
    gtk_widget_grab_focus(app->entry);
    update_count(app);
    changed(app);
}

static void undo_remove(GtkButton *button, gpointer data)
{
    (void)button;
    Tasks *app = data;
    if (!app->removed || !app->writable)
        return;
    guint index = MIN(app->removed_index, app->tasks->len);
    Task *task = app->removed;
    app->removed = NULL;
    g_ptr_array_insert(app->tasks, (gint)index, task);
    add_row(app, task, (int)index);
    gtk_widget_hide(app->undo_bar);
    update_count(app);
    changed(app);
}

static void filter_changed(GtkToggleButton *button, gpointer data)
{
    Tasks *app = data;
    if (!gtk_toggle_button_get_active(button))
        return;
    app->filter = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "filter"));
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(app->list));
}

static gboolean key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    Tasks *app = data;
    if (!(event->state & GDK_CONTROL_MASK))
        return FALSE;
    if (event->keyval == GDK_KEY_n || event->keyval == GDK_KEY_N) {
        gtk_widget_grab_focus(app->entry);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S) {
        if (app->save_timer) {
            g_source_remove(app->save_timer);
            app->save_timer = 0;
        }
        app->warned = FALSE;
        save_tasks(app);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_z || event->keyval == GDK_KEY_Z) {
        if (app->removed && gtk_window_get_focus(GTK_WINDOW(app->window)) != app->entry) {
            undo_remove(NULL, app);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean close_window(GtkWidget *window, GdkEvent *event, gpointer data)
{
    (void)event;
    Tasks *app = data;
    if (app->save_timer) {
        g_source_remove(app->save_timer);
        app->save_timer = 0;
    }
    if (save_tasks(app))
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

int main(int argc, char **argv)
{
    if (lime_cli(argc, argv, "LimeTasks"))
        return 0;
    gtk_init(&argc, &argv);
    lime_init();
    Tasks app = {0};
    app.lock_fd = -1;
    app.tasks = g_ptr_array_new_with_free_func(task_free);
    app.writable = TRUE;
    GError *error = NULL;
    app.path = lime_data_path("tasks.ini", &error);
    if (!app.path || !lock_tasks(&app, &error) || !load_tasks(&app, &error))
        app.writable = FALSE;
    app.window = lime_window("LimeTasks", "Make room for what matters", 620, 620);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(root), 20);
    gtk_container_add(GTK_CONTAINER(app.window), root);
    GtkWidget *intro = lime_label("A little less on your mind.", "title");
    gtk_label_set_xalign(GTK_LABEL(intro), 0);
    gtk_box_pack_start(GTK_BOX(root), intro, FALSE, FALSE, 0);
    app.counter = lime_label("", "muted");
    gtk_label_set_xalign(GTK_LABEL(app.counter), 0);
    gtk_box_pack_start(GTK_BOX(root), app.counter, FALSE, FALSE, 0);
    GtkWidget *input = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app.entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(app.entry), 512);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry), "What needs doing? Press Enter to add");
    gtk_widget_set_sensitive(app.entry, app.writable);
    app.add = lime_button("Add task", "suggested-action");
    gtk_widget_set_sensitive(app.add, app.writable);
    gtk_box_pack_start(GTK_BOX(input), app.entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(input), app.add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), input, FALSE, FALSE, 0);
    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GSList *group = NULL;
    const char *names[] = {"All", "Active", "Done"};
    for (int i = 0; i < 3; ++i) {
        GtkWidget *button = gtk_radio_button_new_with_label(group, names[i]);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
        g_object_set_data(G_OBJECT(button), "filter", GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(filters), button, FALSE, FALSE, 0);
        g_signal_connect(button, "toggled", G_CALLBACK(filter_changed), &app);
    }
    gtk_box_pack_start(GTK_BOX(root), filters, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    app.list = gtk_list_box_new();
    lime_class(app.list, "card");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.list), GTK_SELECTION_NONE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(app.list), filter_task, &app, NULL);
    GtkWidget *empty = lime_label("A clear list. A clear head.\nAdd a task above, or try another filter.", "muted");
    gtk_label_set_justify(GTK_LABEL(empty), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_top(empty, 64);
    gtk_widget_set_margin_bottom(empty, 64);
    gtk_widget_show(empty);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(app.list), empty);
    gtk_container_add(GTK_CONTAINER(scroll), app.list);
    gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);
    app.undo_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *removed_label = lime_label("Task removed", "muted");
    GtkWidget *undo = lime_button("Undo", "flat");
    gtk_box_pack_start(GTK_BOX(app.undo_bar), removed_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(app.undo_bar), undo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), app.undo_bar, FALSE, FALSE, 0);
    gtk_widget_show(removed_label);
    gtk_widget_show(undo);
    gtk_widget_set_no_show_all(app.undo_bar, TRUE);
    app.status = lime_label(app.writable ? "Saved locally · One thing at a time" : "Storage unavailable · Existing file left untouched", "muted");
    gtk_label_set_xalign(GTK_LABEL(app.status), 0);
    gtk_box_pack_end(GTK_BOX(root), app.status, FALSE, FALSE, 0);
    g_signal_connect(app.entry, "activate", G_CALLBACK(add_task), &app);
    g_signal_connect(app.add, "clicked", G_CALLBACK(add_task), &app);
    g_signal_connect(undo, "clicked", G_CALLBACK(undo_remove), &app);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(key_pressed), &app);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(close_window), &app);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    for (guint i = 0; i < app.tasks->len; ++i)
        add_row(&app, g_ptr_array_index(app.tasks, i), -1);
    update_count(&app);
    gtk_widget_show_all(app.window);
    gtk_widget_grab_focus(app.entry);
    lime_smoke_test(app.window);
    if (error) {
        char *message = g_strdup_printf("The tasks store could not be opened:\n%s\n\nEditing is disabled to protect your saved tasks. Close other LimeTasks windows or fix the file or permissions, then reopen LimeTasks.", error->message);
        lime_error(GTK_WINDOW(app.window), message);
        g_free(message);
        g_clear_error(&error);
    }
    gtk_main();
    if (app.save_timer)
        g_source_remove(app.save_timer);
    if (app.removed)
        task_free(app.removed);
    g_ptr_array_unref(app.tasks);
    if (app.lock_fd >= 0)
        close(app.lock_fd);
    g_free(app.path);
    return 0;
}
