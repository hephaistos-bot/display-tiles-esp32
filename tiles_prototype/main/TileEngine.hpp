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

private:
    struct TileInfo {
        lv_obj_t* img_obj;
        int x_idx;
        int y_idx;
        int zoom;
        char path[64];
    };

    static constexpr int TILE_SIZE = 256;
    static constexpr int GRID_COLS = 5;
    static constexpr int GRID_ROWS = 4;
    static constexpr int SCREEN_WIDTH = 800;
    static constexpr int SCREEN_HEIGHT = 480;

    lv_obj_t* _map_container;
    std::vector<TileInfo> _tile_grid;

    void updateTiles(double lat, double lon, int zoom);
    void latLonToTile(double lat, double lon, int zoom, double& x, double& y);
};

#endif // TILE_ENGINE_HPP
