// ==========================================================================
// Wololoader - Shared Mod Hub Core
//
// Platform-independent & WinHTTP mod-hub operations shared between
// Scriptorium.exe and version.dll (and testable natively on Linux):
//   - Index URL resolution & relative URL handling
//   - Index JSON fetching & schema validation
//   - Version comparison & mod ID validation
//   - SHA-256 & CRC-32 verification
//   - Safe ZIP inspection & extraction (anti-traversal, anti-symlink, limits)
//   - Dependency-first installation into mods/<id>.tmp + atomic rename
//   - .hub.json tracking & enabled-state preservation
//   - Trash-based uninstallation into wololoader/trash/<id>-<date>
// ==========================================================================

#ifndef WL_HUBCORE_H
#define WL_HUBCORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "json.h"

#define HUB_DEFAULT_URL       "https://raw.githubusercontent.com/Vitrie32/wololoader-mods/main/index.json"
#define HUB_CACHE_TTL_SEC     600
#define HUB_MAX_INDEX_BYTES   (4u * 1024u * 1024u)
#define HUB_MAX_ICON_BYTES    (5u * 1024u * 1024u)
#define HUB_MAX_ZIP_BYTES     (100u * 1024u * 1024u)
#define HUB_MAX_ENTRIES       2048u
#define HUB_MAX_ENTRY_BYTES   (32u * 1024u * 1024u)
#define HUB_MAX_TOTAL_BYTES   (200u * 1024u * 1024u)
#define HUB_MAX_DEP_DEPTH     16
#define HUB_PATH_CAP          1024

// Raw DEFLATE bounded inflate callback: returns 0 on success, -1 on failure or if > max_out.
typedef int (*HubInflateFn)(const unsigned char *in, size_t in_len, size_t max_out,
                            unsigned char **out, size_t *out_len, void *user);

// Progress callback for UI/logging during fetch & install.
// stage: "downloading", "verifying", "extracting", "installing"
typedef void (*HubProgressFn)(const char *stage, const char *mod_id,
                              size_t done_bytes, size_t total_bytes, void *user);

// Optional log callback.
typedef void (*HubLogFn)(const char *msg, void *user);

// Dynamic string buffer & JSON escaping helpers
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} HubBuf;

void hub_buf_init(HubBuf *b);
int  hub_buf_append(HubBuf *b, const char *s, size_t n);
void hub_buf_puts(HubBuf *b, const char *s);
void hub_buf_puts_escaped(HubBuf *b, const char *s);

// Portable case-insensitive ASCII string comparisons
int hub_stricmp(const char *a, const char *b);
int hub_strnicmp(const char *a, const char *b, size_t n);

// Validation & version comparison
int hub_is_valid_id(const char *id);
int hub_compare_versions(const char *a, const char *b);
int hub_is_valid_sha256_hex(const char *s);

// SHA-256 (FIPS 180-4) & CRC-32 (IEEE 802.3)
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    size_t   datalen;
} HubSha256;

void     hub_sha256_init(HubSha256 *ctx);
void     hub_sha256_update(HubSha256 *ctx, const uint8_t *data, size_t len);
void     hub_sha256_final(HubSha256 *ctx, uint8_t hash[32]);
void     hub_sha256_hex(const uint8_t *data, size_t len, char out_hex[65]);
int      hub_sha256_file(const char *path, char out_hex[65]);
uint32_t hub_crc32(const uint8_t *data, size_t len);

// URL resolution & fetching
void hub_resolve_index_url(const char *game_dir, char *eff_out, size_t eff_cap,
                           char *cfg_out, size_t cfg_cap, int *env_override);
void hub_resolve_rel_url(const char *base_url, const char *rel, char *out, size_t cap);
int  hub_url_to_local_path(const char *url, char *out, size_t cap);
int  hub_fetch_stream(const char *url, size_t max_bytes, FILE *fp,
                      char **out_buf, size_t *out_len,
                      HubProgressFn progress_cb, void *progress_user,
                      const char *progress_mod_id,
                      char *err, size_t err_cap);

// Filesystem & JSON output helpers
int   hub_mkdir_p(const char *path);
void  hub_rmdir_recursive(const char *dir);
int   hub_path_exists(const char *path);
int   hub_dir_exists(const char *path);
int   hub_file_exists(const char *path);
int   hub_atomic_write_file(const char *path, const void *data, size_t len,
                            char *err, size_t err_cap);
char *hub_json_write_pretty(const WlJsonVal *val, size_t *out_len);

// Safe ZIP path validator & archive extractor
int hub_validate_zip_path(const char *raw, size_t raw_len, uint16_t ver_made,
                          uint32_t ext_attr, char out_norm[512], int *out_is_dir);
int hub_extract_zip_safe(const char *zip_path, const char *dest_dir,
                         HubInflateFn inflate_fn, void *inflate_user,
                         char *err, size_t err_cap);

// Index fetch/parse & local mod inspection
typedef struct {
    int  installed;
    int  hub_managed;
    int  has_enabled;
    int  enabled;
    char version[64];
} HubLocalState;

WlJsonVal       *hub_fetch_and_parse_index(const char *url, char *err, size_t err_cap);
void             hub_inspect_local_mod(const char *mods_dir, const char *id, HubLocalState *out);
const WlJsonVal *hub_find_mod_in_index(const WlJsonVal *root, const char *id);

// Install / Update / Uninstall
typedef struct {
    int   updated;
    char  version[64];
    char *installed_deps[64];
    int   installed_dep_count;
    int   http_status;
    char  err[512];
} HubInstallResult;

void hub_install_result_free(HubInstallResult *res);

int hub_install_mod(const char *game_dir, const char *mods_dir,
                    const WlJsonVal *index_root, const char *index_url,
                    const char *id, int overwrite,
                    const char *wololoader_version,
                    HubInflateFn inflate_fn, void *inflate_user,
                    HubProgressFn progress_cb, void *progress_user,
                    HubLogFn log_cb, void *log_user,
                    HubInstallResult *out);

int hub_uninstall_mod(const char *game_dir, const char *mods_dir,
                      const char *id, char *out_trash_path, size_t trash_cap,
                      int *out_http_status, char *err, size_t err_cap);

#endif // WL_HUBCORE_H
