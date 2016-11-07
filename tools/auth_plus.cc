#include <assert.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <curl/curl.h>
#include <iostream>
#include <sstream>

#include "auth_plus.h"

using boost::property_tree::ptree;
using boost::property_tree::json_parser::json_parser_error;
using std::cout;
using std::stringstream;

/**
 * Handle CURL write callbacks by appending to a stringstream
 */
size_t curl_handle_write_sstream(void *buffer, size_t size, size_t nmemb,
                                 void *userp) {
  stringstream *body = (stringstream *)userp;
  body->write((const char *)buffer, size * nmemb);
  return size * nmemb;
}

AuthenticationResult AuthPlus::Authenticate() {
  CURL *curl_handle = curl_easy_init();
  // curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1);
  curl_easy_setopt(curl_handle, CURLOPT_URL, (server_ + "/token").c_str());

  curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
  curl_easy_setopt(curl_handle, CURLOPT_USERNAME, client_id_.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_PASSWORD, client_secret_.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_POST, 1);
  curl_easy_setopt(curl_handle, CURLOPT_COPYPOSTFIELDS,
                   "grant_type=client_credentials");

  stringstream body;
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
                   &curl_handle_write_sstream);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &body);

  curl_easy_perform(curl_handle);

  AuthenticationResult res;
  int rescode;
  curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &rescode);
  if (rescode == 200) {
    ptree pt;
    try {
      read_json(body, pt);
      token_ = pt.get("access_token", "");
      res = AUTHENTICATION_SUCCESS;
    } catch (json_parser_error e) {
      token_ = "";
      res = AUTHENTICATION_FAILURE;
    }
  } else {
    // TODO, be more specfic about the failure cases
    res = AUTHENTICATION_FAILURE;
  }
  curl_easy_cleanup(curl_handle);

  cout << "GOT TOKEN\n" << token_ << "\n";
  return res;
}

// vim: set tabstop=2 shiftwidth=2 expandtab:
