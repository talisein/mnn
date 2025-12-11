#include "net_definition.hpp"

PEEL_CLASS_IMPL (NetDefinition, "NetDefinition", Object)

void
NetDefinition::Class::init ()
{
}


void
NetDefinition::say_hello ()
{
    g_print ("Hello from %s!\n", get_type_name ());
}
