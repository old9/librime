//
// Copyleft RIME Developers
// License: GPLv3
//
// 2013-10-17 GONG Chen <chen.sst@gmail.com>
//
#ifndef RIME_MODULE_H_
#define RIME_MODULE_H_

#include <map>
#include <string>
#include <unordered_set>
#include <rime/common.h>

struct rime_module_t;
typedef struct rime_module_t RimeModule;

namespace rime {

class ModuleManager {
 public:
  // module is supposed to be a pointer to static variable
  void Register(const std::string& name, RimeModule* module);
  RimeModule* Find(const std::string& name);

  void LoadModule(RimeModule* module);
  void UnloadModules();

  static ModuleManager& instance();

 private:
  ModuleManager() {}

  // module registry
  using ModuleMap = std::map<std::string, RimeModule*>;
  ModuleMap map_;
  // set of loaded modules
  std::unordered_set<RimeModule*> loaded_;
};

}  // namespace rime

#endif  // RIME_MODULE_H_
