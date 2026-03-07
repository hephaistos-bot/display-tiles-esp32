#define _USE_MATH_DEFINES
#include "TileEngine.hpp"
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include "esp_log.h"
#include "esp_timer.h"

#include <sys/stat.h>

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

void TileEngine::init() {
    _map_container = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_map_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_remove_style_all(_map_container);
    lv_obj_set_scrollbar_mode(_map_container, LV_SCROLLBAR_MODE_OFF);

    for (int r = 0; r < GRID_ROWS; ++r) {
        for (int c = 0; c < GRID_COLS; ++c) {
            lv_obj_t* img = lv_image_create(_map_container);
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
        snprintf(buf, buf_size, "%s%s/%d/%d/%d.png", LV_DRIVE_PREFIX, TILE_PATH_BASE, zoom, x, y);
    } else {
        snprintf(buf, buf_size, "/sdcard%s/%d/%d/%d.png", TILE_PATH_BASE, zoom, x, y);
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
                    // PNG Signature: 89 50 4E 47 0D 0A 1A 0A
                    if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
                        header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A) {
                        signature_ok = true;
                        total_valid++;
                    }
                }
                // --- Fermeture ---
                lv_fs_close(&f);
                ESP_LOGI(TAG, "Tile [%d,%d] - FOUND - LVGL Path: %s, Size: %u bytes, PNG Header: %s",
                         tile_idx_x, tile_idx_y, full_path, (unsigned int)size, signature_ok ? "OK" : "INVALID");
            } else {
                ESP_LOGE(TAG, "Tile [%d,%d] - MISSING - LVGL Path: %s (Error: %d)", 
                         tile_idx_x, tile_idx_y, full_path, res);
            }
        }
    }
    if (total_valid == GRID_ROWS * GRID_COLS) {
        ESP_LOGI(TAG, "Summary: %d/%d tiles found, %d/%d tiles have valid PNG signature.",
                 total_found, GRID_ROWS * GRID_COLS, total_valid, total_found);
    } else {
        ESP_LOGE(TAG, "Summary: %d/%d tiles found, %d/%d tiles have valid PNG signature.",
                 total_found, GRID_ROWS * GRID_COLS, total_valid, total_found);
    }
    ESP_LOGI(TAG, "--- Tile Engine Debug End ---");
}
#endif
