//
// Copyleft RIME Developers
// License: GPLv3
//
// 2013-04-22 GONG Chen <chen.sst@gmail.com>
//
#ifndef RIME_USER_DB_RECOVERY_TASK_H_
#define RIME_USER_DB_RECOVERY_TASK_H_

#include <rime/common.h>
#include <rime/deployer.h>

namespace rime {

class Db;

class UserDbRecoveryTask : public DeploymentTask {
 public:
  explicit UserDbRecoveryTask(shared_ptr<Db> db);
  bool Run(Deployer* deployer);

 protected:
  void RestoreUserDataFromSnapshot(Deployer* deployer);

  shared_ptr<Db> db_;
};

class UserDbRecoveryTaskComponent : public UserDbRecoveryTask::Component {
 public:
  UserDbRecoveryTask* Create(TaskInitializer arg);
};

}  // namespace rime

#endif  // RIME_USER_DB_RECOVERY_TASK_H_
