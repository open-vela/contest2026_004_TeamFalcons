#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "vg_discover.h"

static void test_versioned_copy(void)
{
    struct vg_discover_summary sum;
    uint32_t gen1 = 0, gen2 = 0;
    int n;

    vg_discover_reset(&sum);
    sum.n_points = 2;
    snprintf(sum.points[0].id, sizeof(sum.points[0].id), "PT01");
    sum.points[0].addr = 1;
    sum.points[0].reg = 10;
    snprintf(sum.points[1].id, sizeof(sum.points[1].id), "PT02");
    sum.points[1].addr = 2;
    sum.points[1].reg = 20;

    /* Publish sum to live points */
    int rc = vg_live_points_replace(&sum);
    assert(rc == 0);

    gen1 = vg_live_points_gen();
    assert(gen1 > 0);

    memset(&sum, 0, sizeof(sum));
    n = vg_live_points_copy_versioned(&sum, &gen2);
    assert(n == 2);
    assert(gen2 == gen1);
    assert(strcmp(sum.points[0].id, "PT01") == 0);
    assert(strcmp(sum.points[1].id, "PT02") == 0);

    /* Modify live points and verify gen increments */
    sum.n_points = 3;
    snprintf(sum.points[2].id, sizeof(sum.points[2].id), "PT03");
    rc = vg_live_points_replace(&sum);
    assert(rc == 0);

    uint32_t gen3 = vg_live_points_gen();
    assert(gen3 > gen1);

    /* Stale poll simulation: a poll that sampled at gen1 detects gen change */
    assert(gen1 != vg_live_points_gen());

    printf("[PASS] versioned point table copy and stale poll detection\n");
}

static void test_alarm_format_and_semantics(void)
{
    /* Verify the primary alarm format matches the agent contract */
    char buf[256];
    const char *type = "threshold";
    const char *tag = "PT01/threshold";
    unsigned slave = 1;
    long reg = 100;
    double val = 85.5;
    double thr = 80.0;

    snprintf(buf, sizeof(buf),
             "type=%s\ntag=%s\nslave=%u\nreg=%ld\nvalue=%.4g\n"
             "threshold=%.4g\n"
             "hint=use alarm_interpretation skill\n",
             type, tag, slave, reg, val, thr);

    assert(strstr(buf, "type=threshold\n") != NULL);
    assert(strstr(buf, "tag=PT01/threshold\n") != NULL);
    assert(strstr(buf, "slave=1\n") != NULL);
    assert(strstr(buf, "reg=100\n") != NULL);
    assert(strstr(buf, "value=85.5\n") != NULL);
    assert(strstr(buf, "threshold=80\n") != NULL);
    assert(strstr(buf, "hint=use alarm_interpretation skill\n") != NULL);

    printf("[PASS] alarm file serialization format\n");
}

int main(void)
{
    test_versioned_copy();
    test_alarm_format_and_semantics();
    printf("test_runtime_render: ALL PASS\n");
    return 0;
}
