#ifndef LIME_COMMON_H
#define LIME_COMMON_H
#include <gtk/gtk.h>
#define LIME_VERSION "0.1.0"
void lime_init(void);
GtkWidget *lime_window(const char *title, const char *subtitle, int width, int height);
GtkWidget *lime_button(const char *label, const char *css_class);
GtkWidget *lime_label(const char *text, const char *css_class);
void lime_class(GtkWidget *widget, const char *name);
void lime_error(GtkWindow *parent, const char *message);
char *lime_data_path(const char *filename, GError **error);
gboolean lime_save(const char *path, const char *contents, gssize length, GError **error);
gboolean lime_cli(int argc, char **argv, const char *name);
void lime_smoke_test(GtkWidget *window);
#endif
