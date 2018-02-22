#ifndef ICD_CONTEXT_H
#define ICD_CONTEXT_H

#include <glib.h>

struct icd_context {
  gboolean daemon; //run as daemon if TRUE, run in foreground if FALSE
  guint shutting_down; //icd shutdown timeout id
  GMainLoop *main_loop; //glib main loop

  GSList *policy_module_list; //list of policy modules

  GSList *request_list; //list of outstanding network connection requests

  GSList *nw_module_list; //list of network modules
  GHashTable *type_to_module; //hash table mapping network types to the modules in the above list

  GSList *srv_module_list; //list of service provider modules
  GHashTable *srv_type_to_srv_module; //hash table mapping service provider types to service modules
  GHashTable *nw_type_to_srv_module; //hash table mapping network types to list of service modules

  guint idle_timer_notify; //idle time gconf notification id

  guint iap_deletion_notify; //IAP deletion gconf notification id
};

gboolean icd_context_init (void); //Initialize context
struct icd_context *icd_context_get(void); //return the global context
void icd_context_run(void); //start the main loop
void icd_context_stop(void); //stop running the main loop
void icd_context_destroy (void); //destroy context

#endif
