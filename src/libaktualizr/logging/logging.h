#ifndef SOTA_CLIENT_TOOLS_LOGGING_H_
#define SOTA_CLIENT_TOOLS_LOGGING_H_

#include "logging_config.h"

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>

#define STREAM_LOGGING

#ifdef STREAM_LOGGING
  #include <string>
  #include <sstream>
  #include "http/httpinterface.h"

  class LoggingStream
  {
  public:
    static std::stringstream& get();
    static LoggingStream& getInstance();
    void setHttp(const std::string& server_url, std::shared_ptr<HttpInterface> http);

  private:
    void transferData();
  private:
    std::string m_server_url;
    std::shared_ptr<HttpInterface> m_http;
    std::stringstream m_data;
  };

  /** Log an unrecoverable error */
  #define LOG_FATAL LoggingStream::get() << std::endl << "FATAL: "
  /** Log that something has definitely gone wrong */
  #define LOG_ERROR LoggingStream::get() << std::endl << "ERROR: "
  /** Warn about behaviour that is probably bad, but hasn't yet caused the system
   * to operate out of spec. */
  #define LOG_WARNING LoggingStream::get() << std::endl << "WARNING: "
  /** Report a user-visible message about operation */
  #define LOG_INFO LoggingStream::get() << std::endl << "INFO: "
  /** Report a message for developer debugging */
  #define LOG_DEBUG LoggingStream::get() << std::endl << "DEBUG: "
  /** Report very-verbose debugging information */
  #define LOG_TRACE LoggingStream::get() << std::endl << "TRACE: "

  void logger_set_http(const std::string& server, std::shared_ptr<HttpInterface> http);
#else
  /** Log an unrecoverable error */
  #define LOG_FATAL BOOST_LOG_TRIVIAL(fatal)
  /** Log that something has definitely gone wrong */
  #define LOG_ERROR BOOST_LOG_TRIVIAL(error)
  /** Warn about behaviour that is probably bad, but hasn't yet caused the system
   * to operate out of spec. */
  #define LOG_WARNING BOOST_LOG_TRIVIAL(warning)
  /** Report a user-visible message about operation */
  #define LOG_INFO BOOST_LOG_TRIVIAL(info)
  /** Report a message for developer debugging */
  #define LOG_DEBUG BOOST_LOG_TRIVIAL(debug)
  /** Report very-verbose debugging information */
  #define LOG_TRACE BOOST_LOG_TRIVIAL(trace)
#endif // STREAM_LOGGING

// Use like:
// curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, get_curlopt_verbose());
int64_t get_curlopt_verbose();

void logger_init(bool use_colors = false);

void logger_set_threshold(boost::log::trivial::severity_level threshold);

void logger_set_threshold(const LoggerConfig& lconfig);

void logger_set_enable(bool enabled);

int loggerGetSeverity();

#endif
