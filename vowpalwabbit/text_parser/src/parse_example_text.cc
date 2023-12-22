// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/text_parser/parse_example_text.h"

#include "vw/common/future_compat.h"
#include "vw/common/string_view.h"
#include "vw/common/text_utils.h"
#include "vw/common/uniform_hash.h"
#include "vw/core/constant.h"
#include "vw/core/global_data.h"
#include "vw/core/hash.h"
#include "vw/core/parse_primitives.h"
#include "vw/core/parser.h"
#include "vw/core/shared_data.h"
#include "vw/core/unique_sort.h"
#include "vw/io/logger.h"

#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
template <bool audit>
class tc_parser
{
private:
  // Parser state
  VW::string_view _line;                 // String being parsed
  size_t _read_idx;                      // Current index into _line
  size_t _anon;                          // Counter for anonymous features
  VW::namespace_index _namespace_index;  // Index of current namespace being parsed

  // Pointers to global configuration data
  std::unordered_map<VW::namespace_index, std::string>* _redefine;
  std::unordered_map<VW::namespace_index, uint64_t>* _affix_features;
  std::unordered_set<VW::namespace_index>* _spelling_features;
  VW::v_array<char> _spelling;
  std::unordered_map<VW::namespace_index, std::vector<std::shared_ptr<VW::details::feature_dict>>>*
      _namespace_dictionaries;
  bool _hash_all;  // if true, hash integer feature indices as strings

  VW::parser* _p;
  VW::example* _ae;  // Parsed data written to this example
  VW::io::logger* _logger;

  // TODO: Currently this function is called by both warning and error conditions. We only log
  //      to warning here though.
  inline FORCE_INLINE void parser_warning(const char* message, VW::string_view var_msg, const char* message2,
      size_t example_number, VW::io::logger& warn_logger)
  {
    // VW::string_view will output the entire view into the output stream.
    // That means if there is a null character somewhere in the range, it will terminate
    // the stringstream at that point! Minor hack to give us the behavior we actually want here (i think)..
    // the alternative is to do what the old code was doing.. str(_line).c_str()...
    // TODO: Find a sane way to handle nulls in the middle of a string (either VW::string_view or substring)
    auto tmp_view = _line.substr(0, _line.find('\0'));
    std::stringstream ss;
    ss << message << var_msg << message2 << "in Example #" << example_number << ": \"" << tmp_view << "\"";

    if (_p->strict_parse)
    {
      // maintain newline behavior
      ss << std::endl;
      THROW_EX(VW::strict_parse_exception, ss.str());
    }
    else { warn_logger.err_warn("{}", ss.str()); }
  }

  inline FORCE_INLINE VW::string_view string_feature_value(VW::string_view sv)
  {
    size_t start_idx = sv.find_first_not_of(" \t\r\n");
    if (start_idx > 0 && start_idx != std::string::npos)
    {
      _read_idx += start_idx;
      sv.remove_prefix(start_idx);
    }

    size_t end_idx = sv.find_first_of(" \t\r\n");
    if (end_idx == std::string::npos) { end_idx = sv.size(); }
    _read_idx += end_idx;
    return sv.substr(0, end_idx);
  }

  inline FORCE_INLINE bool is_feature_value_float(VW::feature_value& float_feature_value)
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      float_feature_value = 1.;
      return true;
    }

    else if (_line[_read_idx] == ':')
    {
      // featureValue --> ':' 'Float'
      ++_read_idx;
      size_t end_read = 0;
      VW::string_view sv = _line.substr(_read_idx);
      float_feature_value = VW::details::parse_float(sv.data(), end_read, sv.data() + sv.size());
      if (end_read == 0) { return false; }
      if (std::isnan(float_feature_value))
      {
        float_feature_value = 0.f;
        parser_warning("Invalid feature value:\"", _line.substr(_read_idx), "\" read as NaN. Replacing with 0.",
            _ae->example_counter, *_logger);
      }
      _read_idx += end_read;
      return true;
    }
    else
    {
      float_feature_value = 0.f;
      // syntax error
      parser_warning("malformed example! '|', ':', space, or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *_logger);
      return true;
    }
  }

  inline FORCE_INLINE VW::string_view read_name()
  {
    size_t name_start = _read_idx;
    while (!(_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == ':' ||
        _line[_read_idx] == '\t' || _line[_read_idx] == '|' || _line[_read_idx] == '\r'))
    {
      ++_read_idx;
    }

    return _line.substr(name_start, _read_idx - name_start);
  }

  inline FORCE_INLINE void maybe_feature()
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      // maybeFeature --> ø
    }
    else
    {
      // maybeFeature --> 'String' FeatureValue

      // Get the feature index
      VW::string_view str_feature_index = read_name();
      VW::feature_index int_feature_index;
      bool feature_index_is_int;

      // Empty feature name is a valid feature name
      // Increment a counter to get an integer feature index
      if (str_feature_index.empty())
      {
        feature_index_is_int = true;
        int_feature_index = _anon++;
      }
      // If _hash_all is set, always hash the feature index as a string
      else if (_hash_all) { feature_index_is_int = false; }
      // Otherwise check if the feature index is an integer or string
      else
      {
        feature_index_is_int = VW::details::is_string_integer(str_feature_index);
        if (feature_index_is_int) { int_feature_index = std::strtoll(str_feature_index.data(), nullptr, 10); }
      }

      // Get the feature value
      VW::string_view str_feature_value;
      VW::feature_value float_feature_value = 0.f;
      bool feature_value_is_float = is_feature_value_float(float_feature_value);

      if (!feature_value_is_float)
      {
        str_feature_value = string_feature_value(_line.substr(_read_idx));
        float_feature_value = 1;
      }
      else if (float_feature_value == 0)
      {
        return;  // dont add 0 valued features to list of features
      }

      // Add the feature
      auto& fs = (*_ae)[_namespace_index];
      if (feature_index_is_int)
      {
        if (feature_value_is_float) { fs.add_feature(int_feature_index, float_feature_value, audit); }
        else { fs.add_feature(int_feature_index, str_feature_value, audit); }
      }
      else
      {
        if (feature_value_is_float) { fs.add_feature(str_feature_index, float_feature_value, audit); }
        else { fs.add_feature(str_feature_index, str_feature_value, audit); }
      }

      // Add affix features
      bool has_affix = _affix_features->find(_namespace_index) != _affix_features->end();
      if (has_affix && !str_feature_index.empty())
      {
        auto& affix_fs = (*_ae)[VW::details::AFFIX_NAMESPACE];
        uint64_t affix = (*_affix_features)[_namespace_index];

        while (affix > 0)
        {
          bool is_prefix = affix & 0x1;
          uint64_t len = (affix >> 1) & 0x7;
          VW::string_view affix_name(str_feature_index);
          if (affix_name.size() > len)
          {
            if (is_prefix) { affix_name.remove_suffix(affix_name.size() - len); }
            else { affix_name.remove_prefix(affix_name.size() - len); }
          }

          VW::feature_index affix_hash = VW::uniform_hash(affix_name, fs.namespace_hash) *
              (VW::details::AFFIX_CONSTANT + (affix & 0xF) * VW::details::QUADRATIC_CONSTANT);

          // Must manually scale feature value by namespace value when using add_feature_raw()
          affix_fs.add_feature_raw(affix_hash, float_feature_value * fs.namespace_value);

          // Must manually add audit string when using add_feature_raw()
          if (audit)
          {
            VW::v_array<char> affix_v;
            if (_namespace_index != VW::details::DEFAULT_NAMESPACE) { affix_v.push_back(_namespace_index); }
            affix_v.push_back(is_prefix ? '+' : '-');
            affix_v.push_back('0' + static_cast<char>(len));
            affix_v.push_back('=');
            affix_v.insert(affix_v.end(), affix_name.begin(), affix_name.end());
            affix_v.push_back('\0');
            affix_fs.add_audit_string(affix_v.begin());
          }
          affix >>= 4;
        }
      }

      // Add spelling features
      if (_spelling_features->find(_namespace_index) != _spelling_features->end())
      {
        auto& spell_fs = (*_ae)[VW::details::SPELLING_NAMESPACE];
        _spelling.clear();
        for (char c : str_feature_index)
        {
          char d = 0;
          if ((c >= '0') && (c <= '9')) { d = '0'; }
          else if ((c >= 'a') && (c <= 'z')) { d = 'a'; }
          else if ((c >= 'A') && (c <= 'Z')) { d = 'A'; }
          else if (c == '.') { d = '.'; }
          else { d = '#'; }
          // if ((spelling.size() == 0) || (spelling.last() != d))
          _spelling.push_back(d);
        }

        VW::string_view spelling_strview(_spelling.data(), _spelling.size());
        VW::feature_index spelling_index = VW::uniform_hash(spelling_strview, fs.namespace_hash);

        // Must manually scale feature value by namespace value when using add_feature_raw()
        spell_fs.add_feature_raw(spelling_index, float_feature_value * fs.namespace_value);

        // Must manually add audit string when using add_feature_raw()
        if (audit)
        {
          VW::v_array<char> spelling_v;
          if (_namespace_index != VW::details::DEFAULT_NAMESPACE)
          {
            spelling_v.push_back(_namespace_index);
            spelling_v.push_back('_');
          }
          spelling_v.insert(spelling_v.end(), spelling_strview.begin(), spelling_strview.end());
          spelling_v.push_back('\0');
          spell_fs.add_audit_string(spelling_v.begin());
        }
      }

      // Add dictionary features
      if (_namespace_dictionaries->find(_namespace_index) != _namespace_dictionaries->end())
      {
        // Heterogeneous lookup not yet implemented in std
        // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r0.html
        const std::string feature_name_str(str_feature_index);
        for (const auto& map : (*_namespace_dictionaries)[_namespace_index])
        {
          const auto& feats_it = map->find(feature_name_str);
          if ((feats_it != map->end()) && (feats_it->second->values.size() > 0))
          {
            const auto& feats = feats_it->second;
            auto& dict_fs = (*_ae)[VW::details::DICTIONARY_NAMESPACE];

            // Manually modify variables inside of features object
            dict_fs.values.insert(dict_fs.values.end(), feats->values.begin(), feats->values.end());
            dict_fs.indices.insert(dict_fs.indices.end(), feats->indices.begin(), feats->indices.end());
            dict_fs.sum_feat_sq += feats->sum_feat_sq;

            // Manually add audit strings
            if (audit)
            {
              for (const auto& id : feats->indices)
              {
                std::stringstream ss;
                ss << fs.namespace_name << '_';
                ss << str_feature_index;
                ss << '=' << id;
                dict_fs.add_audit_string(ss.str());
              }
            }
          }
        }
      }
    }
  }

  inline FORCE_INLINE void name_space_info_value()
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      // nameSpaceInfoValue -->  ø
    }
    else if (_line[_read_idx] == ':')
    {
      // nameSpaceInfoValue --> ':' 'Float'
      ++_read_idx;
      size_t end_read = 0;
      VW::string_view sv = _line.substr(_read_idx);
      float ns_value = VW::details::parse_float(sv.data(), end_read, sv.data() + sv.size());
      if (end_read + _read_idx >= _line.size())
      {
        parser_warning("malformed example! Float expected after : \"", _line.substr(0, _read_idx), "\"",
            _ae->example_counter, *_logger);
      }
      if (std::isnan(ns_value))
      {
        ns_value = 1.f;
        parser_warning("Invalid namespace value:\"", _line.substr(_read_idx), "\" read as NaN. Replacing with 1.",
            _ae->example_counter, *_logger);
      }
      (*_ae)[_namespace_index].namespace_value = ns_value;
      _read_idx += end_read;
    }
    else
    {
      // syntax error
      parser_warning("malformed example! '|',':', space, or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *_logger);
    }
  }

  inline FORCE_INLINE void name_space_info()
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == '|' || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' ||
        _line[_read_idx] == ':' || _line[_read_idx] == '\r')
    {
      // syntax error
      parser_warning("malformed example! String expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *_logger);
    }
    else
    {
      // NameSpaceInfo --> 'String' NameSpaceInfoValue
      VW::string_view name = read_name();
      if (name.empty())
      {
        // syntax error
        parser_warning("malformed example! String expected after : \"", _line.substr(0, _read_idx), "\"",
            _ae->example_counter, *_logger);
      }

      // Check if the namespace is redefined
      auto ns_index = _ae->namespace_string_to_index(name);
      if (_redefine->find(ns_index) != _redefine->end()) { name = (*_redefine)[ns_index]; }

      // Check if there is a wildcard redefine
      else if (_redefine->find(VW::details::WILDCARD_NAMESPACE) != _redefine->end())
      {
        name = (*_redefine)[VW::details::WILDCARD_NAMESPACE];
      }

      // Convert the possible updated name to index
      _namespace_index = _ae->namespace_string_to_index(name);

      name_space_info_value();
    }
  }

  inline FORCE_INLINE void list_features()
  {
    while ((_read_idx < _line.size()) && (_line[_read_idx] == ' ' || _line[_read_idx] == '\t'))
    {
      // listFeatures --> ' ' MaybeFeature ListFeatures
      ++_read_idx;
      maybe_feature();
    }
    if (!(_read_idx >= _line.size() || _line[_read_idx] == '|' || _line[_read_idx] == '\r'))
    {
      // syntax error
      parser_warning("malformed example! '|',space, or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *_logger);
    }
  }

  inline FORCE_INLINE void name_space()
  {
    _anon = 0;
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      // NameSpace --> ListFeatures
      // Check if default namespace is redefined
      if (_redefine->find(VW::details::DEFAULT_NAMESPACE) != _redefine->end())
      {
        auto ns_name = (*_redefine)[VW::details::DEFAULT_NAMESPACE];
        _namespace_index = (*_ae)[ns_name].namespace_hash;
      }
      else { _namespace_index = VW::details::DEFAULT_NAMESPACE; }
      list_features();
    }
    else if (_line[_read_idx] != ':')
    {
      // NameSpace --> NameSpaceInfo ListFeatures
      name_space_info();
      list_features();
    }
    else
    {
      // syntax error
      parser_warning("malformed example! '|',String,space, or EOL expected after : \"", _line.substr(0, _read_idx),
          "\"", _ae->example_counter, *_logger);
    }
  }

  inline FORCE_INLINE void list_name_space()
  {
    while ((_read_idx < _line.size()) && (_line[_read_idx] == '|'))  // ListNameSpace --> '|' NameSpace ListNameSpace
    {
      ++_read_idx;
      name_space();
    }
    if (_read_idx < _line.size() && _line[_read_idx] != '\r')
    {
      // syntax error
      parser_warning("malformed example! '|' or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *_logger);
    }
  }

public:
  tc_parser(VW::string_view line, VW::workspace& all, VW::example* ae) : _line(line)
  {
    if (!_line.empty())
    {
      this->_read_idx = 0;
      this->_p = all.parser_runtime.example_parser.get();
      this->_redefine = &all.feature_tweaks_config.redefine;
      this->_ae = ae;
      this->_affix_features = &all.feature_tweaks_config.affix_features;
      this->_spelling_features = &all.feature_tweaks_config.spelling_features;
      this->_namespace_dictionaries = &all.feature_tweaks_config.namespace_dictionaries;
      this->_logger = &all.logger;
      this->_hash_all = all.parser_runtime.hash_all;
      list_name_space();
    }
    else { ae->is_newline = true; }
  }
};
}  // namespace
void VW::parsers::text::details::substring_to_example(VW::workspace* all, VW::example* ae, VW::string_view example)
{
  if (example.empty()) { ae->is_newline = true; }

  all->parser_runtime.example_parser->lbl_parser.default_label(ae->l);

  size_t bar_idx = example.find('|');

  all->parser_runtime.example_parser->words.clear();
  if (bar_idx != 0)
  {
    VW::string_view label_space(example);
    if (bar_idx != VW::string_view::npos)
    {
      // a little bit iffy since bar_idx is based on example and we're working off label_space
      // but safe as long as this is the first manipulation after the copy
      label_space.remove_suffix(label_space.size() - bar_idx);
    }
    size_t tab_idx = label_space.find('\t');
    if (tab_idx != VW::string_view::npos) { label_space.remove_prefix(tab_idx + 1); }

    VW::tokenize(' ', label_space, all->parser_runtime.example_parser->words);
    if (all->parser_runtime.example_parser->words.size() > 0 &&
        ((all->parser_runtime.example_parser->words.back().data() +
             all->parser_runtime.example_parser->words.back().size()) == (label_space.data() + label_space.size()) ||
            all->parser_runtime.example_parser->words.back().front() ==
                '\''))  // The last field is a tag, so record and strip it off
    {
      VW::string_view tag = all->parser_runtime.example_parser->words.back();
      all->parser_runtime.example_parser->words.pop_back();
      if (tag.front() == '\'') { tag.remove_prefix(1); }
      ae->tag.insert(ae->tag.end(), tag.begin(), tag.end());
    }
  }

  if (!all->parser_runtime.example_parser->words.empty())
  {
    all->parser_runtime.example_parser->lbl_parser.parse_label(ae->l, ae->ex_reduction_features,
        all->parser_runtime.example_parser->parser_memory_to_reuse, all->sd->ldict.get(),
        all->parser_runtime.example_parser->words, all->logger);
  }

  if (bar_idx != VW::string_view::npos)
  {
    if (all->output_config.audit || all->output_config.hash_inv)
    {
      tc_parser<true> parser_line(example.substr(bar_idx), *all, ae);
    }
    else { tc_parser<false> parser_line(example.substr(bar_idx), *all, ae); }
  }
}

size_t VW::parsers::text::details::read_features(io_buf& buf, char*& line, size_t& num_chars)
{
  line = nullptr;
  size_t num_chars_initial = buf.readto(line, '\n');
  if (num_chars_initial < 1) { return num_chars_initial; }
  num_chars = num_chars_initial;
  if (line[0] == '\xef' && num_chars >= 3 && line[1] == '\xbb' && line[2] == '\xbf')
  {
    line += 3;
    num_chars -= 3;
  }
  if (num_chars > 0 && line[num_chars - 1] == '\n') { num_chars--; }
  if (num_chars > 0 && line[num_chars - 1] == '\r') { num_chars--; }
  return num_chars_initial;
}

int VW::parsers::text::read_features_string(VW::workspace* all, io_buf& buf, VW::multi_ex& examples)
{
  char* line;
  size_t num_chars;
  // This function consumes input until it reaches a '\n' then it walks back the '\n' and '\r' if it exists.
  size_t num_bytes_consumed = details::read_features(buf, line, num_chars);
  if (num_bytes_consumed < 1)
  {
    // This branch will get hit once we have reached EOF of the input device.
    return static_cast<int>(num_bytes_consumed);
  }

  VW::string_view example(line, num_chars);
  // If this example is empty substring_to_example will mark it as a newline example.
  details::substring_to_example(all, examples[0], example);

  return static_cast<int>(num_bytes_consumed);
}

void VW::parsers::text::read_line(VW::workspace& all, example* ex, VW::string_view line)
{
  while (line.size() > 0 && line.back() == '\n') { line.remove_suffix(1); }
  details::substring_to_example(&all, ex, line);
}

void VW::parsers::text::read_lines(VW::workspace* all, VW::string_view lines_view, VW::multi_ex& examples)
{
  std::vector<VW::string_view> lines;
  VW::tokenize('\n', lines_view, lines);
  for (size_t i = 0; i < lines.size(); i++)
  {
    // Check if a new empty example needs to be added.
    if (examples.size() < i + 1) { examples.push_back(&VW::get_unused_example(all)); }
    read_line(*all, examples[i], lines[i]);
  }
}
