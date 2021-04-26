/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <util/cmdline.h>
#include <sstream>

std::string verification_file;

cmdlinet::~cmdlinet()
{
  clear();
}

void cmdlinet::clear()
{
  vm.clear();
  args.clear();
  options_map.clear();
}

bool cmdlinet::isset(const char *option) const
{
  return vm.count(option);
}

const std::list<std::string> &cmdlinet::get_values(const char *option) const
{
  auto value = options_map.find(option);
  return value->second;
}

const char *cmdlinet::getval(const char *option) const
{
  auto value = options_map.find(option);
  if(value == options_map.end())
  {
    return (const char *)nullptr;
  }
  if(value->second.empty())
  {
    return (const char *)nullptr;
  }
  return value->second.front().c_str();
}

bool cmdlinet::parse(
  int argc,
  const char **argv,
  const struct group_opt_templ *opts)
{
  clear();
  for(unsigned int i = 0; opts[i].groupname != "end"; i++)
  {
    auto desptr =
      new boost::program_options::options_description(opts[i].groupname);
    auto groupoptions = opts[i].options;
    for(auto it = groupoptions.begin(); it != groupoptions.end(); ++it)
    {
      if(!it->type_default_value)
      {
        desptr->add_options()(it->optstring, it->description);
      }
      else
      {
        desptr->add_options()(
          it->optstring, it->type_default_value, it->description);
      }
    }
    cmdline_options.add(*desptr);
  }
  boost::program_options::positional_options_description p;
  p.add("input-file", -1);
  try
  {
    boost::program_options::store(
      boost::program_options::command_line_parser(argc, argv)
        .options(cmdline_options)
        .positional(p)
        .run(),
      vm);
  }
  catch(std::exception &e)
  {
    std::cerr << "ESBMC error: " << e.what() << "\n";
    return true;
  }

  if(vm.count("input-file"))
  {
    args = vm["input-file"].as<std::vector<std::string>>();
    verification_file = args.back();
  }
  for(const auto &it : vm)
  {
    std::list<std::string> res;
    auto option_name = it.first;
    auto &value = vm[option_name].value();
    if(auto v = boost::any_cast<int>(&value))
    {
      res.emplace_front(std::to_string(*v));
    }
    else if(auto v = boost::any_cast<std::string>(&value))
    {
      res.emplace_front(*v);
    }
    else
    {
      auto src = vm[option_name].as<std::vector<std::string>>();
      res.assign(src.begin(), src.end());
    }
    auto result =
      options_map.insert(options_mapt::value_type(option_name, res));
  }
  return false;
}
