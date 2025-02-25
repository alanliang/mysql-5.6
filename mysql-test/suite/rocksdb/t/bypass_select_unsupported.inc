--disable_query_log
create table t1 (pk INT PRIMARY KEY NOT NULL, a INT NOT NULL, b INT NOT NULL,
c INT NOT NULL, d INT NOT NULL, KEY a (a, b, c)) ENGINE=ROCKSDB;
create table t2 (pk INT PRIMARY KEY NOT NULL, a INT NOT NULL, b INT NOT NULL,
c INT NOT NULL, KEY a (a, b, c)) ENGINE=ROCKSDB;
create table t3 (pk INT PRIMARY KEY NOT NULL, a INT NOT NULL, b INT NOT NULL,
c VARCHAR(15) CHARACTER SET latin1 NOT NULL, KEY a (a, b)) ENGINE=ROCKSDB;
create table t4 (pk INT PRIMARY KEY, a INT) ENGINE=ROCKSDB;
create table t5 (pk int primary key, a varchar(128) not null collate latin1_swedish_ci, b varchar(128) not null collate latin1_swedish_ci, index (a)) engine=rocksdb;
# using latin1_swedish_ci to verify non binary collation fileds can be returned.
CREATE TABLE t6 (
  `id1` bigint unsigned NOT NULL DEFAULT '0',
  `id1_type` int unsigned NOT NULL DEFAULT '0',
  `id2` bigint unsigned NOT NULL DEFAULT '0',
  `id2_type` int unsigned NOT NULL DEFAULT '0',
  `link_type` bigint unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint NOT NULL DEFAULT '0',
  `data` varchar(255) COLLATE latin1_swedish_ci NOT NULL DEFAULT '',
  `time` int unsigned NOT NULL DEFAULT '0',
  `version` bigint unsigned NOT NULL DEFAULT '0',
    PRIMARY KEY (`link_type`,`id1`,`id2`) COMMENT 'cf_link'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;
insert into t6 values
  (1, 1, 5, 2, 2, 1, 'data15', 1, 1),
  (2, 1, 1, 2, 2, 1, 'data21', 1, 1),
  (2, 1, 2, 2, 2, 1, 'data22', 1, 1),
  (2, 1, 3, 2, 2, 1, 'data32', 1, 1),
  (100, 101, 102, 103, 104, 1, 'data100', 105, 106),
  (200, 201, 202, 203, 204, 2, 'data200', 205, 206),
  (300, 301, 302, 303, 304, 3, 'data300', 305, 306),
  (400, 401, 402, 403, 404, 4, 'data400', 405, 406);
--enable_query_log

# Interesting SELECT patterns that are not supported
# Use https://dev.mysql.com/doc/refman/5.7/en/select.html as reference

# Unsupported having
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk=1 having pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;
set global rocksdb_select_bypass_rejected_query_history_size=3;
SELECT @@rocksdb_select_bypass_rejected_query_history_size;

# Unsupported group by
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk=1 group by pk;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Unsupported select *
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ * from t1 WHERE pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ 1 from t1 WHERE pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ 1+2 from t1 WHERE pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ COUNT(*) from t1 WHERE pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ COUNT(pk) from t1 WHERE pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk+1 from t1 WHERE pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Unsupported index
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 USE INDEX (PRIMARY) WHERE pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Unsupported WHERE
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk=1 or pk=2;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

SELECT /*+ bypass */ pk from t1 WHERE pk > 1 AND a = 1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';

SELECT /*+ bypass */ pk from t1 WHERE pk = 1 AND a = 1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';

SELECT /*+ bypass */ d from t1 FORCE INDEX (a)
WHERE a = 1 AND b = 2 AND c = 3 AND d > 4;
SHOW STATUS LIKE 'rocksdb_select_bypass%';

SELECT /*+ bypass */ d from t1 FORCE INDEX (a)
WHERE a = 1 AND b = 2 AND c > 3 AND d > 4;
SHOW STATUS LIKE 'rocksdb_select_bypass%';

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ d from t1 FORCE INDEX (a)
WHERE a = 1 AND b = 2 AND d > 4;
SHOW STATUS LIKE 'rocksdb_select_bypass%';

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ d from t1 FORCE INDEX (a)
WHERE a = 1 AND b > 2 AND d > 4;
SHOW STATUS LIKE 'rocksdb_select_bypass%';

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk > 1 AND pk >= 2;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk >= 1 AND pk > 2;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk > 1 AND pk >= 2;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk >= 1 AND pk > 2;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk > 1 AND pk >= 2;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk < 1 AND pk <= 2;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk <= 1 AND pk < 2;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c from t1 WHERE a > 0 and b > 0;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c from t1 WHERE a > 0 and b in (1, 2);
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c from t1 WHERE a > 0 and b > 0 and c > 0;


if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk < 1 AND pk <= 2;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk <= 1 AND pk < 2;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c from t1 WHERE a > 0 and b > 0;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c from t1 WHERE a > 0 and b in (1, 2);

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c from t1 WHERE a > 0 and b > 0 and c > 0;

SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk<=>1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
SELECT /*+ bypass */ pk from t1 WHERE pk=(1,2,3);
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;
set global rocksdb_select_bypass_rejected_query_history_size=4;
SELECT @@rocksdb_select_bypass_rejected_query_history_size;
}

# DATE/TIME/TIMESTAMP are constant functions
#--error ER_OPERAND_COLUMNS
SELECT /*+ bypass */ pk from t1 WHERE pk=DATE '2019-03-25';
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

#--error ER_OPERAND_COLUMNS
SELECT /*+ bypass */ pk from t1 WHERE pk=TIME '18:01:00';
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

#--error ER_OPERAND_COLUMNS
SELECT /*+ bypass */ pk from t1 WHERE pk=TIMESTAMP '2019-03-25 18:01:00';
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk=ANY (SELECT pk FROM t1);
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ pk from t1 WHERE pk>ALL (SELECT pk FROM t1);
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Select modifiers
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ DISTINCT a from t1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ ALL a, b from t1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;
set global rocksdb_select_bypass_rejected_query_history_size=1;
SELECT @@rocksdb_select_bypass_rejected_query_history_size;
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Joins
SELECT /*+ bypass */ t1.pk, t2.pk from t1, t2;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

SELECT /*+ bypass */ t1.pk FROM t1 LEFT JOIN t2 using (pk)
WHERE t1.pk=1;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Subselect
SELECT /*+ bypass */ pk, a from (SELECT pk, a FROM t1) AS t1_temp;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Union
SELECT /*+ bypass */ pk FROM t1 WHERE pk=3 UNION DISTINCT
SELECT pk FROM t2 WHERE pk=3;
SELECT /*+ bypass */ pk FROM t1 WHERE pk=3 UNION ALL
SELECT pk FROM t2 WHERE pk=3;
SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Index order
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t1 WHERE a=1 ORDER BY a;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t1 FORCE INDEX(`a`) WHERE a=1 ORDER BY b, a;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t1 FORCE INDEX(`a`) WHERE a=1
ORDER BY a, b, c, c, c, a, b, c, d;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t1 FORCE INDEX(`a`) WHERE a=1 ORDER BY c, a;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t1 FORCE INDEX(`a`) WHERE a=1 ORDER BY a, a;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t1 FORCE INDEX(`a`) WHERE a=1
ORDER BY a, a, a, a, a, a, a, a;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t1 FORCE INDEX(`a`) WHERE a=1 ORDER BY b, b;

if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t3 FORCE INDEX(`a`) WHERE a=1 ORDER BY b, b;

SHOW STATUS LIKE 'rocksdb_select_bypass%';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Unrecognized index
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
SELECT /*+ bypass */ a, b, c FROM t1 FORCE INDEX(`abc`) WHERE a=1 ORDER BY b, a;
}

# INTO @var
# SELECT /*+ bypass */ pk into @a FROM t1
# WHERE pk=1;
# SHOW STATUS LIKE 'rocksdb_select_bypass%';

# INTO OUTFILE
let $datadir = `SELECT @@datadir`;
--replace_result $datadir <DATADIR>

--replace_result $datadir datadir
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
eval SELECT /*+ bypass */ a from t1 WHERE a=1 INTO OUTFILE '$datadir/select.out';
SHOW STATUS LIKE 'rocksdb_select_bypass%';

--replace_result $datadir datadir
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
eval SELECT /*+ bypass */ a from t1 WHERE a=1 INTO DUMPFILE '$datadir/select.dump';
SHOW STATUS LIKE 'rocksdb_select_bypass%';

# FOR UPDATE
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t3 WHERE pk=1 FOR UPDATE;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t3 WHERE pk=1 FOR UPDATE SKIP LOCKED;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t3 WHERE pk=1 FOR UPDATE NOWAIT;
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a, b, c FROM t3 WHERE pk=1 LOCK IN SHARE MODE;
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# HIGH PRIORITY
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ HIGH_PRIORITY a, b, c FROM t3 WHERE pk=1;

# Too many WHERE expressions
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a FROM t3 WHERE a=1 AND a=1 AND a=1 AND a=1 AND a=1 AND
                                     a=1 AND a=1 AND a=1 AND a=1 AND a=1 AND
                                     a=1 AND a=1 AND a=1 AND a=1 AND a=1 AND
                                     a=1 AND a=1;
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# NULL fields not supported
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a FROM t4 WHERE a=1;
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# case insensitive condition is not supported
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ a,b FROM t5 FORCE INDEX (a) WHERE a='abc';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;


# test cases verify bypass fallback sends correct data.
# each test case selects records with different values
# to ensure data is set correctly in the fallback path.

set global rocksdb_select_bypass_rejected_query_history_size=1;

# unsupported varchar cond field charset
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM t6 WHERE data = 'data100';
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Unsupported range query pattern
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM t6 WHERE id1 > 100 and id1 < 300 and id1 > 110;
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Unsupported range query pattern
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM t6 WHERE link_type>100 and link_type > 200 and time=305;
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

# Non-optimal queries
if ($bypass_fallback==0) {
--error ER_NOT_SUPPORTED_YET
}
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM t6 WHERE link_type=404 and time > 300;
SELECT QUERY, ERROR_MSG, BYPASS_TYPE from information_schema.ROCKSDB_BYPASS_REJECTED_QUERY_HISTORY;

drop table t1;
drop table t2;
drop table t3;
drop table t4;
drop table t5;
drop table t6;
if ($bypass_fallback != 0) {
--remove_file $datadir/select.out
--remove_file $datadir/select.dump
}
