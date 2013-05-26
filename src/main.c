#include <stdio.h>
#include <dmx_recv.h>
#include <stdlib.h>
#include <glib-unix.h>

static gboolean
sigint_handler(gpointer user_data)
{
  g_main_loop_quit(user_data);
  return TRUE;
}

int
main(int argc, char *argv[])
{
  GError *err = NULL;
  GOptionContext *opt_ctxt;
  GMainLoop *loop;  
  DMXRecv *recv;
  g_type_init();
  opt_ctxt = g_option_context_new (" - map DMX to C2IP");
  if (!g_option_context_parse(opt_ctxt, &argc, &argv, &err)) {
    g_printerr("Failed to parse options: %s\n", err->message);
    return EXIT_FAILURE;
  }
  g_option_context_free(opt_ctxt);
  recv = dmx_recv_new("/dev/ttyAMA0", &err);
  if (!recv) {
    g_printerr("Failed setup DMX port: %s\n", err->message);
    return EXIT_FAILURE;
  }
  loop = g_main_loop_new(NULL, FALSE);
  g_unix_signal_add(SIGINT, sigint_handler, loop);
  g_debug("Starting");
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  g_object_unref(recv);
  g_debug("Exiting");
  return EXIT_SUCCESS;
}
