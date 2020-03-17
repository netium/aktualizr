#ifndef HANDLERMAP_H
#define HANDLERMAP_H

#include <functional>
#include  <unordered_map>


#include "asn1/asn1_message.h"
#include "AKIpUptaneMes.h"

class HandlerMap
{
  public:
    using Handler = std::function<Asn1Message::Ptr(Asn1Message::Ptr)>;

    HandlerMap();
    void registerHandler(AKIpUptaneMes_PR msg_id, Handler handler);
    Handler& operator[](AKIpUptaneMes_PR msg_id);
  private:
    std::unordered_map<AKIpUptaneMes_PR, Handler> _handler_map;
};

#endif // HANDLERMAP_H
