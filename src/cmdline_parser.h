#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vtb {

struct CmdlineArgumentFormat {
   std::string long_name;
   std::string short_name;
   std::string description;
   bool required;
   bool found;
   std::string value;
};

class CmdlineParser {
public:
   CmdlineParser() = default;

   void add_argument(std::string_view long_n, std::string_view short_n,
                     std::string_view desc, bool req = false,
                     std::string_view default_v = "");

   void parse(int argc, char** argv);
   void print_usage() const;

   template <typename T>
   T get(std::string_view name) const;

private:
   std::vector<CmdlineArgumentFormat> arguments_;
};

template <>
std::string CmdlineParser::get<std::string>(std::string_view name) const;

template <>
int CmdlineParser::get<int>(std::string_view name) const;

template <>
uint64_t CmdlineParser::get<uint64_t>(std::string_view name) const;

template <>
bool CmdlineParser::get<bool>(std::string_view name) const;

}  // namespace vtb
