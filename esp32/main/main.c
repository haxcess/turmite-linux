#include "board_7b.h"
#include "ant.h"
#include "renderer.h"
#include "scheduler.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_FREERTOS_UNICORE)
#error "This board target requires a dual-core ESP32-S3"
#endif

static const char *TAG = "turmite";
static World world;
static AntColony colony;
static Scheduler scheduler;
static SemaphoreHandle_t worker_done;
static _Atomic bool stopping;
static uint16_t palette[TURMITE_COLORS];

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f));
}

static void worker(void *arg)
{
    (void)arg;
    Scheduler *members[] = {&scheduler};
    while (!atomic_load(&stopping)) {
        size_t selected, grant;
        Ant *ant = scheduler_acquire_group(members, 1, esp_timer_get_time(), &selected, &grant);
        if (ant) {
            size_t executed = ant_execute_quantum(ant, &colony, &world, grant);
            scheduler_release(&scheduler, ant, executed, esp_timer_get_time());
        }
        // Bound CPU use and give the idle/watchdog and RGB driver time to run.
        vTaskDelay(1);
    }
    xSemaphoreGive(worker_done);
    vTaskDelete(NULL);
}

static void universe_start(void)
{
    Lfsr32 rng;
    uint32_t seed = rng_entropy_seed();
    rng_seed(&rng, seed);
    ESP_ERROR_CHECK(world_init(&world, BOARD_WIDTH / CONFIG_TURMITE_CELL_SIZE,
                              BOARD_HEIGHT / CONFIG_TURMITE_CELL_SIZE) == 0 ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(ant_colony_init(&colony, &world) == 0 ? ESP_OK : ESP_ERR_NO_MEM);
    for (unsigned i = 0; i < CONFIG_TURMITE_ANTS; ++i)
        ESP_ERROR_CHECK(ant_spawn_random(&colony.ants[i], &colony, &world, &rng) ? ESP_OK : ESP_FAIL);
    atomic_store(&colony.active_population, CONFIG_TURMITE_ANTS);
    ESP_ERROR_CHECK(scheduler_init(&scheduler, &colony, SCHED_WFQ, 64) == 0 ? ESP_OK : ESP_FAIL);
    scheduler_set_min_service(&scheduler, 16);
    scheduler_set_token_rate_divisor(&scheduler, CONFIG_TURMITE_DIVISOR);
    uint32_t rgb[TURMITE_COLORS];
    render_palette_init(rgb, RENDER_PALETTE_RANDOMISH, seed);
    rgb[0] = 0;
    for (unsigned i = 0; i < TURMITE_COLORS; ++i) palette[i] = rgb565(rgb[i]);
    atomic_store(&stopping, false);
    for (unsigned core = 0; core < 2; ++core)
        ESP_ERROR_CHECK(xTaskCreatePinnedToCore(worker, "turmite_worker", 4096, NULL, 1,
                                               NULL, core) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Universe seed=%lu grid=%dx%d ants=%d divisor=%d",
             (unsigned long)seed, world.width, world.height, CONFIG_TURMITE_ANTS, CONFIG_TURMITE_DIVISOR);
}

static void universe_stop(void)
{
    atomic_store(&stopping, true);
    scheduler_stop(&scheduler);
    for (unsigned i = 0; i < 2; ++i) xSemaphoreTake(worker_done, portMAX_DELAY);
    scheduler_destroy(&scheduler);
    ant_colony_destroy(&colony);
    world_destroy(&world);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Waveshare ESP32-S3-Touch-LCD-7B; ESP-IDF %s", IDF_VER);
    ESP_ERROR_CHECK(esp_psram_is_initialized() && esp_psram_get_size() >= 8 * 1024 * 1024
                    ? ESP_OK : ESP_ERR_NO_MEM);
    board_init();
    uint16_t *image = heap_caps_malloc(BOARD_WIDTH * BOARD_HEIGHT * sizeof(*image),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(image ? ESP_OK : ESP_ERR_NO_MEM);
    worker_done = xSemaphoreCreateCounting(2, 0);
    ESP_ERROR_CHECK(worker_done ? ESP_OK : ESP_ERR_NO_MEM);
    universe_start();
    int64_t restarted = esp_timer_get_time(), pressed_at = 0;
    bool pressed = false, paused = false;
    uint16_t columns[BOARD_WIDTH];
    for (unsigned x = 0; x < BOARD_WIDTH; ++x)
        columns[x] = x * world.width / BOARD_WIDTH;
    for (;;) {
        int64_t started = esp_timer_get_time();
        bool state;
        bool reseed = CONFIG_TURMITE_LIFETIME_SECONDS > 0 &&
            started - restarted >= (int64_t)CONFIG_TURMITE_LIFETIME_SECONDS * 1000000;
        if (board_touch_report(&state)) {
            if (state && !pressed) pressed_at = started;
            if (!state && pressed) {
                if (started - pressed_at >= 1000000) reseed = true;
                else {
                    paused = !paused;
                    scheduler_set_paused(&scheduler, paused);
                    ESP_LOGI(TAG, "%s", paused ? "Paused" : "Running");
                }
            }
            pressed = state;
        }
        if (reseed) {
            universe_stop();
            universe_start();
            paused = false;
            restarted = esp_timer_get_time();
        }
        // Workers update atomic cells while this frame samples them.
        int previous_row = -1;
        for (unsigned y = 0; y < BOARD_HEIGHT; ++y) {
            int grid_y = y * world.height / BOARD_HEIGHT;
            uint16_t *output = image + y * BOARD_WIDTH;
            if (grid_y == previous_row) {
                memcpy(output, output - BOARD_WIDTH, BOARD_WIDTH * sizeof(*output));
            } else {
                size_t row = (size_t)grid_y * world.width;
                for (unsigned x = 0; x < BOARD_WIDTH; ++x) {
                    uint8_t color = world_load(&world, row + columns[x]);
                    output[x] = palette[color < TURMITE_COLORS ? color : 0];
                }
            }
            previous_row = grid_y;
        }
        board_present(image);
        int64_t remaining = 1000000 / CONFIG_TURMITE_FPS - (esp_timer_get_time() - started);
        vTaskDelay(remaining > 1000 ? pdMS_TO_TICKS(remaining / 1000) : 1);
    }
}
