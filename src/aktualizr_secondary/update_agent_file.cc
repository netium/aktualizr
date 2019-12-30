#include "update_agent_file.h"
#include "logging/logging.h"
#include "uptane/manifest.h"

// TODO: this is an initial version of a file update on Secondary
bool FileUpdateAgent::isTargetSupported(const Uptane::Target& target) const { return target.type() != "OSTREE"; }

bool FileUpdateAgent::getInstalledImageInfo(Uptane::InstalledImageInfo& installed_image_info) const {
  installed_image_info.name = _target_name;
  // TODO: proper verification, file exists, file open/read errors, etc
  if (!_target_filepath.empty()) {
    auto file_content = Utils::readFile(_target_filepath);
    installed_image_info.len = file_content.size();
    installed_image_info.hash = Uptane::ManifestIssuer::generateVersionHashStr(file_content);
  } else {
    // TODO: fake package manager
    const std::string fake_pacman_case = "fake_pacman";
    installed_image_info.len = fake_pacman_case.size();
    installed_image_info.hash = Uptane::ManifestIssuer::generateVersionHashStr(fake_pacman_case);
  }

  return true;
}

bool FileUpdateAgent::download(const Uptane::Target& target, const std::string& data) {
  auto target_hashes = target.hashes();
  if (target_hashes.size() == 0) {
    LOG_ERROR << "No hash found in the target metadata: " << target.filename();
    return false;
  }

  try {
    auto received_image_data_hash = Uptane::ManifestIssuer::generateVersionHash(data);

    if (!target.MatchHash(received_image_data_hash)) {
      LOG_ERROR << "The received image data hash doesn't match the hash specified in the target metadata,"
                   " hash type: "
                << target_hashes[0].TypeString();
      return false;
    }

    if (!_target_filepath.empty()) {
      Utils::writeFile(_target_filepath, data);
    }

  } catch (const std::exception& exc) {
    LOG_ERROR << "Failed to generate a hash of the received image data: " << exc.what();
    return false;
  }
  return true;
}

data::ResultCode::Numeric FileUpdateAgent::install(const Uptane::Target& target) {
  (void)target;
  return data::ResultCode::Numeric::kOk;
}

data::InstallationResult FileUpdateAgent::applyPendingInstall(const Uptane::Target& target) {
  (void)target;
  return data::InstallationResult(data::ResultCode::Numeric::kInternalError,
                                  "Applying of the pending updates are not supported by the file update agent");
}
