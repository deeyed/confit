#include <stdio.h>

#if CONFIG_ENABLE_METRICS
int example_metrics_sample(void);
#endif

int main(void) {
#if CONFIG_ENABLE_METRICS
  const int metrics = example_metrics_sample();
  (void)printf("workers=%d device=0x%x level=%s metrics=%d\n",
               CONFIG_WORKER_COUNT, CONFIG_DEVICE_ID, CONFIG_LOG_LEVEL,
               metrics);
#else
  (void)printf("workers=%d device=0x%x level=%s metrics=disabled\n",
               CONFIG_WORKER_COUNT, CONFIG_DEVICE_ID, CONFIG_LOG_LEVEL);
#endif
  return 0;
}
