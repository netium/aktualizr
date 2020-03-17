#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "asn1/asn1_message.h"
#include "der_encoder.h"
#include "ipuptanesecondary.h"
#include "logging/logging.h"

#include <memory>
#include <iostream>

namespace Uptane {

Uptane::SecondaryInterface::Ptr IpUptaneSecondary::connectAndCreate(const std::string& address, unsigned short port) {
  LOG_INFO << "Connecting to and getting info about IP Secondary: " << address << ":" << port << "...";

  ConnectionSocket con_sock{address, port};

  if (con_sock.connect() == 0) {
    LOG_INFO << "Connected to IP Secondary: "
             << "(" << address << ":" << port << ")";
  } else {
    LOG_WARNING << "Failed to connect to a secondary: " << std::strerror(errno);
    return nullptr;
  }

  return create(address, port, *con_sock);
}

Uptane::SecondaryInterface::Ptr IpUptaneSecondary::create(const std::string& address, unsigned short port, int con_fd) {
  Asn1Message::Ptr req(Asn1Message::Empty());
  req->present(AKIpUptaneMes_PR_getInfoReq);

  auto m = req->getInfoReq();

  auto resp = Asn1Rpc(req, con_fd);

  if (resp->present() != AKIpUptaneMes_PR_getInfoResp) {
    LOG_ERROR << "Failed to get info response message from secondary";
    throw std::runtime_error("Failed to obtain information about a secondary: " + address + std::to_string(port));
  }
  auto r = resp->getInfoResp();

  EcuSerial serial = EcuSerial(ToString(r->ecuSerial));
  HardwareIdentifier hw_id = HardwareIdentifier(ToString(r->hwId));
  std::string key = ToString(r->key);
  auto type = static_cast<KeyType>(r->keyType);
  PublicKey pub_key = PublicKey(key, type);

  LOG_INFO << "Got info on IP Secondary: "
           << "hw-ID: " << hw_id << " serial: " << serial;

  return std::make_shared<IpUptaneSecondary>(address, port, serial, hw_id, pub_key);
}

SecondaryInterface::Ptr IpUptaneSecondary::connectAndCheck(const std::string& address, unsigned short port,
                                                           EcuSerial serial, HardwareIdentifier hw_id,
                                                           PublicKey pub_key) {
  // try to connect:
  // - if it succeeds compare with what we expect
  // - otherwise, keep using what we know
  try {
    auto sec = IpUptaneSecondary::connectAndCreate(address, port);
    if (sec != nullptr) {
      auto s = sec->getSerial();
      if (s != serial) {
        LOG_ERROR << "Mismatch between secondary serials " << s << " and " << serial;
        return nullptr;
      }
      auto h = sec->getHwId();
      if (h != hw_id) {
        LOG_ERROR << "Mismatch between hardware ids " << h << " and " << hw_id;
        return nullptr;
      }
      auto p = sec->getPublicKey();
      if (pub_key.Type() == KeyType::kUnknown) {
        LOG_INFO << "Secondary " << s << " do not have a known public key";
      } else if (p != pub_key) {
        LOG_ERROR << "Mismatch between public keys " << p.Value() << " and " << pub_key.Value() << " for secondary "
                  << serial;
        return nullptr;
      }
      return sec;
    }
  } catch (std::exception& e) {
    LOG_WARNING << "Could not connect to secondary " << serial << " at " << address << ":" << port
                << ", using previously known registration data";
  }

  return std::make_shared<IpUptaneSecondary>(address, port, std::move(serial), std::move(hw_id), std::move(pub_key));
}

IpUptaneSecondary::IpUptaneSecondary(const std::string& address, unsigned short port, EcuSerial serial,
                                     HardwareIdentifier hw_id, PublicKey pub_key)
    : addr_{address, port}, serial_{std::move(serial)}, hw_id_{std::move(hw_id)}, pub_key_{std::move(pub_key)} {}

bool IpUptaneSecondary::putMetadata(const RawMetaPack& meta_pack) {
  LOG_INFO << "Sending Uptane metadata to the secondary";
  Asn1Message::Ptr req(Asn1Message::Empty());
  req->present(AKIpUptaneMes_PR_putMetaReq);

  auto m = req->putMetaReq();
  m->image.present = image_PR_json;
  SetString(&m->image.choice.json.root, meta_pack.image_root);            // NOLINT
  SetString(&m->image.choice.json.targets, meta_pack.image_targets);      // NOLINT
  SetString(&m->image.choice.json.snapshot, meta_pack.image_snapshot);    // NOLINT
  SetString(&m->image.choice.json.timestamp, meta_pack.image_timestamp);  // NOLINT

  m->director.present = director_PR_json;
  SetString(&m->director.choice.json.root, meta_pack.director_root);        // NOLINT
  SetString(&m->director.choice.json.targets, meta_pack.director_targets);  // NOLINT

  auto resp = Asn1Rpc(req, getAddr());

  if (resp->present() != AKIpUptaneMes_PR_putMetaResp) {
    LOG_ERROR << "Failed to get response to sending manifest to secondary";
    return false;
  }

  auto r = resp->putMetaResp();
  return r->result == AKInstallationResult_success;
}

bool IpUptaneSecondary::sendFirmware(const std::string& data) {
  std::lock_guard<std::mutex> l(install_mutex);
  LOG_INFO << "Sending firmware to the secondary";
  Asn1Message::Ptr req(Asn1Message::Empty());
  req->present(AKIpUptaneMes_PR_sendFirmwareReqv1);

  auto m = req->sendFirmwareReqv1();
  SetString(&m->target, data);
  auto resp = Asn1Rpc(req, getAddr());

  if (resp->present() != AKIpUptaneMes_PR_notSupportedResp) {

    LOG_ERROR << "The given version of request is not supported, fallback to the previous/another version";
    req->present(AKIpUptaneMes_PR_sendFirmwareReq);

    auto m1 = req->sendFirmwareReq();
    SetString(&m1->firmware, data);
    resp = Asn1Rpc(req, getAddr());
  }

  if (resp->present() != AKIpUptaneMes_PR_sendFirmwareResp) {
    LOG_ERROR << "Failed to get response to sending firmware to secondary";
    return false;
  }

  auto r = resp->sendFirmwareResp();
  return r->result == AKInstallationResult_success;
}

data::ResultCode::Numeric IpUptaneSecondary::install(const std::string& target_name) {
  LOG_INFO << "Invoking an installation of the target on the secondary: " << target_name;

  Asn1Message::Ptr req(Asn1Message::Empty());
  req->present(AKIpUptaneMes_PR_installReq);

  // prepare request message
  auto req_mes = req->installReq();
  SetString(&req_mes->hash, target_name);
  // send request and receive response, a request-response type of RPC
  auto resp = Asn1Rpc(req, getAddr());

  // invalid type of an response message
  if (resp->present() != AKIpUptaneMes_PR_installResp) {
    LOG_ERROR << "Failed to get response to an installation request to secondary";
    return data::ResultCode::Numeric::kInternalError;
  }

  // deserialize the response message
  auto r = resp->installResp();

  return static_cast<data::ResultCode::Numeric>(r->result);
}

Manifest IpUptaneSecondary::getManifest() const {
  LOG_DEBUG << "Getting the manifest from secondary with serial " << getSerial();
  Asn1Message::Ptr req(Asn1Message::Empty());

  req->present(AKIpUptaneMes_PR_manifestReq);

  auto resp = Asn1Rpc(req, getAddr());

  if (resp->present() != AKIpUptaneMes_PR_manifestResp) {
    LOG_ERROR << "Failed to get a response to a get manifest request to secondary";
    return Json::Value();
  }
  auto r = resp->manifestResp();

  if (r->manifest.present != manifest_PR_json) {
    LOG_ERROR << "Manifest wasn't in json format";
    return Json::Value();
  }
  std::string manifest = ToString(r->manifest.choice.json);  // NOLINT
  return Utils::parseJSON(manifest);
}

bool IpUptaneSecondary::ping() const {
  Asn1Message::Ptr req(Asn1Message::Empty());
  req->present(AKIpUptaneMes_PR_getInfoReq);

  auto m = req->getInfoReq();

  auto resp = Asn1Rpc(req, getAddr());

  return resp->present() == AKIpUptaneMes_PR_getInfoResp;
}

int serve_file(uint16_t port, const std::string& filename) {
  int sockfd, connfd;
  struct sockaddr_in servaddr, cli;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
      printf("socket creation failed...\n");
      return  -1;
  }
  else
      printf("Socket successfully created..\n");

  bzero(&servaddr, sizeof(servaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(port);

  if ((bind(sockfd, reinterpret_cast<struct sockaddr*>(&servaddr), sizeof(servaddr))) != 0) {
      printf("socket bind failed...\n");
      return -1;
  }
  else {
    std::cout << "Socket successfully binded..." << std::endl;
  }

  // Now server is ready to listen and verification
  if ((listen(sockfd, 5)) != 0) {
      printf("Listen failed...\n");
      return -1;
  } else {
    std::cout << "Server listening..." << std::endl;
  }
  socklen_t len1 = sizeof(cli);

  // Accept the data packet from client and verification
  connfd = accept(sockfd, reinterpret_cast<sockaddr*>(&cli), &len1);
  if (connfd < 0) {
      std::cerr << "server acccept failed..." << std::endl;
      return -1;
  } else {
      std::cout << "server acccept the client..." << std::endl;
  }

  int no_delay = 1;
  setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(int));

  {
    FILE* fp = fopen(filename.c_str(), "rb");
    if (fp == NULL) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return -1;
    }

    auto read_fuffer = new char[1024];
    size_t read_bytes = 0;
    size_t total_written_bytes = 0;

    while (!feof(fp)) {
      read_bytes = fread(read_fuffer,  1, 1024, fp);
      std::cout << "Read: " << read_bytes << std::endl;
      total_written_bytes += write(connfd, read_fuffer, read_bytes);
      std::cout << "Total written to socket: " << total_written_bytes << std::endl;
    }

    fclose(fp);
  }

  std::cout << "Closing the server: " << std::endl;
  shutdown(sockfd, SHUT_RDWR);
  close(sockfd);

  return 0;
}


data::ResultCode::Numeric IpUptaneSecondary::install(const Uptane::Target& target) {

  std::thread server_thread([&](){
    serve_file(8333, target.custom_data()["target_abs_filepath"].asString());
  });

  LOG_INFO << "Invoking an installation of the target on the secondary: " << target.filename();

  Asn1Message::Ptr req(Asn1Message::Empty());
  req->present(AKIpUptaneMes_PR_downloadFileReq);

  // prepare request message
  auto req_mes = req->downloadFileReq();

  // send request and receive response, a request-response type of RPC
  auto resp = Asn1Rpc(req, getAddr());

  // invalid type of an response message
  if (resp->present() != AKIpUptaneMes_PR_installResp) {
    LOG_ERROR << "Failed to get response to an installation request to secondary";
    return data::ResultCode::Numeric::kInternalError;
  }

  // deserialize the response message
  auto r = resp->installResp();

  if (server_thread.joinable()) {
    server_thread.join();
  }

  return static_cast<data::ResultCode::Numeric>(r->result);
}

}  // namespace Uptane
