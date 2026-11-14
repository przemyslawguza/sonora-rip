/* sonora-rip-gui - GTK3 front end for the sonora-rip core.
 * Long operations (disc scan, CD rip, SACD extraction) run in worker threads;
 * results reach the UI through g_idle_add(). */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "cdrip.h"
#include "sacd.h"
#include "sink.h"
#include "meta.h"

#ifndef VERSION
#define VERSION "0.1.0"
#endif

enum { COL_RIP, COL_NUM, COL_ARTIST, COL_TITLE, COL_LEN, COL_PRE, COL_AUDIO, NCOLS };
enum { M_LOG, M_PROGRESS, M_SCAN_DONE, M_RIP_DONE, M_SACD_DONE };

typedef struct {
    int kind, track, rc;
    double a, b;
    char *text;
    void *data;
} Msg;

typedef struct {
    GtkWidget *win, *notebook;
    /* CD tab */
    GtkWidget *dev, *scan_btn, *lookup_chk, *http_chk, *server;
    GtkWidget *a_artist, *a_album, *a_year, *a_genre;
    GtkListStore *store;
    GtkWidget *fmt, *level, *deemph, *dither, *paranoia, *rip_btn;
    /* SACD tab */
    GtkWidget *s_input, *s_mc, *s_pcm, *s_rate, *s_bits, *s_fmt, *s_btn;
    /* common */
    GtkWidget *outdir, *progress, *cancel_btn, *status, *logview;
    GtkTextBuffer *log;
    cd_disc_t disc;
    int have_disc;
    gint cancel;
    guint pulse_id;
} App;
static App A;

/* ---------- helpers ---------- */
static char *sdup(const char *s) { return strdup(s ? s : ""); }
static char *sdup_or_null(const char *s) { return (s && *s) ? strdup(s) : NULL; }

static void parse_server(const char *in, char **host, int *port)
{
    *host = NULL; *port = 0;
    if (!in || !*in) return;
    char *c = strdup(in), *colon = strrchr(c, ':');
    if (colon) { *colon = 0; *port = atoi(colon + 1); }
    if (*c) *host = c; else free(c);
}

static void set_status(const char *s) { gtk_label_set_text(GTK_LABEL(A.status), s); }

static void log_append(const char *msg)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(A.log, &end);
    gtk_text_buffer_insert(A.log, &end, msg, -1);
    gtk_text_buffer_get_end_iter(A.log, &end);
    gtk_text_buffer_place_cursor(A.log, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(A.logview), gtk_text_buffer_get_insert(A.log));
}

static void set_busy(gboolean busy, gboolean cancellable)
{
    gtk_widget_set_sensitive(A.scan_btn, !busy);
    gtk_widget_set_sensitive(A.rip_btn, !busy);
    gtk_widget_set_sensitive(A.s_btn, !busy);
    gtk_widget_set_sensitive(A.cancel_btn, busy && cancellable);
}

static void error_dialog(const char *msg)
{
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(A.win), GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

/* ---------- thread -> UI messages ---------- */
typedef struct { char dev[256]; sr_meta_opts_t mo; char *server; int lookup; cd_disc_t disc; int rc; } ScanJob;

static void fill_from_disc(void)
{
    const cd_disc_t *d = &A.disc;
    char buf[32];
    gtk_list_store_clear(A.store);
    for (int i = 0; i < d->ntracks; i++) {
        const cd_track_info_t *t = &d->tracks[i];
        char len[16];
        unsigned secs = t->sectors / 75;
        snprintf(len, sizeof len, "%u:%02u", secs / 60, secs % 60);
        const char *title = "", *tart = "";
        if (d->meta.valid && i < d->meta.ntracks) {
            if (d->meta.title[i]) title = d->meta.title[i];
            if (d->meta.track_artist[i] && (!d->meta.artist || strcmp(d->meta.track_artist[i], d->meta.artist)))
                tart = d->meta.track_artist[i];
        }
        if (!t->is_audio) title = "(data track)";
        GtkTreeIter it;
        gtk_list_store_append(A.store, &it);
        gtk_list_store_set(A.store, &it,
            COL_RIP, t->is_audio ? TRUE : FALSE, COL_NUM, t->number,
            COL_ARTIST, tart, COL_TITLE, title, COL_LEN, len,
            COL_PRE, t->is_audio ? (t->preemph ? "yes" : "no") : "-",
            COL_AUDIO, t->is_audio ? TRUE : FALSE, -1);
    }
    gtk_entry_set_text(GTK_ENTRY(A.a_artist), d->meta.valid && d->meta.artist ? d->meta.artist : "");
    gtk_entry_set_text(GTK_ENTRY(A.a_album),  d->meta.valid && d->meta.album  ? d->meta.album  : "");
    gtk_entry_set_text(GTK_ENTRY(A.a_genre),  d->meta.valid && d->meta.genre  ? d->meta.genre  : "");
    buf[0] = 0;
    if (d->meta.valid && d->meta.year) snprintf(buf, sizeof buf, "%u", d->meta.year);
    gtk_entry_set_text(GTK_ENTRY(A.a_year), buf);
}

static gboolean pulse_cb(gpointer p)
{
    (void)p;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(A.progress));
    return TRUE;
}

static gboolean msg_idle(gpointer p)
{
    Msg *m = p;
    char t[96];
    switch (m->kind) {
    case M_LOG:
        log_append(m->text);
        break;
    case M_PROGRESS:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(A.progress), m->a);
        snprintf(t, sizeof t, "Track %d: %d%%   (total %d%%)", m->track, (int)(m->b * 100), (int)(m->a * 100));
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(A.progress), t);
        break;
    case M_SCAN_DONE: {
        ScanJob *j = m->data;
        if (j->rc != 0) {
            set_status("No audio disc found (or the drive cannot be read).");
            log_append("Scan failed: no disc or drive not accessible.\n");
        } else {
            if (A.have_disc) cd_disc_free(&A.disc);
            A.disc = j->disc;
            A.have_disc = 1;
            fill_from_disc();
            snprintf(t, sizeof t, "%d track(s). %s", A.disc.ntracks,
                     A.disc.meta.valid ? "CDDB: match found." :
                     (j->lookup ? "CDDB: no match - edit the fields by hand." : "CDDB lookup off."));
            set_status(t);
        }
        free(j->server); free(j);
        set_busy(FALSE, FALSE);
        break; }
    case M_RIP_DONE:
        if (m->rc == 0) { gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(A.progress), 1.0); set_status("Ripping finished."); }
        else if (m->rc == 2) set_status("Cancelled.");
        else set_status("Ripping failed - see the log.");
        set_busy(FALSE, FALSE);
        break;
    case M_SACD_DONE:
        if (A.pulse_id) { g_source_remove(A.pulse_id); A.pulse_id = 0; }
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(A.progress), m->rc == 0 ? 1.0 : 0.0);
        set_status(m->rc == 0 ? "SACD extraction finished." : "SACD extraction failed - see the log.");
        set_busy(FALSE, FALSE);
        break;
    }
    g_free(m->text);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void post(Msg *m) { g_idle_add(msg_idle, m); }

static void cb_log(void *u, const char *s)
{
    (void)u;
    Msg *m = g_new0(Msg, 1);
    m->kind = M_LOG; m->text = g_strdup(s);
    post(m);
}
static void cb_progress(void *u, int track, double tf, double total)
{
    (void)u;
    Msg *m = g_new0(Msg, 1);
    m->kind = M_PROGRESS; m->track = track; m->b = tf; m->a = total;
    post(m);
}
static int cb_cancel(void *u) { (void)u; return g_atomic_int_get(&A.cancel); }

/* ---------- scan ---------- */
static gpointer scan_thread(gpointer p)
{
    ScanJob *j = p;
    j->rc = cdrip_scan(j->dev, j->lookup, &j->mo, &j->disc);
    Msg *m = g_new0(Msg, 1);
    m->kind = M_SCAN_DONE; m->data = j;
    post(m);
    return NULL;
}

static void on_scan(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    ScanJob *j = g_new0(ScanJob, 1);
    snprintf(j->dev, sizeof j->dev, "%s", gtk_entry_get_text(GTK_ENTRY(A.dev)));
    j->lookup = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A.lookup_chk));
    j->mo.http = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A.http_chk));
    char *host; int port;
    parse_server(gtk_entry_get_text(GTK_ENTRY(A.server)), &host, &port);
    j->server = host; j->mo.server = host; j->mo.port = port;
    set_busy(TRUE, FALSE);
    set_status(j->lookup ? "Reading disc and querying CDDB..." : "Reading disc...");
    g_thread_unref(g_thread_new("scan", scan_thread, j));
}

static gboolean startup_scan(gpointer p) { on_scan(NULL, p); return G_SOURCE_REMOVE; }

/* ---------- CD rip ---------- */
typedef struct {
    cdrip_opts_t o;
    sr_meta_t meta;
    unsigned char *mask;
    char *device, *outdir, *server;
} RipJob;

static gpointer rip_thread(gpointer p)
{
    RipJob *j = p;
    int rc = cdrip_run(&j->o);
    sr_meta_free(&j->meta);
    free(j->mask); free(j->device); free(j->outdir); free(j->server);
    free(j);
    Msg *m = g_new0(Msg, 1);
    m->kind = M_RIP_DONE; m->rc = rc;
    post(m);
    return NULL;
}

static void on_rip(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    if (!A.have_disc) { error_dialog("Scan a disc first."); return; }
    char *outdir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(A.outdir));
    if (!outdir) { error_dialog("Choose an output folder."); return; }

    RipJob *j = calloc(1, sizeof *j);
    int n = A.disc.ntracks, any = 0;
    j->mask = calloc(n, 1);
    j->meta.valid = 1;
    j->meta.discid = A.disc.meta.valid ? A.disc.meta.discid : 0;
    j->meta.artist = sdup_or_null(gtk_entry_get_text(GTK_ENTRY(A.a_artist)));
    j->meta.album  = sdup_or_null(gtk_entry_get_text(GTK_ENTRY(A.a_album)));
    j->meta.genre  = sdup_or_null(gtk_entry_get_text(GTK_ENTRY(A.a_genre)));
    j->meta.year   = (unsigned)atoi(gtk_entry_get_text(GTK_ENTRY(A.a_year)));
    j->meta.ntracks = n;
    j->meta.title = calloc(n, sizeof(char *));
    j->meta.track_artist = calloc(n, sizeof(char *));

    GtkTreeModel *model = GTK_TREE_MODEL(A.store);
    GtkTreeIter it;
    int i = 0;
    for (gboolean ok = gtk_tree_model_get_iter_first(model, &it); ok && i < n;
         ok = gtk_tree_model_iter_next(model, &it), i++) {
        gboolean rip, audio;
        gchar *ta, *ti;
        gtk_tree_model_get(model, &it, COL_RIP, &rip, COL_AUDIO, &audio,
                           COL_ARTIST, &ta, COL_TITLE, &ti, -1);
        j->mask[i] = (rip && audio) ? 1 : 0;
        any |= j->mask[i];
        if (audio) j->meta.title[i] = sdup_or_null(ti);
        if (ta && *ta && (!j->meta.artist || strcmp(ta, j->meta.artist)))
            j->meta.track_artist[i] = strdup(ta);
        g_free(ta); g_free(ti);
    }
    if (!any) {
        sr_meta_free(&j->meta); free(j->mask); free(j); g_free(outdir);
        error_dialog("Select at least one audio track.");
        return;
    }

    j->device = sdup(gtk_entry_get_text(GTK_ENTRY(A.dev)));
    j->outdir = strdup(outdir); g_free(outdir);
    j->o.device = j->device;
    j->o.outdir = j->outdir;
    j->o.deemph = (deemph_mode_t)gtk_combo_box_get_active(GTK_COMBO_BOX(A.deemph)); /* auto,on,off */
    j->o.dither = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A.dither));
    j->o.paranoia = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A.paranoia));
    j->o.format = gtk_combo_box_get_active(GTK_COMBO_BOX(A.fmt)) == 1 ? FMT_FLAC : FMT_WAV;
    j->o.flac_level = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(A.level));
    j->o.use_cddb = 0;
    j->o.meta_in = &j->meta;
    j->o.track_mask = j->mask;
    j->o.log = cb_log; j->o.progress = cb_progress; j->o.cancelled = cb_cancel;

    g_atomic_int_set(&A.cancel, 0);
    set_busy(TRUE, TRUE);
    set_status("Ripping...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(A.progress), 0.0);
    g_thread_unref(g_thread_new("rip", rip_thread, j));
}

static void on_cancel(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    g_atomic_int_set(&A.cancel, 1);
    set_status("Cancelling...");
}

/* ---------- SACD ---------- */
typedef struct { sacd_opts_t o; char *input, *outdir; } SacdJob;

static gpointer sacd_thread(gpointer p)
{
    SacdJob *j = p;
    int rc = sacd_run(&j->o);
    free(j->input); free(j->outdir); free(j);
    Msg *m = g_new0(Msg, 1);
    m->kind = M_SACD_DONE; m->rc = rc;
    post(m);
    return NULL;
}

static void on_sacd_browse(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Select SACD image", GTK_WINDOW(A.win),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (f) { gtk_entry_set_text(GTK_ENTRY(A.s_input), f); g_free(f); }
    }
    gtk_widget_destroy(dlg);
}

static void on_sacd(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    const char *in = gtk_entry_get_text(GTK_ENTRY(A.s_input));
    char *outdir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(A.outdir));
    if (!*in) { g_free(outdir); error_dialog("Enter an ISO path, a device, or host[:2002]."); return; }
    if (!outdir) { error_dialog("Choose an output folder."); return; }

    SacdJob *j = calloc(1, sizeof *j);
    j->input = strdup(in);
    j->outdir = strdup(outdir); g_free(outdir);
    j->o.input = j->input;
    j->o.outdir = j->outdir;
    j->o.multichannel = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A.s_mc));
    j->o.to_pcm = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A.s_pcm));
    char *rate = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(A.s_rate));
    char *bits = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(A.s_bits));
    j->o.pcm_rate = rate ? atoi(rate) : 88200;
    j->o.pcm_bits = bits ? atoi(bits) : 24;
    g_free(rate); g_free(bits);
    j->o.flac = gtk_combo_box_get_active(GTK_COMBO_BOX(A.s_fmt)) == 1;
    j->o.flac_level = -1;
    j->o.log = cb_log;

    set_busy(TRUE, FALSE);
    set_status("Extracting SACD (this cannot be cancelled yet)...");
    if (!A.pulse_id) A.pulse_id = g_timeout_add(200, pulse_cb, NULL);
    g_thread_unref(g_thread_new("sacd", sacd_thread, j));
}

/* ---------- tree view callbacks ---------- */
static void on_toggled(GtkCellRendererToggle *r, gchar *path, gpointer d)
{
    (void)r; (void)d;
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(A.store), &it, path)) return;
    gboolean on, audio;
    gtk_tree_model_get(GTK_TREE_MODEL(A.store), &it, COL_RIP, &on, COL_AUDIO, &audio, -1);
    if (audio) gtk_list_store_set(A.store, &it, COL_RIP, !on, -1);
}

static void on_edited(GtkCellRendererText *r, gchar *path, gchar *text, gpointer col)
{
    (void)r;
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(A.store), &it, path)) return;
    gtk_list_store_set(A.store, &it, GPOINTER_TO_INT(col), text, -1);
}

static void on_select_all(GtkWidget *w, gpointer data)
{
    (void)w;
    gboolean val = GPOINTER_TO_INT(data);
    GtkTreeModel *m = GTK_TREE_MODEL(A.store);
    GtkTreeIter it;
    for (gboolean ok = gtk_tree_model_get_iter_first(m, &it); ok; ok = gtk_tree_model_iter_next(m, &it)) {
        gboolean audio;
        gtk_tree_model_get(m, &it, COL_AUDIO, &audio, -1);
        if (audio) gtk_list_store_set(A.store, &it, COL_RIP, val, -1);
    }
}

static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)w; (void)e; (void)d;
    g_atomic_int_set(&A.cancel, 1);   /* worker stops and removes its partial file */
    return FALSE;
}

/* ---------- UI construction ---------- */
static GtkWidget *label(const char *s)
{
    GtkWidget *l = gtk_label_new(s);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

static GtkWidget *combo(const char *const *items, int active)
{
    GtkWidget *c = gtk_combo_box_text_new();
    for (int i = 0; items[i]; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), items[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
    return c;
}

static GtkWidget *build_cd_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);

    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 4); gtk_grid_set_column_spacing(GTK_GRID(g), 6);
    A.dev = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(A.dev), "auto-detect (e.g. /dev/sr0)");
    gtk_widget_set_hexpand(A.dev, TRUE);
    A.scan_btn = gtk_button_new_with_label("Scan disc");
    g_signal_connect(A.scan_btn, "clicked", G_CALLBACK(on_scan), NULL);
    A.lookup_chk = gtk_check_button_new_with_label("CDDB lookup");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A.lookup_chk), TRUE);
    A.http_chk = gtk_check_button_new_with_label("use HTTP");
    A.server = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(A.server), "gnudb.gnudb.org[:port]");
    gtk_widget_set_hexpand(A.server, TRUE);
    gtk_grid_attach(GTK_GRID(g), label("Device:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.dev, 1, 0, 3, 1);
    gtk_grid_attach(GTK_GRID(g), A.scan_btn, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.lookup_chk, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.http_chk, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label("Server:"), 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.server, 3, 1, 2, 1);

    A.a_artist = gtk_entry_new(); A.a_album = gtk_entry_new();
    A.a_year = gtk_entry_new();   A.a_genre = gtk_entry_new();
    gtk_widget_set_hexpand(A.a_artist, TRUE); gtk_widget_set_hexpand(A.a_album, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(A.a_year), 6);
    gtk_grid_attach(GTK_GRID(g), label("Album artist:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.a_artist, 1, 2, 3, 1);
    gtk_grid_attach(GTK_GRID(g), label("Album:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.a_album, 1, 3, 3, 1);
    gtk_grid_attach(GTK_GRID(g), label("Year:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.a_year, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label("Genre:"), 2, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.a_genre, 3, 4, 2, 1);
    gtk_box_pack_start(GTK_BOX(box), g, FALSE, FALSE, 0);

    A.store = gtk_list_store_new(NCOLS, G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_STRING,
                                 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(A.store));
    GtkCellRenderer *r;
    r = gtk_cell_renderer_toggle_new();
    g_signal_connect(r, "toggled", G_CALLBACK(on_toggled), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("Rip", r, "active", COL_RIP, "activatable", COL_AUDIO, NULL));
    r = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("#", r, "text", COL_NUM, NULL));
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "editable", TRUE, NULL);
    g_signal_connect(r, "edited", G_CALLBACK(on_edited), GINT_TO_POINTER(COL_ARTIST));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("Artist (if different)", r, "text", COL_ARTIST, NULL));
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "editable", TRUE, NULL);
    g_signal_connect(r, "edited", G_CALLBACK(on_edited), GINT_TO_POINTER(COL_TITLE));
    GtkTreeViewColumn *tc = gtk_tree_view_column_new_with_attributes("Title", r, "text", COL_TITLE, NULL);
    gtk_tree_view_column_set_expand(tc, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), tc);
    r = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("Length", r, "text", COL_LEN, NULL));
    r = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("Pre-emphasis", r, "text", COL_PRE, NULL));
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), tree);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);

    GtkWidget *sel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *b1 = gtk_button_new_with_label("Select all");
    GtkWidget *b0 = gtk_button_new_with_label("Select none");
    g_signal_connect(b1, "clicked", G_CALLBACK(on_select_all), GINT_TO_POINTER(1));
    g_signal_connect(b0, "clicked", G_CALLBACK(on_select_all), GINT_TO_POINTER(0));
    gtk_box_pack_start(GTK_BOX(sel), b1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sel), b0, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sel, FALSE, FALSE, 0);

    static const char *const fmts[] = { "WAV", "FLAC", NULL };
    static const char *const de[] = { "Pre-emphasis: auto (flagged tracks)", "Pre-emphasis: always de-emphasise",
                                      "Pre-emphasis: never", NULL };
    GtkWidget *o = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    A.fmt = combo(fmts, 1);
    A.level = gtk_spin_button_new_with_range(0, 8, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(A.level), 5);
    A.deemph = combo(de, 0);
    A.dither = gtk_check_button_new_with_label("dither");
    A.paranoia = gtk_check_button_new_with_label("secure mode (paranoia)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A.paranoia), TRUE);
    A.rip_btn = gtk_button_new_with_label("Rip selected tracks");
    g_signal_connect(A.rip_btn, "clicked", G_CALLBACK(on_rip), NULL);
    gtk_box_pack_start(GTK_BOX(o), label("Format:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(o), A.fmt, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(o), label("FLAC level:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(o), A.level, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(o), A.deemph, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(o), A.dither, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(o), A.paranoia, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(o), A.rip_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), o, FALSE, FALSE, 0);
    return box;
}

static GtkWidget *build_sacd_tab(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);

    GtkWidget *note = gtk_label_new(
        "SACD extraction uses the external 'sacd_extract' tool (sacd-ripper). An ordinary DVD drive "
        "generally cannot read the SACD layer: use an ISO image, a PS3 running the network ripper "
        "(host:2002) or a drive known to work. PCM conversion needs ffmpeg.");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 0);

    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 6); gtk_grid_set_column_spacing(GTK_GRID(g), 6);
    A.s_input = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(A.s_input), "disc.iso  |  /dev/sr0  |  192.168.1.20:2002");
    gtk_widget_set_hexpand(A.s_input, TRUE);
    GtkWidget *br = gtk_button_new_with_label("Browse...");
    g_signal_connect(br, "clicked", G_CALLBACK(on_sacd_browse), NULL);
    A.s_mc = gtk_check_button_new_with_label("multichannel area (default: stereo)");
    A.s_pcm = gtk_check_button_new_with_label("also convert DSD to PCM");
    static const char *const rates[] = { "44100", "88200", "176400", NULL };
    static const char *const bits[]  = { "16", "24", NULL };
    static const char *const fmts[]  = { "WAV", "FLAC", NULL };
    A.s_rate = combo(rates, 1); A.s_bits = combo(bits, 1); A.s_fmt = combo(fmts, 1);
    A.s_btn = gtk_button_new_with_label("Extract SACD");
    g_signal_connect(A.s_btn, "clicked", G_CALLBACK(on_sacd), NULL);
    gtk_grid_attach(GTK_GRID(g), label("Source:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.s_input, 1, 0, 3, 1);
    gtk_grid_attach(GTK_GRID(g), br, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.s_mc, 1, 1, 4, 1);
    gtk_grid_attach(GTK_GRID(g), A.s_pcm, 1, 2, 4, 1);
    gtk_grid_attach(GTK_GRID(g), label("PCM rate (Hz):"), 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.s_rate, 2, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label("bits:"), 3, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.s_bits, 4, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label("PCM format:"), 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g), A.s_fmt, 2, 4, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), g, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), A.s_btn, FALSE, FALSE, 0);
    return box;
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    A.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(A.win), "sonora-rip " VERSION);
    gtk_window_set_default_size(GTK_WINDOW(A.win), 900, 720);
    g_signal_connect(A.win, "delete-event", G_CALLBACK(on_delete), NULL);
    g_signal_connect(A.win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(root), 6);
    gtk_container_add(GTK_CONTAINER(A.win), root);

    A.notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(A.notebook), build_cd_tab(), gtk_label_new("Audio CD"));
    gtk_notebook_append_page(GTK_NOTEBOOK(A.notebook), build_sacd_tab(), gtk_label_new("SACD"));
    gtk_widget_set_vexpand(A.notebook, TRUE);
    gtk_box_pack_start(GTK_BOX(root), A.notebook, TRUE, TRUE, 0);

    GtkWidget *od = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    A.outdir = gtk_file_chooser_button_new("Select output folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    const char *music = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(A.outdir), music ? music : g_get_home_dir());
    gtk_widget_set_hexpand(A.outdir, TRUE);
    gtk_box_pack_start(GTK_BOX(od), label("Output folder:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(od), A.outdir, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), od, FALSE, FALSE, 0);

    GtkWidget *pr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    A.progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(A.progress), TRUE);
    gtk_widget_set_hexpand(A.progress, TRUE);
    A.cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_widget_set_sensitive(A.cancel_btn, FALSE);
    g_signal_connect(A.cancel_btn, "clicked", G_CALLBACK(on_cancel), NULL);
    gtk_box_pack_start(GTK_BOX(pr), A.progress, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pr), A.cancel_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), pr, FALSE, FALSE, 0);

    A.status = label("Insert an audio CD and press \"Scan disc\".");
    gtk_box_pack_start(GTK_BOX(root), A.status, FALSE, FALSE, 0);

    A.logview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(A.logview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(A.logview), TRUE);
    A.log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(A.logview));
    GtkWidget *lsw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(lsw, -1, 130);
    gtk_container_add(GTK_CONTAINER(lsw), A.logview);
    gtk_box_pack_start(GTK_BOX(root), lsw, FALSE, FALSE, 0);

    gtk_widget_show_all(A.win);
    g_idle_add(startup_scan, NULL);
    gtk_main();
    if (A.have_disc) cd_disc_free(&A.disc);
    return 0;
}
