#define _USE_MATH_DEFINES
#include "TileEngine.hpp"
#include "src/draw/lv_image_decoder_private.h"
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include "esp_log.h"
#include "esp_timer.h"

#include <sys/stat.h>

#define JPEG_FORMAT   1
#define PNG_FORMAT    2
#define RGB565_FORMAT 3
#define TILE_FORMAT JPEG_FORMAT

#if TILE_FORMAT == JPEG_FORMAT
#define TILE_EXTENTION "jpg"
#define TILE_PATH_BASE_DIR "/tiles-jpg"
#elif TILE_FORMAT == PNG_FORMAT
#define TILE_EXTENTION "png"
#define TILE_PATH_BASE_DIR "/tiles-png"
#elif TILE_FORMAT == RGB565_FORMAT
#define TILE_EXTENTION "rgb565"
#define TILE_PATH_BASE_DIR "/tiles-rgb565"
#endif

static const char* TAG = "TileEngine";

void list_sd_card_contents(const char *main_dir) {
    lv_fs_dir_t dir;
    lv_fs_res_t res;
    char fn[256]; // Buffer pour stocker le nom du fichier/dossier

    // 1. Ouvrir le répertoire racine
    res = lv_fs_dir_open(&dir, main_dir);
    if (res != LV_FS_RES_OK) {
        ESP_LOGE(TAG, "Impossible d'ouvrir le répertoire S:/ (Erreur: %d)", res);
        return;
    }

    // 2. Boucler sur les éléments
    while (lv_fs_dir_read(&dir, fn, sizeof(fn)) == LV_FS_RES_OK) {
        // Si fn est vide, on a fini de lister
        if (fn[0] == '\0') {
            break;
        }

        // 3. Distinguer Dossiers et Fichiers
        // Dans LVGL, par convention, les noms de dossiers commencent souvent par '/'
        if (fn[0] == '/') {
            ESP_LOGI(TAG, "[DIR]  %s%s", main_dir, fn);
            char subdir[256];
            snprintf(subdir, sizeof(subdir), "%s%s", main_dir, fn);
            list_sd_card_contents(subdir);
        } else {
            ESP_LOGI(TAG, "[FILE] %s/%s", main_dir, fn);
        }
    }

    // 4. Fermer le répertoire
    lv_fs_dir_close(&dir);
}
TileEngine::TileEngine() : _map_container(nullptr) {}

TileEngine::~TileEngine() {}

static lv_result_t rgb565_decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header) {
    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        const char * path = (const char *)dsc->src;
        if(strstr(path, ".rgb565") != NULL) {
            header->cf = LV_COLOR_FORMAT_RGB565;
            header->w = 256;
            header->h = 256;
            header->stride = 256 * 2;
            header->magic = LV_IMAGE_HEADER_MAGIC;
            return LV_RESULT_OK;
        }
    }
    return LV_RESULT_INVALID;
}

static lv_result_t rgb565_decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc) {
    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        const char * path = (const char *)dsc->src;
        if(strstr(path, ".rgb565") != NULL) {
            uint32_t w = 256;
            uint32_t h = 256;
            lv_draw_buf_t * draw_buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
            if(!draw_buf) return LV_RESULT_INVALID;

            lv_fs_file_t f;
            lv_fs_res_t res = lv_fs_open(&f, path, LV_FS_MODE_RD);
            if(res != LV_FS_RES_OK) {
                lv_draw_buf_destroy(draw_buf);
                return LV_RESULT_INVALID;
            }

            uint32_t br;
            res = lv_fs_read(&f, draw_buf->data, draw_buf->data_size, &br);
            lv_fs_close(&f);

            if(res != LV_FS_RES_OK || br != draw_buf->data_size) {
                lv_draw_buf_destroy(draw_buf);
                return LV_RESULT_INVALID;
            }

            dsc->decoded = draw_buf;
            return LV_RESULT_OK;
        }
    }
    return LV_RESULT_INVALID;
}

static void rgb565_decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc) {
    if(dsc->decoded) {
        lv_draw_buf_destroy((lv_draw_buf_t *)dsc->decoded);
        dsc->decoded = NULL;
    }
}

void TileEngine::lv_rgb565_decoder_init() {
    lv_image_decoder_t * decoder = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(decoder, rgb565_decoder_info);
    lv_image_decoder_set_open_cb(decoder, rgb565_decoder_open);
    lv_image_decoder_set_close_cb(decoder, rgb565_decoder_close);
}

void TileEngine::init() {
    lv_rgb565_decoder_init();
    _map_container = lv_obj_create(lv_screen_active());
    // Remove all default styles first, then set our own properties
    lv_obj_remove_style_all(_map_container);
    lv_obj_set_size(_map_container, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Set a dark background to show while tiles are loading
    lv_obj_set_style_bg_color(_map_container, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_bg_opa(_map_container, LV_OPA_COVER, 0);

    // Ensure the container itself doesn't scroll; it's a fixed viewport for our tile grid
    lv_obj_remove_flag(_map_container, LV_OBJ_FLAG_SCROLLABLE);

    for (int r = 0; r < GRID_ROWS; ++r) {
        for (int c = 0; c < GRID_COLS; ++c) {
            lv_obj_t* img = lv_image_create(_map_container);
            // Explicitly set tile size to avoid auto-layout or initialization glitches
            lv_obj_set_size(img, TILE_SIZE, TILE_SIZE);
            lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
            TileInfo info = {img, -1, -1, -1, ""};
            _tile_grid.push_back(info);
        }
    }
}

void TileEngine::setMapCenter(double lat, double lon, int zoom) {
    updateTiles(lat, lon, zoom);
}

void TileEngine::latLonToTile(double lat, double lon, int zoom, double& x, double& y) {
    double n = std::pow(2.0, zoom);
    x = (lon + 180.0) / 360.0 * n;
    double lat_rad = lat * M_PI / 180.0;
    y = (1.0 - std::log(std::tan(lat_rad) + (1.0 / std::cos(lat_rad))) / M_PI) / 2.0 * n;
}

void TileEngine::getTilePath(char* buf, size_t buf_size, int zoom, int x, int y, bool for_lvgl) {
    if (for_lvgl) {
        snprintf(buf, buf_size, "%s%s/%d/%d/%d." TILE_EXTENTION, LV_DRIVE_PREFIX, TILE_PATH_BASE_DIR, zoom, x, y);
    } else {
        snprintf(buf, buf_size, "/sdcard%s/%d/%d/%d." TILE_EXTENTION, TILE_PATH_BASE_DIR, zoom, x, y);
    }
}

void TileEngine::updateTiles(double lat, double lon, int zoom) {
    int64_t start_time = esp_timer_get_time();
    double tile_x, tile_y;
    latLonToTile(lat, lon, zoom, tile_x, tile_y);

    // Precise pixel coordinates of the target center in the global map at this zoom level
    double center_pixel_x = tile_x * TILE_SIZE;
    double center_pixel_y = tile_y * TILE_SIZE;

    // We want (lat, lon) to be at (SCREEN_WIDTH/2, SCREEN_HEIGHT/2)
    int screen_center_x = SCREEN_WIDTH / 2;
    int screen_center_y = SCREEN_HEIGHT / 2;

    // Calculate the top-left corner of the screen in global pixel coordinates
    int screen_tl_x = (int)(center_pixel_x - screen_center_x);
    int screen_tl_y = (int)(center_pixel_y - screen_center_y);

    // Find the base tile index that covers the top-left corner
    int base_tile_x = (int)std::floor((double)screen_tl_x / TILE_SIZE);
    int base_tile_y = (int)std::floor((double)screen_tl_y / TILE_SIZE);

    // Calculate pixel offset of the base tile relative to the screen top-left
    int offset_x = (base_tile_x * TILE_SIZE) - screen_tl_x;
    int offset_y = (base_tile_y * TILE_SIZE) - screen_tl_y;

    ESP_LOGI(TAG, "Centering on Lat: %f, Lon: %f, Zoom: %d", lat, lon, zoom);
    ESP_LOGI(TAG, "Base Tile: %d, %d | Offset: %d, %d", base_tile_x, base_tile_y, offset_x, offset_y);

    for (int r = 0; r < GRID_ROWS; ++r) {
        for (int c = 0; c < GRID_COLS; ++c) {
            int tile_idx_x = base_tile_x + c;
            int tile_idx_y = base_tile_y + r;
            auto& tile = _tile_grid[r * GRID_COLS + c];

            // Update position
            lv_obj_set_pos(tile.img_obj, offset_x + c * TILE_SIZE, offset_y + r * TILE_SIZE);

            // Update source if tile indices changed
            if (tile.x_idx != tile_idx_x || tile.y_idx != tile_idx_y || tile.zoom != zoom) {
                tile.x_idx = tile_idx_x;
                tile.y_idx = tile_idx_y;
                tile.zoom = zoom;

                // Important: Each tile must have its own persistent path string buffer
                struct stat st;
                getTilePath(tile.path, sizeof(tile.path), zoom, tile_idx_x, tile_idx_y, false);
                if (stat(tile.path, &st) == 0) {
                    getTilePath(tile.path, sizeof(tile.path), zoom, tile_idx_x, tile_idx_y, true);
                    lv_image_set_src(tile.img_obj, tile.path);
                }
            }
        }
    }
    int64_t end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Map tiles updated in %lld us", (end_time - start_time));
}

#if TILE_DEBUG
void TileEngine::debug(double lat, double lon, int zoom) {
    ESP_LOGI(TAG, "--- Tile Engine Debug Start (Lat: %f, Lon: %f, Zoom: %d) ---", lat, lon, zoom);

    // 2. Check if the LVGL drive 'S:' is usable
    lv_fs_dir_t lv_dir;
    char root_path[128];
    snprintf(root_path, sizeof(root_path), "%s/", LV_DRIVE_PREFIX);
    lv_fs_res_t res = lv_fs_dir_open(&lv_dir, root_path);
    if (res == LV_FS_RES_OK) {
        ESP_LOGI(TAG, "LVGL FS CHECK: SUCCESS - Drive '%s' is usable.", root_path);
        lv_fs_dir_close(&lv_dir);
    } else {
        ESP_LOGE(TAG, "LVGL FS CHECK: FAILED - Drive '%s' is NOT usable (Error: %d). Check LVGL FatFS config.", root_path, res);
    }
//    ESP_LOGI(TAG, "Lecture du contenu de S:");
//    list_sd_card_contents("S:");

    double tile_x, tile_y;
    latLonToTile(lat, lon, zoom, tile_x, tile_y);

    double center_pixel_x = tile_x * TILE_SIZE;
    double center_pixel_y = tile_y * TILE_SIZE;

    int screen_tl_x = (int)(center_pixel_x - (SCREEN_WIDTH / 2));
    int screen_tl_y = (int)(center_pixel_y - (SCREEN_HEIGHT / 2));

    int base_tile_x = (int)std::floor((double)screen_tl_x / TILE_SIZE);
    int base_tile_y = (int)std::floor((double)screen_tl_y / TILE_SIZE);

    ESP_LOGI(TAG, "Screen Top-Left Pixel: %d, %d", screen_tl_x, screen_tl_y);
    ESP_LOGI(TAG, "Base Tile Indices: X=%d, Y=%d", base_tile_x, base_tile_y);

    int total_found = 0;
    int total_valid = 0;

    for (int r = 0; r < GRID_ROWS; ++r) {
        for (int c = 0; c < GRID_COLS; ++c) {
            int tile_idx_x = base_tile_x + c;
            int tile_idx_y = base_tile_y + r;
            char full_path[128];
            struct stat st;

            getTilePath(full_path, sizeof(full_path), zoom, tile_idx_x, tile_idx_y, false);
            if (stat(full_path, &st) == 0) {
                lv_fs_file_t f;
                // On utilise le chemin LVGL (commençant par S:/)
                getTilePath(full_path, sizeof(full_path), zoom, tile_idx_x, tile_idx_y, true);
                lv_fs_res_t res = lv_fs_open(&f, full_path, LV_FS_MODE_RD);
                total_found++;
                // --- Équivalent de fseek(f, 0, SEEK_END) + ftell(f) ---
                uint32_t size = 0;
                lv_fs_seek(&f, 0, LV_FS_SEEK_END);
                lv_fs_tell(&f, &size);
                // On revient au début : fseek(f, 0, SEEK_SET)
                lv_fs_seek(&f, 0, LV_FS_SEEK_SET);
                // --- Lecture du Header ---
                uint8_t header[8];
                uint32_t br; // Bytes Read (indispensable avec LVGL)
                bool signature_ok = false;
                // lv_fs_read prend l'adresse de br pour retourner le nombre d'octets lus
                res = lv_fs_read(&f, header, 8, &br);
                if (res == LV_FS_RES_OK && br == 8) {
#if TILE_FORMAT == PNG_FORMAT
                    // PNG Signature: 89 50 4E 47 0D 0A 1A 0A
                    if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
                        header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A) {
                        signature_ok = true;
                        total_valid++;
                    }
#elif TILE_FORMAT == JPEG_FORMAT
                    // Signature JPEG : FF D8 FF
                    if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
                        signature_ok = true;
                        total_valid++;
                    }
#elif TILE_FORMAT == RGB565_FORMAT
                    // Signature JPEG : 19 12 0
                    if (header[0] == 0x19 && header[1] == 0x12 && header[2] == 0x00) {
                    // && header[3] == 0x00 && header[4] == 0x00 && header[5] == 0x01 && header[6] == 0x00 && header[7] == 0x01) {
                        signature_ok = true;
                        total_valid++;
                    }
#else
                    total_valid++;
#endif
                }
                // --- Fermeture ---
                lv_fs_close(&f);
                ESP_LOGI(TAG, "Tile [%d,%d] - FOUND - LVGL Path: %s, Size: %u bytes, %s Header: %s",
                         tile_idx_x, tile_idx_y, full_path, (unsigned int)size, TILE_EXTENTION, signature_ok ? "OK" : "INVALID");
            } else {
                ESP_LOGE(TAG, "Tile [%d,%d] - MISSING - LVGL Path: %s (Error: %d)", 
                         tile_idx_x, tile_idx_y, full_path, res);
            }
        }
    }
    if (total_valid == GRID_ROWS * GRID_COLS) {
        ESP_LOGI(TAG, "Summary: %d/%d tiles found, %d/%d tiles have valid %s signature.",
                 total_found, GRID_ROWS * GRID_COLS, total_valid, total_found, TILE_EXTENTION);
    } else {
        ESP_LOGE(TAG, "Summary: %d/%d tiles found, %d/%d tiles have valid %s signature.",
                 total_found, GRID_ROWS * GRID_COLS, total_valid, total_found, TILE_EXTENTION);
    }
    ESP_LOGI(TAG, "--- Tile Engine Debug End ---");
}
#endif
