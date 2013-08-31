#include "ola_wrapper.h"
#include <ola/DmxBuffer.h>
#include <ola/Logging.h>
#include <ola/OlaClientWrapper.h>
#include <string>

using std::string;

extern "C" {
  struct _OLAWrapper
  {
    ola::OlaCallbackClientWrapper *wrapper;
    OLAWrapperReceived recv_cb;
    gpointer user_data;
  };
}

class GLibLog : public ola::LogDestination
{
public:
  void Write(ola::log_level level, const string &log_line);
};

void
GLibLog::Write(ola::log_level level, const string &log_line)
{
  GLogLevelFlags l;
  switch(level) {
  case ola::OLA_LOG_FATAL:
    l = G_LOG_LEVEL_ERROR;
    break;

  case ola::OLA_LOG_WARN:
  default:
    l = G_LOG_LEVEL_WARNING;
    break;
  case ola::OLA_LOG_INFO:
    l = G_LOG_LEVEL_INFO;
    break;
  case ola::OLA_LOG_DEBUG:
    l = G_LOG_LEVEL_DEBUG;
    break;
  }
  g_log(G_LOG_DOMAIN, l, "%s", log_line.c_str());
}

// Called when universe registration completes.
void RegisterComplete(const std::string& error) {
  if (!error.empty()) {
    OLA_WARN << "Failed to register universe: "<<error;
  }
}

// Called when new DMX data arrives.
void NewDmx(OLAWrapper *wrapper,
	    unsigned int universe,
	    const ola::DmxBuffer &data,
	    const std::string &error) {
  if (error.empty()) {
    wrapper->recv_cb(data.GetRaw(), data.Size(), wrapper->user_data);
  } else {
    OLA_WARN << "Receive failed: " << error;
  }
}


extern "C" {
#define OLA_WRAPPER_ERROR (ola_wrapper_get_error_quark())
  
  static GQuark
  ola_wrapper_get_error_quark(void)
  {
    static GQuark q = 0;
    if (!q) {
      q = g_quark_from_static_string("ola-wrapper-error-quark");
    }
    return q;
  }
  
  OLAWrapper *
  ola_wrapper_read_new(OLAWrapperReceived recv_cb, gpointer user_data,
		       guint universe, GError **err)
  {
    OLAWrapper *wrapper = g_new(OLAWrapper,1);
    wrapper->recv_cb = recv_cb;
    wrapper->user_data = user_data;
    ola::InitLogging(ola::OLA_LOG_DEBUG, new GLibLog());
    OLA_INFO << "OLA logging";
    wrapper->wrapper = new ola::OlaCallbackClientWrapper;
    if (!wrapper->wrapper->Setup()) {
      g_set_error(err, OLA_WRAPPER_ERROR, 0, "Client wrapper setup failed");
      return NULL;
    }
    ola::OlaCallbackClient *client = wrapper->wrapper->GetClient();
    // Set the callback and register our interest in this universe
    client->SetDmxCallback(ola::NewCallback(&NewDmx, wrapper));
    client->RegisterUniverse(universe, ola::REGISTER,
			     ola::NewSingleCallback(&RegisterComplete));
    //wrapper.GetSelectServer()->Run();
    return wrapper;
  }

  void
  ola_wrapper_run(OLAWrapper *wrapper)
  {
     wrapper->wrapper->GetSelectServer()->Run();
  }

  void
  ola_wrapper_terminate(OLAWrapper *wrapper)
  {
     wrapper->wrapper->GetSelectServer()->Terminate();
  }

  void
  ola_wrapper_destroy(OLAWrapper *wrapper)
  {
    delete wrapper->wrapper;
    g_free(wrapper);
  }
}

