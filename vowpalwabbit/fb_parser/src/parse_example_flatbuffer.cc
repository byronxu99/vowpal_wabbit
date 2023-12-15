// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/fb_parser/parse_example_flatbuffer.h"

#include "vw/core/action_score.h"
#include "vw/core/best_constant.h"
#include "vw/core/cb.h"
#include "vw/core/constant.h"
#include "vw/core/global_data.h"
#include "vw/core/parser.h"
#include "vw/core/parse_primitives.h"

#include <cfloat>
#include <fstream>
#include <iostream>

namespace VW
{
namespace parsers
{
namespace flatbuffer
{
int flatbuffer_to_examples(VW::workspace* all, io_buf& buf, VW::multi_ex& examples)
{
  return static_cast<int>(all->parser_runtime.flat_converter->parse_examples(all, buf, examples));
}

const VW::parsers::flatbuffer::ExampleRoot* parser::data() { return _data; }

bool parser::parse(io_buf& buf, uint8_t* buffer_pointer)
{
  if (buffer_pointer)
  {
    _flatbuffer_pointer = buffer_pointer;

    _data = VW::parsers::flatbuffer::GetSizePrefixedExampleRoot(_flatbuffer_pointer);
    return true;
  }

  char* line = nullptr;
  auto len = buf.buf_read(line, sizeof(uint32_t));

  if (len < sizeof(uint32_t)) { return false; }

  _object_size = flatbuffers::ReadScalar<flatbuffers::uoffset_t>(line);

  // read one object, object size defined by the read prefix
  buf.buf_read(line, _object_size);

  _flatbuffer_pointer = reinterpret_cast<uint8_t*>(line);
  _data = VW::parsers::flatbuffer::GetExampleRoot(_flatbuffer_pointer);
  return true;
}

void parser::process_collection_item(VW::workspace* all, VW::multi_ex& examples)
{
  // new example/multi example object to process from collection
  if (_data->example_obj_as_ExampleCollection()->is_multiline())
  {
    _active_multi_ex = true;
    _multi_example_object = _data->example_obj_as_ExampleCollection()->multi_examples()->Get(_example_index);
    parse_multi_example(all, examples[0], _multi_example_object);
    // read from active collection
    _example_index++;
    if (_example_index == _data->example_obj_as_ExampleCollection()->multi_examples()->size())
    {
      _example_index = 0;
      _active_collection = false;
    }
  }
  else
  {
    const auto ex = _data->example_obj_as_ExampleCollection()->examples()->Get(_example_index);
    parse_example(all, examples[0], ex);
    _example_index++;
    if (_example_index == _data->example_obj_as_ExampleCollection()->examples()->size())
    {
      _example_index = 0;
      _active_collection = false;
    }
  }
}

bool parser::parse_examples(VW::workspace* all, io_buf& buf, VW::multi_ex& examples, uint8_t* buffer_pointer)
{
  if (_active_multi_ex)
  {
    parse_multi_example(all, examples[0], _multi_example_object);
    return true;
  }

  if (_active_collection)
  {
    process_collection_item(all, examples);
    return true;
  }
  else
  {
    // new object to be read from file
    if (!parse(buf, buffer_pointer)) { return false; }

    switch (_data->example_obj_type())
    {
      case VW::parsers::flatbuffer::ExampleType_Example:
      {
        const auto example = _data->example_obj_as_Example();
        parse_example(all, examples[0], example);
        return true;
      }
      break;
      case VW::parsers::flatbuffer::ExampleType_MultiExample:
      {
        _multi_example_object = _data->example_obj_as_MultiExample();
        _active_multi_ex = true;
        parse_multi_example(all, examples[0], _multi_example_object);
        return true;
      }
      break;
      case VW::parsers::flatbuffer::ExampleType_ExampleCollection:
      {
        _active_collection = true;
        process_collection_item(all, examples);
        return true;
      }
      break;

      default:
        break;
    }
    return false;
  }
}

void parser::parse_example(VW::workspace* all, example* ae, const Example* eg)
{
  all->parser_runtime.example_parser->lbl_parser.default_label(ae->l);
  ae->is_newline = eg->is_newline();
  parse_flat_label(all->sd.get(), ae, eg, all->logger);

  if (flatbuffers::IsFieldPresent(eg, Example::VT_TAG))
  {
    VW::string_view tag(eg->tag()->c_str());
    ae->tag.insert(ae->tag.end(), tag.begin(), tag.end());
  }

  for (const auto& ns : *(eg->namespaces())) { parse_namespaces(all, ae, ns); }
}

void parser::parse_multi_example(VW::workspace* all, example* ae, const MultiExample* eg)
{
  all->parser_runtime.example_parser->lbl_parser.default_label(ae->l);
  if (_multi_ex_index >= eg->examples()->size())
  {
    // done with multi example, send a newline example and reset
    ae->is_newline = true;
    _multi_ex_index = 0;
    _active_multi_ex = false;
    _multi_example_object = nullptr;
    return;
  }

  parse_example(all, ae, eg->examples()->Get(_multi_ex_index));
  _multi_ex_index++;
}

VW::features& get_or_create_namespace(VW::example& ae, const Namespace* ns)
{
  if (flatbuffers::IsFieldPresent(ns, Namespace::VT_NAME))
  {
    // String name available
    auto ns_name = ns->name()->str();
    return ae[ns_name];
  }
  else if (flatbuffers::IsFieldPresent(ns, Namespace::VT_FULL_HASH))
  {
    // Full hash available
    auto ns_index = ns->full_hash();
    return ae[ns_index];
  }
  else if (flatbuffers::IsFieldPresent(ns, Namespace::VT_HASH))
  {
    // Only 8 bit hash available
    auto ns_index = ns->hash();
    return ae[ns_index];
  }

  THROW("Either full_hash, name, or hash field must be specified to get the namespace index.");
}

void parser::parse_namespaces(VW::workspace* all, example* ae, const Namespace* ns)
{
  auto& fs = get_or_create_namespace(*ae, ns);
  for (const auto& feature : *(ns->features()))
  {
    parse_features(all, fs, feature, ns->name(), all->output_config.audit || all->output_config.hash_inv);
  }
}

void parser::parse_features(VW::workspace* all, features& fs, const Feature* feature, const flatbuffers::String* ns_name, bool audit)
{
  VW::feature_value fv = flatbuffers::IsFieldPresent(feature, Feature::VT_VALUE) ? feature->value() : 1.0f;
  if (flatbuffers::IsFieldPresent(feature, Feature::VT_NAME))
  {
    // If hash_all is specified, then we always treat the feature name as a string
    if (all->parser_runtime.hash_all)
    {
      fs.add_feature(feature->name()->str(), fv, audit);
    }
    // Otherwise we check if the string is an integer
    else
    {
      if (VW::details::is_string_integer(feature->name()->str()))
      {
        VW::feature_index fi = std::strtoll(feature->name()->c_str(), nullptr, 10);
        fs.add_feature(fi, fv, audit);
      }
      else { fs.add_feature(feature->name()->str(), fv); }
    }
  }
  else
  {
    fs.add_feature_raw(feature->hash(), fv);
    if (audit && ns_name != nullptr)
    {
      fs.add_audit_string(ns_name->str());
    }
  }
}

void parser::parse_flat_label(shared_data* sd, example* ae, const Example* eg, VW::io::logger& logger)
{
  switch (eg->label_type())
  {
    case Label_SimpleLabel:
    {
      const SimpleLabel* simple_lbl = static_cast<const SimpleLabel*>(eg->label());
      parse_simple_label(sd, &(ae->l), &(ae->ex_reduction_features), simple_lbl);
      break;
    }
    case Label_CBLabel:
    {
      const CBLabel* cb_label = static_cast<const CBLabel*>(eg->label());
      parse_cb_label(&(ae->l), cb_label);
      break;
    }
    case Label_CCBLabel:
    {
      const CCBLabel* ccb_label = static_cast<const CCBLabel*>(eg->label());
      parse_ccb_label(&(ae->l), ccb_label);
      break;
    }
    case Label_CB_EVAL_Label:
    {
      auto cb_eval_label = static_cast<const CB_EVAL_Label*>(eg->label());
      parse_cb_eval_label(&(ae->l), cb_eval_label);
      break;
    }
    case Label_CS_Label:
    {
      auto cs_label = static_cast<const CS_Label*>(eg->label());
      parse_cs_label(&(ae->l), cs_label);
      break;
    }
    case Label_MultiClass:
    {
      auto mc_label = static_cast<const MultiClass*>(eg->label());
      parse_mc_label(sd, &(ae->l), mc_label, logger);
      break;
    }
    case Label_MultiLabel:
    {
      auto multi_label = static_cast<const MultiLabel*>(eg->label());
      parse_multi_label(&(ae->l), multi_label);
      break;
    }
    case Label_Slates_Label:
    {
      auto slates_label = static_cast<const Slates_Label*>(eg->label());
      parse_slates_label(&(ae->l), slates_label);
      break;
    }
    case Label_ContinuousLabel:
    {
      auto continuous_label = static_cast<const ContinuousLabel*>(eg->label());
      parse_continuous_action_label(&(ae->l), continuous_label);
      break;
    }
    case Label_NONE:
      break;
    default:
      THROW("Label type in Flatbuffer not understood");
  }
}

}  // namespace flatbuffer
}  // namespace parsers
}  // namespace VW
