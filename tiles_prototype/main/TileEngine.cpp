#define _USE_MATH_DEFINES
#include "TileEngine.hpp"
#include <cmath>
#include <cstdio>
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "TileEngine";

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
                // and the path must include the mount point '/sdcard'
                snprintf(tile.path, sizeof(tile.path), "S:/sdcard/tiles/%d/%d/%d.png", zoom, tile_idx_x, tile_idx_y);
                lv_image_set_src(tile.img_obj, tile.path);
            }
        }
    }
    int64_t end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Map tiles updated in %lld us", (end_time - start_time));
}

#if TILE_DEBUG
void TileEngine::debug(double lat, double lon, int zoom) {
    ESP_LOGI(TAG, "--- Tile Engine Debug Start (Lat: %f, Lon: %f, Zoom: %d) ---", lat, lon, zoom);

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

            // Format for standard C file operations (using mount point /sdcard)
            snprintf(full_path, sizeof(full_path), "/sdcard/tiles/%d/%d/%d.png", zoom, tile_idx_x, tile_idx_y);

            FILE* f = fopen(full_path, "rb");
            if (f) {
                total_found++;
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);

                unsigned char header[8];
                bool signature_ok = false;
                if (fread(header, 1, 8, f) == 8) {
                    // PNG Signature: 89 50 4E 47 0D 0A 1A 0A
                    if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
                        header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A) {
                        signature_ok = true;
                        total_valid++;
                    }
                }
                fclose(f);

                ESP_LOGI(TAG, "Tile [%d,%d] - FOUND - Path: %s, Size: %ld bytes, PNG Header: %s",
                         tile_idx_x, tile_idx_y, full_path, size, signature_ok ? "OK" : "INVALID");
            } else {
                ESP_LOGE(TAG, "Tile [%d,%d] - MISSING - Path: %s", tile_idx_x, tile_idx_y, full_path);
            }
        }
    }

    ESP_LOGI(TAG, "Summary: %d/%d tiles found, %d/%d tiles have valid PNG signature.",
             total_found, GRID_ROWS * GRID_COLS, total_valid, total_found);
    ESP_LOGI(TAG, "--- Tile Engine Debug End ---");
}
#endif
