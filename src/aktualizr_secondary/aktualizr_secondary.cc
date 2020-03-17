#include "aktualizr_secondary.h"

#include "crypto/keymanager.h"
#include "logging/logging.h"
#include "update_agent.h"
#include "uptane/manifest.h"
#include "utilities/utils.h"
#include "handlermap.h"

#include <netinet/tcp.h>
#include <arpa/inet.h>


#include <iostream>

#include <sys/types.h>
#include <memory>

AktualizrSecondary::AktualizrSecondary(AktualizrSecondaryConfig config, std::shared_ptr<INvStorage> storage,
                                       std::shared_ptr<KeyManager> key_mngr, std::shared_ptr<UpdateAgent> update_agent)
    : config_(std::move(config)),
      storage_(std::move(storage)),
      keys_(std::move(key_mngr)),
      update_agent_(std::move(update_agent)),
      handler_map_(std::make_shared<HandlerMap>()) {
  uptaneInitialize();
  manifest_issuer_ = std::make_shared<Uptane::ManifestIssuer>(keys_, ecu_serial_);
  initHandlerMap();
  initPendingTargetIfAny();

  if (hasPendingUpdate()) {
    // TODO(OTA-4545): refactor this to make it simpler as we don't need to persist/store
    // an installation status of each ECU but store it just for a given secondary ECU
    std::vector<Uptane::Target> installed_versions;
    boost::optional<Uptane::Target> pending_target;
    storage_->loadInstalledVersions(ecu_serial_.ToString(), nullptr, &pending_target);

    if (!!pending_target) {
      data::InstallationResult install_res =
          data::InstallationResult(data::ResultCode::Numeric::kUnknown, "Unknown installation error");
      LOG_INFO << "Pending update found; attempting to apply it. Target hash: " << pending_target->sha256Hash();

      install_res = update_agent_->applyPendingInstall(*pending_target);

      if (install_res.result_code != data::ResultCode::Numeric::kNeedCompletion) {
        storage_->saveEcuInstallationResult(ecu_serial_, install_res);

        if (install_res.success) {
          LOG_INFO << "Pending update has been successfully applied: " << pending_target->sha256Hash();
          storage_->saveInstalledVersion(ecu_serial_.ToString(), *pending_target, InstalledVersionUpdateMode::kCurrent);
        } else {
          LOG_ERROR << "Application of the pending update has failed: (" << install_res.result_code.toString() << ")"
                    << install_res.description;
          storage_->saveInstalledVersion(ecu_serial_.ToString(), *pending_target, InstalledVersionUpdateMode::kNone);
        }

        director_repo_.dropTargets(*storage_);
      } else {
        LOG_INFO << "Pending update hasn't been applied because a reboot hasn't been detected";
      }
    }
  }
}

Uptane::EcuSerial AktualizrSecondary::getSerial() const { return ecu_serial_; }

Uptane::HardwareIdentifier AktualizrSecondary::getHwId() const { return hardware_id_; }

PublicKey AktualizrSecondary::getPublicKey() const { return keys_->UptanePublicKey(); }

Uptane::Manifest AktualizrSecondary::getManifest() const {
  Uptane::InstalledImageInfo installed_image_info;
  Uptane::Manifest manifest;
  if (update_agent_->getInstalledImageInfo(installed_image_info)) {
    manifest = manifest_issuer_->assembleAndSignManifest(installed_image_info);
  }

  return manifest;
}

int32_t AktualizrSecondary::getRootVersion(bool director) const {
  std::string root_meta;
  if (!storage_->loadLatestRoot(&root_meta,
                                (director) ? Uptane::RepositoryType::Director() : Uptane::RepositoryType::Image())) {
    LOG_ERROR << "Could not load Root metadata";
    return -1;
  }

  return Uptane::extractVersionUntrusted(root_meta);
}

bool AktualizrSecondary::putRoot(const std::string& root, bool director) {
  (void)root;
  (void)director;
  LOG_ERROR << "putRootResp is not implemented yet";
  return false;
}

bool AktualizrSecondary::putMetadata(const Metadata& metadata) { return doFullVerification(metadata); }

bool AktualizrSecondary::sendFirmware(const std::string& firmware) {
  if (!pending_target_.IsValid()) {
    LOG_ERROR << "Aborting image download/receiving; no valid target found.";
    return false;
  }

  if (!update_agent_->download(pending_target_, firmware)) {
    LOG_ERROR << "Failed to pull/store an update data";
    pending_target_ = Uptane::Target::Unknown();
    return false;
  }

  return true;
}

data::ResultCode::Numeric AktualizrSecondary::install(const std::string& target_name) {
  if (!pending_target_.IsValid()) {
    LOG_ERROR << "Aborting target image installation; no valid target found.";
    return data::ResultCode::Numeric::kInternalError;
  }

  if (pending_target_.filename() != target_name) {
    LOG_ERROR << "name of the target to install and a name of the pending target do not match";
    return data::ResultCode::Numeric::kInternalError;
  }

  auto install_result = update_agent_->install(pending_target_);

  switch (install_result) {
    case data::ResultCode::Numeric::kOk: {
      storage_->saveInstalledVersion(ecu_serial_.ToString(), pending_target_, InstalledVersionUpdateMode::kCurrent);
      pending_target_ = Uptane::Target::Unknown();
      LOG_INFO << "The target has been successfully installed: " << target_name;
      break;
    }
    case data::ResultCode::Numeric::kNeedCompletion: {
      storage_->saveInstalledVersion(ecu_serial_.ToString(), pending_target_, InstalledVersionUpdateMode::kPending);
      LOG_INFO << "The target has been successfully installed, but a reboot is required to be applied: " << target_name;
      break;
    }
    default: { LOG_INFO << "Failed to install the target: " << target_name; }
  }

  return install_result;
}

void AktualizrSecondary::completeInstall() { update_agent_->completeInstall(); }

bool AktualizrSecondary::doFullVerification(const Metadata& metadata) {
  // 5.4.4.2. Full verification  https://uptane.github.io/uptane-standard/uptane-standard.html#metadata_verification

  // 1. Load and verify the current time or the most recent securely attested time.
  //
  //    We trust the time that the given system/OS/ECU provides, In ECU We Trust :)
  TimeStamp now(TimeStamp::Now());

  // 2. Download and check the Root metadata file from the Director repository, following the procedure in
  // Section 5.4.4.3. DirectorRepository::updateMeta() method implements this verification step, certain steps are
  // missing though. see the method source code for details

  // 3. NOT SUPPORTED: Download and check the Timestamp metadata file from the Director repository, following the
  // procedure in Section 5.4.4.4.
  // 4. NOT SUPPORTED: Download and check the Snapshot metadata file from the Director repository, following the
  // procedure in Section 5.4.4.5.
  //
  // 5. Download and check the Targets metadata file from the Director repository, following the procedure in
  // Section 5.4.4.6. DirectorRepository::updateMeta() method implements this verification step
  //
  // The following steps of the Director's Targets metadata verification are missing in DirectorRepository::updateMeta()
  //  6. If checking Targets metadata from the Director repository, verify that there are no delegations.
  //  7. If checking Targets metadata from the Director repository, check that no ECU identifier is represented more
  //  than once.
  if (!director_repo_.updateMeta(*storage_, metadata)) {
    LOG_ERROR << "Failed to update Director metadata: " << director_repo_.getLastException().what();
    return false;
  }

  // 6. Download and check the Root metadata file from the Image repository, following the procedure in Section 5.4.4.3.
  // 7. Download and check the Timestamp metadata file from the Image repository, following the procedure in
  // Section 5.4.4.4.
  // 8. Download and check the Snapshot metadata file from the Image repository, following the procedure in
  // Section 5.4.4.5.
  // 9. Download and check the top-level Targets metadata file from the Image repository, following the procedure in
  // Section 5.4.4.6.
  if (!image_repo_.updateMeta(*storage_, metadata)) {
    LOG_ERROR << "Failed to update Image repo metadata: " << image_repo_.getLastException().what();
    return false;
  }

  // 10. Verify that Targets metadata from the Director and Image repositories match.
  if (!director_repo_.matchTargetsWithImageTargets(*(image_repo_.getTargets()))) {
    LOG_ERROR << "Targets metadata from the Director and Image repositories DOES NOT match ";
    return false;
  }

  auto targetsForThisEcu = director_repo_.getTargets(getSerial(), getHwId());

  if (targetsForThisEcu.size() != 1) {
    LOG_ERROR << "Invalid number of targets (should be 1): " << targetsForThisEcu.size();
    return false;
  }

  if (!update_agent_->isTargetSupported(targetsForThisEcu[0])) {
    LOG_ERROR << "The given target type is not supported: " << targetsForThisEcu[0].type();
    return false;
  }

  pending_target_ = targetsForThisEcu[0];

  return true;
}

void AktualizrSecondary::uptaneInitialize() {
  if (keys_->generateUptaneKeyPair().size() == 0) {
    throw std::runtime_error("Failed to generate uptane key pair");
  }

  // from uptane/initialize.cc but we only take care of our own serial/hwid
  EcuSerials ecu_serials;

  if (storage_->loadEcuSerials(&ecu_serials)) {
    ecu_serial_ = ecu_serials[0].first;
    hardware_id_ = ecu_serials[0].second;
    return;
  }

  std::string ecu_serial_local = config_.uptane.ecu_serial;
  if (ecu_serial_local.empty()) {
    ecu_serial_local = keys_->UptanePublicKey().KeyId();
  }

  std::string ecu_hardware_id = config_.uptane.ecu_hardware_id;
  if (ecu_hardware_id.empty()) {
    ecu_hardware_id = Utils::getHostname();
    if (ecu_hardware_id == "") {
      throw std::runtime_error("Failed to define ECU hardware ID");
    }
  }

  ecu_serials.emplace_back(Uptane::EcuSerial(ecu_serial_local), Uptane::HardwareIdentifier(ecu_hardware_id));
  storage_->storeEcuSerials(ecu_serials);
  ecu_serial_ = ecu_serials[0].first;
  hardware_id_ = ecu_serials[0].second;

  // this is a way to find out and store a value of the target name that is installed
  // at the initial/provisioning stage and included into a device manifest
  // i.e. 'filepath' field or ["signed"]["installed_image"]["filepath"]
  // this value must match the value pushed to the backend during the bitbaking process,
  // specifically, at its ostree push phase and is equal to
  // GARAGE_TARGET_NAME ?= "${OSTREE_BRANCHNAME}" which in turn is equal to OSTREE_BRANCHNAME ?= "${SOTA_HARDWARE_ID}"
  // therefore, by default GARAGE_TARGET_NAME == OSTREE_BRANCHNAME == SOTA_HARDWARE_ID
  // If there is no match then the backend/UI will not render/highlight currently installed version at all/correctly
  storage_->importInstalledVersions(config_.import.base_path);
}

void AktualizrSecondary::initPendingTargetIfAny() {
  if (!director_repo_.checkMetaOffline(*storage_)) {
    LOG_INFO << "No valid and pending Director targets to be applied";
    return;
  }

  auto targetsForThisEcu = director_repo_.getTargets(ecu_serial_, hardware_id_);

  if (targetsForThisEcu.size() != 1) {
    LOG_ERROR << "Invalid number of targets (should be 1): " << targetsForThisEcu.size();
    return;
  }

  if (!update_agent_->isTargetSupported(targetsForThisEcu[0])) {
    LOG_ERROR << "The given target type is not supported: " << targetsForThisEcu[0].type();
    return;
  }

  LOG_INFO << "There is a valid and pending Director target to be applied";
  pending_target_ = targetsForThisEcu[0];
}

data::ResultCode::Numeric AktualizrSecondary::install(const Uptane::Target& target_name) {

  (void)target_name;

  return data::ResultCode::Numeric::kOk;
}


int download_file(const std::string& ip, uint16_t port, size_t file_size, const std::string& dest_file);

Asn1Message::Ptr AktualizrSecondary::download(Asn1Message::Ptr msg) {
  LOG_INFO << "?????????????????????? Got download message";

  auto request = msg->installReq();

  //handler = impl_.handler["download"]
  download_file("127.0.0.1", 8333,  1048576,  "target.file");


  //auto install_result = impl_.install();
  auto install_result = data::ResultCode::Numeric::kOk;
  Asn1Message::Ptr resp = Asn1Message::Empty();
  resp->present(AKIpUptaneMes_PR_installResp);
  auto response_message = resp->installResp();
  response_message->result = static_cast<AKInstallationResultCode_t>(install_result);

  return resp;
}

void AktualizrSecondary::initHandlerMap() {
  using std::placeholders::_1;
  HandlerMap::Handler hdlr = std::bind(&AktualizrSecondary::download, this, _1);

  handler_map_->registerHandler(AKIpUptaneMes_PR_downloadFileReq, hdlr);
}

int download_file(const std::string& ip, uint16_t port, size_t file_size, const std::string& dest_file) {
  std::cout << "Downloading file from " << ip << ":" << port << std::endl;
  int socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == socket_fd_) {
    throw std::system_error(errno, std::system_category(), std::strerror(errno));
  }

  struct sockaddr_in remote_sock_address_;

  memset(&remote_sock_address_, 0, sizeof(remote_sock_address_));
  remote_sock_address_.sin_family = AF_INET;

  if (-1 == inet_pton(AF_INET, ip.c_str(), &(remote_sock_address_.sin_addr))) {
    throw std::system_error(errno, std::system_category(), "socket");
  }
  remote_sock_address_.sin_port = htons(port);

  std::cout << "Connecting to " << port << std::endl;
  if (-1 == ::connect(socket_fd_, reinterpret_cast<const struct sockaddr *>(&remote_sock_address_),
                      sizeof(remote_sock_address_))) {
    throw std::system_error(errno, std::system_category(), std::strerror(errno));
  }

  auto read_buffer = new char[1024];

  ssize_t read_bytes = 0;
  size_t total_read_bytes = 0;

  FILE* fp = fopen(dest_file.c_str(), "wb");

  if (fp == NULL) {

    std::cerr << "Failed to open file: " << dest_file << std::endl;
    return -1;
  }

  do {
    read_bytes = recv(socket_fd_, read_buffer, 1024, 0);
    if (read_bytes > 0) {
      std::cout << "Read from socket: " << read_bytes << std::endl;
      fwrite(read_buffer, 1, read_bytes, fp);
      fflush(fp);
      total_read_bytes += read_bytes;
      std::cout << "Total written to file: " << total_read_bytes << std::endl;
    }
  } while (read_bytes > 0 && total_read_bytes < file_size);

  std::cout << "End of download " << std::endl;

  fclose(fp);
  shutdown(socket_fd_, SHUT_RD);
  close(socket_fd_);

  return 0;
}
