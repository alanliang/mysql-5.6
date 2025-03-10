/* Copyright (c) 2010, 2019, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
  */

/**
  @file storage/perfschema/table_esms_by_all.cc
  Table EVENTS_STATEMENTS_SUMMARY_GLOBAL_BY_ALL (implementation).
*/

#include "storage/perfschema/table_esms_by_all.h"

#include <stddef.h>

#include "include/my_md5_size.h"
#include "my_dbug.h"
#include "my_md5.h"
#include "my_thread.h"
#include "sql/field.h"
#include "sql/plugin_table.h"
#include "sql/table.h"
#include "storage/perfschema/pfs_column_types.h"
#include "storage/perfschema/pfs_column_values.h"
#include "storage/perfschema/pfs_digest.h"
#include "storage/perfschema/pfs_global.h"
#include "storage/perfschema/pfs_instr.h"
#include "storage/perfschema/pfs_instr_class.h"
#include "storage/perfschema/pfs_timer.h"
#include "storage/perfschema/pfs_visitor.h"
#include "storage/perfschema/table_helper.h"

THR_LOCK table_esms_by_all::m_table_lock;

Plugin_table table_esms_by_all::m_table_def(
    /* Schema name */
    "performance_schema",
    /* Name */
    "events_statements_summary_by_all",
    /* Definition */
    "  SCHEMA_NAME VARCHAR(64),\n"
    "  DIGEST VARCHAR(64),\n"
    "  USER VARCHAR(80),\n"
    "  CLIENT_ID VARCHAR(32),\n"
    "  PLAN_ID VARCHAR(32),\n"
    "  COUNT_STAR BIGINT unsigned not null,\n"
    "  SUM_TIMER_WAIT BIGINT unsigned not null,\n"
    "  MIN_TIMER_WAIT BIGINT unsigned not null,\n"
    "  AVG_TIMER_WAIT BIGINT unsigned not null,\n"
    "  MAX_TIMER_WAIT BIGINT unsigned not null,\n"
    "  SUM_LOCK_TIME BIGINT unsigned not null,\n"
    "  SUM_ERRORS BIGINT unsigned not null,\n"
    "  SUM_WARNINGS BIGINT unsigned not null,\n"
    "  SUM_ROWS_AFFECTED BIGINT unsigned not null,\n"
    "  SUM_ROWS_SENT BIGINT unsigned not null,\n"
    "  SUM_ROWS_EXAMINED BIGINT unsigned not null,\n"
    "  SUM_CREATED_TMP_DISK_TABLES BIGINT unsigned not null,\n"
    "  SUM_CREATED_TMP_TABLES BIGINT unsigned not null,\n"
    "  SUM_SELECT_FULL_JOIN BIGINT unsigned not null,\n"
    "  SUM_SELECT_FULL_RANGE_JOIN BIGINT unsigned not null,\n"
    "  SUM_SELECT_RANGE BIGINT unsigned not null,\n"
    "  SUM_SELECT_RANGE_CHECK BIGINT unsigned not null,\n"
    "  SUM_SELECT_SCAN BIGINT unsigned not null,\n"
    "  SUM_SORT_MERGE_PASSES BIGINT unsigned not null,\n"
    "  SUM_SORT_RANGE BIGINT unsigned not null,\n"
    "  SUM_SORT_ROWS BIGINT unsigned not null,\n"
    "  SUM_SORT_SCAN BIGINT unsigned not null,\n"
    "  SUM_NO_INDEX_USED BIGINT unsigned not null,\n"
    "  SUM_NO_GOOD_INDEX_USED BIGINT unsigned not null,\n"
    "  SUM_CPU_TIME BIGINT unsigned not null,\n"
    "  SUM_ROWS_DELETED BIGINT unsigned not null,\n"
    "  SUM_ROWS_INSERTED BIGINT unsigned not null,\n"
    "  SUM_ROWS_UPDATED BIGINT unsigned not null,\n"
    "  SUM_TMP_TABLE_BYTES_WRITTEN BIGINT unsigned not null,\n"
    "  SUM_FILESORT_BYTES_WRITTEN BIGINT unsigned not null,\n"
    "  SUM_INDEX_DIVE_COUNT BIGINT unsigned not null,\n"
    "  SUM_INDEX_DIVE_CPU BIGINT unsigned not null,\n"
    "  SUM_COMPILATION_CPU BIGINT unsigned not null,\n"
    "  SUM_ELAPSED_TIME BIGINT unsigned not null,\n"
    "  SUM_SKIPPED BIGINT unsigned not null,\n"
    "  SUM_FILESORT_DISK_USAGE BIGINT unsigned not null,\n"
    "  SUM_TMP_TABLE_DISK_USAGE BIGINT unsigned not null,\n"
    "  FIRST_SEEN TIMESTAMP(6) NOT NULL default 0,\n"
    "  LAST_SEEN TIMESTAMP(6) NOT NULL default 0,\n"
    "  QUANTILE_95 BIGINT unsigned not null,\n"
    "  QUANTILE_99 BIGINT unsigned not null,\n"
    "  QUANTILE_999 BIGINT unsigned not null,\n"
    "  QUERY_SAMPLE_TEXT LONGTEXT,\n"
    "  QUERY_SAMPLE_SEEN TIMESTAMP(6) NOT NULL default 0,\n"
    "  QUERY_SAMPLE_TIMER_WAIT BIGINT unsigned NOT NULL,\n"
    "  UNIQUE KEY (SCHEMA_NAME, DIGEST, USER, CLIENT_ID) USING HASH\n",
    /* Options */
    " ENGINE=PERFORMANCE_SCHEMA",
    /* Tablespace */
    nullptr);

PFS_engine_table_share table_esms_by_all::m_share = {
    &pfs_truncatable_acl,
    table_esms_by_all::create,
    NULL, /* write_row */
    table_esms_by_all::delete_all_rows,
    table_esms_by_all::get_row_count,
    sizeof(PFS_simple_index),
    &m_table_lock,
    &m_table_def,
    false, /* perpetual */
    PFS_engine_table_proxy(),
    {0},
    false /* m_in_purgatory */
};

bool PFS_index_esms_by_all::match(PFS_statements_digest_stat *pfs,
                                  const char *schema_name,
                                  const char *user_name) {
  if (m_fields >= 1) {
    if (!m_key_1.match(schema_name)) {
      return false;
    }
  }

  if (m_fields >= 2) {
    if (!m_key_2.match(pfs)) {
      return false;
    }
  }

  if (m_fields >= 3) {
    if (!m_key_3.match(user_name)) {
      return false;
    }
  }

  if (m_fields >= 4) {
    return m_key_4.match(pfs);
  }
  return true;
}

PFS_engine_table *table_esms_by_all::create(PFS_engine_table_share *) {
  return new table_esms_by_all();
}

int table_esms_by_all::delete_all_rows(void) {
  reset_esms_by_digest();
  return 0;
}

ha_rows table_esms_by_all::get_row_count(void) { return digest_max; }

table_esms_by_all::table_esms_by_all()
    : PFS_engine_table(&m_share, &m_pos), m_pos(0), m_next_pos(0) {
  m_normalizer = time_normalizer::get_statement();
  pfs_digest_name_id_map.fill_invert_map(DB_MAP_NAME, &m_db_map);
  pfs_digest_name_id_map.fill_invert_map(USER_MAP_NAME, &m_user_map);
  pfs_digest_sample_query_text_map.copy_map(QUERY_TEXT_MAP_NAME,
                                            &m_query_text_map);
}

void table_esms_by_all::reset_position(void) {
  m_pos = 0;
  m_next_pos = 0;
}

int table_esms_by_all::rnd_next(void) {
  PFS_statements_digest_stat *digest_stat;

  if (statements_digest_stat_array == NULL ||
      !pfs_param.m_esms_by_all_enabled) {
    return HA_ERR_END_OF_FILE;
  }

  for (m_pos.set_at(&m_next_pos); m_pos.m_index < digest_max; m_pos.next()) {
    digest_stat = &statements_digest_stat_array[m_pos.m_index];
    if (digest_stat->m_lock.is_populated()) {
      if (digest_stat->m_first_seen != 0) {
        m_next_pos.set_after(&m_pos);
        return make_row(digest_stat);
      }
    }
  }

  return HA_ERR_END_OF_FILE;
}

int table_esms_by_all::rnd_pos(const void *pos) {
  PFS_statements_digest_stat *digest_stat;

  if (statements_digest_stat_array == NULL ||
      !pfs_param.m_esms_by_all_enabled) {
    return HA_ERR_END_OF_FILE;
  }

  set_position(pos);
  digest_stat = &statements_digest_stat_array[m_pos.m_index];

  if (digest_stat->m_lock.is_populated()) {
    if (digest_stat->m_first_seen != 0) {
      return make_row(digest_stat);
    }
  }

  return HA_ERR_RECORD_DELETED;
}

int table_esms_by_all::index_init(uint idx MY_ATTRIBUTE((unused)), bool) {
  PFS_index_esms_by_all *result = NULL;
  assert(idx == 0);
  result = PFS_NEW(PFS_index_esms_by_all);
  m_opened_index = result;
  m_index = result;
  return 0;
}

int table_esms_by_all::index_next(void) {
  PFS_statements_digest_stat *digest_stat;

  if (statements_digest_stat_array == NULL ||
      !pfs_param.m_esms_by_all_enabled) {
    return HA_ERR_END_OF_FILE;
  }

  for (m_pos.set_at(&m_next_pos); m_pos.m_index < digest_max; m_pos.next()) {
    digest_stat = &statements_digest_stat_array[m_pos.m_index];
    if (digest_stat->m_lock.is_populated() && digest_stat->m_first_seen != 0) {
      if (m_opened_index->match(
              digest_stat,
              pfs_digest_name_id_map.get_name(&m_db_map,
                                              digest_stat->m_digest_key.db_id),
              pfs_digest_name_id_map.get_name(
                  &m_user_map, digest_stat->m_digest_key.user_id))) {
        if (!make_row(digest_stat)) {
          m_next_pos.set_after(&m_pos);
          return 0;
        }
      }
    }
  }

  return HA_ERR_END_OF_FILE;
}

int table_esms_by_all::make_row(PFS_statements_digest_stat *digest_stat) {
  if (check_pre_make_row("EVENTS_STATEMENTS_SUMMARY_BY_ALL")) return 1;

  m_row.m_first_seen = digest_stat->m_first_seen;
  m_row.m_last_seen = digest_stat->m_last_seen;
  m_row.m_digest.make_row(digest_stat,
                          pfs_digest_name_id_map.get_name(
                              &m_db_map, digest_stat->m_digest_key.db_id));
  auto user_name = pfs_digest_name_id_map.get_name(
      &m_user_map, digest_stat->m_digest_key.user_id);
  m_row.m_user_name_length = user_name ? strlen(user_name) : 0;
  if (m_row.m_user_name_length > 0)
    memcpy(m_row.m_user_name, user_name, m_row.m_user_name_length);
  array_to_hex(m_row.client_id, digest_stat->m_digest_key.client_id,
               MD5_HASH_SIZE);
  m_row.client_id[MD5_HASH_TO_STRING_LENGTH] = '\0';
  array_to_hex(m_row.plan_id, digest_stat->m_digest_key.plan_id, MD5_HASH_SIZE);
  m_row.plan_id[MD5_HASH_TO_STRING_LENGTH] = '\0';

  /*
    Get statements stats.
  */
  m_row.m_stat.set(m_normalizer, &digest_stat->m_stat);

  m_row.m_p95 = 0;
  m_row.m_p99 = 0;
  m_row.m_p999 = 0;

  PFS_histogram *histogram = digest_stat->get_histogram();
  if (histogram) {
    ulong index;
    ulonglong count_star = 0;

    for (index = 0; index < NUMBER_OF_BUCKETS; index++) {
      count_star += histogram->read_bucket(index);
    }

    if (count_star > 0) {
      ulonglong count_95 = ((count_star * 95) + 99) / 100;
      ulonglong count_99 = ((count_star * 99) + 99) / 100;
      ulonglong count_999 = ((count_star * 999) + 999) / 1000;

      assert(count_95 != 0);
      assert(count_95 <= count_star);
      assert(count_99 != 0);
      assert(count_99 <= count_star);
      assert(count_999 != 0);
      assert(count_999 <= count_star);

      ulong index_95 = 0;
      ulong index_99 = 0;
      ulong index_999 = 0;
      bool index_95_set = false;
      bool index_99_set = false;
      bool index_999_set = false;
      ulonglong count = 0;

      for (index = 0; index < NUMBER_OF_BUCKETS; index++) {
        count += histogram->read_bucket(index);

        if ((count >= count_95) && !index_95_set) {
          index_95 = index;
          index_95_set = true;
        }

        if ((count >= count_99) && !index_99_set) {
          index_99 = index;
          index_99_set = true;
        }

        if ((count >= count_999) && !index_999_set) {
          index_999 = index;
          index_999_set = true;
        }
      }

      m_row.m_p95 = g_histogram_pico_timers.m_bucket_timer[index_95 + 1];
      m_row.m_p99 = g_histogram_pico_timers.m_bucket_timer[index_99 + 1];
      m_row.m_p999 = g_histogram_pico_timers.m_bucket_timer[index_999 + 1];
    }
  }

  /* Format the query sample sqltext string for output. */
  format_sqltext(m_query_text_map[digest_stat->m_query_sample_id].c_str(),
                 m_query_text_map[digest_stat->m_query_sample_id].length(),
                 get_charset(digest_stat->m_query_sample_cs_number, MYF(0)),
                 digest_stat->m_query_sample_truncated, m_row.m_query_sample);

  m_row.m_query_sample_seen = digest_stat->m_query_sample_seen;
  m_row.m_query_sample_timer_wait =
      m_normalizer->wait_to_pico(digest_stat->m_query_sample_timer_wait);
  return 0;
}

int table_esms_by_all::read_row_values(TABLE *table, unsigned char *buf,
                                       Field **fields, bool read_all) {
  Field *f;

  /*
    Set the null bits. It indicates how many fields could be null
    in the table.
  */
  assert(table->s->null_bytes == 1);
  buf[0] = 0;

  for (; (f = *fields); fields++) {
    if (read_all || bitmap_is_set(table->read_set, f->field_index())) {
      switch (f->field_index()) {
        case 0: /* SCHEMA_NAME */
        case 1: /* DIGEST */
          m_row.m_digest.set_field(f->field_index(), f);
          break;
        case 2: /* USER */
          if (m_row.m_user_name_length > 0) {
            set_field_varchar_utf8(f, m_row.m_user_name,
                                   m_row.m_user_name_length);
          } else {
            f->set_null();
          }
          break;
        case 3: /* CLIENT_ID */
          if (strlen(m_row.client_id) > 0) {
            set_field_varchar_utf8(f, m_row.client_id, strlen(m_row.client_id));
          } else {
            f->set_null();
          }
          break;
        case 4: /* PLAN_ID */
          if (strlen(m_row.plan_id) > 0) {
            set_field_varchar_utf8(f, m_row.plan_id, strlen(m_row.plan_id));
          } else {
            f->set_null();
          }
          break;
        case 42: /* FIRST_SEEN */
          set_field_timestamp(f, m_row.m_first_seen);
          break;
        case 43: /* LAST_SEEN */
          set_field_timestamp(f, m_row.m_last_seen);
          break;
        case 44: /* QUANTILE_95 */
          set_field_ulonglong(f, m_row.m_p95);
          break;
        case 45: /* QUANTILE_99 */
          set_field_ulonglong(f, m_row.m_p99);
          break;
        case 46: /* QUANTILE_999 */
          set_field_ulonglong(f, m_row.m_p999);
          break;
        case 47: /* QUERY_SAMPLE_TEXT */
          if (m_row.m_query_sample.length())
            set_field_text(f, m_row.m_query_sample.ptr(),
                           m_row.m_query_sample.length(),
                           m_row.m_query_sample.charset());
          else {
            f->set_null();
          }
          break;
        case 48: /* QUERY_SAMPLE_SEEN */
          set_field_timestamp(f, m_row.m_query_sample_seen);
          break;
        case 49: /* QUERY_SAMPLE_TIMER_WAIT */
          set_field_ulonglong(f, m_row.m_query_sample_timer_wait);
          break;
        default: /* 3, ... COUNT/SUM/MIN/AVG/MAX */
          m_row.m_stat.set_field(f->field_index() - 5, f);
          break;
      }
    }
  }

  return 0;
}
