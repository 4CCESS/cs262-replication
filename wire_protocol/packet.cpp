#include "packet.h"

Packet::Packet()
    : op_code('?'),
      username(""),
      password(""),
      sender(""),
      recipient(""),
      message(""),
      message_id(""),
      isValidated(false)
{
    // Nothing else needed in default constructor
}
