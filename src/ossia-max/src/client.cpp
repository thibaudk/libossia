// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "client.hpp"
#include "device.hpp"
#include "model.hpp"
#include "parameter.hpp"
#include "remote.hpp"
#include "view.hpp"
#include "utils.hpp"

#include <ossia/network/osc/osc.hpp>
#include <ossia/network/oscquery/oscquery_mirror.hpp>
#include <ossia/network/minuit/minuit.hpp>
#include <ossia/network/zeroconf/zeroconf.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include "ossia-max.hpp"

#include <boost/algorithm/string.hpp>

using namespace ossia::max;

#pragma mark -
#pragma mark ossia_client class methods

extern "C" void ossia_client_setup()
{
  auto& ossia_library = ossia_max::instance();

  // instantiate the ossia.client class
  t_class* c = class_new(
      "ossia.client", (method)client::create, (method)client::destroy,
      (short)sizeof(client), 0L, A_GIMME, 0);

  device_base::class_setup(c);

  class_addmethod(
      c, (method)client::update,
      "update", A_NOTHING, 0);

  class_addmethod(
      c, (method)client::connect_mess_cb,
      "connect", A_GIMME, 0);

  class_addmethod(
      c, (method)client::disconnect,
      "disconnect", A_GIMME, 0);

  class_addmethod(
      c, (method)client::get_mess_cb,
      "get", A_SYM, 0);

  class_addmethod(
      c, (method)client::assist,
      "assist", A_NOTHING, 0);

  class_addmethod(
      c, (method) client::notify,
      "notify", A_CANT, 0);

  class_register(CLASS_BOX, c);
  ossia_library.ossia_client_class = c;

}


namespace ossia
{
namespace max
{

void* client::create(t_symbol* name, long argc, t_atom* argv)
{
  auto& ossia_library = ossia_max::instance();
  auto x = make_ossia<client>();

  if (x)
  {
    auto& pat_desc = ossia_max::instance().patchers[x->m_patcher];
    if(!pat_desc.client && !pat_desc.device)
      pat_desc.client = x;
    else
    {
      error("You can put only one [ossia.device] or [ossia.client] per patcher");
      object_free(x);
      return nullptr;
    }

    // make outlets
    x->m_dumpout = outlet_new(x, NULL); // anything outlet to dump client state

    x->m_device = 0;

    x->m_otype = object_class::client;

    x->m_poll_clock = clock_new(x, (method)client::poll_message);
    x->m_clock = clock_new(x, (method) client::connect);

    // parse arguments
    long attrstart = attr_args_offset(argc, argv);

    // check name argument
    x->m_name = gensym("Max");
    if (attrstart && argv)
    {
      if (atom_gettype(argv) == A_SYM)
      {
        x->m_name = atom_getsym(argv);       
        bool autoconnect = true;
        if(argc == 2 && atom_gettype(argv+1) == A_FLOAT)
        {
          float f = atom_getfloat(argv+1);
          autoconnect = f > 0.;
          if( autoconnect )
            connect_mess_cb(x,nullptr,1,argv);
        }
        else
          connect_mess_cb(x,nullptr,attrstart-1,argv);
      }
    }

    // process attr args, if any
    attr_args_process(x, argc - attrstart, argv + attrstart);

    // need to schedule a loadbang because objects only receive a loadbang when patcher loads.
    x->m_reg_clock = clock_new(x, (method) object_base::loadbang);
    clock_set(x->m_reg_clock, 1);

    ossia_library.clients.push_back(x);
  }

  return (x);
}

void client::destroy(client* x)
{
  auto pat_it = ossia_max::instance().patchers.find(x->m_patcher);
  if(pat_it != ossia_max::instance().patchers.end())
  {
    auto& pat_desc = pat_it->second;
    if(pat_desc.client == x)
      pat_desc.client = nullptr;
    if(pat_desc.empty())
    {
      ossia_max::instance().patchers.erase(pat_it);
    }
  }

  x->m_dead = true;
  x->m_matchers.clear();

  if(x->m_clock)
    clock_free((t_object*)x->m_clock);

  disconnect(x);
  if(x->m_dumpout)
    outlet_delete(x->m_dumpout);
  ossia_max::instance().clients.remove_all(x);

  x->~client();
}

#pragma mark -
#pragma mark t_client structure functions

void client::assist(client*, void*, long m, long a, char* s)
{
  if (m == ASSIST_INLET)
  {
    sprintf(s, "Client messages input");
  }
  else
  {
    switch(a)
    {
      case 0:
        sprintf(s, "Dumpout");
        break;
      default:
        ;
    }
  }
}

void client::get_mess_cb(client* x, t_symbol* s)
{
  if ( s == gensym("devices") )
    client::get_devices(x);
  else
    device_base::get_mess_cb(x,s);
}

void client::connect_mess_cb(client* x, t_symbol*, int argc, t_atom* argv)
{
  if(x->m_argv)
    delete x->m_argv;

  x->m_argc = argc;
  x->m_argv = new t_atom[argc];

  memcpy(x->m_argv, argv, argc * sizeof(t_atom));

  client::connect(x);
}

void client::connect(client* x)
{
  int argc = x->m_argc;
  t_atom* argv = x->m_argv;

  disconnect(x);

  ossia::net::minuit_connection_data minuit_settings;
  minuit_settings.name = x->m_name->s_name;
  minuit_settings.host = "127.0.0.1";
  minuit_settings.remote_port = 6666;
  minuit_settings.local_port = 9999;

  ossia::net::oscquery_connection_data oscq_settings;
  oscq_settings.name = x->m_name->s_name;
  oscq_settings.host = "127.0.0.1";
  oscq_settings.port = 5678;

  ossia::net::osc_connection_data osc_settings;
  osc_settings.name = x->m_name->s_name;
  osc_settings.host = "127.0.0.1";
  osc_settings.remote_port = 6666;
  osc_settings.local_port = 9999;

  t_atom connection_status[6];
  int count=0;

  if (argc && argv->a_type == A_SYM)
  {
    std::string protocol_name = argv->a_w.w_sym->s_name;
    std::string name = protocol_name;
    boost::algorithm::to_lower(protocol_name);

    x->m_zeroconf = false;

    if (protocol_name == "minuit")
    {
      argc--;
      argv++;
      if (argc == 4
          && argv[0].a_type == A_SYM
          && argv[1].a_type == A_SYM
          && ( argv[2].a_type == A_LONG )
          && ( argv[3].a_type == A_LONG ))
      {
        minuit_settings.name = atom_getsym(argv++)->s_name;
        minuit_settings.host = atom_getsym(argv++)->s_name;
        minuit_settings.remote_port = atom_getlong(argv++);
        minuit_settings.local_port = atom_getlong(argv++);
      }

      A_SETSYM(connection_status+1, gensym("minuit"));
      A_SETSYM(connection_status+2, gensym(minuit_settings.name.c_str()));
      A_SETSYM(connection_status+3, gensym(minuit_settings.host.c_str()));
      A_SETFLOAT(connection_status+4, minuit_settings.remote_port);
      A_SETFLOAT(connection_status+5, minuit_settings.local_port);

      try
      {
        x->m_device = std::make_shared<ossia::net::generic_device>(
            std::make_unique<ossia::net::minuit_protocol>(
              minuit_settings.name, minuit_settings.host,
              minuit_settings.remote_port, minuit_settings.local_port),
            x->m_name->s_name);
      }
      catch (const std::exception& e)
      {
        object_error((t_object*)x, "%s", e.what());
      }

      count = 6;
    }
    else if (protocol_name == "oscquery")
    {
      argc--;
      argv++;
      std::string wsurl = "ws://" + oscq_settings.host + ":"
                          + std::to_string(oscq_settings.port);
      if (argc == 1
          && argv[0].a_type == A_SYM)
      {
        wsurl = atom_getsym(argv)->s_name;
      }

      A_SETSYM(connection_status+1, gensym("oscquery"));
      A_SETSYM(connection_status+2, gensym(oscq_settings.name.c_str()));
      A_SETSYM(connection_status+3, gensym(wsurl.c_str()));

      try
      {
        x->m_oscq_protocol = new ossia::oscquery::oscquery_mirror_protocol{wsurl};
        x->m_oscq_protocol->set_zombie_on_remove(false);
        x->m_device = std::make_shared<ossia::net::generic_device>(
            std::unique_ptr<ossia::net::protocol_base>(x->m_oscq_protocol), oscq_settings.name);

        clock_set(x->m_poll_clock, 1);
      }
      catch (const std::exception& e)
      {
        object_error((t_object*)x, "%s", e.what());
      }

      count = 4;
    }
    else if (protocol_name == "osc")
    {
      argc--;
      argv++;
      if (argc == 4
          && argv[0].a_type == A_SYM
          && argv[1].a_type == A_SYM
          && ( argv[2].a_type == A_FLOAT || argv[2].a_type == A_LONG )
          && ( argv[3].a_type == A_FLOAT || argv[3].a_type == A_LONG ))
      {
        osc_settings.name = atom_getsym(argv++)->s_name;
        osc_settings.host = atom_getsym(argv++)->s_name;
        osc_settings.remote_port = atom_getfloat(argv++);
        osc_settings.local_port = atom_getfloat(argv++);
      }

      A_SETSYM(connection_status+1, gensym("osc"));
      A_SETSYM(connection_status+2, gensym(osc_settings.name.c_str()));
      A_SETSYM(connection_status+3, gensym(osc_settings.host.c_str()));
      A_SETFLOAT(connection_status+4, osc_settings.remote_port);
      A_SETFLOAT(connection_status+5, osc_settings.local_port);

      try
      {
        x->m_device = std::make_shared<ossia::net::generic_device>(
            std::make_unique<ossia::net::osc_protocol>(
              osc_settings.host, osc_settings.remote_port,
              osc_settings.local_port, osc_settings.name),
            x->m_name->s_name);
      }
      catch (const std::exception& e)
      {
        object_error((t_object*)x, "%s", e.what());
      }
      count = 6;

    }
    else
    {
      // Connect by device name : retrieve connection info
      x->m_device = ZeroconfOscqueryListener::find_device(name);
      if(!x->m_device)
      {
        x->m_device = ZeroconfMinuitListener::find_device(name);
      }
      else
      {
        x->m_zeroconf = true;
      }

      A_SETSYM(connection_status+1, gensym(name.c_str()));
      count = 2;
    }
  }
  else
  {
    client::print_protocol_help();
  }

  if(x->m_device)
  {
    A_SETFLOAT(connection_status,1);

    outlet_anything(x->m_dumpout, gensym("connect"), count, connection_status);

    x->connect_slots();
    client::update(x);
    client::on_device_created(x);
    clock_unset(x->m_clock);

    register_children_in_patcher_recursively(get_patcher(&x->m_object), x);
    output_all_values(get_patcher(&x->m_object), true);
  }
  else
  {
    A_SETFLOAT(connection_status,0);

    outlet_anything(x->m_dumpout, gensym("connect"), count, connection_status);

    clock_delay(x->m_clock, 1000); // hardcoded reconnection delay
  }
}

void client::get_devices(client* x)
{
 // TODO
}

void client::unregister_children()
{
  std::vector<object_base*> children_view = find_children_to_register(
      &m_object, m_patcher, gensym("ossia.view"));

  for (auto child : children_view)
  {
    if (child->m_otype == object_class::view)
    {
      ossia::max::view* view = (ossia::max::view*)child;
      view->unregister();
    }
    else if (child->m_otype == object_class::remote)
    {
      ossia::max::remote* remote = (ossia::max::remote*)child;
      remote->unregister();
    }
  }
}

void client::update(client* x)
{
  if (x->m_device)
  {
    x->m_device->get_protocol().update(*x->m_device);
  }
}

void client::disconnect(client* x)
{
  if (x->m_device)
  {
    x->m_matchers.clear();
    x->disconnect_slots();
    x->unregister_children();
    x->m_device = nullptr;
    x->m_oscq_protocol = nullptr;
    client::on_device_removing(x);
  }
  if(x->m_clock)
    clock_unset(x->m_clock); // avoid automatic reconnection
}

void client::poll_message(client* x)
{
  if (x->m_oscq_protocol)
  {
    x->m_oscq_protocol->run_commands();
    clock_delay(x->m_poll_clock, x->m_rate);
  }
}

} // max namespace
} // ossia namespace
