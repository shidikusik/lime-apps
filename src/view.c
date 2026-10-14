#include "common.h"
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <math.h>
#include <sys/stat.h>

/* One capped pixbuf is kept in memory; zooming is performed by Cairo. */
#define MAX_DECODE_DIM 4096
#define MAX_FILE_BYTES ((goffset)128 * 1024 * 1024)
#define MAX_IMAGE_PIXELS ((guint64)64 * 1024 * 1024)

typedef struct {
    GtkWidget *window, *scroll, *canvas, *status, *empty, *fit_button;
    GtkWidget *previous, *next, *zoom_out, *zoom_in, *rotate;
    GdkPixbuf *pixbuf;
    GPtrArray *files;
    GHashTable *extensions;
    char *path;
    int index, source_width, source_height;
    double scale;
    gboolean fit, fullscreen;
} Viewer;

static void update_size(Viewer *v);

static gboolean image_extension(Viewer *v, const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1]) return FALSE;
    char *extension = g_ascii_strdown(dot + 1, -1);
    gboolean supported = g_hash_table_contains(v->extensions, extension);
    g_free(extension);
    return supported;
}

static gint compare_paths(gconstpointer a, gconstpointer b)
{
    const char *pa = *(char * const *)a, *pb = *(char * const *)b;
    char *da = g_filename_display_basename(pa);
    char *db = g_filename_display_basename(pb);
    gint result = g_utf8_collate(da, db);
    g_free(da);
    g_free(db);
    return result;
}

static void rebuild_directory(Viewer *v, const char *path)
{
    g_ptr_array_set_size(v->files, 0);
    char *directory = g_path_get_dirname(path);
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir))) {
            if (image_extension(v, name)) {
                char *candidate = g_build_filename(directory, name, NULL);
                if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
                    g_ptr_array_add(v->files, candidate);
                else
                    g_free(candidate);
            }
        }
        g_dir_close(dir);
    }
    gboolean included = FALSE;
    for (guint i = 0; i < v->files->len; ++i)
        if (g_str_equal(g_ptr_array_index(v->files, i), path)) included = TRUE;
    if (!included) g_ptr_array_add(v->files, g_strdup(path));
    g_ptr_array_sort(v->files, compare_paths);
    g_free(directory);
}

static void update_status(Viewer *v)
{
    if (!v->pixbuf) return;
    char *base = g_filename_display_basename(v->path);
    char *text = g_strdup_printf("%s  ·  %d × %d  ·  %.0f%%  ·  %d / %u%s",
        base, v->source_width, v->source_height, v->scale * 100.,
        v->index + 1, v->files->len,
        MAX(v->source_width, v->source_height) > MAX_DECODE_DIM
            ? "  ·  memory-saving preview" : "");
    gtk_label_set_text(GTK_LABEL(v->status), text);
    gtk_widget_set_tooltip_text(v->status, v->path);
    g_free(base);
    g_free(text);
}

static void update_size(Viewer *v)
{
    if (!v->pixbuf) return;
    int width = gdk_pixbuf_get_width(v->pixbuf);
    int height = gdk_pixbuf_get_height(v->pixbuf);
    if (v->fit) {
        GtkAllocation allocation;
        gtk_widget_get_allocation(v->scroll, &allocation);
        v->scale = MIN(1., MIN(MAX(1, allocation.width - 24) / (double)width,
                              MAX(1, allocation.height - 24) / (double)height));
    }
    gtk_widget_set_size_request(v->canvas,
        MAX(1, (int)ceil(width * v->scale)),
        MAX(1, (int)ceil(height * v->scale)));
    gtk_widget_queue_draw(v->canvas);
    update_status(v);
}

static gboolean draw_image(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    (void)widget;
    Viewer *v = data;
    if (v->pixbuf) {
        cairo_scale(cr, v->scale, v->scale);
        gdk_cairo_set_source_pixbuf(cr, v->pixbuf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
    }
    return FALSE;
}

static void scroll_allocated(GtkWidget *widget, GtkAllocation *allocation, gpointer data)
{
    (void)widget;
    (void)allocation;
    Viewer *v = data;
    if (v->fit) update_size(v);
}

static gboolean load_file(Viewer *v, const char *filename, gboolean rebuild, GError **error)
{
    GStatBuf st;
    if (g_stat(filename, &st) != 0 || !S_ISREG(st.st_mode)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
                            "The image does not exist or is not a regular file.");
        return FALSE;
    }
    if (st.st_size > MAX_FILE_BYTES) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                            "This image exceeds the 128 MiB file limit.");
        return FALSE;
    }
    int width = 0, height = 0;
    if (!gdk_pixbuf_get_file_info(filename, &width, &height) || width <= 0 || height <= 0) {
        g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_UNKNOWN_TYPE,
                            "This file is not a supported image.");
        return FALSE;
    }
    if ((guint64)width * (guint64)height > MAX_IMAGE_PIXELS) {
        g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
                            "This image exceeds the 64 megapixel safety limit.");
        return FALSE;
    }
    int decode_width = width, decode_height = height;
    if (MAX(width, height) > MAX_DECODE_DIM) {
        double ratio = MAX_DECODE_DIM / (double)MAX(width, height);
        decode_width = MAX(1, (int)(width * ratio));
        decode_height = MAX(1, (int)(height * ratio));
    }
    GdkPixbuf *decoded = gdk_pixbuf_new_from_file_at_scale(
        filename, decode_width, decode_height, TRUE, error);
    if (!decoded) return FALSE;
    GdkPixbuf *oriented = gdk_pixbuf_apply_embedded_orientation(decoded);
    if (!oriented) {
        g_object_unref(decoded);
        g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
                            "The image could not be oriented.");
        return FALSE;
    }
    const char *orientation = gdk_pixbuf_get_option(decoded, "orientation");
    if (orientation && atoi(orientation) >= 5 && atoi(orientation) <= 8) {
        int temporary = width;
        width = height;
        height = temporary;
    }
    g_object_unref(decoded);
    char *absolute = g_canonicalize_filename(filename, NULL);
    if (rebuild) rebuild_directory(v, absolute);
    v->index = 0;
    for (guint i = 0; i < v->files->len; ++i)
        if (g_str_equal(g_ptr_array_index(v->files, i), absolute)) v->index = (int)i;
    g_clear_object(&v->pixbuf);
    v->pixbuf = oriented;
    g_free(v->path);
    v->path = absolute;
    v->source_width = width;
    v->source_height = height;
    v->fit = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->fit_button), TRUE);
    GtkWidget *bar = gtk_window_get_titlebar(GTK_WINDOW(v->window));
    char *display = g_filename_display_basename(filename);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(bar), display);
    char *title = g_strdup_printf("%s — LimeView", display);
    gtk_window_set_title(GTK_WINDOW(v->window), title);
    g_free(title);
    g_free(display);
    gtk_widget_hide(v->empty);
    gtk_widget_show(v->scroll);
    gtk_widget_set_sensitive(v->previous, v->files->len > 1);
    gtk_widget_set_sensitive(v->next, v->files->len > 1);
    gtk_widget_set_sensitive(v->zoom_out, TRUE);
    gtk_widget_set_sensitive(v->zoom_in, TRUE);
    gtk_widget_set_sensitive(v->rotate, TRUE);
    gtk_widget_set_sensitive(v->fit_button, TRUE);
    update_size(v);
    return TRUE;
}

static void open_image(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *v = data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open image", GTK_WINDOW(v->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL,
        "Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Supported images");
    gtk_file_filter_add_pixbuf_formats(filter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (v->path) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), v->path);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_widget_destroy(dialog);
        GError *error = NULL;
        if (!load_file(v, path, TRUE, &error)) {
            lime_error(GTK_WINDOW(v->window), error->message);
            g_clear_error(&error);
        }
        g_free(path);
    } else {
        gtk_widget_destroy(dialog);
    }
}

static void navigate(Viewer *v, int direction)
{
    if (v->files->len < 2) return;
    int original = v->index;
    GError *error = NULL;
    for (guint offset = 1; offset < v->files->len; ++offset) {
        int index = (original + direction * (int)offset + (int)v->files->len) % (int)v->files->len;
        g_clear_error(&error);
        if (load_file(v, g_ptr_array_index(v->files, index), FALSE, &error)) return;
    }
    if (error) lime_error(GTK_WINDOW(v->window), error->message);
    g_clear_error(&error);
}

static void previous_image(GtkButton *button, gpointer data)
{
    (void)button;
    navigate(data, -1);
}

static void next_image(GtkButton *button, gpointer data)
{
    (void)button;
    navigate(data, 1);
}

static void zoom(Viewer *v, double factor)
{
    if (!v->pixbuf) return;
    v->fit = FALSE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->fit_button), FALSE);
    v->scale = CLAMP(v->scale * factor, .02, 8.);
    update_size(v);
}

static void zoom_in(GtkButton *button, gpointer data)
{
    (void)button;
    zoom(data, 1.25);
}

static void zoom_out(GtkButton *button, gpointer data)
{
    (void)button;
    zoom(data, .8);
}

static void toggle_fit(GtkToggleButton *button, gpointer data)
{
    Viewer *v = data;
    v->fit = gtk_toggle_button_get_active(button);
    update_size(v);
}

static void rotate_image(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *v = data;
    if (!v->pixbuf) return;
    GdkPixbuf *rotated = gdk_pixbuf_rotate_simple(v->pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
    if (!rotated) {
        lime_error(GTK_WINDOW(v->window), "Not enough memory to rotate this image.");
        return;
    }
    g_object_unref(v->pixbuf);
    v->pixbuf = rotated;
    int temporary = v->source_width;
    v->source_width = v->source_height;
    v->source_height = temporary;
    update_size(v);
}

static gboolean scroll_image(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    (void)widget;
    if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
    if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_LEFT)
        zoom(data, 1.15);
    else if (event->direction == GDK_SCROLL_DOWN || event->direction == GDK_SCROLL_RIGHT)
        zoom(data, 1. / 1.15);
    else if (event->direction == GDK_SCROLL_SMOOTH && event->delta_y != 0)
        zoom(data, pow(1.15, -CLAMP(event->delta_y, -3., 3.)));
    return TRUE;
}

static gboolean key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    Viewer *v = data;
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_o || event->keyval == GDK_KEY_O)) {
        open_image(NULL, v);
        return TRUE;
    }
    switch (event->keyval) {
    case GDK_KEY_Left: previous_image(NULL, v); return TRUE;
    case GDK_KEY_Right: next_image(NULL, v); return TRUE;
    case GDK_KEY_plus: case GDK_KEY_equal: case GDK_KEY_KP_Add: zoom(v, 1.25); return TRUE;
    case GDK_KEY_minus: case GDK_KEY_KP_Subtract: zoom(v, .8); return TRUE;
    case GDK_KEY_0: case GDK_KEY_KP_0:
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->fit_button), TRUE); return TRUE;
    case GDK_KEY_r: case GDK_KEY_R: rotate_image(NULL, v); return TRUE;
    case GDK_KEY_F11:
        v->fullscreen = !v->fullscreen;
        if (v->fullscreen) gtk_window_fullscreen(GTK_WINDOW(v->window));
        else gtk_window_unfullscreen(GTK_WINDOW(v->window));
        return TRUE;
    case GDK_KEY_Escape:
        v->fullscreen = FALSE;
        gtk_window_unfullscreen(GTK_WINDOW(v->window));
        return TRUE;
    default: return FALSE;
    }
}

static void receive_drop(GtkWidget *widget, GdkDragContext *context, int x, int y,
                         GtkSelectionData *selection, guint info, guint time, gpointer data)
{
    (void)widget; (void)x; (void)y; (void)info;
    Viewer *v = data;
    char **uris = gtk_selection_data_get_uris(selection);
    gboolean success = FALSE;
    if (uris && uris[0]) {
        GError *error = NULL;
        char *path = g_filename_from_uri(uris[0], NULL, &error);
        if (path) success = load_file(v, path, TRUE, &error);
        if (error) lime_error(GTK_WINDOW(v->window), error->message);
        g_clear_error(&error);
        g_free(path);
    }
    g_strfreev(uris);
    gtk_drag_finish(context, success, FALSE, time);
}

int main(int argc, char **argv)
{
    if (lime_cli(argc, argv, "LimeView")) return 0;
    gtk_init(&argc, &argv);
    lime_init();
    Viewer v = {0};
    v.scale = 1.;
    v.fit = TRUE;
    v.files = g_ptr_array_new_with_free_func(g_free);
    v.extensions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GSList *formats = gdk_pixbuf_get_formats();
    for (GSList *item = formats; item; item = item->next) {
        if (gdk_pixbuf_format_is_disabled(item->data)) continue;
        char **extensions = gdk_pixbuf_format_get_extensions(item->data);
        for (guint i = 0; extensions[i]; ++i)
            g_hash_table_add(v.extensions, g_ascii_strdown(extensions[i], -1));
        g_strfreev(extensions);
    }
    g_slist_free(formats);
    v.window = lime_window("LimeView", "A little room for your pictures", 920, 650);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(v.window), box);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
    GtkWidget *open = lime_button("Open", "suggested-action");
    v.previous = lime_button("←", "flat");
    v.next = lime_button("→", "flat");
    v.zoom_out = lime_button("−", "flat");
    v.zoom_in = lime_button("+", "flat");
    v.rotate = lime_button("Rotate", "flat");
    v.fit_button = gtk_toggle_button_new_with_label("Fit");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v.fit_button), TRUE);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), open, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), v.previous, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), v.next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), spacer, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), v.zoom_out, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), v.fit_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), v.zoom_in, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), v.rotate, FALSE, FALSE, 0);
    GtkWidget *tools[] = {v.previous, v.next, v.zoom_out, v.fit_button, v.zoom_in, v.rotate};
    const char *tips[] = {"Previous image (Left)", "Next image (Right)", "Zoom out (−)",
                         "Fit to window (0)", "Zoom in (+ or Ctrl+wheel)", "Rotate clockwise (R)"};
    for (guint i = 0; i < G_N_ELEMENTS(tools); ++i) {
        gtk_widget_set_tooltip_text(tools[i], tips[i]);
        gtk_widget_set_sensitive(tools[i], FALSE);
    }
    gtk_widget_set_tooltip_text(open, "Open image (Ctrl+O); drop an image anywhere");
    v.empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign(v.empty, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(v.empty), lime_label("A clear view.", "title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v.empty), lime_label("Open or drop an image to begin.", "muted"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), v.empty, TRUE, TRUE, 0);
    v.scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(v.scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_no_show_all(v.scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(box), v.scroll, TRUE, TRUE, 0);
    v.canvas = gtk_drawing_area_new();
    gtk_widget_set_halign(v.canvas, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(v.canvas, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(v.scroll), v.canvas);
    gtk_widget_show(v.canvas);
    GtkWidget *viewport = gtk_widget_get_parent(v.canvas);
    gtk_widget_show(viewport);
    gtk_widget_add_events(v.scroll, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    v.status = lime_label("Static images · Ctrl+wheel to zoom · F11 fullscreen", "muted");
    gtk_label_set_ellipsize(GTK_LABEL(v.status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(v.status), 0.);
    gtk_box_pack_end(GTK_BOX(box), v.status, FALSE, FALSE, 0);
    g_signal_connect(open, "clicked", G_CALLBACK(open_image), &v);
    g_signal_connect(v.previous, "clicked", G_CALLBACK(previous_image), &v);
    g_signal_connect(v.next, "clicked", G_CALLBACK(next_image), &v);
    g_signal_connect(v.zoom_in, "clicked", G_CALLBACK(zoom_in), &v);
    g_signal_connect(v.zoom_out, "clicked", G_CALLBACK(zoom_out), &v);
    g_signal_connect(v.fit_button, "toggled", G_CALLBACK(toggle_fit), &v);
    g_signal_connect(v.rotate, "clicked", G_CALLBACK(rotate_image), &v);
    g_signal_connect(v.canvas, "draw", G_CALLBACK(draw_image), &v);
    g_signal_connect(v.scroll, "size-allocate", G_CALLBACK(scroll_allocated), &v);
    g_signal_connect(v.scroll, "scroll-event", G_CALLBACK(scroll_image), &v);
    g_signal_connect(v.window, "key-press-event", G_CALLBACK(key_pressed), &v);
    GtkTargetEntry target = {"text/uri-list", 0, 0};
    gtk_drag_dest_set(v.window, GTK_DEST_DEFAULT_ALL, &target, 1, GDK_ACTION_COPY);
    g_signal_connect(v.window, "drag-data-received", G_CALLBACK(receive_drop), &v);
    g_signal_connect(v.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(v.window);
    if (argc > 1) {
        GError *error = NULL;
        if (!load_file(&v, argv[1], TRUE, &error)) {
            lime_error(GTK_WINDOW(v.window), error->message);
            g_clear_error(&error);
        }
    }
    lime_smoke_test(v.window);
    gtk_main();
    g_clear_object(&v.pixbuf);
    g_free(v.path);
    g_ptr_array_unref(v.files);
    g_hash_table_unref(v.extensions);
    return 0;
}
