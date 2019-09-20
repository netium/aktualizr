#include "logging.h"

using boost::log::trivial::severity_level;

static severity_level gLoggingThreshold;

extern void logger_init_sink(bool use_colors = false);

int64_t get_curlopt_verbose() { return gLoggingThreshold <= boost::log::trivial::trace ? 1L : 0L; }

void logger_init(bool use_colors) {
  gLoggingThreshold = boost::log::trivial::info;

  logger_init_sink(use_colors);

  boost::log::core::get()->set_filter(boost::log::trivial::severity >= gLoggingThreshold);
}

#ifdef STREAM_LOGGING
#include "http/httpinterface.h"
void logger_set_http(const std::string& server, std::shared_ptr<HttpInterface> http)
{
  LoggingStream::getInstance().setHttp(server, http);
}
#endif

void logger_set_threshold(const severity_level threshold) {
  gLoggingThreshold = threshold;
  boost::log::core::get()->set_filter(boost::log::trivial::severity >= gLoggingThreshold);
}

void logger_set_threshold(const LoggerConfig& lconfig) {
  int loglevel = lconfig.loglevel;
  if (loglevel < boost::log::trivial::trace) {
    LOG_WARNING << "Invalid log level: " << loglevel;
    loglevel = boost::log::trivial::trace;
  }
  if (boost::log::trivial::fatal < loglevel) {
    LOG_WARNING << "Invalid log level: " << loglevel;
    loglevel = boost::log::trivial::fatal;
  }
  logger_set_threshold(static_cast<boost::log::trivial::severity_level>(loglevel));
}

void logger_set_enable(bool enabled) { boost::log::core::get()->set_logging_enabled(enabled); }

int loggerGetSeverity() { return static_cast<int>(gLoggingThreshold); }

// vim: set tabstop=2 shiftwidth=2 expandtab:

#ifdef STREAM_LOGGING

/*static*/ std::stringstream& LoggingStream::get()
{
    static std::mutex get_lock;
    std::lock_guard<std::mutex> guard(get_lock);

    if (getInstance().m_data.str().size() >= 100)
        getInstance().transferData();

    return getInstance().m_data;
}

/*static*/ LoggingStream& LoggingStream::getInstance()
{
    static LoggingStream instance;
    return instance;
}

void LoggingStream::transferData()
{
    if (m_http.get() && !(m_server_url.empty()))
    {
      static int i = 0;
      std::cout << std::endl << "TRANSFER: " << m_data.str().size() << " bytes:" << std::endl << m_data.str();
      std::cout << std::endl << "TRANSFER END " << i++;

      const bool previous_logging = m_http->getLogging();
      m_http->setLogging(false); // to avoid cycling
      m_http->postString(m_server_url + "/logs", m_data.str());
      m_http->setLogging(previous_logging);

      std::cout << std::endl << "TRANSFER HTTP POST END " << i;
      m_data.clear();
    }
}

void LoggingStream::setHttp(const std::string& server_url, std::shared_ptr<HttpInterface> http)
{
    m_server_url = server_url;
    m_http = http;
}
#endif
