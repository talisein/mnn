Notes on using libpeel
======================

libpeel is a new gintrospection based c++ wrapper for gtk. It takes a unique
approach of relying on GObject OO rather than the native C++, so using C++
constructors isn't done, instead you tell GObject about the init method that
will do construction. On one hand its pretty bizarre from a C++ perspective, but
it does lead to easier integration with the rest of the gintrospection universe.

I think its safe to call it 'experimental' still, so I thought I would record my
thoughts on it as I kicked the tires in this project.

o Passing `UniquePtr<GLib::Error>` as a pointer is weird. Why not have those
  functions take a ref.
  - i.e. the signature is currently `Gio::Resource::lookup_data(..., UniquePtr<Glib::Error>* err)`
  - i guess it might be optional allowing nullptr to be passed? I'd rather see a
    defaulted method.
o Examples have `#include <peel/Gio/SpecificClass.h>` but there is a
  `<peel/Gio/Gio.h>` that has everything. Seems like a better starting point.
