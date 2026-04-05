#include "cmdline_parser.h"

#include <iomanip>
#include <iostream>

namespace vtb {

void CmdlineParser::add_argument(std::string_view long_n,
                                 std::string_view short_n,
                                 std::string_view desc, bool req,
                                 std::string_view default_v) {
   arguments_.push_back({std::string(long_n), std::string(short_n),
                         std::string(desc), req, false,
                         std::string(default_v)});
}

void CmdlineParser::parse(int argc, char** argv) {
   int start_index = argc;
   bool separator_found = false;

   // 1. Check for the DPDK separator
   for (int i = 1; i < argc; ++i) {
      if (std::string_view(argv[i]) == "--") {
         start_index = i + 1;
         separator_found = true;
         break;
      }
   }

   // 2. Identify a "Missing Separator" syntax error
   // If user provided any arguments but no '--', it's a usage error.
   if (!separator_found && argc > 1) {
      throw std::runtime_error(
          "Invalid Command Line: DPDK separator '--' is missing.\n"
          "Proper Usage: [EAL options] -- [APP options]");
   }

   // 3. Process the application arguments
   for (int i = start_index; i < argc; ++i) {
      std::string_view token(argv[i]);
      bool matched = false;
      for (auto& arg : arguments_) {
         if (token == arg.long_name || token == arg.short_name) {
            arg.found = true;
            matched = true;

            // Handle value extraction
            if (i + 1 < argc && argv[i + 1][0] != '-') {
               arg.value = argv[++i];
            } else if (arg.required &&
                       arg.value
                           .empty()) {  // Use your 'expects value' flag here
               throw std::runtime_error("Argument " + std::string(token) +
                                        " requires a value.");
            }
            break;
         }
      }

      if (!matched) {
         throw std::runtime_error("Unknown argument '" + std::string(token) +
                                  "'");
      }
   }

   // 4. Final validation for mandatory fields
   for (const auto& arg : arguments_) {
      if (arg.required && !arg.found) {
         throw std::runtime_error("Required argument missing: " +
                                  arg.long_name);
      }
   }
}

void CmdlineParser::print_usage() const {
   std::cout << "\nUsage: ./app [EAL options] -- [APP options]\n\n";
   std::cout << std::left << std::setw(25) << "Long Flag" << std::setw(10)
             << "Short" << std::setw(55) << "Description" << "Required\n";
   std::cout << std::string(100, '-') << "\n";

   for (const auto& arg : arguments_) {
      std::cout << std::left << std::setw(25) << arg.long_name << std::setw(10)
                << arg.short_name << std::setw(55) << arg.description
                << (arg.required ? "Yes" : "No") << "\n";
   }
   std::cout << std::endl;
}

template <>
std::string CmdlineParser::get<std::string>(std::string_view name) const {
   for (const auto& arg : arguments_) {
      if (arg.long_name == name || arg.short_name == name) return arg.value;
   }
   return "";
}

template <>
int CmdlineParser::get<int>(std::string_view name) const {
   std::string s = get<std::string>(name);
   return s.empty() ? 0 : std::stoi(s);
}

template <>
uint64_t CmdlineParser::get<uint64_t>(std::string_view name) const {
   std::string s = get<std::string>(name);
   return s.empty() ? 0 : std::stoull(s, nullptr, 0);
}

template <>
bool CmdlineParser::get<bool>(std::string_view name) const {
   std::string s = get<std::string>(name);
   return (s == "true" || s == "1" || s == "yes" || s == "-h" || s == "--help");
}

}  // namespace vtb
