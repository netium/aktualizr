#include <gtest/gtest.h>

#include <ostree.h>
#include <boost/process.hpp>
#include "aktualizr_secondary.h"
#include "aktualizr_secondary_factory.h"
#include "package_manager/ostreemanager.h"
#include "test_utils.h"
#include "uptane_repo.h"

class Treehub {
 public:
  Treehub(const std::string& server_path)
      : _port(TestUtils::getFreePort()),
        _url("http://127.0.0.1:" + _port),
        _process(server_path, "-p", _port, "-d", _root_dir.PathString(), "-s0.5", "--create") {
    TestUtils::waitForServer(url() + "/");
    auto rev_process = Process("ostree").run({"rev-parse", "--repo", _root_dir.PathString(), "master"});
    EXPECT_EQ(std::get<0>(rev_process), 0) << std::get<2>(rev_process);
    _cur_rev = std::get<1>(rev_process);
    boost::trim_right_if(_cur_rev, boost::is_any_of(" \t\r\n"));

    LOG_INFO << "Treehub is running on: " << _port << " current revision: " << _cur_rev;
  }

  ~Treehub() {
    _process.terminate();
    _process.wait_for(std::chrono::seconds(10));
    if (_process.running()) {
      LOG_ERROR << "Failed to stop Treehub server";
    } else {
      LOG_INFO << "Treehub server has been stopped";
    }
  }

 public:
  const std::string& url() const { return _url; }
  const std::string curRev() const { return _cur_rev; }

 private:
  TemporaryDirectory _root_dir;
  const std::string _port;
  const std::string _url;
  boost::process::child _process;
  std::string _cur_rev;
};

class OstreeRootfs {
 public:
  OstreeRootfs(const std::string& rootfs_template) {
    auto sysroot_copy = Process("cp").run({"-r", rootfs_template, getPath()});
    EXPECT_EQ(std::get<0>(sysroot_copy), 0) << std::get<1>(sysroot_copy);

    auto deployment_rev = Process("ostree").run(
        {"rev-parse", std::string("--repo"), getPath() + "/ostree/repo", "generate-remote/generated"});

    EXPECT_EQ(std::get<0>(deployment_rev), 0) << std::get<2>(deployment_rev);

    _rev = std::get<1>(deployment_rev);
    boost::trim_right_if(_rev, boost::is_any_of(" \t\r\n"));

    _deployment.reset(ostree_deployment_new(0, getOSName(), getDeploymentRev(), getDeploymentSerial(),
                                            getDeploymentRev(), getDeploymentSerial()));
  }

  const std::string getPath() const { return _sysroot_dir; }
  const char* getDeploymentRev() const { return _rev.c_str(); }
  int getDeploymentSerial() const { return 0; }
  const char* getOSName() const { return _os_name.c_str(); }

  OstreeDeployment* getDeployment() const { return _deployment.get(); }

 private:
  const std::string _os_name{"dummy-os"};
  TemporaryDirectory _tmp_dir;
  std::string _sysroot_dir{(_tmp_dir / "ostree-rootfs").c_str()};
  std::string _rev;
  GObjectUniquePtr<OstreeDeployment> _deployment;
};

class AktualizrSecondaryWrapper {
 public:
  AktualizrSecondaryWrapper(const OstreeRootfs& sysroot, const Treehub& treehub) {
    // ostree update
    AktualizrSecondaryConfig config;

    config.pacman.type = PackageManager::kOstree;
    config.pacman.os = sysroot.getOSName();
    config.pacman.sysroot = sysroot.getPath();
    config.pacman.ostree_server = treehub.url();

    config.storage.path = _storage_dir.Path();
    config.storage.type = StorageType::kSqlite;

    _storage = INvStorage::newStorage(config.storage);
    _secondary = AktualizrSecondaryFactory::create(config, _storage);
  }

 public:
  AktualizrSecondary::Ptr& operator->() { return _secondary; }

  Uptane::Target getPendingVersion() const {
    boost::optional<Uptane::Target> pending_target;

    _storage->loadInstalledVersions(_secondary->getSerial().ToString(), nullptr, &pending_target);
    return *pending_target;
  }

  std::string hardwareID() const { return _secondary->getHwId().ToString(); }

  std::string serial() const { return _secondary->getSerial().ToString(); }

 private:
  TemporaryDirectory _storage_dir;
  AktualizrSecondary::Ptr _secondary;
  std::shared_ptr<INvStorage> _storage;
};

class UptaneRepoWrapper {
 public:
  UptaneRepoWrapper() { _uptane_repo.generateRepo(KeyType::kED25519); }

  Metadata addImageFile(const std::string& targetname, const std::string& hardware_id, const std::string& serial,
                        bool add_and_sign_target = true) {
    const auto image_file_path = _root_dir / targetname;
    boost::filesystem::ofstream(image_file_path) << "some data";

    _uptane_repo.addImage(image_file_path, targetname, hardware_id, "", Delegation());

    if (add_and_sign_target) {
      _uptane_repo.addTarget(targetname, hardware_id, serial, "");
      _uptane_repo.signTargets();
    }

    return getCurrentMetadata();
  }

  Metadata addOstreeRev(const std::string& rev, const std::string& hardware_id, const std::string& serial) {
    // it makes sense to add 'addOstreeImage' to UptaneRepo interface/class uptane_repo.h
    auto custom = Json::Value();
    custom["targetFormat"] = "OSTREE";
    _uptane_repo.addCustomImage(rev, Uptane::Hash(Uptane::Hash::Type::kSha256, rev), 0, hardware_id, "", Delegation(),
                                custom);

    _uptane_repo.addTarget(rev, hardware_id, serial, "");
    _uptane_repo.signTargets();

    return getCurrentMetadata();
  }

  Uptane::RawMetaPack getCurrentMetadata() const {
    Uptane::RawMetaPack metadata;

    boost::filesystem::load_string_file(_director_dir / "root.json", metadata.director_root);
    boost::filesystem::load_string_file(_director_dir / "targets.json", metadata.director_targets);

    boost::filesystem::load_string_file(_imagerepo_dir / "root.json", metadata.image_root);
    boost::filesystem::load_string_file(_imagerepo_dir / "timestamp.json", metadata.image_timestamp);
    boost::filesystem::load_string_file(_imagerepo_dir / "snapshot.json", metadata.image_snapshot);
    boost::filesystem::load_string_file(_imagerepo_dir / "targets.json", metadata.image_targets);

    return metadata;
  }

  std::shared_ptr<std::string> getImageData(const std::string& targetname) const {
    auto image_data = std::make_shared<std::string>();
    boost::filesystem::load_string_file(_root_dir / targetname, *image_data);
    return image_data;
  }

 private:
  TemporaryDirectory _root_dir;
  boost::filesystem::path _director_dir{_root_dir / "repo/director"};
  boost::filesystem::path _imagerepo_dir{_root_dir / "repo/repo"};
  UptaneRepo _uptane_repo{_root_dir.Path(), "", ""};
};

class SecondaryOstreeTest : public ::testing::Test {
 public:
  static const char* curOstreeRootfsRev(OstreeDeployment* ostree_depl) {
    (void)ostree_depl;
    return _sysroot->getDeploymentRev();
  }

  static OstreeDeployment* curOstreeDeployment(OstreeSysroot* ostree_sysroot) {
    (void)ostree_sysroot;
    return _sysroot->getDeployment();
  }

  static void setOstreeRootfsTemplate(const std::string& ostree_rootfs_template) {
    _ostree_rootfs_template = ostree_rootfs_template;
  }

 protected:
  static void SetUpTestSuite() {
    _treehub = std::make_shared<Treehub>("tests/sota_tools/treehub_server.py");
    _sysroot = std::make_shared<OstreeRootfs>(_ostree_rootfs_template);
  }

  static void TearDownTestSuite() {
    _treehub.reset();
    _sysroot.reset();
  }

 protected:
  SecondaryOstreeTest() {}

  Uptane::RawMetaPack addDefaultTarget() { return addTarget(_treehub->curRev()); }

  Uptane::RawMetaPack addTarget(const std::string& rev = "", const std::string& hardware_id = "",
                                const std::string& serial = "") {
    auto rev_to_apply = rev.empty() ? _treehub->curRev() : rev;
    auto hw_id = hardware_id.empty() ? _secondary.hardwareID() : hardware_id;
    auto serial_id = serial.empty() ? _secondary.serial() : serial;

    _uptane_repo.addOstreeRev(rev, hw_id, serial_id);

    return currentMetadata();
  }

  Uptane::RawMetaPack currentMetadata() const { return _uptane_repo.getCurrentMetadata(); }

  std::shared_ptr<std::string> getCredsToSend() const {
    std::map<std::string, std::string> creds_map = {
        {"ca.pem", ""}, {"client.pem", ""}, {"pkey.pem", ""}, {"server.url", _treehub->url()}};

    std::stringstream creads_strstream;
    Utils::writeArchive(creds_map, creads_strstream);

    return std::make_shared<std::string>(creads_strstream.str());
  }

  Uptane::Hash treehubCurRev() const { return Uptane::Hash(Uptane::Hash::Type::kSha256, _treehub->curRev()); }

 protected:
  static std::shared_ptr<Treehub> _treehub;
  static std::string _ostree_rootfs_template;
  static std::shared_ptr<OstreeRootfs> _sysroot;

  AktualizrSecondaryWrapper _secondary{*_sysroot, *_treehub};
  UptaneRepoWrapper _uptane_repo;
};

std::shared_ptr<Treehub> SecondaryOstreeTest::_treehub{nullptr};
std::string SecondaryOstreeTest::_ostree_rootfs_template{"./build/ostree_repo"};
std::shared_ptr<OstreeRootfs> SecondaryOstreeTest::_sysroot{nullptr};

TEST_F(SecondaryOstreeTest, fullUptaneVerificationPositive) {
  EXPECT_TRUE(_secondary->putMetadata(addDefaultTarget()));
  EXPECT_TRUE(_secondary->sendFirmware(getCredsToSend()));
  EXPECT_TRUE(_secondary.getPendingVersion().MatchHash(treehubCurRev()));
  // TODO: emulate reboot and check installed version once ostree update finalization is supported by secondary
}

TEST_F(SecondaryOstreeTest, fullUptaneVerificationInvalidRevision) {
  EXPECT_TRUE(_secondary->putMetadata(addTarget("invalid-revision")));
  EXPECT_FALSE(_secondary->sendFirmware(getCredsToSend()));
}

TEST_F(SecondaryOstreeTest, fullUptaneVerificationInvalidHwID) {
  EXPECT_FALSE(_secondary->putMetadata(addTarget("", "invalid-hardware-id", "")));
}

TEST_F(SecondaryOstreeTest, fullUptaneVerificationInvalidSerial) {
  EXPECT_FALSE(_secondary->putMetadata(addTarget("", "", "invalid-serial-id")));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  if (argc != 2) {
    std::cerr << "Error: " << argv[0] << " <ostree rootfs path>\n";
    return EXIT_FAILURE;
  }

  SecondaryOstreeTest::setOstreeRootfsTemplate(argv[1]);

  logger_init();
  logger_set_threshold(boost::log::trivial::info);

  return RUN_ALL_TESTS();
}

extern "C" OstreeDeployment* ostree_sysroot_get_booted_deployment(OstreeSysroot* ostree_sysroot) {
  return SecondaryOstreeTest::curOstreeDeployment(ostree_sysroot);
}

extern "C" const char* ostree_deployment_get_csum(OstreeDeployment* ostree_deployment) {
  return SecondaryOstreeTest::curOstreeRootfsRev(ostree_deployment);
}