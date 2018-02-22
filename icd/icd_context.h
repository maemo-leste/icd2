#ifndef ICD_CONTEXT_H
#define ICD_CONTEXT_H

#include <glib.h>

/** ICd Context */
struct icd_context {

	/** run as daemon if TRUE, run in foreground if FALSE */
	gboolean daemon;

	/** icd shutdown timeout id */
	guint shutting_down;

	/** glib main loop */
	GMainLoop *main_loop;


	/** list of policy modules */
	GSList *policy_module_list;


	/** list of outstanding network connection requests */
	GSList *request_list;


	/** list of network modules */
	GSList *nw_module_list;

	/** hash table mapping network types to the modules in the above list */
	GHashTable *type_to_module;


	/** list of service provider modules */
	GSList *srv_module_list;

	/** hash table mapping service provider types to service modules */
	GHashTable *srv_type_to_srv_module;

	/** hash table mapping network types to list of service modules */
	GHashTable *nw_type_to_srv_module;


	/** idle time gconf notification id */
	guint idle_timer_notify;


	/** IAP deletion gconf notification id */
	guint iap_deletion_notify;
};


	/** Initialize context */
gboolean icd_context_init (void);

	/** return the global context*/
struct icd_context *icd_context_get(void);

	/** start the main loop */
void icd_context_run(void);

	/** stop running the main loop */
void icd_context_stop(void);

	/** destroy context */
void icd_context_destroy (void);


#endif
