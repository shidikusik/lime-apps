#include "common.h"
#include "calc_engine.h"
#include <stdio.h>
#include <string.h>

struct calculator {
    GtkWidget *window;
    GtkWidget *entry;
    GtkWidget *result;
    GtkWidget *message;
    GtkWidget *history;
    gboolean evaluated;
    char answer[G_ASCII_DTOSTR_BUF_SIZE];
};

static void clear_calculation(struct calculator *calc)
{
    gtk_entry_set_text(GTK_ENTRY(calc->entry), "");
    gtk_label_set_text(GTK_LABEL(calc->result), "0");
    gtk_label_set_text(GTK_LABEL(calc->message), "Enter to calculate · Esc to clear");
    calc->evaluated = FALSE;
    gtk_widget_grab_focus(calc->entry);
}

static void entry_changed(GtkEditable *entry, gpointer data)
{
    (void)entry;
    struct calculator *calc = data;
    calc->evaluated = FALSE;
}

static void calculate(struct calculator *calc)
{
    const char *input = gtk_entry_get_text(GTK_ENTRY(calc->entry));
    if (!*input) return;
    double answer;
    char error[160];
    if (!lime_calc_evaluate(input, &answer, error, sizeof error)) {
        gtk_label_set_text(GTK_LABEL(calc->message), error);
        calc->evaluated = FALSE;
        gtk_widget_grab_focus(calc->entry);
        return;
    }
    g_ascii_formatd(calc->answer, sizeof calc->answer, "%.15g", answer);
    gtk_label_set_text(GTK_LABEL(calc->result), calc->answer);
    gtk_label_set_text(GTK_LABEL(calc->message), "Result copied when you click it");
    char *record = g_strdup_printf("%s = %s", input, calc->answer);
    GtkWidget *line = lime_label(record, "muted");
    gtk_label_set_xalign(GTK_LABEL(line), 1.0f);
    gtk_label_set_ellipsize(GTK_LABEL(line), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_tooltip_text(line, record);
    gtk_box_pack_start(GTK_BOX(calc->history), line, FALSE, FALSE, 0);
    gtk_widget_show(line);
    g_free(record);
    GList *children = gtk_container_get_children(GTK_CONTAINER(calc->history));
    if (g_list_length(children) > 3) gtk_widget_destroy(GTK_WIDGET(children->data));
    g_list_free(children);
    calc->evaluated = TRUE;
    gtk_widget_grab_focus(calc->entry);
    gtk_editable_set_position(GTK_EDITABLE(calc->entry), -1);
}

static void insert_token(struct calculator *calc, const char *token)
{
    gboolean is_operator = token[0] && strchr("+-*/^%", token[0]) != NULL;
    if (calc->evaluated) {
        gtk_entry_set_text(GTK_ENTRY(calc->entry), is_operator ? calc->answer : "");
        gtk_editable_set_position(GTK_EDITABLE(calc->entry), -1);
    }
    gint start, end;
    if (gtk_editable_get_selection_bounds(GTK_EDITABLE(calc->entry), &start, &end))
        gtk_editable_delete_text(GTK_EDITABLE(calc->entry), start, end);
    gint position = gtk_editable_get_position(GTK_EDITABLE(calc->entry));
    gtk_editable_insert_text(GTK_EDITABLE(calc->entry), token, -1, &position);
    gtk_editable_set_position(GTK_EDITABLE(calc->entry), position);
    gtk_widget_grab_focus(calc->entry);
    calc->evaluated = FALSE;
}

static void key_clicked(GtkButton *button, gpointer data)
{
    struct calculator *calc = data;
    const char *token = g_object_get_data(G_OBJECT(button), "lime-token");
    if (strcmp(token, "clear") == 0) clear_calculation(calc);
    else if (strcmp(token, "equals") == 0) calculate(calc);
    else if (strcmp(token, "backspace") == 0) {
        gint start, end;
        if (gtk_editable_get_selection_bounds(GTK_EDITABLE(calc->entry), &start, &end))
            gtk_editable_delete_text(GTK_EDITABLE(calc->entry), start, end);
        else {
            gint position = gtk_editable_get_position(GTK_EDITABLE(calc->entry));
            if (position > 0)
                gtk_editable_delete_text(GTK_EDITABLE(calc->entry), position - 1, position);
        }
        calc->evaluated = FALSE;
        gtk_widget_grab_focus(calc->entry);
    } else insert_token(calc, token);
}

static gboolean keyboard(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    struct calculator *calc = data;
    if (event->keyval == GDK_KEY_Escape) {
        clear_calculation(calc);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        calculate(calc);
        return TRUE;
    }
    return FALSE;
}

static void copy_answer(GtkButton *button, gpointer data)
{
    (void)button;
    struct calculator *calc = data;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), calc->answer, -1);
    gtk_label_set_text(GTK_LABEL(calc->message), "Result copied to clipboard");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--eval") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: lime-calc --eval 'EXPRESSION'\n");
            return 2;
        }
        double result;
        char error[160];
        if (!lime_calc_evaluate(argv[2], &result, error, sizeof error)) {
            fprintf(stderr, "lime-calc: %s\n", error);
            return 1;
        }
        char formatted[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(formatted, sizeof formatted, "%.15g", result);
        puts(formatted);
        return 0;
    }
    if (lime_cli(argc, argv, "LimeCalc")) return 0;
    if (argc > 1) {
        fprintf(stderr, "Usage: lime-calc [--eval 'EXPRESSION' | --help | --version]\n");
        return 2;
    }
    gtk_init(&argc, &argv);
    lime_init();
    struct calculator calc = { 0 };
    g_strlcpy(calc.answer, "0", sizeof calc.answer);
    calc.window = lime_window("LimeCalc", "A little space for numbers", 390, 610);
    g_signal_connect(calc.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(calc.window, "key-press-event", G_CALLBACK(keyboard), &calc);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    gtk_container_add(GTK_CONTAINER(calc.window), box);

    GtkWidget *display = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lime_class(display, "card");
    gtk_container_set_border_width(GTK_CONTAINER(display), 12);
    gtk_box_pack_start(GTK_BOX(box), display, FALSE, FALSE, 0);
    calc.entry = gtk_entry_new();
    gtk_entry_set_alignment(GTK_ENTRY(calc.entry), 1.0f);
    gtk_entry_set_placeholder_text(GTK_ENTRY(calc.entry), "Type an expression");
    gtk_entry_set_max_length(GTK_ENTRY(calc.entry), 4096);
    gtk_widget_set_tooltip_text(calc.entry, "Examples: (2 + 3) * 4, sqrt(81), pi * 2, 200 * 15%");
    gtk_box_pack_start(GTK_BOX(display), calc.entry, FALSE, FALSE, 0);
    g_signal_connect(calc.entry, "changed", G_CALLBACK(entry_changed), &calc);

    GtkWidget *result_button = gtk_button_new();
    lime_class(result_button, "flat");
    calc.result = lime_label("0", "display");
    gtk_label_set_xalign(GTK_LABEL(calc.result), 1.0f);
    gtk_label_set_ellipsize(GTK_LABEL(calc.result), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(calc.result, TRUE);
    gtk_container_add(GTK_CONTAINER(result_button), calc.result);
    gtk_widget_set_tooltip_text(result_button, "Copy result");
    g_signal_connect(result_button, "clicked", G_CALLBACK(copy_answer), &calc);
    gtk_box_pack_start(GTK_BOX(display), result_button, FALSE, FALSE, 0);

    calc.message = lime_label("Enter to calculate · Esc to clear", "muted");
    gtk_label_set_line_wrap(GTK_LABEL(calc.message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(calc.message), 1.0f);
    gtk_box_pack_start(GTK_BOX(box), calc.message, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);
    static const struct { const char *label; const char *token; const char *style; } keys[] = {
        { "C", "clear", "operator" }, { "(", "(", "operator" },
        { ")", ")", "operator" }, { "⌫", "backspace", "operator" },
        { "√", "sqrt(", "operator" }, { "x²", "^2", "operator" },
        { "%", "%", "operator" }, { "÷", "/", "operator" },
        { "7", "7", "key" }, { "8", "8", "key" },
        { "9", "9", "key" }, { "×", "*", "operator" },
        { "4", "4", "key" }, { "5", "5", "key" },
        { "6", "6", "key" }, { "−", "-", "operator" },
        { "1", "1", "key" }, { "2", "2", "key" },
        { "3", "3", "key" }, { "+", "+", "operator" },
        { "π", "pi", "operator" }, { "0", "0", "key" },
        { ".", ".", "key" }, { "=", "equals", "suggested-action" }
    };
    for (guint i = 0; i < G_N_ELEMENTS(keys); ++i) {
        GtkWidget *button = lime_button(keys[i].label, keys[i].style);
        gtk_widget_set_size_request(button, 48, 44);
        g_object_set_data(G_OBJECT(button), "lime-token", (gpointer)keys[i].token);
        gtk_widget_set_can_focus(button, FALSE);
        g_signal_connect(button, "clicked", G_CALLBACK(key_clicked), &calc);
        gtk_grid_attach(GTK_GRID(grid), button, i % 4, i / 4, 1, 1);
    }
    calc.history = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(box), calc.history, FALSE, FALSE, 0);
    GtkWidget *hint = lime_label("+  −  ×  ÷  ·  powers ^  ·  sqrt()  ·  pi  ·  e", "muted");
    gtk_box_pack_end(GTK_BOX(box), hint, FALSE, FALSE, 0);
    gtk_widget_show_all(calc.window);
    gtk_widget_grab_focus(calc.entry);
    lime_smoke_test(calc.window);
    gtk_main();
    return 0;
}
