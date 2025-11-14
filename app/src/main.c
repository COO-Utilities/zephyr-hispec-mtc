#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

int main(void) {
    LOG_INF("Zephyr HISPEC MTC (Multichannel Temperature Controller)");

    return 0;
}