/* Copyright (c) 2013, 2021, Oracle and/or its affiliates.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql/parse_tree_node_base.h"

#include "sql/sql_class.h"

Parse_context::Parse_context(THD *thd_arg, Query_block *sl_arg)
    : thd(thd_arg), mem_root(thd->mem_root), select(sl_arg) {
  // When thd query arena is swapped, the caller for query_arena swap will
  // manage its own mem_root lifetime.
  // When thd query arena isn't swapped and
  // clean_parser_memory_per_statement is set, use parser_mem_root
  // instead of main_mem_root to release parser memory after each statement
  if (thd->variables.clean_parser_memory_per_statement &&
      !thd->is_query_arena_swapped())
    mem_root = thd->get_parser_mem_root();
}
