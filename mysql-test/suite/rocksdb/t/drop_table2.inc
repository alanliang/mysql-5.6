--disable_warnings
DROP TABLE IF EXISTS t1;
DROP TABLE IF EXISTS t2;
DROP TABLE IF EXISTS t3;
DROP TABLE IF EXISTS t4;
DROP TABLE IF EXISTS t5;
--enable_warnings

call mtr.add_suppression("Column family 'cf1' not found");
call mtr.add_suppression("Column family 'rev:cf2' not found");

# Start from clean slate
set global rocksdb_compact_cf = 'cf1';
set global rocksdb_compact_cf = 'rev:cf2';
set global rocksdb_compact_cf = '__system__';
set global rocksdb_signal_drop_index_thread = 1;
--let $shutdown_server_timeout = 300
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


# Restart the server before t2's indices are deleted
--source include/restart_mysqld.inc

let $table = t1;
--source drop_table_repopulate_table.inc
let $table = t4;
--source drop_table_repopulate_table.inc


# Insert enough data to trigger compactions that eliminate t2 and t3
let $max = 50000;
let $table = t1;
--source drop_table_repopulate_table.inc
let $table = t4;
--source drop_table_repopulate_table.inc


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

set @@global.rocksdb_compact_cf = 'cf1';
set @@global.rocksdb_compact_cf = 'rev:cf2';
set @@global.rocksdb_compact_cf = 'default';
set @@global.rocksdb_compact_cf = '__system__';

let $output= $MYSQLTEST_VARDIR/tmp/size_output;

--exec du -c $MYSQLTEST_VARDIR/mysqld.1/data/.rocksdb/*.sst |grep total |sed 's/[\t]total/ before/' > $output
drop table t1;
drop table t2;
drop table t3;
drop table t4;
drop table t5;

set @@global.rocksdb_compact_cf = 'cf1';
set @@global.rocksdb_compact_cf = 'rev:cf2';
set @@global.rocksdb_compact_cf = 'default';
set @@global.rocksdb_compact_cf = '__system__';

let $show_rpl_debug_info= 1; # to force post-failure printout
let $wait_timeout= 300; # Override default 30 seconds with 300.
--let $timeout_multiplier = 6
if ($VALGRIND_TEST)
{
  --expr $wait_timeout = $wait_timeout * $timeout_multiplier
}
let $wait_condition = select count(*) = 0 
                      as c from information_schema.rocksdb_global_info
                      where TYPE = 'DDL_DROP_INDEX_ONGOING';
--source include/wait_condition.inc

let $is_rocksdb_ddse = `SELECT @@default_dd_storage_engine = 'RocksDB'`;
# Check that space is reclaimed
--exec du -c $MYSQLTEST_VARDIR/mysqld.1/data/.rocksdb/*.sst |grep total |sed 's/[\t]total/ after/' >> $output
--exec perl suite/rocksdb/t/drop_table2_check.pl $output $is_rocksdb_ddse

# Cleanup
--remove_file $output
