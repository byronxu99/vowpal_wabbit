// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/csv_parser/parse_example_csv.h"

#include "vw/core/best_constant.h"
#include "vw/core/constant.h"
#include "vw/core/parse_args.h"
#include "vw/core/parse_primitives.h"
#include "vw/core/parser.h"

#include <string>

namespace VW
{
namespace parsers
{
namespace csv
{
int parse_csv_examples(VW::workspace* all, io_buf& buf, VW::multi_ex& examples)
{
  bool keep_reading = all->parser_runtime.custom_parser->next(*all, buf, examples);
  return keep_reading ? 1 : 0;
}

void csv_parser::set_csv_separator(std::string& str, const std::string& name)
{
  if (str.length() == 0) { THROW("Empty string passed as " << name); }

  if (str.length() == 1)
  {
    const std::string csv_separator_forbid_chars = "\"|:";
    if (csv_separator_forbid_chars.find(str[0]) != std::string::npos)
    {
      THROW("Forbidden field separator used: " << str[0]);
    }

    // All other single characters are allowed
    return;
  }

  if (str.length() >= 2)
  {
    // Allow to specify tab character as literal \t
    // As pressing tabs usually means auto completion
    if (str != "\\t") { THROW("Multiple characters passed as " << name << ": " << str); }

    // Replace with real tab character
    str = "\t";
    return;
  }
}

void csv_parser::set_parse_args(VW::config::option_group_definition& in_options, csv_parser_options& parsed_options)
{
  in_options
      .add(VW::config::make_option("csv", parsed_options.enabled)
               .help("Data file will be interpreted as a CSV file")
               .experimental())
      .add(VW::config::make_option("csv_separator", parsed_options.csv_separator)
               .default_value(",")
               .help("CSV Parser: Specify field separator in one character, "
                     "\" | : are not allowed for reservation.")
               .experimental())
      .add(VW::config::make_option("csv_no_file_header", parsed_options.csv_no_file_header)
               .default_value(false)
               .help("CSV Parser: First line is NOT a header. By default, CSV files "
                     "are assumed to have a header with feature and/or namespaces names. "
                     "You MUST specify the header with --csv_header if you use this option.")
               .experimental())
      .add(VW::config::make_option("csv_header", parsed_options.csv_header)
               .default_value("")
               .help("CSV Parser: Override the CSV header by providing (namespace, '|' and) "
                     "feature name separated with ','. By default, CSV files are assumed to "
                     "have a header with feature and/or namespaces names in the CSV first line. "
                     "You can override it by specifying here. Combined with --csv_no_file_header, "
                     "we assume that there is no header in the CSV file.")
               .experimental())
      .add(VW::config::make_option("csv_ns_value", parsed_options.csv_ns_value)
               .default_value("")
               .help("CSV Parser: Scale the namespace values by specifying the float "
                     "ratio. e.g. --csv_ns_value=a:0.5,b:0.3,:8 ")
               .experimental());
}

void csv_parser::handle_parse_args(csv_parser_options& parsed_options)
{
  if (parsed_options.enabled)
  {
    set_csv_separator(parsed_options.csv_separator, "CSV separator");

    if (parsed_options.csv_no_file_header && parsed_options.csv_header.empty())
    {
      THROW("No header specified while --csv_no_file_header is set.");
    }
  }
}

class CSV_parser
{
public:
  CSV_parser(VW::workspace* all, VW::example* ae, VW::string_view csv_line, VW::parsers::csv::csv_parser* parser)
      : _parser(parser), _all(all), _ae(ae)
  {
    if (csv_line.empty()) { THROW("Malformed CSV, empty line at " << _parser->line_num << "!"); }
    else
    {
      _csv_line = split(csv_line, parser->options.csv_separator[0], true);
      parse_line();
    }
  }

private:
  VW::parsers::csv::csv_parser* _parser;
  VW::workspace* _all;
  VW::example* _ae;
  VW::v_array<VW::string_view> _csv_line;
  std::vector<std::string> _token_storage;
  size_t _anon{};
  uint64_t _namespace_hash{};

  inline FORCE_INLINE void parse_line()
  {
    bool this_line_is_header = false;

    // Handle the headers and initialize the configuration
    if (_parser->header_feature_names_str.empty())
    {
      if (_parser->options.csv_header.empty()) { parse_header(_csv_line); }
      else
      {
        VW::v_array<VW::string_view> header_elements = split(_parser->options.csv_header, ',');
        parse_header(header_elements);
      }

      if (!_parser->options.csv_no_file_header) { this_line_is_header = true; }

      // Store the ns value from CmdLine
      if (_parser->ns_value.empty() && !_parser->options.csv_ns_value.empty()) { parse_ns_value(); }
    }

    if (_csv_line.size() != _parser->header_feature_names_str.size())
    {
      THROW("CSV line " << _parser->line_num << " has " << _csv_line.size() << " elements, but the header has "
                        << _parser->header_feature_names_str.size() << " elements!");
    }
    else if (!this_line_is_header) { parse_example(); }
  }

  inline FORCE_INLINE void parse_ns_value()
  {
    VW::v_array<VW::string_view> ns_values = split(_parser->options.csv_ns_value, ',', true);
    for (size_t i = 0; i < ns_values.size(); i++)
    {
      VW::v_array<VW::string_view> pair = split(ns_values[i], ':', true);
      std::string ns = VW::details::DEFAULT_NAMESPACE_STR;
      float value = 1.f;
      if (pair.size() != 2 || pair[1].empty())
      {
        THROW("Malformed namespace value pair at cell " << i + 1 << ": " << ns_values[i]);
      }
      else if (!pair[0].empty()) { ns = std::string{pair[0]}; }

      value = string_to_float(pair[1]);
      if (std::isnan(value)) { THROW("NaN namespace value at cell " << i + 1 << ": " << ns_values[i]); }
      else { _parser->ns_value[std::string{pair[0]}] = value; }
    }
  }

  inline FORCE_INLINE void parse_header(VW::v_array<VW::string_view>& header_elements)
  {
    for (size_t i = 0; i < header_elements.size(); i++)
    {
      if (_parser->options.csv_remove_outer_quotes) { remove_quotation_marks(header_elements[i]); }

      // Handle special column names
      if (header_elements[i] == "_tag" || header_elements[i] == "_label")
      {
        // Handle the tag column
        if (header_elements[i] == "_tag") { _parser->tag_list.emplace_back(i); }
        // Handle the label column
        else if (header_elements[i] == "_label") { _parser->label_list.emplace_back(i); }

        // Add an empty entry
        _parser->header_feature_names_str.emplace_back();
        _parser->header_namespace_names.emplace_back();
        _parser->feature_name_is_int.push_back(false);
        _parser->header_feature_names_int.push_back(0);
        continue;
      }

      // Handle other column names as feature names
      // Seperate the feature name and namespace from the header.
      VW::v_array<VW::string_view> splitted = split(header_elements[i], '|');
      VW::string_view feature_name;
      VW::string_view namespace_name = VW::details::DEFAULT_NAMESPACE_STR;
      if (splitted.size() == 1) { feature_name = header_elements[i]; }
      else if (splitted.size() == 2)
      {
        namespace_name = splitted[0];
        feature_name = splitted[1];
      }
      else
      {
        THROW("Malformed header for feature name and namespace separator at cell " << i + 1 << ": "
                                                                                   << header_elements[i]);
      }
      _parser->header_feature_names_str.emplace_back(feature_name);
      _parser->header_namespace_names.emplace_back(namespace_name);
      _parser->feature_list[std::string{namespace_name}].emplace_back(i);

      // Check if the feature name is integer or string
      // If _hash_all is true, all feature names are treated as strings
      if (_all->parser_runtime.hash_all)
      {
        _parser->feature_name_is_int.push_back(false);
        _parser->header_feature_names_int.push_back(0);
      }
      else
      {
        bool is_int = !feature_name.empty() && VW::details::is_string_integer(feature_name);
        _parser->feature_name_is_int.push_back(is_int);
        if (is_int) { _parser->header_feature_names_int.push_back(std::strtoll(feature_name.data(), nullptr, 10)); }
        else { _parser->header_feature_names_int.push_back(0); }
      }
    }

    if (_parser->label_list.empty())
    {
      _all->logger.err_warn("No '_label' column found in the header/CSV first line!");
    }

    // Make sure all the output vectors have the correct size
    size_t n_cols = header_elements.size();
    assert(_parser->header_feature_names_str.size() == n_cols);
    assert(_parser->header_namespace_names.size() == n_cols);
    assert(_parser->feature_name_is_int.size() == n_cols);
    assert(_parser->header_feature_names_int.size() == n_cols);
  }

  inline FORCE_INLINE void parse_example()
  {
    _all->parser_runtime.example_parser->lbl_parser.default_label(_ae->l);
    if (!_parser->label_list.empty()) { parse_label(); }
    if (!_parser->tag_list.empty()) { parse_tag(); }

    parse_namespaces();
  }

  inline FORCE_INLINE void parse_label()
  {
    VW::string_view label_content = _csv_line[_parser->label_list[0]];
    if (_parser->options.csv_remove_outer_quotes) { remove_quotation_marks(label_content); }

    _all->parser_runtime.example_parser->words.clear();
    VW::tokenize(' ', label_content, _all->parser_runtime.example_parser->words);

    if (!_all->parser_runtime.example_parser->words.empty())
    {
      _all->parser_runtime.example_parser->lbl_parser.parse_label(_ae->l, _ae->ex_reduction_features,
          _all->parser_runtime.example_parser->parser_memory_to_reuse, _all->sd->ldict.get(),
          _all->parser_runtime.example_parser->words, _all->logger);
    }
  }

  inline FORCE_INLINE void parse_tag()
  {
    VW::string_view tag = _csv_line[_parser->tag_list[0]];
    if (_parser->options.csv_remove_outer_quotes) { remove_quotation_marks(tag); }
    if (!tag.empty() && tag.front() == '\'') { tag.remove_prefix(1); }
    _ae->tag.insert(_ae->tag.end(), tag.begin(), tag.end());
  }

  inline FORCE_INLINE void parse_namespaces()
  {
    // Mark to check if all the cells in the line is empty
    bool empty_line = true;
    for (auto& f : _parser->feature_list)
    {
      // Counter for anonymous features
      _anon = 0;

      // Get or create the namespace
      std::string ns_name = f.first;
      const auto& ns_csv_columns = f.second;
      if (ns_name.empty()) { ns_name = VW::details::DEFAULT_NAMESPACE_STR; }
      auto& fs = (*_ae)[ns_name];

      // Set the namespace value
      if (!_parser->ns_value.empty())
      {
        auto it = _parser->ns_value.find(ns_name);
        if (it != _parser->ns_value.end()) { fs.namespace_value = it->second; }
      }

      // Parse the features
      bool audit = _all->output_config.audit || _all->output_config.hash_inv;
      for (size_t i = 0; i < ns_csv_columns.size(); i++)
      {
        size_t column_index = ns_csv_columns[i];
        empty_line = empty_line && _csv_line[column_index].empty();
        parse_features(fs, column_index, audit);
      }
    }
    _ae->is_newline = empty_line;
  }

  inline FORCE_INLINE void parse_features(features& fs, size_t column_index, bool audit)
  {
    VW::string_view string_feature_name = _parser->header_feature_names_str[column_index];
    VW::string_view string_feature_value = _csv_line[column_index];
    VW::feature_index int_feature_name = _parser->header_feature_names_int[column_index];
    VW::feature_value float_feature_value = 1.f;
    bool is_feature_name_int = _parser->feature_name_is_int[column_index];

    // Don't add empty valued features to list of features
    if (string_feature_value.empty()) { return; }

    // Empty feature name is always treated as int
    // Increment a counter to get an integer feature index
    if (string_feature_name.empty())
    {
      is_feature_name_int = true;
      int_feature_name = _anon++;
    }

    // Get the feature value
    bool is_feature_value_float = false;

    // If value is not quoted, try to parse it as float
    if (string_feature_value[0] != '"')
    {
      float_feature_value = string_to_float(string_feature_value);
      if (!std::isnan(float_feature_value)) { is_feature_value_float = true; }
    }
    if (!is_feature_value_float)
    {
      float_feature_value = 1.f;
      if (_parser->options.csv_remove_outer_quotes) { remove_quotation_marks(string_feature_value); }
    }

    // Don't add 0 valued features to list of features
    if (float_feature_value == 0) { return; }

    // Add the feature
    if (is_feature_name_int)
    {
      if (is_feature_value_float) { fs.add_feature(int_feature_name, float_feature_value, audit); }
      else { fs.add_feature(int_feature_name, string_feature_value, audit); }
    }
    else
    {
      if (is_feature_value_float) { fs.add_feature(string_feature_name, float_feature_value, audit); }
      else { fs.add_feature(string_feature_name, string_feature_value, audit); }
    }
  }

  inline FORCE_INLINE VW::v_array<VW::string_view> split(VW::string_view sv, const char ch, bool use_quotes = false)
  {
    VW::v_array<VW::string_view> collections;
    size_t pointer = 0;
    // Trim extra characters that are useless for us to read
    const char* trim_list = "\r\n\xef\xbb\xbf\f\v";
    sv.remove_prefix(std::min(sv.find_first_not_of(trim_list), sv.size()));
    sv.remove_suffix(std::min(sv.size() - sv.find_last_not_of(trim_list) - 1, sv.size()));

    VW::v_array<size_t> unquoted_quotes_index;
    bool inside_quotes = false;

    if (sv.empty())
    {
      collections.emplace_back();
      return collections;
    }

    for (size_t i = 0; i <= sv.length(); i++)
    {
      if (i == sv.length() && inside_quotes) { THROW("Unclosed quote at end of line " << _parser->line_num << "."); }
      // Skip Quotes at the start and end of the cell
      else if (use_quotes && !inside_quotes && i == pointer && i < sv.length() && sv[i] == '"')
      {
        inside_quotes = true;
      }
      else if (use_quotes && inside_quotes && i < sv.length() - 1 && sv[i] == '"' && sv[i] == sv[i + 1])
      {
        // RFC-4180, paragraph "If double-quotes are used to enclose fields,
        // then a double-quote appearing inside a field must be escaped by
        // preceding it with another double-quote."
        unquoted_quotes_index.emplace_back(i - pointer);
        i++;
      }
      else if (use_quotes && inside_quotes &&
          ((i < sv.length() - 1 && sv[i] == '"' && sv[i + 1] == ch) || (i == sv.length() - 1 && sv[i] == '"')))
      {
        inside_quotes = false;
      }
      else if (use_quotes && inside_quotes && i < sv.length() && sv[i] == '"')
      {
        THROW("Unescaped quote at position "
            << i + 1 << " of line " << _parser->line_num
            << ", double-quote appearing inside a cell must be escaped by preceding it with another double-quote!");
      }
      else if (i == sv.length() || (!inside_quotes && sv[i] == ch))
      {
        VW::string_view element(&sv[pointer], i - pointer);
        if (i == sv.length() && sv[i - 1] == ch) { element = VW::string_view(); }

        if (unquoted_quotes_index.empty()) { collections.emplace_back(element); }
        else
        {
          // Make double escaped quotes into one
          std::string new_string;
          size_t quotes_pointer = 0;
          unquoted_quotes_index.emplace_back(element.size());
          for (size_t j = 0; j < unquoted_quotes_index.size(); j++)
          {
            size_t sv_size = unquoted_quotes_index[j] - quotes_pointer;
            if (sv_size > 0 && quotes_pointer < element.size())
            {
              VW::string_view str_part(&element[quotes_pointer], sv_size);
              new_string += {str_part.begin(), str_part.end()};
            }
            quotes_pointer = unquoted_quotes_index[j] + 1;
          }
          // This is a bit of a hack to expand string lifetime.
          _token_storage.emplace_back(new_string);
          collections.emplace_back(_token_storage.back());
        }
        unquoted_quotes_index.clear();
        if (i < sv.length() - 1) { pointer = i + 1; }
      }
    }
    return collections;
  }

  inline FORCE_INLINE void remove_quotation_marks(VW::string_view& sv)
  {
    // When the outer quotes pair, we just remove them.
    // If they don't, we just keep them without throwing any errors.
    if (sv.size() > 1 && sv[0] == '"' && sv[0] == sv[sv.size() - 1])
    {
      sv.remove_prefix(1);
      sv.remove_suffix(1);
    }
  }

  inline FORCE_INLINE float string_to_float(VW::string_view sv)
  {
    size_t end_read = 0;
    float parsed = VW::details::parse_float(sv.data(), end_read, sv.data() + sv.size());
    // Not a valid float, return NaN
    if (!(end_read == sv.size())) { parsed = std::numeric_limits<float>::quiet_NaN(); }
    return parsed;
  }
};

void csv_parser::reset()
{
  if (options.csv_header.empty())
  {
    header_feature_names_str.clear();
    header_feature_names_int.clear();
    header_namespace_names.clear();
    feature_name_is_int.clear();
    label_list.clear();
    tag_list.clear();
    feature_list.clear();
  }
  line_num = 0;
}

int csv_parser::parse_csv(VW::workspace* all, VW::example* ae, io_buf& buf)
{
  // This function consumes input until it reaches a '\n' then it walks back the '\n' and '\r' if it exists.
  size_t num_bytes_consumed = read_line(all, ae, buf);
  // Read the data again if what just read is header.
  if (line_num == 1 && !options.csv_no_file_header) { num_bytes_consumed += read_line(all, ae, buf); }
  return static_cast<int>(num_bytes_consumed);
}

size_t csv_parser::read_line(VW::workspace* all, VW::example* ae, io_buf& buf)
{
  char* line = nullptr;
  size_t num_chars_initial = buf.readto(line, '\n');
  // This branch will get hit when we haven't reached EOF of the input device.
  if (num_chars_initial > 0)
  {
    size_t num_chars = num_chars_initial;
    if (line[0] == '\xef' && num_chars >= 3 && line[1] == '\xbb' && line[2] == '\xbf')
    {
      line += 3;
      num_chars -= 3;
    }
    if (num_chars > 0 && line[num_chars - 1] == '\n') { num_chars--; }
    if (num_chars > 0 && line[num_chars - 1] == '\r') { num_chars--; }

    line_num++;
    VW::string_view csv_line(line, num_chars);
    CSV_parser parse_line(all, ae, csv_line, this);
  }
  // EOF is reached, reset for possible next file.
  else { reset(); }
  return num_chars_initial;
}

}  // namespace csv
}  // namespace parsers
}  // namespace VW
