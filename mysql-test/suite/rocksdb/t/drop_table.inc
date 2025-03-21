--source include/have_rocksdb.inc

--disable_warnings
DROP TABLE IF EXISTS t1;
DROP TABLE IF EXISTS t2;
DROP TABLE IF EXISTS t3;
DROP TABLE IF EXISTS t4;
DROP TABLE IF EXISTS t5;
--enable_warnings

call mtr.add_suppression("Column family 'cf1' not found");
call mtr.add_suppression("Column family 'rev:cf2' not found");
call mtr.add_suppression("LibRocksDB");

# Start from clean slate
set global rocksdb_compact_cf = 'cf1';
set global rocksdb_compact_cf = 'rev:cf2';
set global rocksdb_signal_drop_index_thread = 1;
--source include/restart_mysqld.inc

CREATE TABLE t1 (
  a int not null,
  b int not null,
  primary key (a,b) comment 'cf1',
  key (b) comment 'rev:cf2'
) ENGINE=RocksDB;

CREATE TABLE t2 (
  a int not null,
  b int not null,
  primary key (a,b) comment 'cf1',
  key (b) comment 'rev:cf2'
) ENGINE=RocksDB;

CREATE TABLE t3 (
  a int not null,
  b int not null,
  primary key (a,b) comment 'cf1',
  key (b) comment 'rev:cf2'
) ENGINE=RocksDB;

CREATE TABLE t4 (
  a int not null,
  b int not null,
  primary key (a,b) comment 'cf1',
  key (b) comment 'rev:cf2'
) ENGINE=RocksDB;

# Populate tables
let $max = 1000;
let $table = t1;
--source drop_table_repopulate_table.inc
let $table = t2;
--source drop_table_repopulate_table.inc
let $table = t3;
--source drop_table_repopulate_table.inc
let $table = t4;
--source drop_table_repopulate_table.inc

drop table t2;

# Restart the server before t2's indices are deleted
--source include/restart_mysqld.inc

let $table = t1;
--source drop_table_repopulate_table.inc
let $table = t4;
--source drop_table_repopulate_table.inc

drop table t3;

# Insert enough data to trigger compactions that eliminate t2 and t3
let $max = 5000;
let $table = t1;
--source drop_table_repopulate_table.inc
let $table = t4;
--source drop_table_repopulate_table.inc

# Run manual compaction, then restarting mysqld
# and confirming it is not blocked.
SET GLOBAL rocksdb_max_manual_compactions = 2;
SET GLOBAL rocksdb_debug_manual_compaction_delay = 3600;
connect (con1, localhost, root,,);
connect (con2, localhost, root,,);
connect (con3, localhost, root,,);
connection con1;
send SET GLOBAL rocksdb_compact_cf='cf1';
connection con2;
send SET GLOBAL rocksdb_compact_cf='rev:cf2';
connection default;
let $wait_condition = select count(*) = 2 from information_schema.processlist where info like 'SET GLOBAL rocksdb_compact_cf%';
--source include/wait_condition.inc
# longer enough than manual compaction thread to start compaction
--sleep 2
select * from performance_schema.global_status where variable_name='rocksdb_manual_compactions_running';

connection con3;
--error ER_INTERNAL_ERROR
SET GLOBAL rocksdb_compact_cf='cf1';
--error ER_INTERNAL_ERROR
SET GLOBAL rocksdb_compact_cf='rev:cf2';

connection default;
--let $shutdown_server_timeout= 500
--source include/restart_mysqld.inc

drop table t4;

# Restart the server before t4's indices are deleted
--source include/restart_mysqld.inc

# Make sure new table gets unique indices
CREATE TABLE t5 (
  a int not null,
  b int not null,
  primary key (a,b) comment 'cf1',
  key (b) comment 'rev:cf2'
) ENGINE=RocksDB;

let $max = 1000;
let $table = t5;
--source drop_table_repopulate_table.inc

drop table t5;

# Manually compact column families, cleaning up all lingering data
set global rocksdb_compact_cf = 'cf1';
set global rocksdb_compact_cf = 'rev:cf2';

# Signal thread to check for dropped indices
set global rocksdb_signal_drop_index_thread = 1;

let $show_rpl_debug_info= 1; # to force post-failure printout
let $wait_timeout= 300; # Override default 30 seconds with 300.
let $wait_condition = select count(*) = 0
                      as c from information_schema.rocksdb_global_info
                      where TYPE = 'DDL_DROP_INDEX_ONGOING';
--source include/wait_condition.inc

# Cleanup
disconnect con1;
disconnect con2;
disconnect con3;
drop table t1;
