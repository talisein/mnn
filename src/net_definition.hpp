#include <peel/GObject/Object.h>
#include <peel/class.h>

class NetDefinition final : public peel::Object
{
  PEEL_SIMPLE_CLASS (NetDefinition, Object)

public:
  void say_hello ();
};
