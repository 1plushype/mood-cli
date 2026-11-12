#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define W 31 /* blob grid width in cells (each cell = 2 chars) */
#define H 17 /* blob grid height */
#define MAXE 8192

enum { K_NONE = 256, K_LEFT, K_RIGHT, K_UP, K_DOWN, K_ESC };

static struct termios orig_tio;
static int raw_on = 0;
static char obuf[1 << 16];
static char logpath[600];

/* ---------- handeling the terminal ---------- */

static void restore(void) {
    if (raw_on) {
        tcsetattr(0, TCSAFLUSH, &orig_tio);
        const char *s = "\x1b[0m\x1b[?25h\x1b[?1049l";
        if (write(1, s, strlen(s)) < 0) { /* ignore */ }
        raw_on = 0;
    }
}

static void on_sig(int s) {
    (void)s;
    restore();
    _exit(130);
}

static void enter_raw(void) {
    tcgetattr(0, &orig_tio);
    struct termios t = orig_tio;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    raw_on = 1;
    atexit(restore);
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    fputs("\x1b[?1049h\x1b[?25l", stdout);
    fflush(stdout);
}

static int read_key(int ms) {
    struct pollfd p = {0, POLLIN, 0};
    if (poll(&p, 1, ms) <= 0) return K_NONE;
    unsigned char b[16];
    ssize_t n = read(0, b, sizeof b);
    if (n <= 0) return K_NONE;
    if (b[0] == 27) {
        if (n >= 3 && b[1] == '[') {
            switch (b[2]) {
            case 'C': return K_RIGHT;
            case 'D': return K_LEFT;
            case 'A': return K_UP;
            case 'B': return K_DOWN;
            }
            return K_NONE;
        }
        return K_ESC;
    }
    return b[0];
}

/* ---------- colors and the blob thingy ---------- */

/* valence v in [-1,1] -> purple/blue -> teal -> warm yellow */
static void color_for(double v, int *r, int *g, int *b) {
    static const double st[3][3] = {{110, 70, 220}, {60, 200, 200}, {255, 190, 60}};
    double t = (v + 1.0) / 2.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int a = t < 0.5 ? 0 : 1;
    double u = t < 0.5 ? t * 2 : (t - 0.5) * 2;
    *r = (int)(st[a][0] + (st[a + 1][0] - st[a][0]) * u);
    *g = (int)(st[a][1] + (st[a + 1][1] - st[a][1]) * u);
    *b = (int)(st[a][2] + (st[a + 1][2] - st[a][2]) * u);
}

static const char *vlabel(double v) {
    if (v < -0.75) return "sad/angry";
    if (v < -0.40) return "exhausted";
    if (v < -0.10) return "meh";
    if (v < 0.10) return "Neutral";
    if (v < 0.40) return "sort of happy";
    if (v < 0.75) return "really happy";
    return "Very Pleasant";
}

/*
 * Unpleasant -> lwk sad |  Pleasant -> lwk happy
 */
static void draw_blob(double v, double ph) {
    double t = (v + 1.0) / 2.0;
    double size = 0.62 + 0.38 * t;
    double amp = 0.22 + 0.04 * sin(ph * 0.7);
    double p = 6.0;
    double cx = (W - 1) / 2.0, cy = (H - 1) / 2.0;
    int r, g, b;
    color_for(v, &r, &g, &b);

    for (int y = 0; y < H; y++) {
        fputs("    ", stdout);
        for (int x = 0; x < W; x++) {
            double dx = x - cx, dy = y - cy;
            double rr = sqrt(dx * dx + dy * dy);
            double th = atan2(dy, dx);
            double arg = p * th + ph;
            double smooth = cos(arg);
            double spiky = 1.0 - 2.0 * fabs(sin(arg / 2.0));
            double f = (1.0 - t) * spiky + t * smooth;
            double R = 6.3 * size * (1.0 + amp * f);
            if (rr <= R) {
                double d = rr / R;
                double sh = 1.0 - 0.38 * d * d;
                printf("\x1b[48;2;%d;%d;%dm  ", (int)(r * sh), (int)(g * sh), (int)(b * sh));
            } else {
                fputs("\x1b[0m  ", stdout);
            }
        }
        fputs("\x1b[0m\x1b[K\n", stdout);
    }
}

static void draw_gauge(double v) {
    int idx = (int)((v + 1.0) / 2.0 * 40.0 + 0.5);
    int r, g, b;
    color_for(v, &r, &g, &b);
    fputs("    Unpleasant ", stdout);
    for (int i = 0; i <= 40; i++) {
        if (i == idx) printf("\x1b[38;2;%d;%d;%dm●\x1b[0m", r, g, b);
        else fputs("\x1b[2m·\x1b[0m", stdout);
    }
    fputs(" Pleasant\x1b[K\n", stdout);
}

/* ---------- context of le heckin system ---------- */

static double get_load(void) {
    double l = -1;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf", &l) != 1) l = -1;
        fclose(f);
    }
    return l;
}

static int get_temp(void) {
    int best = -1;
    char path[96];
    for (int i = 0; i < 12; i++) {
        snprintf(path, sizeof path, "/sys/class/thermal/thermal_zone%d/temp", i);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int t;
        if (fscanf(f, "%d", &t) == 1 && t > best) best = t;
        fclose(f);
    }
    return best > 0 ? best / 1000 : -1;
}

/* ---------- storage (bacons93 dream) ---------- */

static void mkdir_p(const char *path) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void init_paths(void) {
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char dir[560];
    if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/mood", xdg);
    else snprintf(dir, sizeof dir, "%s/.local/share/mood", home ? home : ".");
    mkdir_p(dir);
    snprintf(logpath, sizeof logpath, "%s/log.csv", dir);
}

typedef struct {
    time_t t;
    double v;
    char emo[32], tag[32];
    double load;
    int temp, hour;
} Entry;

static Entry ents[MAXE];
static int nents = 0;

static void load_entries(void) {
    FILE *f = fopen(logpath, "r");
    if (!f) return;
    char line[256];
    while (nents < MAXE && fgets(line, sizeof line, f)) {
        long t;
        int v, l, temp, hour;
        char emo[32], tag[32];
        if (sscanf(line, "%ld,%d,%31[^,],%31[^,],%d,%d,%d", &t, &v, emo, tag, &l, &temp, &hour) == 7) {
            Entry *e = &ents[nents++];
            e->t = t;
            e->v = v / 100.0;
            snprintf(e->emo, sizeof e->emo, "%s", emo);
            snprintf(e->tag, sizeof e->tag, "%s", tag);
            e->load = l / 100.0;
            e->temp = temp;
            e->hour = hour;
        }
    }
    fclose(f);
}

static int save_entry(double v, const char *emo, const char *tag) {
    FILE *f = fopen(logpath, "a");
    if (!f) return -1;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    double load = get_load();
    fprintf(f, "%ld,%d,%s,%s,%d,%d,%d\n", (long)now, (int)lround(v * 100), emo, tag,
            (int)lround(load * 100), get_temp(), tm.tm_hour);
    fclose(f);
    return 0;
}

/* ---------- interactive shit ---------- */

static const char *EMO_NEG[] = {"Stressed", "Anxious", "Sad", "exhausted", "Angy","scared", "other"};
static const char *EMO_NEU[] = {"Calm", "Tired", "Bored", "Focused", "other"};
static const char *EMO_POS[] = {"Happy", "Proud", "Gratefull", "Excited", "Peaceful", "other"};
static const char *TAGS[] = {"coding", "doing hobbies", "sleep", "work", "social", "other"};
#define NTAGS ((int)(sizeof TAGS / sizeof TAGS[0]))

static int pick(const char *title, const char **opts, int n, double v, double ph) {
    fputs("\x1b[H", stdout);
    draw_blob(v, ph);
    draw_gauge(v);
    printf("\n    \x1b[1m%s\x1b[0m\x1b[K\n\n", title);
    for (int i = 0; i < n; i++) printf("      %d) %s\x1b[K\n", i + 1, opts[i]);
    printf("\n    \x1b[2m(1-%d to choose, q to cancel)\x1b[0m\x1b[K\n\x1b[J", n);
    fflush(stdout);
    for (;;) {
        int k = read_key(-1);
        if (k == 'q' || k == K_ESC) return -1;
        if (k >= '1' && k < '1' + n) return k - '1';
    }
}

static int run_log(void) {
    if (!isatty(0) || !isatty(1)) {
        fputs("mood: needs an interactive terminal\n", stderr);
        return 1;
    }
    setvbuf(stdout, obuf, _IOFBF, sizeof obuf);
    enter_raw();

    double v = 0.0, ph = 0.0;
    int done = 0;
    while (!done) {
        fputs("\x1b[H", stdout);
        draw_blob(v, ph);
        draw_gauge(v);
        printf("\n    \x1b[1m%s\x1b[0m\x1b[K\n", vlabel(v));
        printf("\n    \x1b[2m<- -> slide   ENTER confirm   q quit\x1b[0m\x1b[K\n\x1b[J");
        fflush(stdout);

        int k = read_key(70);
        ph += 0.10;
        switch (k) {
        case K_LEFT: case 'h': v -= 0.04; break;
        case K_RIGHT: case 'l': v += 0.04; break;
        case K_DOWN: v -= 0.25; break;
        case K_UP: v += 0.25; break;
        case '\n': case '\r': done = 1; break;
        case 'q': case K_ESC: return 0;
        }
        if (v < -1) v = -1;
        if (v > 1) v = 1;
    }

    const char **list = v < -0.15 ? EMO_NEG : (v > 0.15 ? EMO_POS : EMO_NEU);
    int e = pick("What best describes this feeling?", list, 6, v, ph);
    if (e < 0) return 0;
    int tg = pick("What's having the biggest impact?", TAGS, NTAGS, v, ph);
    if (tg < 0) return 0;

    char emo[32], tag[32];
    snprintf(emo, sizeof emo, "%s", list[e]);
    snprintf(tag, sizeof tag, "%s", TAGS[tg]);
    int rc = save_entry(v, emo, tag);
    restore();

    int r, g, b;
    color_for(v, &r, &g, &b);
    double load = get_load();
    int temp = get_temp();
    if (rc == 0) {
        printf("\x1b[38;2;%d;%d;%dm●\x1b[0m Logged: %s · %s · %s", r, g, b, vlabel(v), emo, tag);
        printf("\n  context: load %.2f", load);
        if (temp > 0) printf(", %dC", temp);
        printf("\n");
    } else {
        fprintf(stderr, "mood: could not write %s: %s\n", logpath, strerror(errno));
    }
    return rc ? 1 : 0;
}

/* ---------- le heckin stats ---------- */

typedef struct {
    const char *name;
    double sum;
    int n;
} Bucket;

static void bar(double avg) {
    int r, g, b;
    color_for(avg, &r, &g, &b);
    int pos = (int)lround(avg * 14);
    for (int i = -14; i <= 14; i++) {
        if (i == 0) fputs("│", stdout);
        else if ((avg >= 0 && i > 0 && i <= pos) || (avg < 0 && i < 0 && i >= pos))
            printf("\x1b[38;2;%d;%d;%dm█\x1b[0m", r, g, b);
        else fputc(' ', stdout);
    }
}

static void section(const char *title, Bucket *bk, int nb) {
    printf("\n\x1b[1m%s\x1b[0m\n", title);
    int best = -1, worst = -1, seen = 0;
    for (int i = 0; i < nb; i++) {
        if (!bk[i].n) continue;
        double avg = bk[i].sum / bk[i].n;
        printf("  %-18s ", bk[i].name);
        bar(avg);
        printf(" %+.2f (%d)\n", avg, bk[i].n);
        seen++;
        if (best < 0 || avg > bk[best].sum / bk[best].n) best = i;
        if (worst < 0 || avg < bk[worst].sum / bk[worst].n) worst = i;
    }
    if (seen >= 2 && best != worst)
        printf("  \x1b[2m-> best: %s | lowest: %s\x1b[0m\n", bk[best].name, bk[worst].name);
    if (!seen) printf("  \x1b[2mno data\x1b[0m\n");
}

static int run_stats(void) {
    load_entries();
    if (nents == 0) {
        puts("No entries yet. Run mood to log your first one.");
        return 0;
    }
    double sum = 0;
    for (int i = 0; i < nents; i++) sum += ents[i].v;
    double avg = sum / nents;
    char d1[32], d2[32];
    struct tm tm;
    localtime_r(&ents[0].t, &tm);
    strftime(d1, sizeof d1, "%Y-%m-%d", &tm);
    localtime_r(&ents[nents - 1].t, &tm);
    strftime(d2, sizeof d2, "%Y-%m-%d", &tm);
    int r, g, b;
    color_for(avg, &r, &g, &b);
    printf("\x1b[1mmood stats\x1b[0m  %d entries, %s -> %s\n", nents, d1, d2);
    printf("overall: \x1b[38;2;%d;%d;%dm●\x1b[0m %+.2f (%s)\n", r, g, b, avg, vlabel(avg));

    /* time of day */
    Bucket tod[4] = {{"night (00-05)", 0, 0}, {"morning (06-11)", 0, 0},
                     {"afternoon (12-17)", 0, 0}, {"evening (18-23)", 0, 0}};
    /* system load relative to core count */
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    Bucket ld[3] = {{"idle (<25%)", 0, 0}, {"busy (25-75%)", 0, 0}, {"slammed (>75%)", 0, 0}};
    Bucket tp[3] = {{"cool (<50C)", 0, 0}, {"warm (50-70C)", 0, 0}, {"hot (>70C)", 0, 0}};
    Bucket tg[NTAGS];
    for (int i = 0; i < NTAGS; i++) tg[i] = (Bucket){TAGS[i], 0, 0};
    Bucket emo[64];
    int nemo = 0;

    for (int i = 0; i < nents; i++) {
        Entry *e = &ents[i];
        int h = e->hour;
        int ti = h < 6 ? 0 : h < 12 ? 1 : h < 18 ? 2 : 3;
        tod[ti].sum += e->v, tod[ti].n++;
        if (e->load >= 0) {
            double ratio = e->load / ncpu;
            int li = ratio < 0.25 ? 0 : ratio < 0.75 ? 1 : 2;
            ld[li].sum += e->v, ld[li].n++;
        }
        if (e->temp > 0) {
            int pi = e->temp < 50 ? 0 : e->temp <= 70 ? 1 : 2;
            tp[pi].sum += e->v, tp[pi].n++;
        }
        for (int j = 0; j < NTAGS; j++)
            if (!strcmp(e->tag, TAGS[j])) {
                tg[j].sum += e->v, tg[j].n++;
                break;
            }
        int k = 0;
        while (k < nemo && strcmp(emo[k].name, e->emo)) k++;
        if (k == nemo && nemo < 64) emo[nemo++] = (Bucket){e->emo, 0, 0};
        if (k < nemo) emo[k].sum += e->v, emo[k].n++;
    }

    section("by time of day", tod, 4);
    section("by system load", ld, 3);
    section("by temperature", tp, 3);
    section("by activity", tg, NTAGS);

    /* top emotions */
    printf("\n\x1b[1mtop emotions\x1b[0m\n");
    for (int shown = 0; shown < 5; shown++) {
        int best = -1;
        for (int i = 0; i < nemo; i++)
            if (emo[i].n > 0 && (best < 0 || emo[i].n > emo[best].n)) best = i;
        if (best < 0) break;
        printf("  %-14s x%d\n", emo[best].name, emo[best].n);
        emo[best].n = 0; /* consumed */
    }
    if (nents < 10) printf("\n\x1b[2m(patterns get more meaningful after ~10+ entries)\x1b[0m\n");
    return 0;
}

static int run_list(int n) {
    load_entries();
    if (nents == 0) {
        puts("No entries yet.");
        return 0;
    }
    int start = nents > n ? nents - n : 0;
    for (int i = start; i < nents; i++) {
        Entry *e = &ents[i];
        struct tm tm;
        char ds[32];
        localtime_r(&e->t, &tm);
        strftime(ds, sizeof ds, "%Y-%m-%d %H:%M", &tm);
        int r, g, b;
        color_for(e->v, &r, &g, &b);
        printf("%s  \x1b[38;2;%d;%d;%dm●\x1b[0m %+.2f %-19s %-12s %-9s load %.2f", ds, r, g, b, e->v,
               vlabel(e->v), e->emo, e->tag, e->load);
        if (e->temp > 0) printf("  %dC", e->temp);
        putchar('\n');
    }
    return 0;
}

static void usage(void) {
    puts("mood - log how you feel, see how it lines up with your machine\n\n"
         "  mood          log a mood (<- -> to slide, ENTER to confirm)\n"
         "  mood stats    show patterns\n"
         "  mood log [n]  show last n entries (default 10)");
}

int main(int argc, char **argv) {
    init_paths();
    if (argc == 1) return run_log();
    if (!strcmp(argv[1], "stats")) return run_stats();
    if (!strcmp(argv[1], "log")) return run_list(argc > 2 ? atoi(argv[2]) : 10);
    usage();
    return !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") ? 0 : 1;
}
