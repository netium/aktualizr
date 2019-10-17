#include <boost/process.hpp>
#include "config/config.h"
#include "httpfake.h"
#include "test_utils.h"

void Run_fake_http_server(const char *path) {
  std::string port = TestUtils::getFreePort();
  std::string server = "http://127.0.0.1:" + port;
  boost::process::child http_server_process(path, port, "-f");
  TestUtils::waitForServer(server + "/");
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Incorrect input params\nUsage:\n\t%s FAKE_HTTP_SERVER_PATH\n", argv[0]);
    return EXIT_FAILURE;
  }

  Run_fake_http_server(argv[1]);
  return EXIT_SUCCESS;
}
#endif
