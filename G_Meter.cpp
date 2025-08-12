#include "G_Meter.h"
#include <math.h>
#include <stdio.h>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "TCA9554PWR.h"
#include "SD_Card.h"
#include "logo_b08c.h"

// --- Mini logo (monochrome) 64x64 en ARGB8888 pour simplicité (peut être remplacé par votre data compressée) ---
// Pour économiser la mémoire, on utilise une version 64x64 simplifiée (placeholder semi-transparente)
static const uint32_t logo64_placeholder[] PROGMEM = { 0x00000000 };
static inline void draw_logo_bottom_left(lv_obj_t *parent) {
    const int target_w = 64; // largeur visée pour le logo
    const int margin = 12;   // marge pour rester dans le disque
    const int pad = 6;       // marge interne du fond rond (px)
    static lv_obj_t *logo_bg = NULL;
    static lv_obj_t *logo_img = NULL;
    if (!logo_bg && !logo_img) {
        // Fond rond blanc semi-transparent
        logo_bg = lv_obj_create(parent);
        lv_obj_set_style_border_width(logo_bg, 0, 0);
        lv_obj_set_style_pad_all(logo_bg, 0, 0);
        lv_obj_set_style_bg_color(logo_bg, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(logo_bg, 120, 0);

        // Image centrée dans le fond
        logo_img = lv_img_create(logo_bg);
        lv_img_set_src(logo_img, &logo_b08c_img);

        // Zoom pour viser ~64 px de large
        uint16_t base_w = logo_b08c_img.header.w ? logo_b08c_img.header.w : 1;
        uint16_t zoom = (uint16_t)((256.0f * target_w) / (float)base_w);
        if (zoom > 256) zoom = 256; if (zoom == 0) zoom = 1;
        lv_img_set_zoom(logo_img, zoom);

        // Dimension du fond circulaire
        int32_t eff_w = (base_w * zoom) / 256;
        int32_t eff_h = (logo_b08c_img.header.h * zoom) / 256;
        int32_t bg_d = (eff_w > eff_h ? eff_w : eff_h) + pad * 2;
        if (bg_d < 8) bg_d = 8;
        lv_obj_set_size(logo_bg, bg_d, bg_d);
        lv_obj_set_style_radius(logo_bg, bg_d / 2, 0);

        // Placement dans le disque visible
        int32_t parent_w = lv_obj_get_width(parent);
        int32_t parent_h = lv_obj_get_height(parent);
        int32_t radius = (parent_w < parent_h ? parent_w : parent_h) / 2;
        int32_t half_size = bg_d / 2;
        float theta = 225.0f * (float)M_PI / 180.0f;
        float r = (float)(radius - half_size - margin);
        if (r < 0) r = 0;
        int dx = (int)(cosf(theta) * r);
        int dy = (int)(sinf(theta) * r);
        lv_obj_align(logo_bg, LV_ALIGN_CENTER, dx, dy);

        // Centrage et rendu
        lv_obj_center(logo_img);
        lv_obj_clear_flag(logo_bg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(logo_img, 160, 0);
        lv_obj_move_foreground(logo_bg);
    }
}

// Variables globales
G_Max_Values g_max_values = {0};
G_Current_Values g_current_values = {0};

// Objets LVGL pour le G-meter
static lv_obj_t *g_meter_container = NULL;
static lv_obj_t *grid_canvas = NULL;     // Grille statique
static lv_obj_t *stain_canvas = NULL;    // Zone parcourue (alpha)
static lv_obj_t *point_canvas = NULL;    // Canvas du point blanc (alpha)
static lv_obj_t *g_meter_title = NULL;
static lv_obj_t *g_meter_value = NULL;   // valeur instantanée
static lv_obj_t *g_peak_label = NULL;    // pic total XYZ
static lv_obj_t *label_top = NULL;       // avant
static lv_obj_t *label_bottom = NULL;    // arrière/freinage
static lv_obj_t *label_left = NULL;      // gauche
static lv_obj_t *label_right = NULL;     // droite

// Constantes pour le G-meter
#define G_METER_RADIUS 200
#define G_METER_CENTER_X 240
#define G_METER_CENTER_Y 240
#define G_METER_SCALE_FACTOR 100.0f  // 1G = 100 pixels
#define G_METER_MAX_G 2.0f           // Maximum 2G affiché

// Orientation (corrige axes inversés)
#define GM_SWAP_XY   1
#define GM_INVERT_X  1  // inverser X (droite/gauche)
#define GM_INVERT_Y  0  // inverser Y (avant/arrière)

// Apparence
#define POINT_SIZE        16          // Taille du point blanc (px)
#define RED_STAIN_OPA     LV_OPA_70   // Opacité coeur de la zone rouge
#define STAIN_RADIUS      10          // Rayon coeur (px)
#define STAIN_SOFT_EDGE   4           // Bord doux supplémentaire (px)

// Buffers canvas -> alloués dynamiquement (PSRAM si dispo)
static lv_color_t   *grid_canvas_buf  = NULL;   // TRUE_COLOR (16-bit)
static lv_color32_t *stain_canvas_buf = NULL;   // TRUE_COLOR_ALPHA (32-bit)
static lv_color32_t *point_canvas_buf = NULL;   // TRUE_COLOR_ALPHA (32-bit)

// Baseline (calibrage) pour centrer le 0G à la position initiale
static float base_raw_x = 0.0f, base_raw_y = 0.0f, base_raw_z = 0.0f;   // base en repère capteur
static float base_map_x = 0.0f, base_map_y = 0.0f;                       // base mappée sur l’écran pour le plan sélectionné
static bool  has_baseline = false;

// Axe principal de gravité détecté sur la baseline: 0=X, 1=Y, 2=Z
static int gravity_axis = 2; // par défaut Z (écran à plat)

// Applique l'échange/inversion de façon dynamique: on NE swap pas en mode vertical (gravité=Y)
static inline void apply_mapping(float u, float v, float *mx, float *my) {
    float x = u, y = v;
    if (gravity_axis == 2) {
    #if GM_SWAP_XY
        float t = x; x = y; y = t;
    #endif
    }
    // inversions toujours appliquées
#if GM_INVERT_X
    x = -x;
#endif
#if GM_INVERT_Y
    y = -y;
#endif
    *mx = x; *my = y;
}

// Convertit les bruts (x,y,z) en composantes du plan choisi (u,v) suivant l'axe de gravité
static inline void get_plane_uv_from_raw(float rx, float ry, float rz, float *u, float *v) {
    if (gravity_axis == 2) {         // gravité ~ Z  => plan XY
        *u = rx; *v = ry;
    } else if (gravity_axis == 1) {  // gravité ~ Y  => plan XZ
        *u = rx; *v = rz;            // comme demandé: remplacer Y par Z
    } else {                         // gravité ~ X  => plan YZ (rare)
        *u = ry; *v = rz;
    }
}

// Calcule les deltas du plan (u_d, v_d) et de l'axe orthogonal (w_d) par rapport à la baseline
static inline void get_deltas(float rx, float ry, float rz, float *u_d, float *v_d, float *w_d) {
    float u, v; get_plane_uv_from_raw(rx, ry, rz, &u, &v);
    // baseline mappée déjà convertie en base_map_x/base_map_y
    if (has_baseline) { u -= base_map_x; v -= base_map_y; }

    // w_d: delta sur l'axe de gravité (orthogonal au plan)
    float dx = rx - base_raw_x;
    float dy = ry - base_raw_y;
    float dz = rz - base_raw_z;
    float w = 0.0f;
    if (gravity_axis == 2) w = dz; else if (gravity_axis == 1) w = dy; else w = dx;

    *u_d = u; *v_d = v; *w_d = w;
}

// Sélectionne la plus grande police dispo
static const lv_font_t *get_label_font() {
#if LV_FONT_MONTSERRAT_40
    return &lv_font_montserrat_40;
#elif LV_FONT_MONTSERRAT_36
    return &lv_font_montserrat_36;
#elif LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#elif LV_FONT_MONTSERRAT_28
    return &lv_font_montserrat_28;
#elif LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#elif LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#else
    return LV_FONT_DEFAULT;
#endif
}

// Grille (une seule fois)
static void G_Meter_Draw_Grid(void) {
    lv_canvas_fill_bg(grid_canvas, lv_color_hex(0x1a1a1a), LV_OPA_COVER);

    for (int i = 1; i <= 4; i++) {
        float g_value = i * 0.5f;
        int radius = (int)(g_value * G_METER_SCALE_FACTOR);
        for (int angle = 0; angle < 360; angle += 5) {
            float rad = angle * M_PI / 180.0f;
            int x1 = G_METER_CENTER_X + (int)(radius * cos(rad));
            int y1 = G_METER_CENTER_Y + (int)(radius * sin(rad));
            int x2 = G_METER_CENTER_X + (int)(radius * cos((angle + 5) * M_PI / 180.0f));
            int y2 = G_METER_CENTER_Y + (int)(radius * sin((angle + 5) * M_PI / 180.0f));
            
            lv_draw_line_dsc_t line_dsc; lv_draw_line_dsc_init(&line_dsc);
            line_dsc.color = lv_color_hex(0x666666); line_dsc.width = 1; line_dsc.opa = LV_OPA_50;
            lv_point_t pts[2] = {{x1,y1},{x2,y2}}; lv_canvas_draw_line(grid_canvas, pts, 2, &line_dsc);
        }
    }

    for (int i = 0; i < 4; i++) {
        float angle = i * 90.0f * M_PI / 180.0f;
        int end_x = G_METER_CENTER_X + (int)(G_METER_RADIUS * cos(angle));
        int end_y = G_METER_CENTER_Y + (int)(G_METER_RADIUS * sin(angle));
        lv_draw_line_dsc_t line_dsc; lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = lv_color_hex(0x666666); line_dsc.width = 1; line_dsc.opa = LV_OPA_50;
        lv_point_t pts[2] = {{G_METER_CENTER_X, G_METER_CENTER_Y},{end_x,end_y}}; lv_canvas_draw_line(grid_canvas, pts, 2, &line_dsc);
    }
}

// Zone parcourue rouge avec bord doux
static void Stain_Add_Dot(int x, int y) {
    // Couronne douce (opacité faible)
    if (STAIN_SOFT_EDGE > 0) {
        lv_draw_rect_dsc_t soft; lv_draw_rect_dsc_init(&soft);
        soft.bg_color = lv_color_hex(0xFF0000); soft.bg_opa = LV_OPA_20; soft.radius = STAIN_RADIUS + STAIN_SOFT_EDGE;
        int d = (STAIN_RADIUS + STAIN_SOFT_EDGE) * 2;
        lv_canvas_draw_rect(stain_canvas, x - (d/2), y - (d/2), d, d, &soft);
    }
    // Coeur plus opaque
    lv_draw_rect_dsc_t core; lv_draw_rect_dsc_init(&core);
    core.bg_color = lv_color_hex(0xFF0000); core.bg_opa = RED_STAIN_OPA; core.radius = STAIN_RADIUS;
    int dc = STAIN_RADIUS * 2;
    lv_canvas_draw_rect(stain_canvas, x - (dc/2), y - (dc/2), dc, dc, &core);
}

static void update_peak_labels(void) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.1f", g_max_values.y_neg);  lv_label_set_text(label_top, buf);     // haut/avant
    snprintf(buf, sizeof(buf), "%.1f", g_max_values.y_pos);  lv_label_set_text(label_bottom, buf);  // bas/arrière
    snprintf(buf, sizeof(buf), "%.1f", g_max_values.x_neg);  lv_label_set_text(label_left, buf);    // gauche
    snprintf(buf, sizeof(buf), "%.1f", g_max_values.x_pos);  lv_label_set_text(label_right, buf);   // droite

    snprintf(buf, sizeof(buf), "Max total : %.1f G", g_max_values.total);
    if (g_peak_label) lv_label_set_text(g_peak_label, buf);
}

// Calibrage: moyenne sur N échantillons pour définir la baseline + détection d’axe de gravité
static void perform_calibration(void) {
    const int samples = 50;
    float sx = 0, sy = 0, sz = 0;
    for (int i = 0; i < samples; i++) {
        sx += Accel.x; sy += Accel.y; sz += Accel.z;
#ifdef ARDUINO_ARCH_ESP32
        vTaskDelay(pdMS_TO_TICKS(5));
#else
        delay(5);
#endif
    }
    base_raw_x = sx / samples;
    base_raw_y = sy / samples;
    base_raw_z = sz / samples;

    // Détection de l’axe de gravité avec seuil
    float ax = fabsf(base_raw_x), ay = fabsf(base_raw_y), az = fabsf(base_raw_z);
    float gmag = sqrtf(ax*ax + ay*ay + az*az);
    float nx = (gmag > 0) ? ax / gmag : 0, ny = (gmag > 0) ? ay / gmag : 0, nz = (gmag > 0) ? az / gmag : 0;
    const float TH = 0.7f; // 70% du vecteur gravité
    if (nz >= TH) gravity_axis = 2;
    else if (ny >= TH) gravity_axis = 1;
    else if (nx >= TH) gravity_axis = 0;
    else {
        // garder l’axe précédent si incertain
    }

    // Baseline mappée vers le plan choisi puis swap/invert dynamique
    float base_u, base_v; get_plane_uv_from_raw(base_raw_x, base_raw_y, base_raw_z, &base_u, &base_v);
    apply_mapping(base_u, base_v, &base_map_x, &base_map_y);

    has_baseline = true;
}

// Mettre à jour la position du point et peindre la zone
void G_Meter_Update_Display(void) {
    if (stain_canvas == NULL || point_canvas == NULL) return;

    // Deltas du plan (u_d, v_d)
    float u_d, v_d, w_d; get_deltas(g_current_values.x, g_current_values.y, g_current_values.z, &u_d, &v_d, &w_d);
    float mx, my; apply_mapping(u_d, v_d, &mx, &my);

    int sx = G_METER_CENTER_X + (int)(mx * G_METER_SCALE_FACTOR);
    int sy = G_METER_CENTER_Y + (int)(my * G_METER_SCALE_FACTOR);

    int dx = sx - G_METER_CENTER_X; int dy = sy - G_METER_CENTER_Y; int dist = (int)sqrt(dx*dx + dy*dy);
    if (dist > G_METER_RADIUS) { float sc = (float)G_METER_RADIUS / dist; sx = G_METER_CENTER_X + (int)(dx * sc); sy = G_METER_CENTER_Y + (int)(dy * sc); }

    // Déplacer et redessiner le point blanc sur son propre canvas (alpha)
    lv_obj_set_pos(point_canvas, sx - (POINT_SIZE/2), sy - (POINT_SIZE/2));
    lv_canvas_fill_bg(point_canvas, lv_color_black(), LV_OPA_TRANSP);
    lv_draw_rect_dsc_t pd; lv_draw_rect_dsc_init(&pd);
    pd.bg_color = lv_color_white(); pd.bg_opa = LV_OPA_COVER; pd.radius = POINT_SIZE/2;
    lv_canvas_draw_rect(point_canvas, 0, 0, POINT_SIZE, POINT_SIZE, &pd);
    lv_obj_move_foreground(point_canvas);

    // Peindre la zone parcourue en arrière-plan
    Stain_Add_Dot(sx, sy);

    if (g_meter_value) { char b[16]; snprintf(b, sizeof(b), "%.1f", g_current_values.total); lv_label_set_text(g_meter_value, b); }

    update_peak_labels();
}

// Petit double bip bref
static void short_double_beep(void) {
    for (int i = 0; i < 2; i++) {
        Set_EXIO(EXIO_PIN8, High);
#ifdef ARDUINO_ARCH_ESP32
        vTaskDelay(pdMS_TO_TICKS(60));
#else
        delay(60);
#endif
        Set_EXIO(EXIO_PIN8, Low);
#ifdef ARDUINO_ARCH_ESP32
        vTaskDelay(pdMS_TO_TICKS(120));
#else
        delay(120);
#endif
    }
}

static void container_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        if (stain_canvas) lv_canvas_fill_bg(stain_canvas, lv_color_black(), LV_OPA_TRANSP);
        G_Meter_Reset_Max();
        perform_calibration();
        short_double_beep();
        update_peak_labels();
    }
}

void G_Meter_Create(lv_obj_t *parent) {
    g_meter_container = lv_obj_create(parent);
    lv_obj_set_size(g_meter_container, 480, 480);
    lv_obj_align(g_meter_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(g_meter_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(g_meter_container, 0, 0);
    lv_obj_set_style_pad_all(g_meter_container, 0, 0);
    lv_obj_add_event_cb(g_meter_container, container_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    g_meter_title = lv_label_create(g_meter_container);
    lv_label_set_text(g_meter_title, "G meter");
    lv_obj_set_style_text_color(g_meter_title, lv_color_white(), 0);
    lv_obj_align(g_meter_title, LV_ALIGN_TOP_MID, 0, 8);

    g_meter_value = lv_label_create(g_meter_container);
    lv_label_set_text(g_meter_value, "0.0");
    lv_obj_set_style_text_color(g_meter_value, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_meter_value, get_label_font(), 0);
    lv_obj_align_to(g_meter_value, g_meter_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    // Canvases (grille, tache)
    grid_canvas = lv_canvas_create(g_meter_container);
    lv_obj_set_size(grid_canvas, 480, 480); lv_obj_align(grid_canvas, LV_ALIGN_CENTER, 0, 0);
#ifdef ARDUINO_ARCH_ESP32
    grid_canvas_buf = (lv_color_t *)heap_caps_malloc(480 * 480 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    grid_canvas_buf = (lv_color_t *)malloc(480 * 480 * sizeof(lv_color_t));
#endif
    if (!grid_canvas_buf) { grid_canvas_buf = (lv_color_t *)malloc(240 * 240 * sizeof(lv_color_t)); lv_canvas_set_buffer(grid_canvas, grid_canvas_buf, 240, 240, LV_IMG_CF_TRUE_COLOR); lv_obj_set_size(grid_canvas, 240, 240); }
    else { lv_canvas_set_buffer(grid_canvas, grid_canvas_buf, 480, 480, LV_IMG_CF_TRUE_COLOR); }
    G_Meter_Draw_Grid();

    stain_canvas = lv_canvas_create(g_meter_container);
    lv_obj_set_size(stain_canvas, 480, 480); lv_obj_align(stain_canvas, LV_ALIGN_CENTER, 0, 0);
#ifdef ARDUINO_ARCH_ESP32
    stain_canvas_buf = (lv_color32_t *)heap_caps_malloc(480 * 480 * sizeof(lv_color32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    stain_canvas_buf = (lv_color32_t *)malloc(480 * 480 * sizeof(lv_color32_t));
#endif
    if (!stain_canvas_buf) { stain_canvas_buf = (lv_color32_t *)malloc(240 * 240 * sizeof(lv_color32_t)); lv_canvas_set_buffer(stain_canvas, stain_canvas_buf, 240, 240, LV_IMG_CF_TRUE_COLOR_ALPHA); lv_obj_set_size(stain_canvas, 240, 240); }
    else { lv_canvas_set_buffer(stain_canvas, stain_canvas_buf, 480, 480, LV_IMG_CF_TRUE_COLOR_ALPHA); }
    lv_canvas_fill_bg(stain_canvas, lv_color_black(), LV_OPA_TRANSP);

    // Canvas du point blanc (petit, alpha), créé en dernier pour être tout en haut
    point_canvas = lv_canvas_create(g_meter_container);
    lv_obj_set_size(point_canvas, POINT_SIZE, POINT_SIZE);
#ifdef ARDUINO_ARCH_ESP32
    point_canvas_buf = (lv_color32_t *)heap_caps_malloc(POINT_SIZE * POINT_SIZE * sizeof(lv_color32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    point_canvas_buf = (lv_color32_t *)malloc(POINT_SIZE * POINT_SIZE * sizeof(lv_color32_t));
#endif
    if (!point_canvas_buf) { point_canvas_buf = (lv_color32_t *)malloc(POINT_SIZE * POINT_SIZE * sizeof(lv_color32_t)); }
    lv_canvas_set_buffer(point_canvas, point_canvas_buf, POINT_SIZE, POINT_SIZE, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(point_canvas, lv_color_black(), LV_OPA_TRANSP);
    lv_obj_move_foreground(point_canvas);

    // Labels de pics aux bords (grande police)
    const lv_font_t *big_font = get_label_font();
    label_top = lv_label_create(g_meter_container); lv_obj_set_style_text_color(label_top, lv_color_white(), 0); lv_obj_set_style_text_font(label_top, big_font, 0); lv_obj_align(label_top, LV_ALIGN_TOP_MID, 0, 10);
    label_bottom = lv_label_create(g_meter_container); lv_obj_set_style_text_color(label_bottom, lv_color_white(), 0); lv_obj_set_style_text_font(label_bottom, big_font, 0); lv_obj_align(label_bottom, LV_ALIGN_BOTTOM_MID, 0, -10);
    label_left = lv_label_create(g_meter_container); lv_obj_set_style_text_color(label_left, lv_color_white(), 0); lv_obj_set_style_text_font(label_left, big_font, 0); lv_obj_align(label_left, LV_ALIGN_LEFT_MID, 10, 0);
    label_right = lv_label_create(g_meter_container); lv_obj_set_style_text_color(label_right, lv_color_white(), 0); lv_obj_set_style_text_font(label_right, big_font, 0); lv_obj_align(label_right, LV_ALIGN_RIGHT_MID, -10, 0);

    // Ligne de pic total XYZ (grosse police)
    g_peak_label = lv_label_create(g_meter_container);
    lv_obj_set_style_text_color(g_peak_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_peak_label, big_font, 0);
    lv_obj_align(g_peak_label, LV_ALIGN_BOTTOM_MID, 0, -40);

    // Calibrage auto au démarrage
    perform_calibration();
    short_double_beep();

    // Dessiner le logo intégré en bas à gauche (firmware)
    draw_logo_bottom_left(g_meter_container);

    update_peak_labels();
}

void G_Meter_Update(void) {
    // Valeurs courantes
    g_current_values.x = Accel.x; g_current_values.y = Accel.y; g_current_values.z = Accel.z;

    // Deltas (u_d, v_d, w_d) suivant l’axe de gravité détecté
    float u_d, v_d, w_d; get_deltas(g_current_values.x, g_current_values.y, g_current_values.z, &u_d, &v_d, &w_d);

    // Appliquer mapping dynamique
    float mx, my; apply_mapping(u_d, v_d, &mx, &my);

    // Total (magnitude) basé sur delta complet (plan + orthogonal)
    float total_delta = sqrtf(mx*mx + my*my + w_d*w_d);
    g_current_values.total = total_delta;

    // Max sur axes (delta) côté écran
    float ax = fabsf(mx); float ay = fabsf(my); float az = fabsf(w_d);

    if (mx > g_max_values.x_pos) g_max_values.x_pos = mx;         // droite
    if (-mx > g_max_values.x_neg) g_max_values.x_neg = -mx;       // gauche
    if (my > g_max_values.y_pos) g_max_values.y_pos = my;         // bas/avant (sens écran)
    if (-my > g_max_values.y_neg) g_max_values.y_neg = -my;       // haut/arrière

    if (ax > g_max_values.x_abs) g_max_values.x_abs = ax;
    if (ay > g_max_values.y_abs) g_max_values.y_abs = ay;
    if (az > g_max_values.z_abs) g_max_values.z_abs = az;

    if (g_current_values.total > g_max_values.total) g_max_values.total = g_current_values.total;
}

void G_Meter_Reset_Max(void) {
    memset(&g_max_values, 0, sizeof(g_max_values));
}

void G_Meter_Delete(void) {
    if (g_meter_container != NULL) { lv_obj_del(g_meter_container); g_meter_container = NULL; grid_canvas = NULL; stain_canvas = NULL; point_canvas = NULL; g_meter_title = NULL; g_meter_value = NULL; g_peak_label = NULL; label_top = label_bottom = label_left = label_right = NULL; }
#ifdef ARDUINO_ARCH_ESP32
    if (grid_canvas_buf) { heap_caps_free(grid_canvas_buf); grid_canvas_buf = NULL; }
    if (stain_canvas_buf) { heap_caps_free(stain_canvas_buf); stain_canvas_buf = NULL; }
    if (point_canvas_buf) { heap_caps_free(point_canvas_buf); point_canvas_buf = NULL; }
#else
    if (grid_canvas_buf) { free(grid_canvas_buf); grid_canvas_buf = NULL; }
    if (stain_canvas_buf) { free(stain_canvas_buf); stain_canvas_buf = NULL; }
    if (point_canvas_buf) { free(point_canvas_buf); point_canvas_buf = NULL; }
#endif
}

void G_Meter_Timer_Callback(lv_timer_t *timer) { G_Meter_Update(); G_Meter_Update_Display(); }
