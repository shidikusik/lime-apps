#include "common.h"
#include <gdk/gdkkeysyms.h>

typedef struct {
    GtkWidget *window, *notebook, *time_label, *date_label, *zone_label, *calendar;
    GtkWidget *stopwatch_label, *stopwatch_button;
    GtkWidget *timer_label, *timer_button, *timer_input, *alarm;
    guint source_id;
    int page, marked_day, marked_month, marked_year;
    gboolean stopwatch_running, timer_running;
    gint64 stopwatch_elapsed, stopwatch_started;
    gint64 timer_remaining, timer_started;
} Clock;

static void reschedule(Clock *c);

static char *duration_text(gint64 microseconds, gboolean hundredths)
{
    guint64 centiseconds = (guint64)MAX((gint64)0, microseconds) / 10000;
    guint64 hours = centiseconds / 360000;
    unsigned minutes = (unsigned)((centiseconds / 6000) % 60);
    unsigned seconds = (unsigned)((centiseconds / 100) % 60);
    if (hundredths)
        return g_strdup_printf("%02" G_GUINT64_FORMAT ":%02u:%02u.%02u", hours, minutes, seconds,
                               (unsigned)(centiseconds % 100));
    /* Round the countdown up: it reaches zero only when the timer has elapsed. */
    guint64 whole_seconds = ((guint64)MAX((gint64)0, microseconds) + 999999) / G_USEC_PER_SEC;
    return g_strdup_printf("%02" G_GUINT64_FORMAT ":%02u:%02u", whole_seconds / 3600,
        (unsigned)((whole_seconds / 60) % 60), (unsigned)(whole_seconds % 60));
}

static gint64 stopwatch_value(Clock *c, gint64 now)
{
    return c->stopwatch_elapsed + (c->stopwatch_running ? now - c->stopwatch_started : 0);
}

static gint64 timer_value(Clock *c, gint64 now)
{
    return MAX((gint64)0, c->timer_remaining - (c->timer_running ? now - c->timer_started : 0));
}

static void mark_today(Clock *c)
{
    guint year, month, day;
    gtk_calendar_get_date(GTK_CALENDAR(c->calendar), &year, &month, &day);
    gtk_calendar_clear_marks(GTK_CALENDAR(c->calendar));
    if ((int)year == c->marked_year && (int)month + 1 == c->marked_month)
        gtk_calendar_mark_day(GTK_CALENDAR(c->calendar), (guint)c->marked_day);
}

static void calendar_changed(GtkCalendar *calendar, gpointer data)
{
    (void)calendar;
    mark_today(data);
}

static void update_clock(Clock *c)
{
    GDateTime *now = g_date_time_new_now_local();
    char *time = g_date_time_format(now, "%H:%M:%S");
    char *date = g_date_time_format(now, "%A, %d %B %Y");
    char *zone = g_date_time_format(now, "%Z · UTC%:z");
    gtk_label_set_text(GTK_LABEL(c->time_label), time ? time : "");
    gtk_label_set_text(GTK_LABEL(c->date_label), date ? date : "");
    gtk_label_set_text(GTK_LABEL(c->zone_label), zone ? zone : "Local time");
    int day = g_date_time_get_day_of_month(now);
    int month = g_date_time_get_month(now);
    int year = g_date_time_get_year(now);
    if (day != c->marked_day || month != c->marked_month || year != c->marked_year) {
        c->marked_day = day;
        c->marked_month = month;
        c->marked_year = year;
        mark_today(c);
    }
    g_free(time);
    g_free(date);
    g_free(zone);
    g_date_time_unref(now);
}

static void update_stopwatch(Clock *c, gint64 now)
{
    char *text = duration_text(stopwatch_value(c, now), TRUE);
    gtk_label_set_text(GTK_LABEL(c->stopwatch_label), text);
    g_free(text);
}

static void update_timer(Clock *c, gint64 now)
{
    char *text = duration_text(timer_value(c, now), FALSE);
    gtk_label_set_text(GTK_LABEL(c->timer_label), text);
    g_free(text);
}

static void dismiss_alarm(GtkInfoBar *bar, gint response, gpointer data)
{
    (void)response;
    Clock *c = data;
    gtk_widget_hide(GTK_WIDGET(bar));
    gtk_window_set_urgency_hint(GTK_WINDOW(c->window), FALSE);
}

static void clear_alarm(Clock *c)
{
    gtk_widget_hide(c->alarm);
    gtk_window_set_urgency_hint(GTK_WINDOW(c->window), FALSE);
}

static void timer_finished(Clock *c)
{
    c->timer_running = FALSE;
    c->timer_remaining = 0;
    gtk_button_set_label(GTK_BUTTON(c->timer_button), "Start again");
    gtk_widget_set_sensitive(c->timer_input, TRUE);
    gtk_widget_show(c->alarm);
    gtk_window_set_urgency_hint(GTK_WINDOW(c->window), TRUE);
    gdk_display_beep(gtk_widget_get_display(c->window));
}

static guint desired_interval(Clock *c)
{
    if (c->page == 1 && c->stopwatch_running) return 50;
    if (c->page == 2 && c->timer_running) return 100;
    if (c->page == 0 || c->timer_running) return 1000;
    return 0;
}

static gboolean tick(gpointer data)
{
    Clock *c = data;
    guint previous_interval = desired_interval(c);
    gint64 now = g_get_monotonic_time();
    if (c->page == 0) update_clock(c);
    if (c->page == 1) update_stopwatch(c, now);
    if (c->timer_running && timer_value(c, now) == 0) timer_finished(c);
    if (c->page == 2) update_timer(c, now);
    guint next_interval = desired_interval(c);
    if (previous_interval != next_interval) {
        c->source_id = next_interval ? g_timeout_add(next_interval, tick, c) : 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void reschedule(Clock *c)
{
    if (c->source_id) {
        g_source_remove(c->source_id);
        c->source_id = 0;
    }
    guint interval = desired_interval(c);
    if (interval) c->source_id = g_timeout_add(interval, tick, c);
}

static void stopwatch_toggle(GtkButton *button, gpointer data)
{
    Clock *c = data;
    gint64 now = g_get_monotonic_time();
    if (c->stopwatch_running) {
        c->stopwatch_elapsed = stopwatch_value(c, now);
        c->stopwatch_running = FALSE;
        gtk_button_set_label(button, "Resume");
    } else {
        c->stopwatch_started = now;
        c->stopwatch_running = TRUE;
        gtk_button_set_label(button, "Pause");
    }
    update_stopwatch(c, now);
    reschedule(c);
}

static void stopwatch_reset(GtkButton *button, gpointer data)
{
    (void)button;
    Clock *c = data;
    c->stopwatch_running = FALSE;
    c->stopwatch_elapsed = 0;
    gtk_button_set_label(GTK_BUTTON(c->stopwatch_button), "Start");
    update_stopwatch(c, g_get_monotonic_time());
    reschedule(c);
}

static void timer_toggle(GtkButton *button, gpointer data)
{
    Clock *c = data;
    gint64 now = g_get_monotonic_time();
    clear_alarm(c);
    if (c->timer_running) {
        c->timer_remaining = timer_value(c, now);
        if (c->timer_remaining == 0) {
            timer_finished(c);
            update_timer(c, now);
            reschedule(c);
            return;
        }
        c->timer_running = FALSE;
        gtk_button_set_label(button, "Resume");
        gtk_widget_set_sensitive(c->timer_input, TRUE);
    } else {
        /* Commit text typed into the spin button before reading the duration. */
        gtk_spin_button_update(GTK_SPIN_BUTTON(c->timer_input));
        if (c->timer_remaining <= 0)
            c->timer_remaining = (gint64)gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(c->timer_input)) * G_USEC_PER_SEC;
        c->timer_started = now;
        c->timer_running = TRUE;
        gtk_button_set_label(button, "Pause");
        gtk_widget_set_sensitive(c->timer_input, FALSE);
    }
    update_timer(c, now);
    reschedule(c);
}

static void timer_reset(GtkButton *button, gpointer data)
{
    (void)button;
    Clock *c = data;
    c->timer_running = FALSE;
    gtk_spin_button_update(GTK_SPIN_BUTTON(c->timer_input));
    c->timer_remaining = (gint64)gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(c->timer_input)) * G_USEC_PER_SEC;
    gtk_button_set_label(GTK_BUTTON(c->timer_button), "Start");
    gtk_widget_set_sensitive(c->timer_input, TRUE);
    clear_alarm(c);
    update_timer(c, g_get_monotonic_time());
    reschedule(c);
}

static void timer_input_changed(GtkSpinButton *spin, gpointer data)
{
    Clock *c = data;
    if (c->timer_running) return;
    c->timer_remaining = (gint64)gtk_spin_button_get_value_as_int(spin) * G_USEC_PER_SEC;
    gtk_button_set_label(GTK_BUTTON(c->timer_button), "Start");
    clear_alarm(c);
    update_timer(c, g_get_monotonic_time());
}

static void timer_preset(GtkButton *button, gpointer data)
{
    Clock *c = data;
    if (c->timer_running) return;
    int seconds = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "seconds"));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->timer_input), seconds);
}

static void switch_page(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data)
{
    (void)notebook; (void)page;
    Clock *c = data;
    c->page = (int)number;
    gint64 now = g_get_monotonic_time();
    if (c->page == 0) update_clock(c);
    if (c->page == 1) update_stopwatch(c, now);
    if (c->page == 2) update_timer(c, now);
    reschedule(c);
}

static gboolean key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    Clock *c = data;
    if ((event->state & GDK_CONTROL_MASK) && event->keyval >= GDK_KEY_1 && event->keyval <= GDK_KEY_3) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(c->notebook), (gint)(event->keyval - GDK_KEY_1));
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        clear_alarm(c);
        return TRUE;
    }
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(c->window));
    if (GTK_IS_EDITABLE(focus)) return FALSE;
    for (GtkWidget *parent = focus; parent; parent = gtk_widget_get_parent(parent))
        if (GTK_IS_COMBO_BOX(parent)) return FALSE;
    if (event->keyval == GDK_KEY_space) {
        if (c->page == 1) stopwatch_toggle(GTK_BUTTON(c->stopwatch_button), c);
        else if (c->page == 2) timer_toggle(GTK_BUTTON(c->timer_button), c);
        else return FALSE;
        return TRUE;
    }
    if (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R) {
        if (c->page == 1) stopwatch_reset(NULL, c);
        else if (c->page == 2) timer_reset(NULL, c);
        else return FALSE;
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *page_box(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_container_set_border_width(GTK_CONTAINER(page), 24);
    return page;
}

static GtkWidget *display_label(const char *text)
{
    GtkWidget *label = lime_label(text, "display");
    PangoAttrList *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_family_new("monospace"));
    gtk_label_set_attributes(GTK_LABEL(label), attributes);
    pango_attr_list_unref(attributes);
    return label;
}

int main(int argc, char **argv)
{
    if (lime_cli(argc, argv, "LimeClock")) return 0;
    gtk_init(&argc, &argv);
    lime_init();
    Clock c = {0};
    c.timer_remaining = (gint64)300 * G_USEC_PER_SEC;
    c.window = lime_window("LimeClock", "A moment, well kept", 560, 570);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 12);
    gtk_container_add(GTK_CONTAINER(c.window), outer);
    c.alarm = gtk_info_bar_new_with_buttons("Dismiss", GTK_RESPONSE_CLOSE, NULL);
    gtk_info_bar_set_message_type(GTK_INFO_BAR(c.alarm), GTK_MESSAGE_INFO);
    GtkWidget *alarm_content = gtk_info_bar_get_content_area(GTK_INFO_BAR(c.alarm));
    gtk_container_add(GTK_CONTAINER(alarm_content), lime_label("Time is up. Your timer has finished.", "accent"));
    gtk_widget_show_all(alarm_content);
    gtk_widget_set_no_show_all(c.alarm, TRUE);
    gtk_box_pack_start(GTK_BOX(outer), c.alarm, FALSE, FALSE, 0);
    g_signal_connect(c.alarm, "response", G_CALLBACK(dismiss_alarm), &c);
    c.notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(outer), c.notebook, TRUE, TRUE, 0);

    GtkWidget *clock_page = page_box();
    c.time_label = display_label("00:00:00");
    c.date_label = lime_label("", "subtitle");
    c.zone_label = lime_label("", "muted");
    gtk_box_pack_start(GTK_BOX(clock_page), c.time_label, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(clock_page), c.date_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clock_page), c.zone_label, FALSE, FALSE, 0);
    c.calendar = gtk_calendar_new();
    gtk_widget_set_halign(c.calendar, GTK_ALIGN_CENTER);
    gtk_calendar_set_display_options(GTK_CALENDAR(c.calendar),
        GTK_CALENDAR_SHOW_HEADING | GTK_CALENDAR_SHOW_DAY_NAMES);
    gtk_box_pack_start(GTK_BOX(clock_page), c.calendar, TRUE, FALSE, 0);
    g_signal_connect(c.calendar, "month-changed", G_CALLBACK(calendar_changed), &c);
    gtk_notebook_append_page(GTK_NOTEBOOK(c.notebook), clock_page, gtk_label_new("Clock"));

    GtkWidget *stopwatch_page = page_box();
    gtk_widget_set_valign(stopwatch_page, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(stopwatch_page), lime_label("STOPWATCH", "muted"), FALSE, FALSE, 0);
    c.stopwatch_label = display_label("00:00:00.00");
    gtk_box_pack_start(GTK_BOX(stopwatch_page), c.stopwatch_label, FALSE, FALSE, 18);
    GtkWidget *stopwatch_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(stopwatch_buttons, GTK_ALIGN_CENTER);
    c.stopwatch_button = lime_button("Start", "suggested-action");
    GtkWidget *stopwatch_clear = lime_button("Reset", "flat");
    gtk_widget_set_size_request(c.stopwatch_button, 110, -1);
    gtk_box_pack_start(GTK_BOX(stopwatch_buttons), c.stopwatch_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stopwatch_buttons), stopwatch_clear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stopwatch_page), stopwatch_buttons, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stopwatch_page), lime_label("Space to start or pause · R to reset", "muted"), FALSE, FALSE, 0);
    g_signal_connect(c.stopwatch_button, "clicked", G_CALLBACK(stopwatch_toggle), &c);
    g_signal_connect(stopwatch_clear, "clicked", G_CALLBACK(stopwatch_reset), &c);
    gtk_notebook_append_page(GTK_NOTEBOOK(c.notebook), stopwatch_page, gtk_label_new("Stopwatch"));

    GtkWidget *timer_page = page_box();
    gtk_widget_set_valign(timer_page, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(timer_page), lime_label("COUNTDOWN", "muted"), FALSE, FALSE, 0);
    c.timer_label = display_label("00:05:00");
    gtk_box_pack_start(GTK_BOX(timer_page), c.timer_label, FALSE, FALSE, 10);
    GtkWidget *input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(input_row, GTK_ALIGN_CENTER);
    GtkWidget *duration_label = lime_label("Seconds", "muted");
    c.timer_input = gtk_spin_button_new_with_range(1, 86400, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(c.timer_input), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(c.timer_input), 300);
    gtk_entry_set_width_chars(GTK_ENTRY(c.timer_input), 8);
    gtk_widget_set_tooltip_text(c.timer_input, "From 1 second to 24 hours");
    gtk_box_pack_start(GTK_BOX(input_row), duration_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(input_row), c.timer_input, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timer_page), input_row, FALSE, FALSE, 0);
    GtkWidget *presets = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(presets, GTK_ALIGN_CENTER);
    const char *preset_names[] = {"1 min", "5 min", "25 min"};
    const int preset_seconds[] = {60, 300, 1500};
    for (guint i = 0; i < G_N_ELEMENTS(preset_names); ++i) {
        GtkWidget *button = lime_button(preset_names[i], "flat");
        g_object_set_data(G_OBJECT(button), "seconds", GINT_TO_POINTER(preset_seconds[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(timer_preset), &c);
        gtk_box_pack_start(GTK_BOX(presets), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(timer_page), presets, FALSE, FALSE, 0);
    GtkWidget *timer_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(timer_buttons, GTK_ALIGN_CENTER);
    c.timer_button = lime_button("Start", "suggested-action");
    GtkWidget *timer_clear = lime_button("Reset", "flat");
    gtk_widget_set_size_request(c.timer_button, 110, -1);
    gtk_box_pack_start(GTK_BOX(timer_buttons), c.timer_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timer_buttons), timer_clear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timer_page), timer_buttons, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(timer_page), lime_label("Keep LimeClock open for the timer alert.", "muted"), FALSE, FALSE, 0);
    g_signal_connect(c.timer_button, "clicked", G_CALLBACK(timer_toggle), &c);
    g_signal_connect(timer_clear, "clicked", G_CALLBACK(timer_reset), &c);
    g_signal_connect(c.timer_input, "value-changed", G_CALLBACK(timer_input_changed), &c);
    gtk_notebook_append_page(GTK_NOTEBOOK(c.notebook), timer_page, gtk_label_new("Timer"));
    gtk_box_pack_end(GTK_BOX(outer), lime_label("Ctrl+1 / Ctrl+2 / Ctrl+3 to switch views", "muted"), FALSE, FALSE, 0);
    g_signal_connect(c.notebook, "switch-page", G_CALLBACK(switch_page), &c);
    g_signal_connect(c.window, "key-press-event", G_CALLBACK(key_pressed), &c);
    g_signal_connect(c.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    update_clock(&c);
    reschedule(&c);
    gtk_widget_show_all(c.window);
    lime_smoke_test(c.window);
    gtk_main();
    if (c.source_id) g_source_remove(c.source_id);
    return 0;
}
