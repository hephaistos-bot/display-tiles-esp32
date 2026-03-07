# 🚀 Tile Engine Optimization: Investigation Points

This document outlines the planned and potential optimizations to improve the display speed of the Map Tile Engine on the ESP32-S3-Touch-LCD-5.

Currently, `TileEngine::updateTiles` takes **279608 us** (approx. 280ms) to refresh 20 tiles (5x4 grid). Our goal is to reduce this blocking time to ensure a smooth UI, especially for future scrolling and zooming.

## 1. Eliminate Synchronous I/O (`stat()` and `fopen`)
**Issue:** The current implementation calls `stat()` for every tile on every update to check if the file exists before passing it to LVGL. LVGL then performs its own `fopen` and header reading. On a FatFS-over-SPI system, these metadata operations are expensive and synchronous.
**Proposed Solution:**
- **File Indexing:** At startup, scan the `/sdcard/tiles` directory and store the available `zoom/x/y` triplets in a memory-efficient `std::unordered_set`.
- **Note on ESP-IDF FatFS:** The `d_type` field in `struct dirent` is not supported. Use `stat()` during the initial scan to distinguish files from directories.
- **Fast Lookup:** Replace the `stat()` call in the main loop with a fast in-memory lookup. This prevents blocking the UI thread with SD card latency during tile calculations.

## 2. LVGL Image Cache Tuning
**Issue:** Decoded images (especially JPEG/PNG) take significant time to process. If the cache is too small or improperly configured, tiles are re-decoded every time they move or re-enter the viewport.
**Investigation:**
- Ensure `CONFIG_LV_CACHE_DEF_SIZE` (currently 4MB) is fully utilized and that decoded bitmaps are stored in **PSRAM** (`MALLOC_CAP_SPIRAM`).
- Verify that LVGL is not discarding tiles too early due to cache pressure.
- For 20 tiles (256x256 at 16bpp), we need ~2.6MB of cache to keep all decoded tiles in memory simultaneously.

## 3. SPI Bus and FatFS Performance
**Issue:** The SPI bus is currently running at **20MHz**. While standard, we should verify if the SD card can handle higher speeds (e.g., 40MHz) or if there's significant overhead in the `esp_vfs_fat` layer.
**Optimization:**
- Increase `host.max_freq_khz` if the hardware supports it.
- Increase `mount_config.max_files` (e.g., to 20) to ensure LVGL can open multiple tiles concurrently without resource exhaustion.

## 4. Tile Format: JPEG vs. PNG
**Trade-off:**
- **JPEG:** Smaller file size (faster I/O) but slightly more CPU-intensive to decode.
- **PNG:** Larger file size (slower I/O) but potentially faster decoding if simple.
**Investigation:** Benchmark both formats to see which yields a better "Total Time = I/O + Decode". JPEG is currently preferred for its lower I/O footprint on the SD card.

## 5. Background Loading (Pop-in Effect)
**Strategy:** Move the actual `lv_image_set_src` calls or the underlying file reading to a lower-priority FreeRTOS task.
- **Benefit:** The UI remains at high FPS while tiles "pop in" as they are decoded.
- **Challenge:** Requires careful synchronization of LVGL objects across tasks using the `lvgl_mux` mutex.

## 6. Optimized Storage Format
**Long-term Idea:** Replace thousands of small files with a single large archive or binary blob (like MBTiles/SQLite).
- **Benefit:** Significantly reduces FatFS overhead and eliminates the need for directory traversal.

---

*Status: Investigation document complete. Ready for implementation phase.*
