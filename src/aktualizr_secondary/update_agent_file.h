#ifndef AKTUALIZR_SECONDARY_UPDATE_AGENT_FILE_H
#define AKTUALIZR_SECONDARY_UPDATE_AGENT_FILE_H

#include "update_agent.h"

class FileUpdateAgent : public UpdateAgent {
 public:
  FileUpdateAgent(std::string target_name, std::string target_filepath = "")
      : _target_name(std::move(target_name)), _target_filepath(std::move(target_filepath)) {}

 public:
  bool isTargetSupported(const Uptane::Target& target) const override;
  bool getInstalledImageInfo(Uptane::InstalledImageInfo& installed_image_info) const override;
  bool download(const Uptane::Target& target, const std::string& data) override;
  data::ResultCode::Numeric install(const Uptane::Target& target) override;
  data::InstallationResult applyPendingInstall(const Uptane::Target& target) override;

 private:
  const std::string _target_name;
  const std::string _target_filepath;
};

#endif  // AKTUALIZR_SECONDARY_UPDATE_AGENT_FILE_H
