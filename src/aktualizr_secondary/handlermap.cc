#include "handlermap.h"

HandlerMap::HandlerMap() {

}

void HandlerMap::registerHandler(AKIpUptaneMes_PR msg_id, Handler handler) {
  _handler_map[msg_id] = handler;
}


HandlerMap::Handler& HandlerMap::operator[](AKIpUptaneMes_PR msg_id) {
  auto find_res_it = _handler_map.find(msg_id);
  if (find_res_it == _handler_map.end()) {
    //throw Ex
  }

  return find_res_it->second;
}
