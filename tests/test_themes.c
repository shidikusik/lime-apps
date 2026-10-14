#include "common.h"
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

/* All settings and persistence tests use a temporary XDG directory. */
static char *config_directory;
static char *test_binary;
static gboolean have_display;

static GtkWidget *find_selector(GtkWidget *widget)
{
    if (GTK_IS_COMBO_BOX(widget) &&
        !g_strcmp0(gtk_widget_get_name(widget), "lime-theme-selector")) return widget;
    if (!GTK_IS_CONTAINER(widget)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *child = children; child && !found; child = child->next)
        found = find_selector(child->data);
    g_list_free(children);
    return found;
}

static void drain_events(void)
{
    while (g_main_context_iteration(NULL, FALSE)) {}
}

static GdkRGBA color(GtkWidget *widget, const char *name)
{
    GdkRGBA result;
    g_assert_true(gtk_style_context_lookup_color(gtk_widget_get_style_context(widget), name, &result));
    return result;
}

static void assert_palette(GtkWidget *window, const char *theme)
{
    GdkRGBA background = color(window, "lime_c0");
    GdkRGBA foreground = color(window, "lime_c1");
    GdkRGBA accent = color(window, "lime_c12");
    if (!strcmp(theme, "light")) {
        g_assert_cmpfloat(background.red, >, .9);
        g_assert_cmpfloat(foreground.red, <, .2);
    } else {
        g_assert_cmpfloat(background.red, <, .2);
        g_assert_cmpfloat(foreground.red, >, .8);
    }
    if (!strcmp(theme, "lime")) {
        g_assert_cmpfloat(accent.green, >, accent.red);
        g_assert_cmpfloat(accent.red, >, accent.blue);
    } else {
        for (guint i = 0; i < 35; ++i) {
            char *name = g_strdup_printf("lime_c%u", i);
            GdkRGBA value = color(window, name);
            g_assert_cmpfloat(fabs(value.red - value.green), <, .0001);
            g_assert_cmpfloat(fabs(value.green - value.blue), <, .0001);
            g_free(name);
        }
    }
}

static void assert_saved_theme(const char *expected)
{
    char *path = g_build_filename(config_directory, "lime-apps", "appearance.ini", NULL);
    GKeyFile *file = g_key_file_new();
    GError *error = NULL;
    g_assert_true(g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, &error));
    g_assert_no_error(error);
    char *saved = g_key_file_get_string(file, "Appearance", "theme", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(saved, ==, expected);
    g_free(saved);
    g_key_file_unref(file);
    g_free(path);
}

static void assert_fresh_process(const char *expected, const char *override)
{
    char **environment = g_get_environ();
    if (override) environment = g_environ_setenv(environment, "LIME_THEME", override, TRUE);
    else environment = g_environ_unsetenv(environment, "LIME_THEME");
    char *arguments[] = {test_binary, "--verify-theme", (char *)expected, NULL};
    char *output = NULL, *errors = NULL;
    gint status;
    GError *error = NULL;
    g_assert_true(g_spawn_sync(NULL, arguments, environment, G_SPAWN_DEFAULT, NULL, NULL,
                              &output, &errors, &status, &error));
    g_strfreev(environment);
    g_assert_no_error(error);
    if (!g_spawn_check_wait_status(status, &error))
        g_test_message("Theme subprocess: %s%s", output ? output : "", errors ? errors : "");
    g_assert_no_error(error);
    g_free(output);
    g_free(errors);
}

static void test_appearance(void)
{
    if (!have_display) {
        g_test_skip("A GTK display is required for appearance integration tests.");
        return;
    }
    lime_init();
    GtkWidget *window = lime_window("Theme test", "Appearance integration", 480, 320);
    GtkWidget *selector = find_selector(gtk_window_get_titlebar(GTK_WINDOW(window)));
    g_assert_nonnull(selector);
    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(selector)), ==, "system");
    const char *themes[] = {"light", "dark", "lime", "system"};
    for (guint i = 0; i < G_N_ELEMENTS(themes); ++i) {
        g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(selector), themes[i]));
        drain_events();
        assert_saved_theme(themes[i]);
        if (strcmp(themes[i], "system")) assert_palette(window, themes[i]);
        assert_fresh_process(themes[i], NULL);
    }

    /* Test environment overrides and invalid-value fallback independently of the selector. */
    assert_fresh_process("dark", "dark");
    assert_fresh_process("light", "light");
    assert_fresh_process("lime", "lime");
    assert_fresh_process("system", "invalid-value");

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source,
        "org.gnome.desktop.interface", TRUE) : NULL;
    GSettings *desktop = schema && g_settings_schema_has_key(schema, "color-scheme")
        ? g_settings_new_full(schema, NULL, NULL) : NULL;
    if (desktop) {
        g_assert_true(g_settings_set_string(desktop, "color-scheme", "default"));
        drain_events();
    }
    GtkSettings *gtk = gtk_settings_get_default();
    g_object_set(gtk, "gtk-theme-name", "Adwaita", "gtk-application-prefer-dark-theme", FALSE, NULL);
    drain_events();
    assert_palette(window, "light");
    g_object_set(gtk, "gtk-application-prefer-dark-theme", TRUE, NULL);
    drain_events();
    assert_palette(window, "dark");
    g_object_set(gtk, "gtk-application-prefer-dark-theme", FALSE, NULL);
    drain_events();
    assert_palette(window, "light");
    if (desktop) {
        g_assert_true(g_settings_set_string(desktop, "color-scheme", "prefer-dark"));
        drain_events();
        assert_palette(window, "dark");
        g_assert_true(g_settings_set_string(desktop, "color-scheme", "prefer-light"));
        drain_events();
        assert_palette(window, "light");
        g_object_unref(desktop);
    }
    if (schema) g_settings_schema_unref(schema);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(selector), "lime");
    drain_events();
    g_object_set(gtk, "gtk-application-prefer-dark-theme", TRUE, NULL);
    drain_events();
    assert_palette(window, "lime");
    gtk_widget_destroy(window);
}

int main(int argc, char **argv)
{
    /* Never alter the host's appearance or contact its desktop settings service. */
    g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
    g_setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-lime-theme-test", TRUE);
    g_setenv("NO_AT_BRIDGE", "1", TRUE);
    if (argc == 3 && !strcmp(argv[1], "--verify-theme")) {
        const char *expected = argv[2];
        gtk_init(&argc, &argv);
        lime_init();
        GtkWidget *window = lime_window("Theme probe", "", 300, 200);
        GtkWidget *selector = find_selector(gtk_window_get_titlebar(GTK_WINDOW(window)));
        g_assert_nonnull(selector);
        g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(selector)), ==, expected);
        if (strcmp(expected, "system")) assert_palette(window, expected);
        gtk_widget_destroy(window);
        return 0;
    }
    GError *error = NULL;
    config_directory = g_dir_make_tmp("lime-appearance-test-XXXXXX", &error);
    g_assert_no_error(error);
    g_setenv("XDG_CONFIG_HOME", config_directory, TRUE);
    g_setenv("LIME_THEME", "system", TRUE);
    test_binary = g_canonicalize_filename(argv[0], NULL);
    g_test_init(&argc, &argv, NULL);
    have_display = gtk_init_check(&argc, &argv);
    g_test_add_func("/appearance/palettes-persistence-system-changes", test_appearance);
    int result = g_test_run();
    char *path = g_build_filename(config_directory, "lime-apps", "appearance.ini", NULL);
    char *directory = g_path_get_dirname(path);
    g_remove(path);
    g_rmdir(directory);
    g_rmdir(config_directory);
    g_free(path);
    g_free(directory);
    g_free(config_directory);
    g_free(test_binary);
    return result;
}
