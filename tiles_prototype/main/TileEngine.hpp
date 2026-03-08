#ifndef TILE_ENGINE_HPP
#define TILE_ENGINE_HPP

#include "lvgl.h"
#include <vector>
#include <string>

#define TILE_DEBUG 1

class TileEngine {
public:
    TileEngine();
    ~TileEngine();

    void init();
    void setMapCenter(double lat, double lon, int zoom);
    void debug(double lat, double lon, int zoom);

    static void lv_rgb565_decoder_init();

private:
    struct TileInfo {
        lv_obj_t* img_obj;
        int x_idx;
        int y_idx;
        int zoom;
        char path[64];
    };

    static constexpr int TILE_SIZE = 256;
    static constexpr int GRID_COLS = 4;
    static constexpr int GRID_ROWS = 3;
    static constexpr int SCREEN_WIDTH = 800;
    static constexpr int SCREEN_HEIGHT = 480;
    static constexpr const char* TILE_PATH_BASE = "/tiles";
    static constexpr const char* LV_DRIVE_PREFIX = "S:";

    lv_obj_t* _map_container;
    std::vector<TileInfo> _tile_grid;

    void updateTiles(double lat, double lon, int zoom);
    void latLonToTile(double lat, double lon, int zoom, double& x, double& y);
    void getTilePath(char* buf, size_t buf_size, int zoom, int x, int y, bool for_lvgl);
};

#endif // TILE_ENGINE_HPP
