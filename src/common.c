#include "common.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char css[] =
"window { background: @lime_c0; color: @lime_c1; }"
"headerbar { background: @lime_c2; color: @lime_c1; border-bottom: 1px solid @lime_c3; box-shadow: none; min-height: 34px; padding: 4px 8px; }"
"headerbar .title { font-size: 12px; font-weight: 600; }"
"headerbar .subtitle { font-size: 10px; color: @lime_c4; }"
"button { background: @lime_c5; color: @lime_c1; border: 1px solid @lime_c6; border-radius: 6px; box-shadow: none; text-shadow: none; padding: 7px 12px; min-height: 20px; }"
"button:hover { background: @lime_c7; border-color: @lime_c8; }"
"button:active { background: @lime_c9; }"
"button:checked { background: @lime_c12; color: @lime_c13; border-color: @lime_c12; }"
"button:disabled { color: @lime_c10; background: @lime_c11; }"
"button.flat { background: transparent; border-color: transparent; }"
"button.suggested-action, button.accent { background: @lime_c12; color: @lime_c13; border-color: @lime_c12; font-weight: 600; }"
"button.suggested-action:hover, button.accent:hover { background: @lime_c14; }"
"button.danger { color: @lime_c15; }"
"entry, textview, textview text { background: @lime_c16; color: @lime_c1; caret-color: @lime_c12; }"
"entry { border: 1px solid @lime_c17; border-radius: 6px; box-shadow: none; padding: 8px 10px; }"
"entry:focus { border-color: @lime_c18; }"
"entry selection, textview text selection { background: @lime_c19; color: @lime_c20; }"
"textview { padding: 12px; }"
".title { font-size: 22px; font-weight: 600; }"
".subtitle, .muted { color: @lime_c4; }"
".accent { color: @lime_c12; }"
".card { background: @lime_c21; border: 1px solid @lime_c17; border-radius: 8px; padding: 12px; }"
".display { font-size: 34px; font-weight: 300; font-family: monospace; }"
"button.key { font-size: 19px; min-width: 42px; min-height: 36px; }"
"button.operator { background: @lime_c22; color: @lime_c23; }"
"list, list row { background: @lime_c24; color: @lime_c1; }"
"list row { border-bottom: 1px solid @lime_c25; padding: 7px; }"
"list row:selected { background: @lime_c26; }"
"list row:hover { background: @lime_c27; }"
"scrolledwindow { border-color: @lime_c28; }"
"separator { background: @lime_c17; min-width: 1px; min-height: 1px; }"
"stack, notebook, notebook > stack { background: @lime_c0; }"
"notebook > header { background: @lime_c29; border-color: @lime_c17; }"
"notebook > header tab { color: @lime_c4; padding: 7px 16px; }"
"notebook > header tab:checked { color: @lime_c30; border-color: @lime_c12; box-shadow: inset 0 -2px @lime_c12; }"
"spinbutton { background: @lime_c29; color: @lime_c1; border-color: @lime_c6; }"
"calendar { background: @lime_c21; color: @lime_c1; border: 1px solid @lime_c17; padding: 8px; }"
"calendar:selected { background: @lime_c12; color: @lime_c13; }"
"checkbutton check { background: @lime_c31; border: 1px solid @lime_c32; border-radius: 4px; }"
"checkbutton check:checked { background: @lime_c12; color: @lime_c13; }"
"radiobutton radio { background: @lime_c31; color: @lime_c13; border: 1px solid @lime_c32; }"
"radiobutton radio:checked { background: @lime_c12; color: @lime_c13; }"
"menu, popover { background: @lime_c21; color: @lime_c1; border-color: @lime_c17; }"
"menuitem { background: transparent; color: @lime_c1; }"
"menuitem:hover, menuitem:selected, modelbutton:hover, modelbutton:selected { background: @lime_c12; color: @lime_c13; }"
"selection { background: @lime_c19; color: @lime_c20; }"
"infobar, infobar.info, infobar.warning, infobar.error, infobar revealer > box { background: @lime_c22; color: @lime_c1; border-color: @lime_c17; }"
".linked > button { border-color: @lime_c6; }"
"scrollbar { background: transparent; }"
"scrollbar slider { background: @lime_c33; border-radius: 6px; min-width: 5px; min-height: 5px; }"
"tooltip { background: @lime_c22; color: @lime_c1; border: 1px solid @lime_c34; }";

static const char *palettes[] = {
    "@define-color lime_c0 #f6f6f6;@define-color lime_c1 #202020;@define-color lime_c2 #eeeeee;@define-color lime_c3 #c9c9c9;@define-color lime_c4 #666666;@define-color lime_c5 #ffffff;@define-color lime_c6 #cccccc;@define-color lime_c7 #e8e8e8;@define-color lime_c8 #939393;@define-color lime_c9 #dddddd;@define-color lime_c10 #999999;@define-color lime_c11 #eeeeee;@define-color lime_c12 #252525;@define-color lime_c13 #ffffff;@define-color lime_c14 #444444;@define-color lime_c15 #4d4d4d;@define-color lime_c16 #ffffff;@define-color lime_c17 #cccccc;@define-color lime_c18 #555555;@define-color lime_c19 #cfcfcf;@define-color lime_c20 #151515;@define-color lime_c21 #ffffff;@define-color lime_c22 #e9e9e9;@define-color lime_c23 #252525;@define-color lime_c24 #f1f1f1;@define-color lime_c25 #dfdfdf;@define-color lime_c26 #d8d8d8;@define-color lime_c27 #e6e6e6;@define-color lime_c28 #cecece;@define-color lime_c29 #ededed;@define-color lime_c30 #202020;@define-color lime_c31 #ffffff;@define-color lime_c32 #888888;@define-color lime_c33 #b5b5b5;@define-color lime_c34 #999999;",
    "@define-color lime_c0 #1c1c1c;@define-color lime_c1 #ededed;@define-color lime_c2 #252525;@define-color lime_c3 #3c3c3c;@define-color lime_c4 #aaaaaa;@define-color lime_c5 #2c2c2c;@define-color lime_c6 #434343;@define-color lime_c7 #393939;@define-color lime_c8 #727272;@define-color lime_c9 #454545;@define-color lime_c10 #777777;@define-color lime_c11 #242424;@define-color lime_c12 #e5e5e5;@define-color lime_c13 #202020;@define-color lime_c14 #ffffff;@define-color lime_c15 #c3c3c3;@define-color lime_c16 #171717;@define-color lime_c17 #3e3e3e;@define-color lime_c18 #a8a8a8;@define-color lime_c19 #575757;@define-color lime_c20 #ffffff;@define-color lime_c21 #252525;@define-color lime_c22 #363636;@define-color lime_c23 #e0e0e0;@define-color lime_c24 #222222;@define-color lime_c25 #303030;@define-color lime_c26 #454545;@define-color lime_c27 #303030;@define-color lime_c28 #383838;@define-color lime_c29 #242424;@define-color lime_c30 #e5e5e5;@define-color lime_c31 #292929;@define-color lime_c32 #7c7c7c;@define-color lime_c33 #555555;@define-color lime_c34 #686868;",
    "@define-color lime_c0 #1c1c1c;@define-color lime_c1 #ededed;@define-color lime_c2 #252525;@define-color lime_c3 #3c3c3c;@define-color lime_c4 #aaaaaa;@define-color lime_c5 #2c2c2c;@define-color lime_c6 #434343;@define-color lime_c7 #393939;@define-color lime_c8 #727272;@define-color lime_c9 #454545;@define-color lime_c10 #777777;@define-color lime_c11 #242424;@define-color lime_c12 #c6df9c;@define-color lime_c13 #20281a;@define-color lime_c14 #d9edb8;@define-color lime_c15 #c3c3c3;@define-color lime_c16 #171717;@define-color lime_c17 #3e3e3e;@define-color lime_c18 #c6df9c;@define-color lime_c19 #4c613d;@define-color lime_c20 #ffffff;@define-color lime_c21 #252525;@define-color lime_c22 #363c32;@define-color lime_c23 #d4e8b6;@define-color lime_c24 #222222;@define-color lime_c25 #303030;@define-color lime_c26 #364230;@define-color lime_c27 #2d332b;@define-color lime_c28 #383838;@define-color lime_c29 #242424;@define-color lime_c30 #c6df9c;@define-color lime_c31 #292929;@define-color lime_c32 #718064;@define-color lime_c33 #555555;@define-color lime_c34 #686868;"
};

static GtkCssProvider *theme_provider;
static char *theme_name;
static int portal_scheme;
static GSettings *desktop_settings;

static gboolean system_is_dark(void) {
    if (portal_scheme) return portal_scheme == 1;
    if (desktop_settings) {
        char *scheme = g_settings_get_string(desktop_settings, "color-scheme");
        gboolean dark = !strcmp(scheme, "prefer-dark");
        gboolean light = !strcmp(scheme, "prefer-light");
        g_free(scheme);
        if (dark || light) return dark;
    }
    char *name = NULL;
    gboolean prefer_dark = FALSE;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &name,
                 "gtk-application-prefer-dark-theme", &prefer_dark, NULL);
    char *lower = name ? g_ascii_strdown(name, -1) : NULL;
    gboolean result = prefer_dark || (lower && strstr(lower, "dark"));
    g_free(lower); g_free(name);
    return result;
}

static void apply_theme(void) {
    int palette = !strcmp(theme_name, "lime") ? 2 :
        (!strcmp(theme_name, "dark") || (!strcmp(theme_name, "system") && system_is_dark()) ? 1 : 0);
    char *stylesheet = g_strconcat(palettes[palette], css, NULL);
    GError *error = NULL;
    if (!gtk_css_provider_load_from_data(theme_provider, stylesheet, -1, &error)) {
        g_printerr("Lime Apps theme: %s\n", error->message);
        g_clear_error(&error);
    }
    g_free(stylesheet);
}

static void system_theme_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)object; (void)pspec; (void)data;
    if (!strcmp(theme_name, "system")) apply_theme();
}
static void desktop_theme_changed(GSettings *settings, char *key, gpointer data) {
    (void)settings; (void)key; (void)data;
    if (!strcmp(theme_name, "system")) apply_theme();
}
static void accept_portal_scheme(GVariant *value) {
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
        guint32 scheme = g_variant_get_uint32(value);
        portal_scheme = scheme <= 2 ? (int)scheme : 0;
        if (!strcmp(theme_name, "system")) apply_theme();
    }
}
static void portal_read(GObject *object, GAsyncResult *result, gpointer data) {
    (void)data;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, NULL);
    if (!reply) return;
    GVariant *value = NULL;
    g_variant_get(reply, "(v)", &value);
    while (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
        GVariant *inner = g_variant_get_variant(value);
        g_variant_unref(value); value = inner;
    }
    accept_portal_scheme(value);
    g_variant_unref(value); g_variant_unref(reply);
}
static void portal_changed(GDBusConnection *connection, const char *sender,
    const char *path, const char *interface, const char *signal, GVariant *parameters, gpointer data) {
    (void)connection; (void)sender; (void)path; (void)interface; (void)signal; (void)data;
    const char *space, *key; GVariant *value;
    g_variant_get(parameters, "(&s&sv)", &space, &key, &value);
    if (!strcmp(space, "org.freedesktop.appearance") && !strcmp(key, "color-scheme")) accept_portal_scheme(value);
    g_variant_unref(value);
}
static void portal_ready(GObject *object, GAsyncResult *result, gpointer data) {
    (void)object; (void)data;
    GDBusConnection *connection = g_bus_get_finish(result, NULL);
    if (!connection) return;
    g_dbus_connection_signal_subscribe(connection, "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Settings", "SettingChanged", "/org/freedesktop/portal/desktop",
        NULL, G_DBUS_SIGNAL_FLAGS_NONE, portal_changed, NULL, NULL);
    g_dbus_connection_call(connection, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "Read", g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 1500, NULL, portal_read, NULL);
    g_object_unref(connection);
}

static gboolean valid_theme(const char *name) {
    return name && (!strcmp(name, "system") || !strcmp(name, "light") || !strcmp(name, "dark") || !strcmp(name, "lime"));
}
void lime_init(void) {
    char *config = g_build_filename(g_get_user_config_dir(), "lime-apps", "appearance.ini", NULL);
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, config, G_KEY_FILE_NONE, NULL))
        theme_name = g_key_file_get_string(key, "Appearance", "theme", NULL);
    g_key_file_unref(key); g_free(config);
    const char *override = g_getenv("LIME_THEME");
    if (valid_theme(override)) { g_free(theme_name); theme_name = g_strdup(override); }
    if (!valid_theme(theme_name)) { g_free(theme_name); theme_name = g_strdup("system"); }
    theme_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(theme_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : NULL;
    if (schema) {
        if (g_settings_schema_has_key(schema, "color-scheme")) {
            desktop_settings = g_settings_new_full(schema, NULL, NULL);
            g_signal_connect(desktop_settings, "changed::color-scheme", G_CALLBACK(desktop_theme_changed), NULL);
        }
        g_settings_schema_unref(schema);
    }
    g_signal_connect(gtk_settings_get_default(), "notify::gtk-theme-name", G_CALLBACK(system_theme_changed), NULL);
    g_signal_connect(gtk_settings_get_default(), "notify::gtk-application-prefer-dark-theme", G_CALLBACK(system_theme_changed), NULL);
    apply_theme();
    g_bus_get(G_BUS_TYPE_SESSION, NULL, portal_ready, NULL);
}
static void theme_selected(GtkComboBox *combo, gpointer data) {
    const char *selected = gtk_combo_box_get_active_id(combo);
    if (!valid_theme(selected)) return;
    g_free(theme_name); theme_name = g_strdup(selected);
    apply_theme();
    char *dir = g_build_filename(g_get_user_config_dir(), "lime-apps", NULL);
    char *path = g_build_filename(dir, "appearance.ini", NULL);
    GError *error = NULL;
    if (g_mkdir_with_parents(dir, 0700) != 0)
        g_set_error(&error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot save appearance: %s", g_strerror(errno));
    else {
        char *content = g_strdup_printf("[Appearance]\ntheme=%s\n", selected);
        lime_save(path, content, -1, &error);
        g_free(content);
    }
    if (error) { lime_error(GTK_WINDOW(data), error->message); g_clear_error(&error); }
    g_free(dir); g_free(path);
}

GtkWidget *lime_window(const char *title, const char *subtitle, int width, int height) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    char *icon = g_ascii_strdown(title, -1);
    gtk_window_set_icon_name(GTK_WINDOW(window), icon);
    g_free(icon);
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), title);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), subtitle);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    GtkWidget *themes = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(themes), "system", "System");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(themes), "light", "Light");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(themes), "dark", "Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(themes), "lime", "Lime");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(themes), theme_name);
    gtk_widget_set_tooltip_text(themes, "Appearance · System / Light / Dark / Lime");
    gtk_widget_set_name(themes, "lime-theme-selector");
    g_signal_connect(themes, "changed", G_CALLBACK(theme_selected), window);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), themes);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    return window;
}

void lime_class(GtkWidget *widget, const char *name) {
    if (name && *name) gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
}
GtkWidget *lime_button(const char *label, const char *css_class) {
    GtkWidget *button = gtk_button_new_with_label(label);
    lime_class(button, css_class);
    return button;
}
GtkWidget *lime_label(const char *text, const char *css_class) {
    GtkWidget *label = gtk_label_new(text);
    lime_class(label, css_class);
    return label;
}
void lime_error(GtkWindow *parent, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
char *lime_data_path(const char *filename, GError **error) {
    char *directory = g_build_filename(g_get_user_data_dir(), "lime-apps", NULL);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create data directory: %s", g_strerror(errno));
        g_free(directory);
        return NULL;
    }
    char *path = g_build_filename(directory, filename, NULL);
    g_free(directory);
    return path;
}
gboolean lime_save(const char *path, const char *contents, gssize length, GError **error) {
    return g_file_set_contents_full(path, contents, length,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
}
gboolean lime_cli(int argc, char **argv, const char *name) {
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        g_print("%s %s\n", name, LIME_VERSION); return TRUE;
    }
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        g_print("%s %s — lightweight native Lime Apps\n", name, LIME_VERSION);
        g_print("Options: --help, --version\n");
        if (!strcmp(name, "LimeCalc")) g_print("  --eval EXPR    Evaluate an expression without a display\n");
        if (!strcmp(name, "LimeView")) g_print("  FILE           Open an image\n");
        return TRUE;
    }
    return FALSE;
}
static gboolean smoke_close(gpointer data) {
    gtk_widget_destroy(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}
void lime_smoke_test(GtkWidget *window) {
    if (g_getenv("LIME_SMOKE_TEST")) g_timeout_add(800, smoke_close, window);
}
