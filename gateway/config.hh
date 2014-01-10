#ifndef __NSHADER_CONFIG_HH__
#define __NSHADER_CONFIG_HH__

#include <unordered_map>
#include <string>

namespace nshader {

extern std::unordered_map<std::string, unsigned> config_unsigned;
bool load_config(const char* filename);

}

#endif
