// In-game History screen (Main Menu > History) mod civs integration.
//
// Features:
// - Adds a "Mod civs: Off" / "Mod civs: On" toggle button at the top of the History screen.
// - When "Mod civs: On":
//   * Populates the listbox with active mod civilization entries.
//   * Displays the custom civ title, history text (from "history": {"text": "..."} or
//     "text_file": "history/<name>.txt"), and image ("image": "history/<name>.png")
//     rendered in the picture area above the text using Wololoader's image panel system.
//   * Hides the game's 4 PicturePanels.
// - When "Mod civs: Off":
//   * Restores the 52 vanilla topics (The Britons ... The Renaissance) in the listbox.
//   * Calls the original topic loader (0x6cd400), restoring vanilla titles, texts,
//     SLP pictures (50162/53209), and civilization selection sounds.
//
// Verified game addresses (AoK HD.exe 5.8, see re/FINDINGS.md):
// - 0x6cc830: HistoryScreen::ctor (__thiscall(void *self), ret 0, returns this in eax).
//   Prologue: 55 8b ec 6a ff (5 bytes: push ebp; mov ebp, esp; push -1).
// - 0x9d5a94: HistoryScreen vtable:
//   * Slot 0 (+0x00): 0x6cd300 destructor (__thiscall(self, byte flags), ret 4).
//   * Slot 49 (+0xc4): 0x6cd3b0 handle_event (__thiscall(self, panel, code, a, b), ret 0x10).
// - 0x6cd400: HistoryScreen::load_topic (__thiscall(self, int topic_id), ret 4).
// - HistoryScreen fields:
//   * [self + 0x27c]: Listbox* (list of topics, x=188, y=31, w=276, h=460)
//   * [self + 0x280]: Listbox scrollbar
//   * [self + 0x284]: TextPanel* (Title label, x=438, y=46, w=552, h=34)
//   * [self + 0x288]: TextPanel* (Body text, x=438, y=349, w=552, h=340)
//   * [self + 0x290]: PicturePanel* 1 (Bottom-left illustration panel, x=184, y=514, w=280, h=206)
//   * [self + 0x294]: PicturePanel* 2 (Top-right illustration panel, x=604, y=96, w=490, h=263)
//   * [self + 0x298]: PicturePanel* 3 (Bottom-left illustration panel, x=184, y=514, w=280, h=206)
//   * [self + 0x29c]: PicturePanel* 4 (Top-right illustration panel, x=604, y=96, w=490, h=263)
//   * [self + 0x2a0]: Button* (Close / Back button)
//   * [self + 0x2a4]: int (Currently selected topic index)
//   * [self + 0x2a8]: void* (Current sound player object or 0)
// - 0x495440: Listbox::clear_items (__thiscall(listbox, int 0), ret 4).
// - 0x495eb0: Listbox::add_item (__thiscall(listbox, text, id, extra_12B), ret 0x14).
// - 0x495350: Listbox::set_selected (__thiscall(listbox, int index), ret 4).
// - 0x4944f0: Listbox::get_selected_id (__thiscall(listbox), ret 0, returns id in eax).
// - 0x48df30: ScreenPanel::add_button (__thiscall, ret 0x28).
// - 0x479ce0: Button::set_text (__thiscall(btn, int slot, const char *text), ret 8).
// - 0x477450: Button::set_checked (__thiscall(btn, char checked), ret 4).
// - Sound playback & control:
//   * 0xada234: SoundSystem* global pointer
//   * 0x46ba10: SoundSystem::play_sound (__thiscall(ss, &out_handle, req_24B, 1, 0), ret 0x24)
//   * 0x46b010: SoundObject::play (__thiscall(sound_obj), ret 0)
//   * 0x46afb0: SoundObject::stop (__thiscall(sound_obj), ret 0)
//   * 0x433210: std::string::assign (__thiscall(str, const char *s), ret 4)
// - Markup parsing & formatted text:
//   * 0x773f70: parse_markup (__cdecl(out_struct_24B, const char *raw_text, 0, 0), caller clears 16B)
//   * 0x4955c0: TextPanel::set_formatted_text (__thiscall(panel, out_struct_24B), ret 4)
//   * 0x474a80: std::vector::dtor (__thiscall(vec), ret 0)
//   * 0x4339a0: std::string::dtor (__thiscall(str), ret 0)
// - Panel vtables:
//   * Slot 4 (+0x10): set_visible (__thiscall(panel, int visible), ret 4).
//   * Slot 54 (+0xd8): set_text (__thiscall(panel, const char *text, 0, 0, 0), ret 0x10).
// - Image panels:
//   * 0x49f1f0: LoadPng (__thiscall(chain, &out, path), ret 8, chain = *(void **)0xada9f4).
//   * 0x491190: ScaleRect (__thiscall(scr, out[4], in[4]), ret 8).
//   * 0x8d47df: operator new (cdecl).
//   * 0x47a5d0: DrawablePanel::ctor (__thiscall(panel, scr, rect[4], drawable), ret 0xc).
//   * 0x134: DrawablePanel size.
// - String lookup:
//   * 0x448370: StringLookup (__thiscall(table, const char *key), ret 4, table = *(void **)0xad13e0).

#include "wololoader.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDR_HISTORY_CTOR         0x6cc830
#define HISTORY_VTBL              0x9d5a94
#define ADDR_HISTORY_DTOR         0x6cd300
#define ADDR_HISTORY_EVENT        0x6cd3b0
#define ADDR_LOAD_TOPIC           0x6cd400

#define ADDR_SOUND_SYSTEM         0xada234
#define ADDR_PLAY_SOUND           0x46ba10
#define ADDR_START_SOUND          0x46b010
#define ADDR_STOP_SOUND           0x46afb0
#define ADDR_STRING_ASSIGN        0x433210
#define ADDR_STRING_DTOR          0x4339a0
#define ADDR_VECTOR_DTOR          0x474a80
#define ADDR_PARSE_MARKUP         0x773f70
#define ADDR_SET_FORMATTED_TEXT   0x4955c0

#define ADDR_LISTBOX_CLEAR        0x495440
#define ADDR_LISTBOX_SET_SEL      0x495350
#define ADDR_LISTBOX_GET_SEL      0x4944f0

#define ADDR_ADD_BUTTON           0x48df30
#define ADDR_BUTTON_SET_TEXT      0x479ce0
#define ADDR_BUTTON_SET_CHECKED   0x477450

#define ADDR_LOAD_PNG             0x49f1f0
#define ADDR_LOADER_CHAIN         0xada9f4
#define ADDR_SCALE_RECT           0x491190
#define ADDR_DRAWABLE_PANEL_CTOR  0x47a5d0
#define DRAWABLE_PANEL_SIZE       0x134
#define ADDR_GAME_NEW             0x8d47df

#define ADDR_STRING_LOOKUP        0x448370
#define ADDR_STRING_TABLE         0xad13e0

#define EV_LEFT_CLICK             3
#define MAX_HIST_CIVS             64
#define MAX_VANILLA_TOPICS        128

typedef void *(__thiscall *HistoryCtor_t)(void *self);
typedef void *(__thiscall *HistoryDtor_t)(void *self, BYTE flags);
typedef int (__thiscall *HistoryEvent_t)(void *self, void *panel, int code, int a, int b);
typedef void (__thiscall *LoadTopic_t)(void *self, int topic_id);

typedef void *(__thiscall *AddButton_t)(void *scr, void *parent, const char *text, const char *text2,
                                         int x, int y, int w, int h, int a, int b, int c);
typedef void (__thiscall *ButtonSetText_t)(void *btn, int slot, const char *text);
typedef void (__thiscall *ButtonSetChecked_t)(void *btn, char checked);
typedef void (__thiscall *ListboxClear_t)(void *self, int param);
typedef void (__thiscall *ListboxSetSel_t)(void *self, int index);
typedef int (__thiscall *ListboxGetSel_t)(void *self);
typedef void (__thiscall *PanelSetText_t)(void *panel, const char *text, int z0, int z1, int z2);
typedef void (__thiscall *PanelSetVisible_t)(void *panel, int visible);
typedef void (__thiscall *ScaleRect_t)(void *scr, int out[4], const int in[4]);
typedef void *(__thiscall *DrawablePanelCtor_t)(void *mem, void *parent, const int rect[4], void *drawable);
typedef void *(__cdecl *New_t)(unsigned size);
typedef const char *(__thiscall *StringLookup_t)(void *table, const char *key);

static HistoryCtor_t o_history_ctor = NULL;
static HistoryDtor_t o_history_dtor = NULL;
static HistoryEvent_t o_history_event = NULL;

typedef struct {
    int civ_id;
    char title[128];
    char *text;
    char image_path[MAX_PATH];
    void *img_panel;
    char side_image_path[MAX_PATH];
    void *side_img_panel;
} WlHistoryCiv;

static WlHistoryCiv g_mod_civs[MAX_HIST_CIVS];
static int g_mod_civ_count = 0;

static void *g_history_screen = NULL;
static void *g_btn_mod_civs = NULL;
static int g_mod_civs_mode = 0;  // 0 = vanilla, 1 = mod civs

static char g_vanilla_topics[MAX_VANILLA_TOPICS][128];
static int g_vanilla_topic_count = 0;

// Sanitize UTF-8 so AoE2 HD's text decoder (0x444e40) never hangs on broken bytes
static void sanitize_utf8(char *s) {
    if (!s) return;
    unsigned char *p = (unsigned char *)s;
    while (*p) {
        int n = (*p < 0x80) ? 1 : ((*p & 0xe0) == 0xc0 && *p >= 0xc2) ? 2
              : ((*p & 0xf0) == 0xe0) ? 3 : ((*p & 0xf8) == 0xf0 && *p <= 0xf4) ? 4 : 0;
        int ok = n > 0;
        for (int i = 1; ok && i < n; i++) ok = (p[i] & 0xc0) == 0x80;
        if (!ok) { *p++ = '?'; continue; }
        p += n;
    }
}

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (d) {
        memcpy(d, s, len + 1);
        sanitize_utf8(d);
    }
    return d;
}

static const char *lookup_str(int sid) {
    char key[32];
    snprintf(key, sizeof key, "%d", sid);
    void *table = *(void **)ADDR_STRING_TABLE;
    if (!table) return NULL;
    return ((StringLookup_t)ADDR_STRING_LOOKUP)(table, key);
}

// 0x495eb0 is __thiscall Listbox::add_item(text, id, extra_12B) with ret 0x14 (20 bytes).
// Pushes: 12 bytes of zeros, then id, then text.
static void add_listbox_item(void *listbox, const char *text, int id) {
    const char *s = text ? text : "";
    __asm__ __volatile__(
        "pushl $0\n\t"
        "pushl $0\n\t"
        "pushl $0\n\t"
        "pushl %0\n\t"
        "pushl %1\n\t"
        "movl %2, %%ecx\n\t"
        "movl $0x495eb0, %%eax\n\t"
        "call *%%eax\n\t"
        :
        : "r" (id), "r" (s), "r" (listbox)
        : "eax", "ecx", "edx", "memory", "cc"
    );
}

static void panel_set_text(void *panel, const char *text) {
    if (!panel) return;
    void **vtbl = *(void ***)panel;
    PanelSetText_t fn = (PanelSetText_t)vtbl[54];
    fn(panel, text ? text : "", 0, 0, 0);
}

static void panel_set_visible(void *panel, int visible) {
    if (!panel) return;
    void **vtbl = *(void ***)panel;
    PanelSetVisible_t fn = (PanelSetVisible_t)vtbl[4];
    fn(panel, visible ? 1 : 0);
}

static void *load_png(const char *path) {
    void *chain = *(void **)ADDR_LOADER_CHAIN;
    if (!chain || !path || !path[0]) return NULL;
    void *drawable = NULL;
    ((void (__thiscall *)(void *, void **, const char *))ADDR_LOAD_PNG)(chain, &drawable, path);
    return drawable;
}

static void drawable_set_fit(void *drawable) {
    if (!drawable) return;
    int mode3[3] = { 1, 1, 1 };
    void (__thiscall *fn)(void *, const int *) =
        (void (__thiscall *)(void *, const int *))(*(void ***)drawable)[3];
    fn(drawable, mode3);
}

static void *panel_from_drawable(void *s, void *d, int x, int y, int w, int h) {
    drawable_set_fit(d);
    int in_rect[4] = { x, y, w, h };
    int out_rect[4] = { 0, 0, 0, 0 };
    ((ScaleRect_t)ADDR_SCALE_RECT)(s, out_rect, in_rect);

    void *panel = ((New_t)ADDR_GAME_NEW)(DRAWABLE_PANEL_SIZE);
    if (!panel) return NULL;
    memset(panel, 0, DRAWABLE_PANEL_SIZE);
    ((DrawablePanelCtor_t)ADDR_DRAWABLE_PANEL_CTOR)(panel, s, out_rect, d);
    return panel;
}

static void *create_image_panel(void *s, const char *path, int x, int y, int w, int h) {
    if (!path || !path[0]) return NULL;
    void *d = load_png(path);
    if (!d) return NULL;
    return panel_from_drawable(s, d, x, y, w, h);
}

// Cache the vanilla topics list from strings table (20310 count, 20311.. titles)
static void cache_vanilla_topics(void) {
    if (g_vanilla_topic_count > 0) return;
    const char *cnt_s = lookup_str(20310);
    int count = cnt_s ? atoi(cnt_s) : 52;
    if (count <= 0) count = 52;
    if (count > MAX_VANILLA_TOPICS) count = MAX_VANILLA_TOPICS;

    for (int i = 0; i < count; i++) {
        const char *t = lookup_str(20311 + i);
        if (t) lstrcpynA(g_vanilla_topics[i], t, sizeof(g_vanilla_topics[i]));
        else g_vanilla_topics[i][0] = '\0';
    }
    g_vanilla_topic_count = count;
    wl_log("history: cached %d vanilla topics", count);
}

// Scan cache\civs.txt and load history definitions from mod civ JSONs
static void load_mod_histories(void) {
    for (int i = 0; i < g_mod_civ_count; i++) {
        if (g_mod_civs[i].text) {
            free(g_mod_civs[i].text);
            g_mod_civs[i].text = NULL;
        }
    }
    g_mod_civ_count = 0;

    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\cache\\civs.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) {
        wl_log("history: cache\\civs.txt not found");
        return;
    }

    char line[1024];
    while (fgets(line, sizeof line, f) && g_mod_civ_count < MAX_HIST_CIVS) {
        line[strcspn(line, "\r\n")] = 0;
        char *id_s = strtok(line, "\t");
        char *base_s = strtok(NULL, "\t");
        char *name_s = strtok(NULL, "\t");
        char *mod_s = strtok(NULL, "\t");
        char *json_s = strtok(NULL, "\t");
        if (!id_s || !base_s || !name_s || !mod_s || !json_s) continue;

        int cid = atoi(id_s);
        if (cid < 35) continue;

        WlHistoryCiv *entry = &g_mod_civs[g_mod_civ_count];
        memset(entry, 0, sizeof *entry);
        entry->civ_id = cid;
        lstrcpynA(entry->title, name_s, sizeof entry->title);

        char jpath[MAX_PATH];
        snprintf(jpath, sizeof jpath, "%s\\mods\\%s\\civs\\%s", g_wl.root, mod_s, json_s);
        size_t jlen = 0;
        char *jtext = wl_read_file(jpath, &jlen);
        if (jtext) {
            char err[256];
            WlJsonVal *root = wl_json_parse(jtext, err, sizeof err);
            free(jtext);
            if (root) {
                const WlJsonVal *h = wl_json_get(root, "history");
                if (h && h->type == WL_JSON_OBJECT) {
                    const char *t = wl_json_str(wl_json_get(h, "title"), NULL);
                    if (t && t[0]) lstrcpynA(entry->title, t, sizeof entry->title);

                    const char *tf = wl_json_str(wl_json_get(h, "text_file"), NULL);
                    const char *raw_txt = wl_json_str(wl_json_get(h, "text"), NULL);
                    if (tf && tf[0]) {
                        char tf_path[MAX_PATH];
                        snprintf(tf_path, sizeof tf_path, "%s\\mods\\%s\\%s", g_wl.root, mod_s, tf);
                        for (char *p = tf_path; *p; p++) if (*p == '/') *p = '\\';
                        char *fc = wl_read_file(tf_path, NULL);
                        if (fc) {
                            entry->text = dup_str(fc);
                            free(fc);
                        }
                    }
                    if (!entry->text && raw_txt && raw_txt[0]) {
                        entry->text = dup_str(raw_txt);
                    }

                    const char *img = wl_json_str(wl_json_get(h, "image"), NULL);
                    if (img && img[0]) {
                        snprintf(entry->image_path, sizeof entry->image_path, "%s\\mods\\%s\\%s", g_wl.root, mod_s, img);
                        for (char *p = entry->image_path; *p; p++) if (*p == '/') *p = '\\';
                        if (GetFileAttributesA(entry->image_path) == INVALID_FILE_ATTRIBUTES) {
                            wl_log("history: image '%s' not found for civ %d", entry->image_path, cid);
                            entry->image_path[0] = '\0';
                        }
                    }

                    const char *simg = wl_json_str(wl_json_get(h, "side_image"), NULL);
                    if (simg && simg[0]) {
                        snprintf(entry->side_image_path, sizeof entry->side_image_path, "%s\\mods\\%s\\%s", g_wl.root, mod_s, simg);
                        for (char *p = entry->side_image_path; *p; p++) if (*p == '/') *p = '\\';
                        if (GetFileAttributesA(entry->side_image_path) == INVALID_FILE_ATTRIBUTES) {
                            wl_log("history: side_image '%s' not found for civ %d", entry->side_image_path, cid);
                            entry->side_image_path[0] = '\0';
                        }
                    }
                } else {
                    const char *desc = wl_json_str(wl_json_get(root, "description"), NULL);
                    if (desc && desc[0]) entry->text = dup_str(desc);
                }
                wl_json_free(root);
            }
        }
        if (!entry->text) entry->text = dup_str("No history entry available for this civilization.");
        sanitize_utf8(entry->title);
        wl_log("history: civ %d '%s' loaded (has_text=%d, img=%s, side_img=%s)",
               entry->civ_id, entry->title, entry->text != NULL,
               entry->image_path[0] ? entry->image_path : "(none)",
               entry->side_image_path[0] ? entry->side_image_path : "(none)");
        g_mod_civ_count++;
    }
    fclose(f);
}

static void history_stop_sound(void *self) {
    if (!self) return;
    void **snd_ptr = (void **)((char *)self + 0x2a8);
    void *obj = *snd_ptr;
    if (obj) {
        *snd_ptr = NULL;
        ((void (__thiscall *)(void *))ADDR_STOP_SOUND)(obj);
        int *ref = (int *)((char *)obj + 4);
        if (--(*ref) == 0) {
            typedef void (__thiscall *SoundDtor_t)(void *self);
            SoundDtor_t dtor = (SoundDtor_t)(*(void ***)obj)[0];
            dtor(obj);
        }
    }
}

static void history_play_sound(void *self, const char *mp3) {
    if (!self || !mp3 || !mp3[0]) return;
    void *ss = *(void **)ADDR_SOUND_SYSTEM;
    if (!ss) return;

    void *out_handle = NULL;
    char str_buf[12];
    *(DWORD *)(&str_buf[0]) = 0xad0380;
    *(DWORD *)(&str_buf[4]) = 0;
    *(DWORD *)(&str_buf[8]) = 0;
    typedef void *(__thiscall *StrAssign_t)(void *str, const char *s);
    ((StrAssign_t)ADDR_STRING_ASSIGN)(str_buf, mp3);

    __asm__ __volatile__(
        "pushl $0\n\t"
        "pushl $1\n\t"
        "subl $24, %%esp\n\t"
        "movl %%esp, %%edx\n\t"
        "movl $0, 0(%%edx)\n\t"
        "movl $0x13, 4(%%edx)\n\t"
        "movl 0(%1), %%eax\n\t"
        "movl %%eax, 8(%%edx)\n\t"
        "movl 4(%1), %%eax\n\t"
        "movl %%eax, 12(%%edx)\n\t"
        "movl 8(%1), %%eax\n\t"
        "movl %%eax, 16(%%edx)\n\t"
        "movl $0, 20(%%edx)\n\t"
        "pushl %2\n\t"
        "movl %3, %%ecx\n\t"
        "movl $0x46ba10, %%eax\n\t"
        "call *%%eax\n\t"
        :
        : "m" (out_handle), "r" (str_buf), "r" (&out_handle), "r" (ss)
        : "eax", "ecx", "edx", "memory", "cc"
    );

    *(DWORD *)(&str_buf[0]) = 0xad0380;
    *(DWORD *)(&str_buf[4]) = 0;
    *(DWORD *)(&str_buf[8]) = 0;

    if (out_handle) {
        *(void **)((char *)self + 0x2a8) = out_handle;
        ((void (__thiscall *)(void *))ADDR_START_SOUND)(out_handle);
    }
}

static void set_body_formatted_text(void *panel, const char *raw_text) {
    if (!panel) return;
    const char *s = raw_text ? raw_text : "";
    char out_struct[24];
    memset(out_struct, 0, sizeof out_struct);

    typedef void *(__cdecl *ParseMarkup_t)(void *out, const char *text, int a, int b);
    ((ParseMarkup_t)ADDR_PARSE_MARKUP)(out_struct, s, 0, 0);

    typedef void (__thiscall *SetFormattedText_t)(void *panel, void *out);
    ((SetFormattedText_t)ADDR_SET_FORMATTED_TEXT)(panel, out_struct);

    typedef void (__thiscall *VectorDtor_t)(void *vec);
    ((VectorDtor_t)ADDR_VECTOR_DTOR)(out_struct + 12);

    typedef void (__thiscall *StringDtor_t)(void *str);
    ((StringDtor_t)ADDR_STRING_DTOR)(out_struct + 0);
}

static void history_display_mod_civ(void *self, int idx) {
    if (!self || idx < 0 || idx >= g_mod_civ_count) return;
    WlHistoryCiv *c = &g_mod_civs[idx];

    *(int *)((char *)self + 0x2a4) = idx;

    void *title_lbl = *(void **)((char *)self + 0x284);
    if (title_lbl) {
        panel_set_text(title_lbl, c->title);
        panel_set_visible(title_lbl, 1);
    }

    void *body_txt = *(void **)((char *)self + 0x288);
    if (body_txt) {
        set_body_formatted_text(body_txt, c->text ? c->text : "");
        panel_set_visible(body_txt, 1);
    }

    // Hide the game's 4 PicturePanels
    for (int i = 0; i < 4; i++) {
        void *pic = *(void **)((char *)self + 0x290 + i * 4);
        if (pic) panel_set_visible(pic, 0);
    }

    // Show selected mod civ image panels, hide others
    for (int i = 0; i < g_mod_civ_count; i++) {
        if (g_mod_civs[i].img_panel) {
            panel_set_visible(g_mod_civs[i].img_panel, (i == idx) ? 1 : 0);
        }
        if (g_mod_civs[i].side_img_panel) {
            panel_set_visible(g_mod_civs[i].side_img_panel, (i == idx) ? 1 : 0);
        }
    }

    // Stop previous sound and play this civ's sound if defined
    history_stop_sound(self);
    const char *mp3 = wl_civ_get_sound(c->civ_id);
    if (mp3 && mp3[0]) {
        history_play_sound(self, mp3);
    }
}

static void history_toggle_mod_civs(void *self) {
    if (!self) return;
    void *listbox = *(void **)((char *)self + 0x27c);
    if (!listbox) return;

    g_mod_civs_mode = !g_mod_civs_mode;

    if (g_btn_mod_civs) {
        ((ButtonSetText_t)ADDR_BUTTON_SET_TEXT)(
            g_btn_mod_civs, 0, g_mod_civs_mode ? "Mod civs: On" : "Mod civs: Off");
        ((ButtonSetChecked_t)ADDR_BUTTON_SET_CHECKED)(
            g_btn_mod_civs, (char)g_mod_civs_mode);
    }

    if (g_mod_civs_mode) {
        // Clear listbox
        ((ListboxClear_t)ADDR_LISTBOX_CLEAR)(listbox, 0);

        if (g_mod_civ_count > 0) {
            for (int i = 0; i < g_mod_civ_count; i++) {
                add_listbox_item(listbox, g_mod_civs[i].title, i);
            }
            ((ListboxSetSel_t)ADDR_LISTBOX_SET_SEL)(listbox, 0);
            history_display_mod_civ(self, 0);
        } else {
            history_stop_sound(self);
            add_listbox_item(listbox, "No Mod Civs", 0);
            ((ListboxSetSel_t)ADDR_LISTBOX_SET_SEL)(listbox, 0);
            void *title_lbl = *(void **)((char *)self + 0x284);
            if (title_lbl) panel_set_text(title_lbl, "Mod Civilizations");
            void *body_txt = *(void **)((char *)self + 0x288);
            if (body_txt) panel_set_text(body_txt, "No mod civilizations with history entries are currently active.");
            for (int i = 0; i < 4; i++) {
                void *pic = *(void **)((char *)self + 0x290 + i * 4);
                if (pic) panel_set_visible(pic, 0);
            }
        }
    } else {
        // Stop any playing mod civ sound
        history_stop_sound(self);

        // Hide all mod civ image panels
        for (int i = 0; i < g_mod_civ_count; i++) {
            if (g_mod_civs[i].img_panel) {
                panel_set_visible(g_mod_civs[i].img_panel, 0);
            }
            if (g_mod_civs[i].side_img_panel) {
                panel_set_visible(g_mod_civs[i].side_img_panel, 0);
            }
        }

        // Clear listbox
        ((ListboxClear_t)ADDR_LISTBOX_CLEAR)(listbox, 0);

        // Repopulate with vanilla topics
        cache_vanilla_topics();
        for (int i = 0; i < g_vanilla_topic_count; i++) {
            add_listbox_item(listbox, g_vanilla_topics[i], i);
        }

        // Select topic 0 and reload via original load_topic (0x6cd400)
        ((ListboxSetSel_t)ADDR_LISTBOX_SET_SEL)(listbox, 0);
        *(int *)((char *)self + 0x2a4) = -1;
        ((LoadTopic_t)ADDR_LOAD_TOPIC)(self, 0);
    }
}

static void * __fastcall history_ctor_hook(void *self, void *edx) {
    (void)edx;
    void *ret = o_history_ctor(self);
    g_history_screen = self;
    g_mod_civs_mode = 0;

    wl_civs_ensure();
    load_mod_histories();
    cache_vanilla_topics();

    // Toggle button at the top, directly above the listbox (listbox x=188, y=31)
    g_btn_mod_civs = ((AddButton_t)ADDR_ADD_BUTTON)(
        self, self, "Mod civs: Off", NULL, 188, 5, 140, 24, 0, 0, 0);

    // Pre-create image panels for mod civs:
    // Top-right picture area: y=90..330, x=710, w=280, h=240
    // Bottom-left picture area: y=514..720, x=184, w=280, h=206
    for (int i = 0; i < g_mod_civ_count; i++) {
        g_mod_civs[i].img_panel = NULL;
        if (g_mod_civs[i].image_path[0]) {
            g_mod_civs[i].img_panel = create_image_panel(self, g_mod_civs[i].image_path, 710, 90, 280, 240);
            if (g_mod_civs[i].img_panel) {
                panel_set_visible(g_mod_civs[i].img_panel, 0);
            }
        }
        g_mod_civs[i].side_img_panel = NULL;
        if (g_mod_civs[i].side_image_path[0]) {
            g_mod_civs[i].side_img_panel = create_image_panel(self, g_mod_civs[i].side_image_path, 184, 514, 280, 206);
            if (g_mod_civs[i].side_img_panel) {
                panel_set_visible(g_mod_civs[i].side_img_panel, 0);
            }
        }
    }

    wl_log("history: screen %p initialized (button %p)", self, g_btn_mod_civs);
    return ret;
}

static void * __fastcall history_dtor_hook(void *self, void *edx, BYTE flags) {
    (void)edx;
    if (self == g_history_screen) {
        history_stop_sound(self);
        g_history_screen = NULL;
        g_btn_mod_civs = NULL;
        for (int i = 0; i < g_mod_civ_count; i++) {
            g_mod_civs[i].img_panel = NULL;
            g_mod_civs[i].side_img_panel = NULL;
            if (g_mod_civs[i].text) {
                free(g_mod_civs[i].text);
                g_mod_civs[i].text = NULL;
            }
        }
        g_mod_civ_count = 0;
    }
    return o_history_dtor(self, flags);
}

static int __fastcall history_event_hook(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    // Close / Back button clicked
    void *btn_back = *(void **)((char *)self + 0x2a0);
    if (panel && panel == btn_back && code == EV_LEFT_CLICK) {
        history_stop_sound(self);
    }

    // Toggle button clicked
    if (panel && panel == g_btn_mod_civs && code == EV_LEFT_CLICK && self == g_history_screen) {
        history_toggle_mod_civs(self);
        return 1;
    }

    // Listbox clicked or selection changed while in mod civs mode
    if (g_mod_civs_mode && self == g_history_screen) {
        void *listbox = *(void **)((char *)self + 0x27c);
        if (panel && panel == listbox) {
            int selected_id = ((ListboxGetSel_t)ADDR_LISTBOX_GET_SEL)(listbox);
            int *cur_sel = (int *)((char *)self + 0x2a4);
            if (selected_id >= 0 && selected_id < g_mod_civ_count && selected_id != *cur_sel) {
                history_display_mod_civ(self, selected_id);
            }
            return 1;
        }
    }

    return o_history_event(self, panel, code, a, b);
}

void wl_install_history(void) {
    // 1. Verify HistoryScreen ctor prologue bytes: 55 8b ec 6a ff
    static const BYTE ctor_bytes[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    if (memcmp((void *)ADDR_HISTORY_CTOR, ctor_bytes, sizeof ctor_bytes) != 0) {
        wl_log("history: unexpected bytes at 0x6cc830, history hooks skipped");
        return;
    }

    // 2. Verify sound & markup function prologue bytes
    static const BYTE play_snd_bytes[]  = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE start_snd_bytes[] = { 0x56, 0x57, 0x8b, 0xf9 };
    static const BYTE stop_snd_bytes[]  = { 0x56, 0x8b, 0xf1, 0x57 };
    static const BYTE str_assign_bytes[] = { 0x55, 0x8b, 0xec, 0x53, 0x57 };
    static const BYTE markup_bytes[]    = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE set_fmt_bytes[]   = { 0x55, 0x8b, 0xec, 0x6a, 0xff };

    if (memcmp((void *)ADDR_PLAY_SOUND, play_snd_bytes, sizeof play_snd_bytes) != 0 ||
        memcmp((void *)ADDR_START_SOUND, start_snd_bytes, sizeof start_snd_bytes) != 0 ||
        memcmp((void *)ADDR_STOP_SOUND, stop_snd_bytes, sizeof stop_snd_bytes) != 0 ||
        memcmp((void *)ADDR_STRING_ASSIGN, str_assign_bytes, sizeof str_assign_bytes) != 0 ||
        memcmp((void *)ADDR_PARSE_MARKUP, markup_bytes, sizeof markup_bytes) != 0 ||
        memcmp((void *)ADDR_SET_FORMATTED_TEXT, set_fmt_bytes, sizeof set_fmt_bytes) != 0) {
        wl_log("history: unexpected bytes at sound/markup routines, history hooks skipped");
        return;
    }

    // 3. Verify HistoryScreen vtable: slot 0 (0x6cd300), slot 49 (0x6cd3b0)
    void **vtbl = (void **)HISTORY_VTBL;
    if ((DWORD)vtbl[0] != ADDR_HISTORY_DTOR || (DWORD)vtbl[49] != ADDR_HISTORY_EVENT) {
        wl_log("history: unexpected vtable at 0x9d5a94 (slot 0=%p, slot 49=%p), skipped",
               vtbl[0], vtbl[49]);
        return;
    }

    // 4. Detour HistoryScreen::ctor
    o_history_ctor = (HistoryCtor_t)wl_hook(ADDR_HISTORY_CTOR, history_ctor_hook, sizeof ctor_bytes);
    if (!o_history_ctor) {
        wl_log("history: hook on 0x6cc830 failed");
        return;
    }

    // 5. Hook HistoryScreen vtable slot 0 (destructor) and slot 49 (event)
    DWORD old;
    o_history_dtor = (HistoryDtor_t)vtbl[0];
    VirtualProtect(&vtbl[0], 4, PAGE_READWRITE, &old);
    vtbl[0] = (void *)history_dtor_hook;
    VirtualProtect(&vtbl[0], 4, old, &old);

    o_history_event = (HistoryEvent_t)vtbl[49];
    VirtualProtect(&vtbl[49], 4, PAGE_READWRITE, &old);
    vtbl[49] = (void *)history_event_hook;
    VirtualProtect(&vtbl[49], 4, old, &old);

    wl_log("history: hooks installed (ctor %p, vtbl slot 0=%p, slot 49=%p)",
           o_history_ctor, o_history_dtor, o_history_event);
}
