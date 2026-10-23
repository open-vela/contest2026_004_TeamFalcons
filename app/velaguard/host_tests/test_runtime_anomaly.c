#include <errno.h>
/****************************************************************************
 * app/velaguard/host_tests/test_runtime_anomaly.c
 *
 * BVO-09: the voice answer path needs the whole anomaly set and a recovery
 * verdict built from events, not from the current value.  These cases pin
 * both rules, plus the two ways the event ring can leave a gap.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vg_frame_stats.h"
#include "vg_runtime.h"

void vg_mqtt_enqueue_alarm(const char *id, const char *kind,
                           const char *state, float value, float thr,
                           const char *level, long long ts_ms)
{
  (void)id;
  (void)kind;
  (void)state;
  (void)value;
  (void)thr;
  (void)level;
  (void)ts_ms;
}

static int g_fail;

static void expect(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      g_fail++;
    }
}

static void note(const char *id, int online, enum vg_runtime_kind kind,
                 float value, float thr, const char *level)
{
  vg_runtime_note_sample(id, 1, online != 0, value, kind, thr,
                         kind == VG_RUNTIME_KIND_THRESHOLD ? "ge" : "",
                         level);
}

static int find_item(const struct vg_runtime_anomaly *items, int n,
                     const char *id)
{
  int i;

  for (i = 0; i < n; i++)
    {
      if (strcmp(items[i].id, id) == 0)
        {
          return i;
        }
    }

  return -1;
}

/* Twelve alarming points must all be reported: the report path stops at
 * eight, and reusing it here would silently hide four anomalies. */

static void test_complete_list(void)
{
  struct vg_runtime_anomaly items[VG_RUNTIME_MAX_POINTS];
  char id[16];
  int total = 0;
  int n;
  int i;

  for (i = 0; i < 12; i++)
    {
      snprintf(id, sizeof(id), "p%02d", i);
      note(id, 1, VG_RUNTIME_KIND_THRESHOLD, 90.0f + i, 80.0f,
           (i < 3) ? "crit" : "warn");
    }

  n = vg_runtime_anomalies(items, VG_RUNTIME_MAX_POINTS, &total, 0);
  expect(n == 12, "twelve threshold points reported");
  expect(total == 12, "total counts all twelve points");

  /* Critical first, then warning. */
  expect(items[0].rank == 0 && items[1].rank == 0 && items[2].rank == 0,
         "three critical points lead the list");
  expect(items[3].rank == 1, "warnings follow");

  /* A bounded caller still learns the true total. */
  n = vg_runtime_anomalies(items, 3, &total, 0);
  expect(n == 3, "bounded read writes three entries");
  expect(total == 12, "bounded read still reports the full total");
}

/* One point, several categories, one entry. */

static void test_overlap_counts_once(void)
{
  struct vg_runtime_anomaly items[VG_RUNTIME_MAX_POINTS];
  int total = 0;
  int n;
  int idx;

  note("p_off", 1, VG_RUNTIME_KIND_NONE, 10.0f, 0, NULL);
  note("p_off", 0, VG_RUNTIME_KIND_OFFLINE, 0.0f, 0, "offline");

  usleep(20000);

  n = vg_runtime_anomalies(items, VG_RUNTIME_MAX_POINTS, &total, 5);
  idx = find_item(items, n, "p_off");

  expect(idx >= 0, "offline point appears");
  if (idx >= 0)
    {
      expect((items[idx].flags & VG_RUNTIME_ANOMALY_F_OFFLINE) != 0,
             "offline flag set");
      expect((items[idx].flags & VG_RUNTIME_ANOMALY_F_STALE) != 0,
             "stale flag also set for the same point");
      expect(items[idx].rank == 2, "offline outranks stale");
      expect(items[idx].has_value == true, "last good value retained");
    }

  /* Total is the number of anomalous points, not the number of flags. */
  {
    int i;
    int seen_off = 0;

    for (i = 0; i < n; i++)
      {
        if (strcmp(items[i].id, "p_off") == 0)
          {
            seen_off++;
          }
      }

    expect(seen_off == 1, "overlapping categories produce one entry");
    expect(total == 13, "total counts the offline point once");
  }
}

/* A stale-only point (online, sample old) is still an anomaly. */

static void test_stale_only(void)
{
  struct vg_runtime_anomaly items[VG_RUNTIME_MAX_POINTS];
  int total = 0;
  int n;
  int idx;

  note("p_fresh", 1, VG_RUNTIME_KIND_NONE, 1.0f, 0, NULL);
  usleep(20000);
  note("p_fresh2", 1, VG_RUNTIME_KIND_NONE, 2.0f, 0, NULL);

  n = vg_runtime_anomalies(items, VG_RUNTIME_MAX_POINTS, &total, 10);
  idx = find_item(items, n, "p_fresh");
  expect(idx >= 0, "old online sample reported as stale");
  if (idx >= 0)
    {
      expect(items[idx].flags == VG_RUNTIME_ANOMALY_F_STALE,
             "stale flag only");
      expect(items[idx].rank == 3, "stale ranks last");
    }

  idx = find_item(items, n, "p_fresh2");
  expect(idx < 0, "fresh sample is not an anomaly");
}

static void test_recovery(void)
{
  struct vg_runtime_recovery rec;

  /* Raised and cleared: both events retained, duration known. */
  note("p_rec", 1, VG_RUNTIME_KIND_NONE, 50.0f, 0, NULL);
  note("p_rec", 1, VG_RUNTIME_KIND_THRESHOLD, 95.0f, 80.0f, "warn");
  usleep(5000);
  note("p_rec", 1, VG_RUNTIME_KIND_NONE, 50.0f, 0, NULL);

  expect(vg_runtime_recovery("p_rec", VG_RUNTIME_KIND_THRESHOLD, &rec) == 0,
         "recovery query succeeds");
  expect(rec.state == VG_RUNTIME_RECOVERY_RECOVERED, "cleared episode recovered");
  expect(rec.history_complete == true, "both events retained");
  expect(rec.has_duration == true, "duration available");
  expect(rec.episode != 0, "episode id recorded");

  /* Low-side thresholds retain their operator in recovery evidence. */
  vg_runtime_note_sample("p_low", 1, true, 1, VG_RUNTIME_KIND_THRESHOLD,
                         10, "le", "warn");
  expect(vg_runtime_recovery("p_low", VG_RUNTIME_KIND_THRESHOLD, &rec) == 0,
         "low threshold recovery query");
  expect(strcmp(rec.cmp, "le") == 0, "recovery keeps low-side comparator");

  /* Raised, still open. */
  note("p_act", 1, VG_RUNTIME_KIND_NONE, 50.0f, 0, NULL);
  note("p_act", 1, VG_RUNTIME_KIND_THRESHOLD, 95.0f, 80.0f, "crit");

  expect(vg_runtime_recovery("p_act", VG_RUNTIME_KIND_THRESHOLD, &rec) == 0,
         "active query succeeds");
  expect(rec.state == VG_RUNTIME_RECOVERY_ACTIVE, "open episode is active");
  expect(rec.history_complete == true, "raise retained");

  /* Losing communication is not evidence of recovery. */
  note("p_overlap", 1, VG_RUNTIME_KIND_THRESHOLD, 95, 80, "crit");
  uint32_t episode;
  vg_runtime_recovery("p_overlap", VG_RUNTIME_KIND_THRESHOLD, &rec);
  episode = rec.episode;
  note("p_overlap", 0, VG_RUNTIME_KIND_OFFLINE, 0, 0, "offline");
  vg_runtime_recovery("p_overlap", VG_RUNTIME_KIND_THRESHOLD, &rec);
  expect(rec.state == VG_RUNTIME_RECOVERY_ACTIVE && rec.episode == episode,
         "offline must not clear an active threshold episode");
  struct vg_runtime_anomaly combined[VG_RUNTIME_MAX_POINTS];
  int total;
  int count = vg_runtime_anomalies(combined, VG_RUNTIME_MAX_POINTS, &total, 0);
  int idx = find_item(combined, count, "p_overlap");
  expect(idx >= 0, "combined alarm is present");
  if (idx >= 0) {
    expect(combined[idx].flags == (VG_RUNTIME_ANOMALY_F_THRESHOLD | VG_RUNTIME_ANOMALY_F_OFFLINE),
           "threshold and offline coexist in one point");
    expect(combined[idx].value == 95 && combined[idx].threshold == 80 && combined[idx].rank == 0,
           "offline retains last successful reading and threshold severity");
  }
  note("p_overlap", 1, VG_RUNTIME_KIND_NONE, 40, 0, NULL);
  vg_runtime_recovery("p_overlap", VG_RUNTIME_KIND_THRESHOLD, &rec);
  expect(rec.state == VG_RUNTIME_RECOVERY_RECOVERED && rec.episode == episode,
         "only an online normal sample closes the same threshold episode");

  /* Normal now, but nothing was ever raised: cannot claim a recovery. */
  note("p_calm", 1, VG_RUNTIME_KIND_NONE, 50.0f, 0, NULL);
  expect(vg_runtime_recovery("p_calm", VG_RUNTIME_KIND_THRESHOLD, &rec) == 0,
         "unknown query succeeds");
  expect(rec.state == VG_RUNTIME_RECOVERY_UNKNOWN,
         "normal current value is not a recovery record");
  expect(rec.history_complete == false, "unknown history");

  /* Offline now, raised while online: active, not recovered. */
  note("p_offrec", 1, VG_RUNTIME_KIND_NONE, 1.0f, 0, NULL);
  note("p_offrec", 0, VG_RUNTIME_KIND_OFFLINE, 0.0f, 0, "offline");
  expect(vg_runtime_recovery("p_offrec", VG_RUNTIME_KIND_OFFLINE, &rec) == 0,
         "offline query succeeds");
  expect(rec.state == VG_RUNTIME_RECOVERY_ACTIVE, "offline episode active");
}

/* A CLEAR whose RAISE was overwritten reports the recovery without inventing
 * a start time. */

static void test_partial_recovery(void)
{
  struct vg_runtime_recovery rec;
  int i;

  note("p_partial", 1, VG_RUNTIME_KIND_NONE, 1.0f, 0, NULL);
  note("p_partial", 1, VG_RUNTIME_KIND_THRESHOLD, 99.0f, 80.0f, "crit");

  /* Each toggle pair emits a RAISE and a CLEAR, so 16 pairs push the
   * p_partial raise out of the 32-entry ring. */

  for (i = 0; i < 16; i++)
    {
      note("p_fill", 1, VG_RUNTIME_KIND_THRESHOLD, 90.0f, 80.0f, "warn");
      note("p_fill", 1, VG_RUNTIME_KIND_NONE, 50.0f, 0, NULL);
    }

  note("p_partial", 1, VG_RUNTIME_KIND_NONE, 50.0f, 0, NULL);

  expect(vg_runtime_events_dropped() > 0, "ring reports dropped events");
  expect(vg_runtime_recovery("p_partial", VG_RUNTIME_KIND_THRESHOLD, &rec) == 0,
         "partial query succeeds");
  expect(rec.state == VG_RUNTIME_RECOVERY_RECOVERED_PARTIAL,
         "clear without raise is a partial recovery");
  expect(rec.history_complete == false, "history marked incomplete");
  expect(rec.has_duration == false, "no duration from a missing start");
}

/* Points seen drives the "first cycle has not finished" note.  It must count
 * distinct points, not samples, or a single point re-sampled would look like
 * a complete cycle. */

static void test_points_seen(void)
{
  int before = vg_runtime_points_seen();

  note("seen_a", 1, VG_RUNTIME_KIND_NONE, 1.0f, 0, NULL);
  expect(vg_runtime_points_seen() == before + 1, "one point counted");

  note("seen_a", 1, VG_RUNTIME_KIND_NONE, 2.0f, 0, NULL);
  expect(vg_runtime_points_seen() == before + 1,
         "re-sampling the same point does not add a second");

  note("seen_b", 0, VG_RUNTIME_KIND_OFFLINE, 0, 0, "offline");
  expect(vg_runtime_points_seen() == before + 2,
         "an offline point is still a seen point");
}

int main(void)
{
  vg_runtime_init();

  expect(vg_runtime_boot_id() != 0, "boot id is set");
  expect(vg_runtime_events_dropped() == 0, "fresh ring has no drops");

  test_complete_list();
  test_overlap_counts_once();
  test_stale_only();
  test_recovery();
  test_partial_recovery();
  test_points_seen();

  /* 模拟已确认表换代，不调用或代答任何板端 apply。 */
  vg_runtime_config_changed(10);
  expect(vg_runtime_points_seen() == 0, "new config discards old members");
  expect(vg_runtime_note_sample_versioned(10,"same",1,true,99,
      VG_RUNTIME_KIND_THRESHOLD,80,"ge","crit") == 0,"current config accepted");
  struct vg_runtime_recovery scoped;
  expect(vg_runtime_recovery("same",VG_RUNTIME_KIND_THRESHOLD,&scoped)==0 &&
      scoped.state == VG_RUNTIME_RECOVERY_ACTIVE,"new config active episode");
  vg_runtime_config_changed(11); /* 删除 */
  vg_runtime_config_changed(12); /* 同名重新添加 */
  expect(vg_runtime_note_sample_versioned(10,"same",1,true,99,
      VG_RUNTIME_KIND_THRESHOLD,80,"ge","crit") == -ESTALE,"old publication rejected");
  expect(vg_runtime_note_sample_versioned(12,"same",2,true,40,
      VG_RUNTIME_KIND_NONE,80,"ge",NULL)==0,"readded point samples normally");
  expect(vg_runtime_recovery("same",VG_RUNTIME_KIND_THRESHOLD,&scoped)==0 &&
      scoped.state == VG_RUNTIME_RECOVERY_UNKNOWN,"readded id has no inherited recovery");
  note("removed",0,VG_RUNTIME_KIND_OFFLINE,0,0,"offline");
  expect(vg_runtime_points_seen()==1,"unversioned stale writer rejected");

  if (g_fail != 0)
    {
      fprintf(stderr, "test_runtime_anomaly: %d failure(s)\n", g_fail);
      return 1;
    }

  printf("test_runtime_anomaly: ok\n");
  return 0;
}
