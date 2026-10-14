/* Exercise the real app persistence paths without opening a display. Only the
 * status label is replaced: serialization, validation and atomic writes run as
 * they do in the applications. Error dialogs are suppressed with warned=TRUE. */
#include "../src/common.h"
#include <glib/gstdio.h>
#include <string.h>

static void storage_status_text(GtkLabel *label, const gchar *text)
{
    (void)label;
    (void)text;
}

#define gtk_label_set_text storage_status_text
#define main notes_application_main
#define changed notes_changed
#define save_timeout notes_save_timeout
#define add_row notes_add_row
#define close_window notes_close_window
#define key_pressed notes_key_pressed
#include "../src/notes.c"
#undef main
#undef changed
#undef save_timeout
#undef add_row
#undef close_window
#undef key_pressed

#define main tasks_application_main
#define changed tasks_changed
#define save_timeout tasks_save_timeout
#define add_row tasks_add_row
#define close_window tasks_close_window
#define key_pressed tasks_key_pressed
#include "../src/tasks.c"
#undef main
#undef changed
#undef save_timeout
#undef add_row
#undef close_window
#undef key_pressed
#undef gtk_label_set_text

static const char *uuid = "611b6206-01a1-4429-9903-f67098ae2502";
static char *xdg_root;

static char *temporary_directory(void)
{
    GError *error = NULL;
    char *path = g_dir_make_tmp("lime-storage-test-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(path);
    return path;
}

static void assert_contents(const char *path, const char *expected)
{
    GError *error = NULL;
    char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(contents, ==, expected);
    g_free(contents);
}

static void test_data_directory(void)
{
    GError *error = NULL;
    char *path = lime_data_path("probe.ini", &error);
    g_assert_no_error(error);
    char *expected = g_build_filename(xdg_root, "lime-apps", "probe.ini", NULL);
    g_assert_cmpstr(path, ==, expected);
    char *parent = g_path_get_dirname(path);
    GStatBuf statbuf;
    g_assert_cmpint(g_stat(parent, &statbuf), ==, 0);
    g_assert_cmpint(statbuf.st_mode & 0777, ==, 0700);
    g_assert_true(lime_save(path, "private", -1, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_stat(path, &statbuf), ==, 0);
    g_assert_cmpint(statbuf.st_mode & 0777, ==, 0600);
    assert_contents(path, "private");
    g_assert_cmpint(g_remove(path), ==, 0);
    g_free(parent);
    g_free(expected);
    g_free(path);
}

static void test_notes_round_trip(void)
{
    char *directory = temporary_directory();
    Notes app = {0};
    app.path = g_build_filename(directory, "notes.ini", NULL);
    app.notes = g_ptr_array_new_with_free_func(note_free);
    app.writable = TRUE;
    GError *error = NULL;
    g_assert_true(load_notes(&app, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(app.notes->len, ==, 0);
    Note *note = g_new0(Note, 1);
    note->id = g_strdup(uuid);
    note->title = g_strdup("Идеи [=] 🍋");
    note->body = g_strdup("Первая строка\nSecond line\\ with ; punctuation\n");
    g_ptr_array_add(app.notes, note);
    app.dirty = TRUE;
    g_assert_true(save_notes(&app));
    g_assert_false(app.dirty);
    GStatBuf statbuf;
    g_assert_cmpint(g_stat(app.path, &statbuf), ==, 0);
    g_assert_cmpint(statbuf.st_mode & 0777, ==, 0600);
    g_assert_true(load_notes(&app, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(app.notes->len, ==, 1);
    Note *loaded = g_ptr_array_index(app.notes, 0);
    g_assert_cmpstr(loaded->id, ==, uuid);
    g_assert_cmpstr(loaded->title, ==, "Идеи [=] 🍋");
    g_assert_cmpstr(loaded->body, ==, "Первая строка\nSecond line\\ with ; punctuation\n");
    /* A deliberate deletion must survive a restart as an empty store. */
    g_ptr_array_set_size(app.notes, 0);
    app.dirty = TRUE;
    g_assert_true(save_notes(&app));
    g_assert_true(load_notes(&app, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(app.notes->len, ==, 0);
    g_ptr_array_unref(app.notes);
    g_assert_cmpint(g_remove(app.path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(app.path);
    g_free(directory);
}

static void test_notes_malformed(void)
{
    char *directory = temporary_directory();
    Notes app = {0};
    app.path = g_build_filename(directory, "notes.ini", NULL);
    app.notes = g_ptr_array_new_with_free_func(note_free);
    Note *retained = g_new0(Note, 1);
    retained->id = g_strdup(uuid);
    retained->title = g_strdup("Existing note");
    retained->body = g_strdup("Do not lose this");
    g_ptr_array_add(app.notes, retained);
    const char *bad[] = {
        "broken",
        "[store]\nversion=2\n",
        "[store]\nversion=1\n[note:invalid]\ntitle=x\nbody=y\n",
        "[store]\nversion=1\n[note:611b6206-01a1-4429-9903-f67098ae2502]\ntitle=x\n",
        "[store]\nversion=1\n[note:611b6206-01a1-4429-9903-f67098ae2502]\ntitle=x\nbody=bad\xff\n"
    };
    for (guint i = 0; i < G_N_ELEMENTS(bad); ++i) {
        GError *error = NULL;
        g_assert_true(g_file_set_contents(app.path, bad[i], -1, &error));
        g_assert_no_error(error);
        g_assert_false(load_notes(&app, &error));
        g_assert_nonnull(error);
        g_clear_error(&error);
        g_assert_cmpuint(app.notes->len, ==, 1);
        g_assert_true(g_ptr_array_index(app.notes, 0) == retained);
        g_assert_cmpstr(retained->body, ==, "Do not lose this");
        assert_contents(app.path, bad[i]);
    }
    g_ptr_array_unref(app.notes);
    g_assert_cmpint(g_remove(app.path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(app.path);
    g_free(directory);
}

static void test_tasks_round_trip(void)
{
    char *directory = temporary_directory();
    Tasks app = {0};
    app.path = g_build_filename(directory, "tasks.ini", NULL);
    app.tasks = g_ptr_array_new_with_free_func(task_free);
    app.writable = TRUE;
    GError *error = NULL;
    g_assert_true(load_tasks(&app, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(app.tasks->len, ==, 0);
    Task *task = g_new0(Task, 1);
    task->app = &app;
    task->id = g_strdup(uuid);
    task->text = g_strdup("Сделать LimeTasks 🍋 [=]");
    task->done = TRUE;
    g_ptr_array_add(app.tasks, task);
    app.dirty = TRUE;
    g_assert_true(save_tasks(&app));
    g_assert_false(app.dirty);
    GStatBuf statbuf;
    g_assert_cmpint(g_stat(app.path, &statbuf), ==, 0);
    g_assert_cmpint(statbuf.st_mode & 0777, ==, 0600);
    g_assert_true(load_tasks(&app, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(app.tasks->len, ==, 1);
    Task *loaded = g_ptr_array_index(app.tasks, 0);
    g_assert_cmpstr(loaded->id, ==, uuid);
    g_assert_cmpstr(loaded->text, ==, "Сделать LimeTasks 🍋 [=]");
    g_assert_true(loaded->done);
    loaded->done = FALSE;
    app.dirty = TRUE;
    g_assert_true(save_tasks(&app));
    g_assert_true(load_tasks(&app, &error));
    g_assert_no_error(error);
    loaded = g_ptr_array_index(app.tasks, 0);
    g_assert_cmpstr(loaded->id, ==, uuid);
    g_assert_false(loaded->done);
    g_ptr_array_unref(app.tasks);
    g_assert_cmpint(g_remove(app.path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(app.path);
    g_free(directory);
}

static void test_tasks_malformed(void)
{
    char *directory = temporary_directory();
    Tasks app = {0};
    app.path = g_build_filename(directory, "tasks.ini", NULL);
    app.tasks = g_ptr_array_new_with_free_func(task_free);
    Task *retained = g_new0(Task, 1);
    retained->id = g_strdup(uuid);
    retained->text = g_strdup("Do not lose this");
    g_ptr_array_add(app.tasks, retained);
    const char *bad[] = {
        "broken",
        "[store]\nversion=2\n",
        "[store]\nversion=1\n[task:invalid]\ntext=x\ndone=true\n",
        "[store]\nversion=1\n[task:611b6206-01a1-4429-9903-f67098ae2502]\ntext=x\ndone=maybe\n",
        "[store]\nversion=1\n[task:611b6206-01a1-4429-9903-f67098ae2502]\ntext=  \ndone=false\n",
        "[store]\nversion=1\n[task:611b6206-01a1-4429-9903-f67098ae2502]\ntext=bad\xff\ndone=false\n"
    };
    for (guint i = 0; i < G_N_ELEMENTS(bad); ++i) {
        GError *error = NULL;
        g_assert_true(g_file_set_contents(app.path, bad[i], -1, &error));
        g_assert_no_error(error);
        g_assert_false(load_tasks(&app, &error));
        g_assert_nonnull(error);
        g_clear_error(&error);
        g_assert_cmpuint(app.tasks->len, ==, 1);
        g_assert_true(g_ptr_array_index(app.tasks, 0) == retained);
        g_assert_cmpstr(retained->text, ==, "Do not lose this");
        assert_contents(app.path, bad[i]);
    }
    g_ptr_array_unref(app.tasks);
    g_assert_cmpint(g_remove(app.path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(app.path);
    g_free(directory);
}

static void test_failed_writes(void)
{
    char *directory = temporary_directory();
    char *blocker = g_build_filename(directory, "regular-file", NULL);
    GError *error = NULL;
    g_assert_true(g_file_set_contents(blocker, "Existing data", -1, &error));
    g_assert_no_error(error);
    /* A regular file cannot be a parent directory, even if tests run as root. */
    char *blocked_path = g_build_filename(blocker, "store.ini", NULL);
    Notes notes = {0};
    notes.path = blocked_path;
    notes.notes = g_ptr_array_new_with_free_func(note_free);
    notes.writable = notes.dirty = notes.warned = TRUE;
    g_assert_false(save_notes(&notes));
    g_assert_true(notes.dirty);
    Tasks tasks = {0};
    tasks.path = blocked_path;
    tasks.tasks = g_ptr_array_new_with_free_func(task_free);
    tasks.writable = tasks.dirty = tasks.warned = TRUE;
    g_assert_false(save_tasks(&tasks));
    g_assert_true(tasks.dirty);
    assert_contents(blocker, "Existing data");
    g_ptr_array_unref(notes.notes);
    g_ptr_array_unref(tasks.tasks);
    g_assert_cmpint(g_remove(blocker), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(blocked_path);
    g_free(blocker);
    g_free(directory);
}

static void test_exclusive_store_locks(void)
{
    char *directory = temporary_directory();
    Notes first_notes = {0}, second_notes = {0};
    first_notes.lock_fd = second_notes.lock_fd = -1;
    first_notes.path = g_build_filename(directory, "notes.ini", NULL);
    second_notes.path = first_notes.path;
    GError *error = NULL;
    g_assert_true(lock_notes(&first_notes, &error));
    g_assert_no_error(error);
    g_assert_false(lock_notes(&second_notes, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_AGAIN);
    g_clear_error(&error);
    g_assert_cmpint(second_notes.lock_fd, ==, -1);
    close(first_notes.lock_fd);
    g_assert_true(lock_notes(&second_notes, &error));
    g_assert_no_error(error);
    close(second_notes.lock_fd);
    Tasks first_tasks = {0}, second_tasks = {0};
    first_tasks.lock_fd = second_tasks.lock_fd = -1;
    first_tasks.path = g_build_filename(directory, "tasks.ini", NULL);
    second_tasks.path = first_tasks.path;
    g_assert_true(lock_tasks(&first_tasks, &error));
    g_assert_no_error(error);
    g_assert_false(lock_tasks(&second_tasks, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_AGAIN);
    g_clear_error(&error);
    g_assert_cmpint(second_tasks.lock_fd, ==, -1);
    close(first_tasks.lock_fd);
    g_assert_true(lock_tasks(&second_tasks, &error));
    g_assert_no_error(error);
    close(second_tasks.lock_fd);
    char *notes_lock = g_strconcat(first_notes.path, ".lock", NULL);
    char *tasks_lock = g_strconcat(first_tasks.path, ".lock", NULL);
    GStatBuf statbuf;
    g_assert_cmpint(g_stat(notes_lock, &statbuf), ==, 0);
    g_assert_cmpint(statbuf.st_mode & 0777, ==, 0600);
    g_assert_cmpint(g_remove(notes_lock), ==, 0);
    g_assert_cmpint(g_remove(tasks_lock), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(notes_lock);
    g_free(tasks_lock);
    g_free(first_notes.path);
    g_free(first_tasks.path);
    g_free(directory);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    xdg_root = temporary_directory();
    g_assert_true(g_setenv("XDG_DATA_HOME", xdg_root, TRUE));
    g_test_add_func("/storage/private-xdg-directory", test_data_directory);
    g_test_add_func("/storage/notes/round-trip-and-deletion", test_notes_round_trip);
    g_test_add_func("/storage/notes/malformed-preserves-data", test_notes_malformed);
    g_test_add_func("/storage/tasks/round-trip-and-completion", test_tasks_round_trip);
    g_test_add_func("/storage/tasks/malformed-preserves-data", test_tasks_malformed);
    g_test_add_func("/storage/failed-writes-retain-dirty-state", test_failed_writes);
    g_test_add_func("/storage/exclusive-instance-locks", test_exclusive_store_locks);
    int result = g_test_run();
    char *data = g_build_filename(xdg_root, "lime-apps", NULL);
    g_rmdir(data);
    g_rmdir(xdg_root);
    g_free(data);
    g_free(xdg_root);
    return result;
}
