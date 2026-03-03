#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#include <limits.h>
#include <sys/uio.h>
#include <sys/time.h>

#ifndef MAX_HEADER_BYTES
#define MAX_HEADER_BYTES (16 * 1024)
#endif
#ifndef RECV_TIMEOUT_SEC
#define RECV_TIMEOUT_SEC 5
#endif

static int safe_write_all_fd(int fd, const char *buf, size_t len) {
    if (fd < 0 || buf == NULL) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int64_t create_server(int64_t port64, int64_t backlog64) {
    signal(SIGPIPE, SIG_IGN);
    if (port64 <= 0 || port64 > 65535) return -1;
    if (backlog64 <= 0) backlog64 = 1;
    int port = (int)port64;
    int backlog = (int)backlog64;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    int opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(s);
        return -1;
    }

    struct timeval tv = { .tv_sec = RECV_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(s); return -1;
    }
    if (listen(s, backlog) < 0) { close(s); return -1; }
    return (int64_t)s;
}

int64_t accept_client(int64_t listen_fd64) {
    if (listen_fd64 < 0 || listen_fd64 > INT_MAX) return -1;
    int listen_fd = (int)listen_fd64;
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int c = accept(listen_fd, (struct sockaddr*)&cli, &len);
    if (c < 0) return -1;
    struct timeval tv = { .tv_sec = RECV_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return (int64_t)c;
}

int64_t close_fd(int64_t fd64) {
    if (fd64 < 0 || fd64 > INT_MAX) return -1;
    int fd = (int)fd64;
    if (close(fd) < 0) return -1;
    return 0;
}

int64_t send_response(int64_t client_fd64, const char *body_json) {
    if (client_fd64 < 0 || client_fd64 > INT_MAX || body_json == NULL) return -1;
    int client_fd = (int)client_fd64;
    size_t body_len = strlen(body_json);
    char header[512];
    int hdr_n = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        body_len);
    if (hdr_n < 0 || hdr_n >= (int)sizeof(header)) return -1;
    if (safe_write_all_fd(client_fd, header, (size_t)hdr_n) != 0) return -1;
    if (body_len > 0) {
        if (safe_write_all_fd(client_fd, body_json, body_len) != 0) return -1;
    }
    return 0;
}

void free_cstring(char *s) {
    if (s) free(s);
}

char *read_request_alloc(int64_t client_fd64) {
    if (client_fd64 < 0 || client_fd64 > INT_MAX) return NULL;
    int client_fd = (int)client_fd64;
    size_t cap = 1024;
    size_t len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    while (len + 1 < (size_t)MAX_HEADER_BYTES) {
        char c;
        ssize_t r = recv(client_fd, &c, 1, 0);
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return NULL;
        }
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            if (ncap > (size_t)MAX_HEADER_BYTES) ncap = MAX_HEADER_BYTES;
            char *tmp = (char*)realloc(buf, ncap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp; cap = ncap;
        }
        buf[len++] = c;
        if (len >= 4 && buf[len-4]=='\r' && buf[len-3]=='\n' && buf[len-2]=='\r' && buf[len-1]=='\n') {
            break;
        }
    }
    buf[len] = '\0';
    return buf;
}

static void url_decode_inplace_local(char *s) {
    char *dst = s, *src = s;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], 0 };
            int v = (int)strtol(hex, NULL, 16);
            *dst++ = (char)v;
            src += 3;
        } else if (*src == '+') { *dst++ = ' '; src++; }
        else { *dst++ = *src++; }
    }
    *dst = '\0';
}

char *get_request_pathq_alloc(const char *request) {
    if (!request) return NULL;
    const char *eol = strstr(request, "\r\n");
    if (!eol) return NULL;
    size_t line_len = (size_t)(eol - request);
    if (line_len == 0 || line_len > 4096) return NULL;
    char *line = (char*)malloc(line_len + 1);
    if (!line) return NULL;
    memcpy(line, request, line_len);
    line[line_len] = '\0';
    char *sp1 = strchr(line, ' ');
    if (!sp1) { free(line); return NULL; }
    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) { free(line); return NULL; }
    size_t pathq_len = (size_t)(sp2 - (sp1 + 1));
    char *out = (char*)malloc(pathq_len + 1);
    if (!out) { free(line); return NULL; }
    memcpy(out, sp1 + 1, pathq_len);
    out[pathq_len] = '\0';
    free(line);
    return out;
}

char *get_query_param_alloc(const char *request, const char *key) {
    if (!request || !key) return NULL;
    char *pathq = get_request_pathq_alloc(request);
    if (!pathq) return NULL;
    char *q = strchr(pathq, '?');
    if (!q) { free(pathq); return NULL; }
    q++;
    size_t keylen = strlen(key);
    const char *p = q;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', seglen);
        if (eq) {
            size_t klen = (size_t)(eq - p);
            if (klen == keylen && strncmp(p, key, keylen) == 0) {
                size_t vlen = seglen - klen - 1;
                char *out = (char*)malloc(vlen + 1);
                if (!out) { free(pathq); return NULL; }
                memcpy(out, eq + 1, vlen);
                out[vlen] = '\0';
                url_decode_inplace_local(out);
                free(pathq);
                return out;
            }
        }
        if (!amp) break;
        p = amp + 1;
    }
    free(pathq);
    return NULL;
}

char *compute_age_bracket_alloc(const char *birthdate_iso) {
    if (!birthdate_iso) return NULL;
    int yy=0, mm=0, dd=0;
    if (sscanf(birthdate_iso, "%4d-%2d-%2d", &yy, &mm, &dd) != 3) return NULL;
    if (yy < 1900 || mm < 1 || mm > 12 || dd < 1 || dd > 31) return NULL;
    time_t t = time(NULL);
    struct tm local;
    localtime_r(&t, &local);
    int y = local.tm_year + 1900;
    int m = local.tm_mon + 1;
    int d = local.tm_mday;
    int age = y - yy;
    if (m < mm || (m == mm && d < dd)) age--;
    if (age < 0) age = 0;
    const char *b = "18_plus";
    if (age < 13) b = "under_13";
    else if (age < 16) b = "13_to_15";
    else if (age < 18) b = "16_to_17";
    return strdup(b);
}

static int make_data_dir_local(char *out_path, size_t out_sz) {
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0] != '\0') {
        if ((size_t)snprintf(out_path, out_sz, "%s/age-signal", xdg) >= out_sz) return -1;
    } else if (home && home[0] != '\0') {
        if ((size_t)snprintf(out_path, out_sz, "%s/.local/share/age-signal", home) >= out_sz) return -1;
    } else {
        if ((size_t)snprintf(out_path, out_sz, "./age-signal") >= out_sz) return -1;
    }
    struct stat st;
    if (stat(out_path, &st) != 0) {
        if (mkdir(out_path, 0700) != 0 && errno != EEXIST) return -1;
    } else {
        if (!S_ISDIR(st.st_mode)) return -1;
    }
    return 0;
}
static int get_bracket_file_local(char *out, size_t out_sz) {
    char dpath[PATH_MAX];
    if (make_data_dir_local(dpath, sizeof(dpath)) != 0) return -1;
    if ((size_t)snprintf(out, out_sz, "%s/age_bracket", dpath) >= out_sz) return -1;
    return 0;
}

int64_t save_bracket_c(const char *bracket) {
    if (!bracket) return -1;
    char path[PATH_MAX];
    if (get_bracket_file_local(path, sizeof(path)) != 0) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    size_t len = strlen(bracket);
    if (safe_write_all_fd(fd, bracket, len) != 0) { close(fd); return -1; }
    close(fd);
    return 0;
}

char *load_bracket_alloc(void) {
    char path[PATH_MAX];
    if (get_bracket_file_local(path, sizeof(path)) != 0) return NULL;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    ssize_t sz = (ssize_t)st.st_size;
    if (sz <= 0 || sz > 1024) { close(fd); return NULL; }
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { close(fd); return NULL; }
    ssize_t r = read(fd, buf, (size_t)sz);
    if (r < 0) { free(buf); close(fd); return NULL; }
    buf[r] = '\0';
    size_t i = strlen(buf);
    while (i > 0 && isspace((unsigned char)buf[i-1])) { buf[i-1] = '\0'; i--; }
    close(fd);
    return buf;
}

char *make_iso_utc_now_alloc(void) {
    char tmp[64];
    time_t t = time(NULL);
    struct tm gm;
    if (!gmtime_r(&t, &gm)) return NULL;
    int n = snprintf(tmp, sizeof(tmp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
        gm.tm_year + 1900, gm.tm_mon + 1, gm.tm_mday,
        gm.tm_hour, gm.tm_min, gm.tm_sec);
    if (n < 0 || n >= (int)sizeof(tmp)) return NULL;
    return strdup(tmp);
}
