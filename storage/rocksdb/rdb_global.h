/*
   Copyright (c) 2018, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* MyRocks global type definitions goes here */

#pragma once

/* C++ standard header files */
#include <limits>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <sys/resource.h>
#endif

/* MySQL header files */
#include "./sql_string.h"
#include "sql/handler.h" /* handler */

/* MyRocks headers */
#include "./ut0counter.h"

/*
  When taking measurements, replace this with __attribute__ ((noinline))
  This is used in performance critical functions to make doing CPU
  profiling easier
 */
#define INLINE_ATTR

namespace myrocks {
/*
 * class for exporting transaction information for
 * information_schema.rocksdb_trx
 */
struct Rdb_trx_info {
  std::string name;
  ulonglong trx_id;
  ulonglong write_count;
  ulonglong lock_count;
  int timeout_sec;
  std::string state;
  std::string waiting_key;
  ulonglong waiting_cf_id;
  int is_replication;
  int skip_trx_api;
  int read_only;
  int deadlock_detect;
  int num_ongoing_bulk_load;
  ulong thread_id;
  std::string query_str;
};

std::vector<Rdb_trx_info> rdb_get_all_trx_info();

/*
 * class for exporting deadlock transaction information for
 * information_schema.rocksdb_deadlock
 */
struct Rdb_deadlock_info {
  struct Rdb_dl_trx_info {
    ulonglong trx_id;
    std::string cf_name;
    std::string waiting_key;
    bool exclusive_lock;
    std::string index_name;
    std::string table_name;
  };
  std::vector<Rdb_dl_trx_info> path;
  int64_t deadlock_time;
  ulonglong victim_trx_id;
};

std::vector<Rdb_deadlock_info> rdb_get_deadlock_info();

/*
  This is
  - the name of the default Column Family (the CF which stores indexes which
    didn't explicitly specify which CF they are in)
  - the name used to set the default column family parameter for per-cf
    arguments.
*/
extern const std::string DEFAULT_CF_NAME;

/*
  This is the name of the default tmp column family (the CF which stores tmp
  tables)
*/
extern const std::string DEFAULT_TMP_CF_NAME;

/*
  This is the name of the tmp system column family which stores the tmp table
  meta-data
*/
extern const std::string DEFAULT_TMP_SYSTEM_CF_NAME;

/*
  This is the name of the Column Family used for storing the data dictionary.
*/
extern const std::string DEFAULT_SYSTEM_CF_NAME;

/*
  This is the name of the hidden primary key for tables with no pk.
*/
const char *const HIDDEN_PK_NAME = "HIDDEN_PK_ID";

/*
  Column family name which means "put this index into its own column family".
  DEPRECATED!!!
*/
extern const std::string PER_INDEX_CF_NAME;

/*
  Name for the background thread.
*/
const char *const BG_THREAD_NAME = "myrocks-bg";

/*
  Name for the drop index thread.
*/
const char *const INDEX_THREAD_NAME = "myrocks-index";

/*
  Name for the index stats calculation thread.
*/
const char *const INDEX_STATS_THREAD_NAME = "myrocks-is";

/*
  Name for the manual compaction thread.
*/
const char *const MANUAL_COMPACTION_THREAD_NAME = "myrocks-mc";

/*
  Separator between partition name and the qualifier. Sample usage:

  - p0_cfname=foo
  - p3_tts_col=bar
*/
const char RDB_PER_PARTITION_QUALIFIER_NAME_SEP = '_';

/*
  Separator between qualifier name and value. Sample usage:

  - p0_cfname=foo
  - p3_tts_col=bar
*/
const char RDB_QUALIFIER_VALUE_SEP = '=';

/*
  Separator between multiple qualifier assignments. Sample usage:

  - p0_cfname=foo;p1_cfname=bar;p2_cfname=baz
*/
const char RDB_QUALIFIER_SEP = ';';

/*
  Qualifier name for a custom per partition column family.
*/
const char *const RDB_CF_NAME_QUALIFIER = "cfname";

/*
  Qualifier name for a custom per partition ttl duration.
*/
const char *const RDB_TTL_DURATION_QUALIFIER = "ttl_duration";

/*
  Qualifier name for a custom per partition ttl duration.
*/
const char *const RDB_TTL_COL_QUALIFIER = "ttl_col";

/*
  Qualifier name for number of prefix keyparts in partial index
*/
const char *const RDB_PARTIAL_INDEX_KEYPARTS_QUALIFIER =
    "partial_group_keyparts";

/*
  Qualifier name for materialization threshold in partial index
*/
const char *const RDB_PARTIAL_INDEX_THRESHOLD_QUALIFIER =
    "partial_group_threshold";

/*
  Default, minimal valid, and maximum valid sampling rate values when collecting
  statistics about table.
*/
#define RDB_DEFAULT_TBL_STATS_SAMPLE_PCT 10
#define RDB_TBL_STATS_SAMPLE_PCT_MIN 1
#define RDB_TBL_STATS_SAMPLE_PCT_MAX 100

#define RDB_TBL_STATS_RECALC_THRESHOLD_PCT_MAX 100

/* Minimum time interval between stats recalc for a given table */
#define RDB_MIN_RECALC_INTERVAL 10 /* seconds */

#define THREAD_PRIO_MIN -20
#ifndef __APPLE__
#define THREAD_PRIO_MAX 19
#else
#define THREAD_PRIO_MAX PRIO_DARWIN_BG
#endif

/*
  Default and maximum values for rocksdb-compaction-sequential-deletes and
  rocksdb-compaction-sequential-deletes-window to add basic boundary checking.
*/
#define DEFAULT_COMPACTION_SEQUENTIAL_DELETES 149999
#define MAX_COMPACTION_SEQUENTIAL_DELETES 2000000

#define DEFAULT_COMPACTION_SEQUENTIAL_DELETES_WINDOW 150000
#define MAX_COMPACTION_SEQUENTIAL_DELETES_WINDOW 2000000

/*
  Default and maximum values for various compaction and flushing related
  options. Numbers are based on the hardware we currently use and our internal
  benchmarks which indicate that parallelization helps with the speed of
  compactions.

  Ideally of course we'll use heuristic technique to determine the number of
  CPU-s and derive the values from there. This however has its own set of
  problems and we'll choose simplicity for now.
*/
#define MAX_BACKGROUND_JOBS 64

#define DEFAULT_SUBCOMPACTIONS 1
#define MAX_SUBCOMPACTIONS 64

/*
  Default value for rocksdb_sst_mgr_rate_bytes_per_sec = 0 (disabled).
*/
#define DEFAULT_SST_MGR_RATE_BYTES_PER_SEC 0

/*
  SELECT BYPASS related policy:
  0x1 = ON/OFF MASK - whether bypass is on or off
  0x2 = FORCED/DEFAULT MASK, whether the ON/OFF bit is being FORCED (0x0)
  or simply inteneded as DEFAULT (0x1).

  always_off = 0x0, always off regardless of hint. This is the default
  always_on  = 0x1, always on regardless of hint
  opt_in     = 0x2, default off, turn on with +bypass hint
  opt_out    = 0x3, default on, turn off with +no_bypass hint
 */
#define SELECT_BYPASS_POLICY_ON_MASK 0x1
#define SELECT_BYPASS_POLICY_DEFAULT_MASK 0x2

enum select_bypass_policy_type {
  always_off = 0,
  always_on = SELECT_BYPASS_POLICY_ON_MASK,
  opt_in = SELECT_BYPASS_POLICY_DEFAULT_MASK,
  opt_out = (SELECT_BYPASS_POLICY_DEFAULT_MASK | SELECT_BYPASS_POLICY_ON_MASK),
  default_value = 0
};

/*
  Defines the field sizes for serializing XID object to a string representation.
  string byte format: [field_size: field_value, ...]
  [
    8: XID.formatID,
    1: XID.gtrid_length,
    1: XID.bqual_length,
    XID.gtrid_length + XID.bqual_length: XID.data
  ]
*/
#define RDB_FORMATID_SZ 8
#define RDB_GTRID_SZ 1
#define RDB_BQUAL_SZ 1
#define RDB_XIDHDR_LEN (RDB_FORMATID_SZ + RDB_GTRID_SZ + RDB_BQUAL_SZ)

/*
  To fix an unhandled exception we specify the upper bound as LONGLONGMAX
  instead of ULONGLONGMAX because the latter is -1 and causes an exception when
  cast to jlong (signed) of JNI

  The reason behind the cast issue is the lack of unsigned int support in Java.
*/
#define MAX_RATE_LIMITER_BYTES_PER_SEC static_cast<uint64_t>(LLONG_MAX)

/*
  Hidden PK column (for tables with no primary key) is a longlong (aka 8 bytes).
  static_assert() in code will validate this assumption.
*/
#define ROCKSDB_SIZEOF_HIDDEN_PK_COLUMN sizeof(longlong)

/*
  Bytes used to store TTL, in the beginning of all records for tables with TTL
  enabled.
*/
#define ROCKSDB_SIZEOF_TTL_RECORD sizeof(longlong)

#define ROCKSDB_SIZEOF_AUTOINC_VALUE sizeof(longlong)

/*
  Maximum index prefix length in bytes.
*/
#define MAX_INDEX_COL_LEN_LARGE 3072
#define MAX_INDEX_COL_LEN_SMALL 767

/*
  MyRocks specific error codes. NB! Please make sure that you will update
  HA_ERR_ROCKSDB_LAST when adding new ones.  Also update the strings in
  rdb_error_messages to include any new error messages.

  NOTE: Given that Oracle keeps bumping up HA_ERR_LAST, we don't want to
  start strictly from HA_ERR_LAST and instead we start from 500 and asserts
  it is larger
*/
#define HA_ERR_ROCKSDB_FIRST (500)
static_assert(HA_ERR_ROCKSDB_FIRST > HA_ERR_LAST,
              "ROCKSDB err need to be larger than HA_ERR_LAST");
#define HA_ERR_ROCKSDB_PK_REQUIRED (HA_ERR_ROCKSDB_FIRST + 0)
#define HA_ERR_ROCKSDB_TABLE_DATA_DIRECTORY_NOT_SUPPORTED \
  (HA_ERR_ROCKSDB_FIRST + 1)
#define HA_ERR_ROCKSDB_TABLE_INDEX_DIRECTORY_NOT_SUPPORTED \
  (HA_ERR_ROCKSDB_FIRST + 2)
#define HA_ERR_ROCKSDB_COMMIT_FAILED (HA_ERR_ROCKSDB_FIRST + 3)
#define HA_ERR_ROCKSDB_BULK_LOAD (HA_ERR_ROCKSDB_FIRST + 4)
#define HA_ERR_ROCKSDB_CORRUPT_DATA (HA_ERR_ROCKSDB_FIRST + 5)
#define HA_ERR_ROCKSDB_CHECKSUM_MISMATCH (HA_ERR_ROCKSDB_FIRST + 6)
#define HA_ERR_ROCKSDB_INVALID_TABLE (HA_ERR_ROCKSDB_FIRST + 7)
#define HA_ERR_ROCKSDB_PROPERTIES (HA_ERR_ROCKSDB_FIRST + 8)
#define HA_ERR_ROCKSDB_MERGE_FILE_ERR (HA_ERR_ROCKSDB_FIRST + 9)
/*
  Each error code below maps to a RocksDB status code found in:
  rocksdb/include/rocksdb/status.h
*/
#define HA_ERR_ROCKSDB_STATUS_NOT_FOUND (HA_ERR_ROCKSDB_FIRST + 10)
#define HA_ERR_ROCKSDB_STATUS_CORRUPTION (HA_ERR_ROCKSDB_FIRST + 11)
#define HA_ERR_ROCKSDB_STATUS_NOT_SUPPORTED (HA_ERR_ROCKSDB_FIRST + 12)
#define HA_ERR_ROCKSDB_STATUS_INVALID_ARGUMENT (HA_ERR_ROCKSDB_FIRST + 13)
#define HA_ERR_ROCKSDB_STATUS_IO_ERROR (HA_ERR_ROCKSDB_FIRST + 14)
#define HA_ERR_ROCKSDB_STATUS_NO_SPACE (HA_ERR_ROCKSDB_FIRST + 15)
#define HA_ERR_ROCKSDB_STATUS_MERGE_IN_PROGRESS (HA_ERR_ROCKSDB_FIRST + 16)
#define HA_ERR_ROCKSDB_STATUS_INCOMPLETE (HA_ERR_ROCKSDB_FIRST + 17)
#define HA_ERR_ROCKSDB_STATUS_SHUTDOWN_IN_PROGRESS (HA_ERR_ROCKSDB_FIRST + 18)
#define HA_ERR_ROCKSDB_STATUS_TIMED_OUT (HA_ERR_ROCKSDB_FIRST + 19)
#define HA_ERR_ROCKSDB_STATUS_ABORTED (HA_ERR_ROCKSDB_FIRST + 20)
#define HA_ERR_ROCKSDB_STATUS_LOCK_LIMIT (HA_ERR_ROCKSDB_FIRST + 21)
#define HA_ERR_ROCKSDB_STATUS_BUSY (HA_ERR_ROCKSDB_FIRST + 22)
#define HA_ERR_ROCKSDB_STATUS_DEADLOCK (HA_ERR_ROCKSDB_FIRST + 23)
#define HA_ERR_ROCKSDB_STATUS_EXPIRED (HA_ERR_ROCKSDB_FIRST + 24)
#define HA_ERR_ROCKSDB_STATUS_TRY_AGAIN (HA_ERR_ROCKSDB_FIRST + 25)
#define HA_ERR_ROCKSDB_TMP_TABLE_COMMIT_FAILED (HA_ERR_ROCKSDB_FIRST + 26)
#define HA_ERR_ROCKSDB_LAST HA_ERR_ROCKSDB_TMP_TABLE_COMMIT_FAILED

const char *const rocksdb_hton_name = "ROCKSDB";

using Index_id = uint32_t;

typedef struct _gl_index_id_s {
  uint32_t cf_id;
  Index_id index_id;
  bool operator==(const struct _gl_index_id_s &other) const {
    return cf_id == other.cf_id && index_id == other.index_id;
  }
  bool operator!=(const struct _gl_index_id_s &other) const {
    return cf_id != other.cf_id || index_id != other.index_id;
  }
  bool operator<(const struct _gl_index_id_s &other) const {
    return cf_id < other.cf_id ||
           (cf_id == other.cf_id && index_id < other.index_id);
  }
  bool operator<=(const struct _gl_index_id_s &other) const {
    return cf_id < other.cf_id ||
           (cf_id == other.cf_id && index_id <= other.index_id);
  }
  bool operator>(const struct _gl_index_id_s &other) const {
    return cf_id > other.cf_id ||
           (cf_id == other.cf_id && index_id > other.index_id);
  }
  bool operator>=(const struct _gl_index_id_s &other) const {
    return cf_id > other.cf_id ||
           (cf_id == other.cf_id && index_id >= other.index_id);
  }
} GL_INDEX_ID;

enum operation_type : int {
  ROWS_DELETED = 0,
  ROWS_INSERTED,
  ROWS_READ,
  ROWS_UPDATED,
  ROWS_DELETED_BLIND,
  ROWS_EXPIRED,
  ROWS_FILTERED,
  ROWS_UNFILTERED_NO_READ_FILTERING_TIME,
  ROWS_MAX
};

enum query_type : int { QUERIES_POINT = 0, QUERIES_RANGE, QUERIES_MAX };

enum table_index_stats_result_type : int {
  TABLE_INDEX_STATS_SUCCESS = 0,
  TABLE_INDEX_STATS_FAILURE,
  TABLE_INDEX_STATS_RESULT_MAX
};

#if defined(HAVE_SCHED_GETCPU)
#define RDB_INDEXER get_sched_indexer_t
#else
#define RDB_INDEXER thread_id_indexer_t
#endif

/* Global statistics struct used inside MyRocks */
struct st_global_stats {
  ib_counter_t<ulonglong, 64, RDB_INDEXER> rows[ROWS_MAX];

  // system_rows_ stats are only for system
  // tables. They are not counted in rows_* stats.
  ib_counter_t<ulonglong, 64, RDB_INDEXER> system_rows[ROWS_MAX];

  ib_counter_t<ulonglong, 64, RDB_INDEXER> queries[QUERIES_MAX];

  ib_counter_t<ulonglong, 64, RDB_INDEXER>
      table_index_stats_result[TABLE_INDEX_STATS_RESULT_MAX];

  ib_counter_t<ulonglong, 64, RDB_INDEXER> covered_secondary_key_lookups;
  ib_counter_t<ulonglong, 64, RDB_INDEXER> intrinsic_tmp_table_commits;
};

/* Struct used for exporting status to MySQL */
struct st_export_stats {
  ulonglong rows_deleted;
  ulonglong rows_inserted;
  ulonglong rows_read;
  ulonglong rows_updated;
  ulonglong rows_deleted_blind;
  ulonglong rows_expired;
  ulonglong rows_filtered;
  ulonglong rows_unfiltered_no_read_filtering_time;

  ulonglong system_rows_deleted;
  ulonglong system_rows_inserted;
  ulonglong system_rows_read;
  ulonglong system_rows_updated;

  ulonglong queries_point;
  ulonglong queries_range;

  ulonglong table_index_stats_success;
  ulonglong table_index_stats_failure;
  ulonglong table_index_stats_req_queue_length;

  ulonglong covered_secondary_key_lookups;
  ulonglong intrinsic_tmp_table_commits;
};

/* Struct used for exporting RocksDB memory status */
struct st_memory_stats {
  ulonglong memtable_total;
  ulonglong memtable_unflushed;
};

/* Struct used for exporting RocksDB IO stalls stats */
struct st_io_stall_stats {
  ulonglong level0_slowdown;
  ulonglong level0_slowdown_with_compaction;
  ulonglong level0_numfiles;
  ulonglong level0_numfiles_with_compaction;
  ulonglong stop_for_pending_compaction_bytes;
  ulonglong slowdown_for_pending_compaction_bytes;
  ulonglong memtable_compaction;
  ulonglong memtable_slowdown;
  ulonglong total_stop;
  ulonglong total_slowdown;

  st_io_stall_stats()
      : level0_slowdown(0),
        level0_slowdown_with_compaction(0),
        level0_numfiles(0),
        level0_numfiles_with_compaction(0),
        stop_for_pending_compaction_bytes(0),
        slowdown_for_pending_compaction_bytes(0),
        memtable_compaction(0),
        memtable_slowdown(0),
        total_stop(0),
        total_slowdown(0) {}
};
}  // namespace myrocks

// We define ROCKSDB_NAMESPACE = my_rocksdb to avoid symbol conflicts
// But keep code with rocksdb for clarity
// Declare my_rocks namespace is needed to make namespace alias happy
#ifdef ROCKSDB_CUSTOM_NAMESPACE
namespace ROCKSDB_CUSTOM_NAMESPACE {};
namespace rocksdb = ROCKSDB_CUSTOM_NAMESPACE;
#endif
