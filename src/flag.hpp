/******************************************************************************\
* Copyright (c) 2019, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      flag.hpp
@brief     Tri-state Flag class and global flags set by options
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef FLAG_HPP
#define FLAG_HPP

#include <reflex/input.h>
#include <string>
#include <set>
#include <vector>

// three-valued logic flags that behave as bool; this allows us to check if a flag was undefined (default) and explicitly enabled/disabled
class Flag {

 public:

  Flag()                     : value(UNDEFINED)  { }
  Flag(bool flag)            : value(flag ? T : F)  { }
  Flag& operator=(bool flag) { value = flag ? T : F; return *this; }
       operator bool() const { return is_true(); }
  bool is_undefined()  const { return value == UNDEFINED; }
  bool is_defined()    const { return value != UNDEFINED; }
  bool is_false()      const { return value == F; }
  bool is_true()       const { return value == T; }

 private:

  enum { UNDEFINED = -1, F = 0, T = 1 } value;

};

// --sort=KEY is n/a or by list, name, score, size, used time, changed time, created time
enum class Sort { NA, NAME, BEST, SIZE, USED, CHANGED, CREATED, LIST };

// -D, --devices and -d, --directories
enum class Action { UNSP, SKIP, READ, RECURSE };

// ugrep command-line options
extern bool flag_all_threads; // internal flag
extern bool flag_any_line;
extern bool flag_basic_regexp;
extern bool flag_best_match;
extern bool flag_bool;
extern bool flag_color_term; // internal flag
extern bool flag_confirm;
extern bool flag_count;
extern bool flag_cpp;
extern bool flag_csv;
extern bool flag_decompress;
extern bool flag_dereference;
extern bool flag_dereference_files;
extern bool flag_files;
extern bool flag_files_with_matches;
extern bool flag_files_without_match;
extern bool flag_fixed_strings;
extern bool flag_glob_ignore_case;
extern bool flag_grep; // internal flag
extern bool flag_hex;
extern bool flag_hex_star; // hexdump flag
extern bool flag_hex_cbr; // hexdump flag
extern bool flag_hex_chr; // hexdump flag
extern bool flag_hex_hbr; // hexdump flag
extern bool flag_hidden;
extern bool flag_hyperlink_line; // internal flag
extern bool flag_invert_match;
extern bool flag_json;
extern bool flag_line_buffered;
extern bool flag_line_regexp;
extern bool flag_match;
extern bool flag_multiline; // internal flag
extern bool flag_no_dereference;
extern bool flag_no_header;
extern bool flag_no_filename;
extern bool flag_no_messages;
extern bool flag_not;
extern bool flag_null;
extern bool flag_null_data;
extern bool flag_only_line_number;
extern bool flag_only_matching;
extern bool flag_perl_regexp;
extern bool flag_query;
extern bool flag_quiet;
extern bool flag_sort_rev; // internal flag
extern bool flag_split;
extern bool flag_stdin; // internal flag
extern bool flag_tty_term; // internal flag
extern bool flag_usage_warnings; // internal flag
extern bool flag_word_regexp;
extern bool flag_xml;
extern bool flag_with_hex;
extern bool flag_with_filename;
extern Flag flag_binary;
extern Flag flag_binary_without_match;
extern Flag flag_break;
extern Flag flag_byte_offset;
extern Flag flag_column_number;
extern Flag flag_empty;
extern Flag flag_dotall;
extern Flag flag_free_space;
extern Flag flag_heading;
extern Flag flag_ignore_case;
extern Flag flag_initial_tab;
extern Flag flag_line_number;
extern Flag flag_smart_case;
extern Flag flag_text;
extern Flag flag_tree;
extern Flag flag_ungroup;
extern Sort flag_sort_key; // internal flag
extern Action flag_devices_action; // internal flag
extern Action flag_directories_action; // internal flag
extern size_t flag_after_context;
extern size_t flag_before_context;
extern size_t flag_delay;
extern size_t flag_exclude_iglob_size; // internal flag
extern size_t flag_exclude_iglob_dir_size; // internal flag
extern size_t flag_fuzzy;
extern size_t flag_hex_after; // internal flag
extern size_t flag_hex_before; // internal flag
extern size_t flag_hex_columns; // internal flag
extern size_t flag_include_iglob_size; // internal flag
extern size_t flag_include_iglob_dir_size; // internal flag
extern size_t flag_jobs;
extern size_t flag_max_count;
extern size_t flag_max_depth;
extern size_t flag_max_files;
extern size_t flag_max_line;
extern size_t flag_max_mmap;
extern size_t flag_max_queue;
extern size_t flag_min_count;
extern size_t flag_min_depth;
extern size_t flag_min_line;
extern size_t flag_min_magic;
extern size_t flag_min_steal;
extern size_t flag_not_magic;
extern size_t flag_tabs;
extern size_t flag_width;
extern size_t flag_zmax;
extern const char *flag_binary_files;
extern const char *flag_color;
extern const char *flag_color_query; // internal flag
extern const char *flag_colors;
extern const char *flag_config;
extern const char *flag_devices;
extern const char *flag_directories;
extern const char *flag_encoding;
extern const char *flag_format;
extern const char *flag_format_begin;
extern const char *flag_format_close;
extern const char *flag_format_end;
extern const char *flag_format_open;
extern const char *flag_group_separator;
extern const char *flag_hexdump;
extern const char *flag_hyperlink;
extern const char *flag_index;
extern const char *flag_label;
extern const char *flag_pager;
extern const char *flag_pretty;
extern const char *flag_replace;
extern const char *flag_save_config;
extern const char *flag_separator;
extern const char *flag_separator_dash; // internal flag
extern const char *flag_separator_bar; // internal flag
extern const char *flag_sort;
extern const char *flag_stats;
extern const char *flag_tag;
extern const char *flag_view;
extern std::string              flag_filter;
extern std::string              flag_hyperlink_prefix; // internal flag
extern std::string              flag_hyperlink_host; // internal flag
extern std::string              flag_hyperlink_path; // internal flag
extern std::string              flag_regexp; // internal flag
extern std::set<std::string>    flag_config_files; // internal flag
extern std::set<std::string>    flag_ignore_files;
extern std::vector<std::string> flag_file;
extern std::vector<std::string> flag_file_type;
extern std::vector<std::string> flag_file_extension;
extern std::vector<std::string> flag_file_magic;
extern std::vector<std::string> flag_filter_magic_label;
extern std::vector<std::string> flag_from;
extern std::vector<std::string> flag_glob;
extern std::vector<std::string> flag_iglob;
extern std::vector<std::string> flag_include;
extern std::vector<std::string> flag_include_dir;
extern std::vector<std::string> flag_include_from;
extern std::vector<std::string> flag_include_fs;
extern std::vector<std::string> flag_exclude;
extern std::vector<std::string> flag_exclude_dir;
extern std::vector<std::string> flag_exclude_from;
extern std::vector<std::string> flag_exclude_fs;
extern std::vector<std::string> flag_all_include;
extern std::vector<std::string> flag_all_include_dir;
extern std::vector<std::string> flag_all_exclude;
extern std::vector<std::string> flag_all_exclude_dir;
extern reflex::Input::file_encoding_type flag_encoding_type;

#endif
