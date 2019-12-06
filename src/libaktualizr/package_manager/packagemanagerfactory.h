#ifndef PACKAGEMANAGERFACTORY_H_
#define PACKAGEMANAGERFACTORY_H_

#include "bootloader/bootloader_config.h"
#include "package_manager/packagemanagerconfig.h"
#include "package_manager/packagemanagerinterface.h"
#include "storage/invstorage.h"

class PackageManagerFactory {
 public:
  static std::shared_ptr<PackageManagerInterface> makePackageManager(const PackageConfig& pconfig,
                                                                     const BootloaderConfig& bconfig,
                                                                     const std::shared_ptr<INvStorage>& storage,
                                                                     const std::shared_ptr<HttpInterface>& http);
};

#endif  // PACKAGEMANAGERFACTORY_H_
